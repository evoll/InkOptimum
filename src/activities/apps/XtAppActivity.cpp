#include "XtAppActivity.h"

#include <HalClock.h>
#include <Logging.h>
#include <HalDisplay.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <JPEGDEC.h>
#include <PNGdec.h>
#include <WiFi.h>
#include <qrcode.h>

#include <algorithm>

#include <Epub.h>
#include <FsHelpers.h>

#include <functional>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/reader/BookReadingStats.h"
#include "activities/reader/DailyReadingStore.h"
#include "activities/reader/GlobalReadingStats.h"
#include "activities/ActivityResult.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include <WebSocketsClient.h>

#include "InkMODSettings.h"
#include "network/XtAppNet.h"
#include "util/XtAppLocator.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t kMaxManifest = 8 * 1024;
constexpr size_t kMaxFile = 2 * 1024 * 1024;

int fontFor(int size) { return size <= 0 ? SMALL_FONT_ID : (size == 1 ? UI_10_FONT_ID : UI_12_FONT_ID); }

bool readWhole(const std::string& path, std::string& out, size_t maxBytes) {
  HalFile f;
  if (!Storage.openFileForRead("XTAPP", path, f)) return false;
  const size_t n = f.size();
  if (n > maxBytes) {
    f.close();
    return false;
  }
  out.resize(n);
  const int got = n ? f.read(out.data(), n) : 0;
  f.close();
  if (got < 0 || static_cast<size_t>(got) != n) return false;
  return true;
}

bool writeWhole(const std::string& path, const std::string& data) {
  const std::string tmp = path + ".tmp";
  HalFile f;
  if (!Storage.openFileForWrite("XTAPP", tmp, f)) return false;
  const size_t w = data.empty() ? 0 : f.write(data.data(), data.size());
  f.flush();
  f.close();
  if (w != data.size()) {
    Storage.remove(tmp.c_str());
    return false;
  }
  Storage.remove(path.c_str());
  return Storage.rename(tmp.c_str(), path.c_str());
}

int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}
}  // namespace


// ---- image decoding for ctx.image:decode (JPEG / PNG -> 8-bit gray, decimated to fit 480x800) -----------------------

namespace {
struct DecodeCtx {
  xtapp::ImageData* img = nullptr;
  int step = 1;       // keep every step-th source pixel in both directions
  int srcW = 0, srcH = 0;
};

int stepFor(int w, int h) {
  int step = 1;
  while (w / step > 480 || h / step > 800) ++step;
  return step;
}

bool allocGray(DecodeCtx& c, int w, int h) {
  c.srcW = w;
  c.srcH = h;
  c.step = stepFor(w, h);
  c.img->w = (w + c.step - 1) / c.step;
  c.img->h = (h + c.step - 1) / c.step;
  if (static_cast<size_t>(c.img->w) * c.img->h > 480u * 800u) return false;
  if (ESP.getMaxAllocHeap() < static_cast<size_t>(c.img->w) * c.img->h + 70 * 1024) return false;
  c.img->gray.assign(static_cast<size_t>(c.img->w) * c.img->h, 255);
  return true;
}

int jpegCb(JPEGDRAW* d) {
  auto* c = static_cast<DecodeCtx*>(d->pUser);
  const uint8_t* px = reinterpret_cast<const uint8_t*>(d->pPixels);
  for (int yy = 0; yy < d->iHeight; ++yy) {
    const int sy = d->y + yy;
    if (sy % c->step) continue;
    const int oy = sy / c->step;
    if (oy >= c->img->h) break;
    for (int xx = 0; xx < d->iWidthUsed; ++xx) {
      const int sx = d->x + xx;
      if (sx % c->step) continue;
      const int ox = sx / c->step;
      if (ox < c->img->w) c->img->gray[static_cast<size_t>(oy) * c->img->w + ox] = px[yy * d->iWidth + xx];
    }
  }
  return 1;
}

int pngCb(PNGDRAW* d) {
  auto* c = static_cast<DecodeCtx*>(d->pUser);
  if (d->y % c->step) return 1;
  const int oy = d->y / c->step;
  if (oy >= c->img->h) return 1;
  const int bpp = d->iPixelType == PNG_PIXEL_TRUECOLOR ? 3 : d->iPixelType == PNG_PIXEL_TRUECOLOR_ALPHA ? 4
                : d->iPixelType == PNG_PIXEL_GRAY_ALPHA ? 2 : 1;
  for (int ox = 0; ox < c->img->w; ++ox) {
    const int sx = ox * c->step;
    if (sx >= d->iWidth) break;
    const uint8_t* p = d->pPixels + sx * bpp;
    int g = 128, a = 255;
    switch (d->iPixelType) {
      case PNG_PIXEL_GRAYSCALE: g = p[0]; break;
      case PNG_PIXEL_TRUECOLOR: g = (p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8; break;
      case PNG_PIXEL_TRUECOLOR_ALPHA: g = (p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8; a = p[3]; break;
      case PNG_PIXEL_GRAY_ALPHA: g = p[0]; a = p[1]; break;
      case PNG_PIXEL_INDEXED:
        if (d->pPalette) {
          const uint8_t* pc = &d->pPalette[p[0] * 3];
          g = (pc[0] * 77 + pc[1] * 150 + pc[2] * 29) >> 8;
          if (d->iHasAlpha) a = d->pPalette[768 + p[0]];
        } else {
          g = p[0];
        }
        break;
    }
    c->img->gray[static_cast<size_t>(oy) * c->img->w + ox] = static_cast<uint8_t>((g * a + 255 * (255 - a)) / 255);
  }
  return 1;
}

bool decodeJpeg(const std::string& bytes, xtapp::ImageData& out, std::string& err) {
  auto* jpeg = new (std::nothrow) JPEGDEC();
  if (!jpeg) { err = "out_of_memory"; return false; }
  DecodeCtx ctx;
  ctx.img = &out;
  bool ok = false;
  if (jpeg->openRAM(reinterpret_cast<uint8_t*>(const_cast<char*>(bytes.data())), static_cast<int>(bytes.size()), jpegCb)) {
    if (!allocGray(ctx, jpeg->getWidth(), jpeg->getHeight())) {
      err = "out_of_memory";
    } else {
      jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
      jpeg->setUserPointer(&ctx);
      ok = jpeg->decode(0, 0, 0) == 1;
      if (!ok) err = "decode_failed";
    }
    jpeg->close();
  } else {
    err = "decode_failed";
  }
  delete jpeg;
  return ok;
}

bool decodePng(const std::string& bytes, xtapp::ImageData& out, std::string& err) {
  void* mem = malloc(sizeof(PNG));
  if (!mem) { err = "out_of_memory"; return false; }
  auto* png = new (mem) PNG();
  DecodeCtx ctx;
  ctx.img = &out;
  bool ok = false;
  if (png->openRAM(reinterpret_cast<uint8_t*>(const_cast<char*>(bytes.data())), static_cast<int>(bytes.size()), pngCb) ==
      PNG_SUCCESS) {
    if (png->getWidth() > 2000 || !allocGray(ctx, png->getWidth(), png->getHeight())) {
      err = "out_of_memory";
    } else {
      ok = png->decode(&ctx, 0) == PNG_SUCCESS;
      if (!ok) err = "decode_failed";
    }
    png->close();
  } else {
    err = "decode_failed";
  }
  png->~PNG();
  free(mem);
  return ok;
}
}  // namespace

// ---- Env backed by the SD card -------------------------------------------------------------------------------

struct XtAppActivity::DeviceEnv final : xtapp::Env {
  std::string dir;
  explicit DeviceEnv(std::string d) : dir(std::move(d)) {}

  std::string filesDir() const { return dir + "/files"; }
  void ensureFilesDir() { Storage.ensureDirectoryExists(filesDir().c_str()); }

  bool readPackage(const std::string& rel, std::string& out) override { return readWhole(dir + "/" + rel, out, kMaxFile); }
  bool readFile(const std::string& n, std::string& out) override { return readWhole(filesDir() + "/" + n, out, kMaxFile); }
  bool writeFile(const std::string& n, const std::string& data) override {
    Storage.ensureDirectoryExists(filesDir().c_str());
    return writeWhole(filesDir() + "/" + n, data);
  }
  bool removeFile(const std::string& n) override { return Storage.remove((filesDir() + "/" + n).c_str()); }
  int64_t fileSize(const std::string& n) override {
    HalFile f;
    if (!Storage.openFileForRead("XTAPP", filesDir() + "/" + n, f)) return -1;
    const int64_t s = static_cast<int64_t>(f.size());
    f.close();
    return s;
  }
  std::vector<std::string> listFiles() override {
    std::vector<std::string> v;
    for (const auto& s : Storage.listFiles(filesDir().c_str(), 600)) {
      std::string name = s.c_str();
      if (!name.empty() && name.back() != '/' && name.size() < 5 + 0 + 64 && name.find(".tmp") == std::string::npos) {
        v.push_back(std::move(name));
      }
    }
    return v;
  }
  bool loadState(std::string& out) override { return readWhole(dir + "/state.json", out, 256 * 1024); }
  bool saveState(const std::string& data) override { return writeWhole(dir + "/state.json", data); }
  uint32_t millis() override { return ::millis(); }
  int64_t epochSec() override {
    // The device only exposes local calendar time; it is reported as UTC with a zero offset.
    uint16_t y;
    uint8_t mo, d, h, mi;
    if (!halClock.getDateTime(y, mo, d, h, mi)) return 0;
    return daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + (::millis() / 1000) % 60;
  }
  int batteryPercent() override { return powerManager.getBatteryPercentage(); }
  void log(int level, const std::string& m) override { LOG_INF("XTAPP", "[%d] %s", level, m.c_str()); }
  bool decodeImage(const std::string& bytes, xtapp::ImageData& out, std::string& err) override {
    if (bytes.size() > 8 && static_cast<uint8_t>(bytes[0]) == 0x89 && bytes.compare(1, 3, "PNG") == 0) return decodePng(bytes, out, err);
    if (bytes.size() > 4 && static_cast<uint8_t>(bytes[0]) == 0xFF && static_cast<uint8_t>(bytes[1]) == 0xD8) return decodeJpeg(bytes, out, err);
    err = "unsupported_format";
    return false;
  }
  // ctx.reader: totals, streaks and (since daily_seconds.bin) seconds per day; no per-book per-day rows.
  bool readerSummary(xtapp::ReaderSummary& out) override {
    const GlobalReadingStats st =
        GlobalReadingStats::hasSyncedStats() ? GlobalReadingStats::loadAggregated() : GlobalReadingStats::load();
    ReadingStatsDateTime now;
    const bool haveDate = getCurrentLocalReadingStatsDateTime(now) && now.date.isValid();
    out.totalSec = st.totalReadingSeconds;
    out.todaySec = haveDate ? DailyReadingStore::load().secondsFor(readingStatsDayIndex(now.date)) : 0;
    out.streakDays = st.currentReadingStreak(haveDate ? &now.date : nullptr);
    out.bestStreakDays = st.displayLongestReadingStreak();
    return true;
  }
  static bool parseIsoDate(const std::string& s, ReadingStatsDate& d) {
    int y = 0, m = 0, dd = 0;
    if (s.size() != 10 || sscanf(s.c_str(), "%4d-%2d-%2d", &y, &m, &dd) != 3) return false;
    d.year = static_cast<uint16_t>(y);
    d.month = static_cast<uint8_t>(m);
    d.day = static_cast<uint8_t>(dd);
    return d.isValid();
  }
  bool readerDays(const std::string& from, const std::string& to, std::vector<xtapp::ReaderDay>& out) override {
    ReadingStatsDate a, b;
    if (!parseIsoDate(from, a) || !parseIsoDate(to, b)) return false;
    const uint32_t lo = readingStatsDayIndex(a), hi = readingStatsDayIndex(b);
    for (const auto& e : DailyReadingStore::load().entries()) {
      if (e.dayIndex < lo || e.dayIndex > hi || e.seconds == 0) continue;
      ReadingStatsDate d;
      if (!readingStatsDateFromDayIndex(e.dayIndex, d)) continue;
      char buf[16];
      snprintf(buf, sizeof(buf), "%04u-%02u-%02u", d.year, d.month, d.day);
      out.push_back({buf, e.seconds});
    }
    return true;
  }
  static std::string bookCachePath(const std::string& path) {
    if (FsHelpers::hasEpubExtension(path)) return Epub::cachePathForFilePath(path, "/.inkmod");
    if (FsHelpers::hasXtcExtension(path)) return "/.inkmod/xtc_" + std::to_string(std::hash<std::string>{}(path));
    if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
      return "/.inkmod/txt_" + std::to_string(std::hash<std::string>{}(path));
    }
    return "";
  }
  static xtapp::ReaderBook toReaderBook(const RecentBook& b) {
    xtapp::ReaderBook r;
    r.id = b.path;
    r.title = b.title;
    r.author = b.author;
    const size_t dot = b.path.find_last_of('.');
    if (dot != std::string::npos) {
      r.format = b.path.substr(dot + 1);
      for (auto& ch : r.format) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    }
    const std::string cache = bookCachePath(b.path);
    if (!cache.empty()) {
      const BookReadingStats bs = BookReadingStats::load(cache);
      r.readSec = bs.totalReadingSeconds;
      r.finished = bs.isCompleted;
    }
    return r;
  }
  bool readerBooks(std::vector<xtapp::ReaderBook>& out) override {
    for (const auto& b : RECENT_BOOKS.getBooks()) out.push_back(toReaderBook(b));
    return true;
  }
  bool readerCurrent(xtapp::ReaderBook& out) override {
    const auto& books = RECENT_BOOKS.getBooks();
    if (books.empty()) return false;
    out = toReaderBook(books.front());
    return true;
  }
  std::string lockscreenApp() override { return SETTINGS.lockscreenApp; }
  bool setLockscreenApp(const std::string& id) override {
    if (id.size() >= sizeof(SETTINGS.lockscreenApp)) return false;
    strncpy(SETTINGS.lockscreenApp, id.c_str(), sizeof(SETTINGS.lockscreenApp) - 1);
    SETTINGS.lockscreenApp[sizeof(SETTINGS.lockscreenApp) - 1] = '\0';
    return SETTINGS.saveToFile();
  }
  bool appInstalled(const std::string& id) override { return !XtAppLocator::findDir(id).empty(); }
  bool qrEncode(const std::string& text, xtapp::ImageData& out) override {
    int version = 4;
    const size_t len = text.size();
    if (len > 114) version = 10;
    if (len > 395) version = 20;
    if (len > 1066) version = 30;
    if (len > 2110) version = 40;
    auto bytes = std::make_unique<uint8_t[]>(qrcode_getBufferSize(version));
    QRCode qr;
    if (qrcode_initText(&qr, bytes.get(), version, ECC_LOW, text.c_str()) != 0) return false;
    out.w = out.h = qr.size;
    out.gray.assign(static_cast<size_t>(qr.size) * qr.size, 255);
    for (int y = 0; y < qr.size; ++y)
      for (int x = 0; x < qr.size; ++x)
        if (qrcode_getModule(&qr, x, y)) out.gray[static_cast<size_t>(y) * qr.size + x] = 0;
    return true;
  }
};

// ---- Canvas on top of GfxRenderer ----------------------------------------------------------------------------

struct XtAppActivity::GfxCanvas final : xtapp::Canvas {
  GfxRenderer& r;
  explicit GfxCanvas(GfxRenderer& rr) : r(rr) {}
  int width() const override { return r.getScreenWidth(); }
  int height() const override { return r.getScreenHeight(); }
  void clear(bool black) override { r.clearScreen(black ? 0x00 : 0xFF); }
  void fillRect(int x, int y, int w, int h, bool black) override { r.fillRect(x, y, w, h, black); }
  void line(int x0, int y0, int x1, int y1, bool black) override { r.drawLine(x0, y0, x1, y1, black); }
  void pixel(int x, int y, bool black) override { r.drawPixel(x, y, black); }
  void text(int x, int y, const char* s, int size, bool black) override { r.drawText(fontFor(size), x, y, s, black); }
  int textWidth(const char* s, int size) override { return r.getTextWidth(fontFor(size), s); }
  int lineHeight(int size) override { return r.getLineHeight(fontFor(size)); }
};


bool XtAppActivity::drawLockscreen(GfxRenderer& renderer) {
  if (!SETTINGS.lockscreenApp[0]) return false;
  const std::string dir = XtAppLocator::findDir(SETTINGS.lockscreenApp);
  if (dir.empty()) return false;
  std::string manifest;
  if (!readWhole(dir + "/manifest.json", manifest, kMaxManifest)) return false;
  DeviceEnv env(dir);
  GfxCanvas canvas(renderer);
  xtapp::Limits lim;
  lim.memoryBytes = std::clamp<size_t>(ESP.getMaxAllocHeap() / 2, 64 * 1024, 512 * 1024);
  if (ESP.getMaxAllocHeap() < 80 * 1024) return false;  // not enough heap for a Lua state on the C3
  bool ok = false;
  {
    xtapp::App app(env, canvas, lim);
    if (app.start(manifest, xtapp::App::Mode::Lockscreen) && !app.failed()) {
      renderer.clearScreen();
      app.draw();
      ok = !app.failed();
      app.leave();
    }
  }
  return ok;
}

// ---- network jobs ---------------------------------------------------------------------------------------------

struct XtAppActivity::NetJob {
  xtapp::NetRequest req;
  std::string savePath;
  xtapp::NetResponse resp;
  std::atomic<bool> cancel{false};
  std::atomic<bool> done{false};
};

namespace {
void netTask(void* arg) {
  auto* holder = static_cast<std::shared_ptr<XtAppActivity::NetJob>*>(arg);
  auto job = *holder;  // keeps the job alive until the task ends
  delete holder;
  xtapp_net::request(job->req, job->savePath, job->resp, job->cancel);
  job->done.store(true);
  vTaskDelete(nullptr);
}
}  // namespace

// ---- Activity ------------------------------------------------------------------------------------------------

XtAppActivity::XtAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string appDir,
                             std::string presetError)
    : Activity("XtApp", renderer, mappedInput), appDir_(std::move(appDir)), startError_(std::move(presetError)) {}

XtAppActivity::~XtAppActivity() = default;

void XtAppActivity::onEnter() {
  Activity::onEnter();
  savedOrientation_ = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  canvas_ = std::make_unique<GfxCanvas>(renderer);
  startApp();
}

void XtAppActivity::startApp() {
  env_ = std::make_unique<DeviceEnv>(appDir_);

  std::string manifest;
  if (!startError_.empty()) {
    // Preset (e.g. an encrypted package): nothing to run, the error screen explains why.
  } else if (!readWhole(appDir_ + "/manifest.json", manifest, kMaxManifest)) {
    startError_ = "manifest.json not found";
  } else {
    xtapp::Limits lim;
    lim.memoryBytes = std::clamp<size_t>(ESP.getMaxAllocHeap() / 2, 64 * 1024, 768 * 1024);
    app_ = std::make_unique<xtapp::App>(*env_, *canvas_, lim);
    std::lock_guard<std::mutex> lock(luaMutex_);
    if (!app_->start(manifest)) startError_ = app_->error();
  }
  requestUpdate();
}

void XtAppActivity::stopApp() {
  closeWebsocket();
  for (auto& j : netJobs_) j->cancel.store(true);
  for (int i = 0; i < 300; ++i) {  // up to ~15 s for workers to notice the cancel flag
    bool pending = false;
    for (auto& j : netJobs_) pending = pending || !j->done.load();
    if (!pending) break;
    delay(50);
  }
  netJobs_.clear();
  waitingNet_.clear();
  {
    std::lock_guard<std::mutex> lock(luaMutex_);
    if (app_) app_->leave();
    app_.reset();
  }
}

void XtAppActivity::onExit() {
  stopApp();
  if (wifiByUs_) {
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
    wifiByUs_ = false;
  }
  renderer.setOrientation(savedOrientation_);
  Activity::onExit();
}

void XtAppActivity::feedKey(const char* key, bool& exitRequested) {
  xtapp::Event ev;
  ev.type = xtapp::Event::Type::Key;
  ev.key = key;
  ev.timeMs = millis();
  ev.phase = "down";
  app_->input(ev);
  ev.phase = "up";
  const bool claimed = app_->input(ev);
  if (!claimed && !strcmp(key, "back")) exitRequested = true;
}


void XtAppActivity::failWaitingNet(const char* error) {
  std::lock_guard<std::mutex> lock(luaMutex_);
  if (!app_) return;
  while (!waitingNet_.empty()) {
    xtapp::NetResponse r;
    r.error = error;
    app_->netDone(waitingNet_.front().id, std::move(r));
    waitingNet_.pop_front();
  }
}

// Moves requests from the app to workers (connecting Wi-Fi first) and delivers finished results.
void XtAppActivity::pumpNetwork() {
  {
    std::lock_guard<std::mutex> lock(luaMutex_);
    xtapp::NetRequest r;
    while (app_->takeNetRequest(r)) waitingNet_.push_back(std::move(r));
    int cid;
    while (app_->takeNetCancel(cid)) {
      for (auto& j : netJobs_) {
        if (j->req.id == cid) j->cancel.store(true);
      }
    }
    // keep_wifi: remember the deadline
    if (app_->keepWifiMs()) keepWifiUntil_ = millis() + app_->keepWifiMs();
  }

  // finished workers
  for (size_t i = 0; i < netJobs_.size();) {
    auto& j = netJobs_[i];
    if (!j->done.load()) {
      ++i;
      continue;
    }
    if (!j->cancel.load()) {
      std::lock_guard<std::mutex> lock(luaMutex_);
      app_->netDone(j->req.id, std::move(j->resp));
    }
    netJobs_.erase(netJobs_.begin() + static_cast<long>(i));
  }

  if (waitingNet_.empty()) return;
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiAsked_) return;  // selection already running / was answered
    wifiAsked_ = true;
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& result) {
                             if (result.isCancelled) {
                               failWaitingNet("wifi");
                             } else {
                               wifiByUs_ = true;
                             }
                             wifiAsked_ = false;
                             fullRefresh_ = true;
                             requestUpdate();
                           });
    return;
  }
  while (!waitingNet_.empty() && netJobs_.size() < 2) {
    auto job = std::make_shared<NetJob>();
    job->req = std::move(waitingNet_.front());
    waitingNet_.pop_front();
    if (!job->req.saveName.empty()) {
      env_->ensureFilesDir();
      job->savePath = env_->filesDir() + "/" + job->req.saveName;
    }
    netJobs_.push_back(job);
    auto* holder = new std::shared_ptr<NetJob>(job);
    if (xTaskCreate(netTask, "xtnet", 14 * 1024, holder, 1, nullptr) != pdPASS) {
      delete holder;
      job->resp.error = "out_of_memory";
      job->done.store(true);
    }
  }
}

// ---- ctx.net:websocket ----------------------------------------------------------------------------------------

struct XtAppActivity::WsState {
  struct Ev {
    std::string type, data;
  };
  WebSocketsClient client;
  int id = 0;
  bool open = false;
  std::deque<Ev> events;  // filled by the client callback (same thread as loop())
};

void XtAppActivity::closeWebsocket() {
  if (!ws_) return;
  ws_->client.disconnect();
  ws_.reset();
}

void XtAppActivity::pumpWebsocket() {  // called from loop() with luaMutex_ held
  xtapp::App::WsOpen o;
  if (app_->takeWsOpen(o)) {
    if (WiFi.status() != WL_CONNECTED || ws_) {
      app_->wsEvent(o.id, "error", ws_ ? "busy" : "wifi");
    } else {
      // url: ws[s]://host[:port]/path
      const bool tls = o.url.compare(0, 6, "wss://") == 0;
      std::string rest = o.url.substr(tls ? 6 : 5);
      std::string path = "/";
      const size_t slash = rest.find('/');
      if (slash != std::string::npos) {
        path = rest.substr(slash);
        rest.resize(slash);
      }
      int port = tls ? 443 : 80;
      const size_t colon = rest.rfind(':');
      if (colon != std::string::npos) {
        port = atoi(rest.c_str() + colon + 1);
        rest.resize(colon);
      }
      ws_ = std::make_unique<WsState>();
      ws_->id = o.id;
      WsState* st = ws_.get();
      st->client.onEvent([st](WStype_t type, uint8_t* payload, size_t length) {
        switch (type) {
          case WStype_CONNECTED: st->open = true; st->events.push_back({"open", ""}); break;
          case WStype_TEXT:
          case WStype_BIN:
            if (st->events.size() < 64) st->events.push_back({"message", std::string(reinterpret_cast<char*>(payload), length)});
            break;
          case WStype_DISCONNECTED: st->events.push_back({st->open ? "close" : "error", st->open ? "" : "connect"}); break;
          case WStype_ERROR: st->events.push_back({"error", "connect"}); break;
          default: break;
        }
      });
      st->client.setReconnectInterval(0);
      if (tls) st->client.beginSSL(rest.c_str(), static_cast<uint16_t>(port), path.c_str());
      else st->client.begin(rest.c_str(), static_cast<uint16_t>(port), path.c_str());
    }
  }
  xtapp::App::WsOut out;
  while (app_->takeWsOut(out)) {
    if (!ws_ || ws_->id != out.id) continue;
    if (out.close) ws_->client.disconnect();
    else if (ws_->open) ws_->client.sendTXT(out.data.c_str(), out.data.size());
  }
  if (!ws_) return;
  ws_->client.loop();
  bool gone = false;
  while (!ws_->events.empty()) {
    auto ev = std::move(ws_->events.front());
    ws_->events.pop_front();
    app_->wsEvent(ws_->id, ev.type, ev.data);
    if (ev.type == "close" || ev.type == "error") {
      gone = true;
      break;
    }
  }
  if (gone) {
    ws_->client.disconnect();
    ws_.reset();
  }
}

void XtAppActivity::loop() {
  const bool failed = !startError_.empty() || !app_ || app_->failed();
  if (failed) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      ActivityResult r;
      r.isCancelled = true;
      setResult(std::move(r));
      finish();
    }
    return;
  }

  bool exitRequested = false;
  bool redraw = false;
  bool haveText = false;
  xtapp::App::TextRequest textReq;
  {
    std::lock_guard<std::mutex> lock(luaMutex_);
    xtapp::App::Confirm confirm;
    if (app_->pendingConfirm(confirm)) {  // modal: Confirm = yes, Back = no
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) app_->answerConfirm(true);
      else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) app_->answerConfirm(false);
      else return;
      requestUpdate();
      return;
    }

    using B = MappedInputManager::Button;
    struct Map {
      B b;
      const char* key;
    };
    static const Map map[] = {{B::Up, "up"},       {B::Down, "down"},       {B::Left, "left"},
                              {B::Right, "right"}, {B::Confirm, "ok"},      {B::Back, "back"}};
    for (const auto& m : map) {
      if (mappedInput.wasReleased(m.b)) {
        toast_.clear();
        feedKey(m.key, exitRequested);
        redraw = true;
      }
    }
    if (mappedInput.hasTouch()) {
      int x = 0, y = 0;
      xtapp::Event ev;
      ev.type = xtapp::Event::Type::Pointer;
      ev.timeMs = millis();
      if (mappedInput.wasScreenTapped(x, y)) {
        ev.x = ev.downX = x;
        ev.y = ev.downY = y;
        ev.gesture = "tap";
        ev.phase = "down";
        app_->input(ev);
        ev.phase = "up";
        app_->input(ev);
        redraw = true;
      } else {
        int sx = 0, sy = 0;
        const auto dir = mappedInput.wasSwipeFrom(sx, sy);
        if (dir != MappedInputManager::SwipeDir::None) {
          ev.x = ev.downX = sx;
          ev.y = ev.downY = sy;
          ev.phase = "up";
          ev.gesture = dir == MappedInputManager::SwipeDir::Left    ? "swipe_left"
                       : dir == MappedInputManager::SwipeDir::Right ? "swipe_right"
                       : dir == MappedInputManager::SwipeDir::Up    ? "swipe_up"
                                                                    : "swipe_down";
          app_->input(ev);
          redraw = true;
        }
      }
    }

    haveText = app_->takeTextRequest(textReq);
    if (!haveText) app_->tick();
    pumpWebsocket();
    keepAwake_ = app_->hasTickHandler() || !netJobs_.empty() || ws_;
    std::string t;
    if (app_->takeToast(t)) {
      toast_ = t;
      lastToastMs_ = millis();
      redraw = true;
    }
    if (!toast_.empty() && millis() - lastToastMs_ > 2500) {
      toast_.clear();
      redraw = true;
    }
    bool full = false;
    if (app_->consumeRedraw(&full)) {
      redraw = true;
      fullRefresh_ = fullRefresh_ || full;
    }
    if (app_->quitRequested()) exitRequested = true;
  }

  if (!haveText) pumpNetwork();
  if (haveText) {
    const size_t maxBytes = textReq.maxBytes;
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput,
                                                textReq.title.empty() ? std::string(tr(STR_ENTER_TEXT)) : textReq.title,
                                                textReq.text, textReq.maxChars),
        [this, maxBytes](const ActivityResult& result) {
          std::lock_guard<std::mutex> lock(luaMutex_);
          if (!app_) return;
          if (result.isCancelled) {
            app_->answerText(false, "");
          } else {
            std::string text = std::get<KeyboardResult>(result.data).text;
            if (maxBytes && text.size() > maxBytes) {
              size_t cut = maxBytes;
              while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;  // stay on a UTF-8 boundary
              text.resize(cut);
            }
            app_->answerText(true, text);
          }
          fullRefresh_ = true;
          requestUpdate();
        });
    return;
  }
  if (exitRequested) {
    // ctx.system.open(id): continue in another installed app instead of going back to the list.
    std::string openId;
    {
      std::lock_guard<std::mutex> lock(luaMutex_);
      if (app_) app_->takeOpenRequest(openId);
    }
    if (!openId.empty()) {
      const std::string dir = XtAppLocator::findDir(openId);
      if (!dir.empty()) {
        stopApp();
        appDir_ = dir;
        startError_.clear();
        startApp();
        fullRefresh_ = true;
        requestUpdate();
        return;
      }
    }
    ActivityResult r;
    r.isCancelled = true;
    setResult(std::move(r));
    finish();
    return;
  }
  if (redraw) requestUpdate();
}

void XtAppActivity::drawMessage(const char* title, const std::string& text) const {
  renderer.clearScreen();
  const int w = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, w, metrics.headerHeight}, title);
  int y = metrics.topPadding + metrics.headerHeight + 30;
  const int lh = renderer.getLineHeight(UI_10_FONT_ID);
  for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, text.c_str(), w - 40, 24)) {
    renderer.drawText(UI_10_FONT_ID, 20, y, line.c_str());
    y += lh + 4;
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void XtAppActivity::drawOverlays() {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  const int lh = renderer.getLineHeight(UI_10_FONT_ID);
  auto box = [&](const std::string& title, const std::string& body, bool confirm) {
    const int bw = w - 80;
    const auto lines = renderer.wrappedText(UI_10_FONT_ID, body.c_str(), bw - 40, 8);
    const int bh = 40 + (title.empty() ? 0 : lh + 12) + static_cast<int>(lines.size()) * (lh + 4) + (confirm ? lh + 24 : 0);
    const int bx = 40, by = (h - bh) / 2;
    renderer.fillRect(bx - 3, by - 3, bw + 6, bh + 6, true);
    renderer.fillRect(bx, by, bw, bh, false);
    int y = by + 20;
    if (!title.empty()) {
      renderer.drawText(UI_10_FONT_ID, bx + 20, y, title.c_str());
      y += lh + 12;
    }
    for (const auto& l : lines) {
      renderer.drawText(UI_10_FONT_ID, bx + 20, y, l.c_str());
      y += lh + 4;
    }
    if (confirm) {
      renderer.drawText(UI_10_FONT_ID, bx + 20, y + 12, (std::string(tr(STR_BACK)) + "  /  " + tr(STR_OPEN)).c_str());
    }
  };
  xtapp::App::Confirm c;
  if (app_->pendingConfirm(c)) {
    box(c.title, c.text, true);
  } else if (!toast_.empty()) {
    box("", toast_, false);
  }
}

void XtAppActivity::render(RenderLock&&) {
  if (!startError_.empty()) {
    drawMessage(tr(STR_APPS), startError_);
    renderer.displayBuffer();
    return;
  }
  if (!app_) return;
  {
    std::lock_guard<std::mutex> lock(luaMutex_);
    if (app_->failed()) {
      drawMessage(tr(STR_APPS), app_->error());
      renderer.displayBuffer();
      return;
    }
    renderer.clearScreen();
    app_->draw();
    if (app_->failed()) {
      drawMessage(tr(STR_APPS), app_->error());
      renderer.displayBuffer();
      return;
    }
    drawOverlays();
  }
  HalDisplay::RefreshMode mode = HalDisplay::FAST_REFRESH;
  if (fullRefresh_) {
    mode = HalDisplay::HALF_REFRESH;
    fullRefresh_ = false;
    fastFrames_ = 0;
  } else if (++fastFrames_ >= 20) {
    mode = HalDisplay::HALF_REFRESH;
    fastFrames_ = 0;
  }
  renderer.displayBuffer(mode);
}
