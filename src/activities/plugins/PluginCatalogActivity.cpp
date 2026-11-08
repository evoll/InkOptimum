#include "PluginCatalogActivity.h"

#include <ArduinoJson.h>
#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <MD5Builder.h>
#include <PluginDetails.h>
#include <PluginRequest.h>
#include <PluginToken.h>
#include <PluginXml.h>
#include <ProtectedPaths.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"
#include "util/PluginEventBus.h"
#include "util/QrUtils.h"
#include "util/PluginLocations.h"
#include "util/TouchListNavigation.h"

using plugincat::Item;
using plugincat::Request;
using plugincat::Vars;

namespace {

constexpr char TMP_RESPONSE[] = "/.inkmod/plugin_catalog.tmp";
constexpr char DEFAULT_PLUGIN_ROOT[] = "/.inkmod/plugins";
constexpr size_t MAX_SMALL_RESPONSE = 48 * 1024;
constexpr int MAX_BUNDLE_FILES = 64;

bool wifiReady() { return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0); }

// ArduinoJson reader over a card file with a small read-ahead buffer.
struct FileReader {
  FsFile& file;
  uint8_t buf[256];
  size_t pos = 0, len = 0;
  explicit FileReader(FsFile& f) : file(f) {}
  bool fill() {
    const int n = file.read(buf, sizeof(buf));
    pos = 0;
    len = n > 0 ? static_cast<size_t>(n) : 0;
    return len > 0;
  }
  int read() { return (pos < len || fill()) ? buf[pos++] : -1; }
  size_t readBytes(char* out, size_t n) {
    size_t got = 0;
    while (got < n) {
      const int c = read();
      if (c < 0) break;
      out[got++] = static_cast<char>(c);
    }
    return got;
  }
};

bool readTextFile(const std::string& path, std::string& out) {
  if (path.empty() || !Storage.exists(path.c_str())) return false;
  const String s = Storage.readFile(path.c_str());
  out.assign(s.c_str(), s.length());
  return true;
}

std::string jsonPathString(JsonVariantConst root, const std::string& path) {
  return plugincat::variantToString(plugincat::resolvePath(root, path));
}

std::string joinUrl(const std::string& base, const std::string& rel) {
  if (base.empty() || base.back() == '/') return base + rel;
  return base + "/" + rel;
}

// A file name from a bundle entry: relative, no climbing, no backslashes.
bool safeRelativePath(const std::string& p) {
  if (p.empty() || p.size() > 120 || p[0] == '/' || p.find("..") != std::string::npos ||
      p.find('\\') != std::string::npos || p.find(':') != std::string::npos) {
    return false;
  }
  return p.back() != '/';
}

void ensureParentDirs(const std::string& path) {
  for (size_t i = 1; i < path.size(); i++) {
    if (path[i] == '/') Storage.mkdir(path.substr(0, i).c_str(), true);
  }
}

}  // namespace

// ---------------------------------------------------------------- lifecycle

void PluginCatalogActivity::onEnter() {
  Activity::onEnter();
  if (!loadManifest()) {
    fail(tr(STR_PLUGIN_BAD_MANIFEST), Retry::None);
    return;
  }
  state_ = State::CONNECTING;
  statusText_ = tr(STR_LOADING);
  requestUpdate();
  connect();
}

void PluginCatalogActivity::onExit() {
  http_.reset();
  Storage.remove(TMP_RESPONSE);
  Activity::onExit();
}

bool PluginCatalogActivity::loadManifest() {
  std::string json;
  if (!readTextFile(pluginDir_ + "/device.json", json)) return false;
  if (!plugincat::parseDeviceManifest(json, pluginDir_, manifest_) || !manifest_.isCatalog()) return false;
  if (!manifest_.title.empty()) title_ = manifest_.title;
  lists_ = manifest_.lists;

  secretVars_.clear();
  std::string text;
  if (readTextFile(manifest_.tokenFile, text)) {
    JsonDocument doc;
    if (deserializeJson(doc, text) == DeserializationError::Ok) {
      secretVars_.emplace_back("token", jsonPathString(doc.as<JsonVariantConst>(), manifest_.tokenPath));
    }
  } else {
    secretVars_.emplace_back("token", "");
  }
  if (readTextFile(manifest_.configFile, text)) {
    JsonDocument doc;
    if (deserializeJson(doc, text) == DeserializationError::Ok && doc.is<JsonObject>()) {
      for (JsonPairConst kv : doc.as<JsonObjectConst>()) {
        secretVars_.emplace_back(std::string("cfg.") + kv.key().c_str(), plugincat::variantToString(kv.value()));
      }
    }
  }
  return true;
}

void PluginCatalogActivity::connect() {
  if (wifiReady()) {
    onWifiResult(true);
    return;
  }
  state_ = State::WIFI_SELECTION;
  requestUpdate();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& r) { onWifiResult(!r.isCancelled); });
}

void PluginCatalogActivity::onWifiResult(bool connected) {
  if (!connected) {
    fail(tr(STR_WIFI_CONN_FAILED), Retry::None);
    return;
  }
  http_ = std::make_unique<PluginHttp>();
  startBrowsing();
}

// ---------------------------------------------------------------- requests

Vars PluginCatalogActivity::baseVars() const {
  Vars vars = secretVars_;
  vars.emplace_back("firmware_family", plugincat::identity::FAMILY);
  vars.emplace_back("firmware_version", "inkmod");
  vars.emplace_back("plugin_api", std::to_string(plugincat::identity::PLUGIN_API));
  vars.emplace_back("plugin_capabilities", plugincat::identity::CAPABILITIES);
  return vars;
}

Request PluginCatalogActivity::build(const Request& tpl, const Vars& vars, const Vars& itemVars) const {
  return plugincat::substituteRequest(tpl, vars, itemVars);
}

bool PluginCatalogActivity::pollCancel() {
  if (cancelled_) return true;
  mappedInput.update();
  if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    cancelled_ = true;
  }
  return cancelled_;
}

void PluginCatalogActivity::failHttp(const PluginHttp::Result& r) {
  if (r.cancelled) {
    fail(tr(STR_PLUGIN_CANCELLED), Retry::None);
    return;
  }
  const std::string rejected = PluginHttp::takeRejectedHost();
  if (!rejected.empty()) {
    fail(std::string(tr(STR_TLS_CHANGED)) + ": " + rejected, Retry::None);
  } else if (r.tooLarge) {
    fail(tr(STR_PLUGIN_BAD_RESPONSE), Retry::None);
  } else if (r.writeFailed) {
    fail(tr(STR_PLUGIN_SD_WRITE_FAILED), Retry::None);
  } else if (r.status > 0) {
    char msg[48];
    snprintf(msg, sizeof(msg), "HTTP %d", r.status);
    fail(msg, retry_);
  } else {
    fail(tr(STR_PLUGIN_NO_CONNECTION), retry_);
  }
}

void PluginCatalogActivity::fail(const std::string& message, Retry retry) {
  errorText_ = message;
  retry_ = retry;
  state_ = State::ERROR;
  requestUpdate();
}

void PluginCatalogActivity::showMessage(const std::string& message) {
  statusText_ = message;
  state_ = State::MESSAGE;
  requestUpdate();
}

// ---------------------------------------------------------------- browsing

void PluginCatalogActivity::startBrowsing() {
  if (needsToken() && currentToken().empty()) {
    using AuthType = plugincat::Auth::Type;
    if (manifest_.auth.type == AuthType::None) {
      fail(tr(STR_PLUGIN_NOT_SIGNED_IN), Retry::None);
      return;
    }
    if (manifest_.auth.type == AuthType::Password) {
      state_ = State::LOADING;
      statusText_ = tr(STR_LOADING);
      requestUpdateAndWait();
      if (!mintPasswordToken()) {
        fail(tr(STR_PLUGIN_SIGN_IN_FAILED), Retry::None);
        return;
      }
    } else {
      state_ = State::SIGN_IN_PROMPT;
      requestUpdate();
      return;
    }
  }
  if (!manifest_.listsUrl.empty()) {
    loadLists();
  } else if (!lists_.empty()) {
    state_ = State::LISTS;
    selected_ = 0;
    requestUpdate();
  } else {
    activeList_ = -1;
    loadPage(1);
  }
}

void PluginCatalogActivity::loadLists() {
  retry_ = Retry::Lists;
  cancelled_ = false;
  state_ = State::LOADING;
  statusText_ = tr(STR_LOADING);
  requestUpdateAndWait();

  Request tpl = manifest_.browse;
  tpl.url = manifest_.listsUrl;
  std::string body;
  const auto result = http_->fetchString(build(tpl, baseVars(), {}), body, MAX_SMALL_RESPONSE, [this] { return pollCancel(); });
  if (!result.ok()) {
    if (!recoverAuth(result, Retry::Lists)) failHttp(result);
    return;
  }
  authRetried_ = false;
  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    fail(tr(STR_PLUGIN_BAD_RESPONSE), Retry::Lists);
    return;
  }
  lists_ = manifest_.lists;
  for (JsonVariantConst l : doc["lists"].as<JsonArrayConst>()) {
    plugincat::BrowseList entry;
    entry.title = l["title"] | "";
    entry.url = l["url"] | "";
    plugincat::resolveServerPath(entry.url, tpl.url);
    entry.body = l["body"] | "";
    entry.notice.title = l["notice"]["title"] | "";
    entry.notice.message = l["notice"]["message"] | "";
    if (!entry.title.empty()) lists_.push_back(std::move(entry));
  }
  if (lists_.empty()) {
    activeList_ = -1;
    loadPage(1);
    return;
  }
  state_ = State::LISTS;
  selected_ = 0;
  requestUpdate();
}

void PluginCatalogActivity::openList(size_t index) {
  if (index >= lists_.size()) return;
  const auto& list = lists_[index];
  if (list.notice.present()) {
    startActivityForResult(std::make_unique<ConfirmationActivity>(
                               renderer, mappedInput, list.notice.title.empty() ? list.title : list.notice.title,
                               list.notice.message),
                           [this, index](const ActivityResult& r) {
                             if (r.isCancelled) {
                               requestUpdate();
                               return;
                             }
                             activeList_ = static_cast<int>(index);
                             loadPage(1);
                           });
    return;
  }
  activeList_ = static_cast<int>(index);
  loadPage(1);
}

void PluginCatalogActivity::loadPage(int page) {
  if (manifest_.xml) {
    page_ = 1;
    if (xmlUrl_.empty()) xmlUrl_ = plugincat::substitute(manifest_.browse.url, baseVars());
    loadXml();
    return;
  }
  page_ = std::max(1, page);
  retry_ = Retry::Page;
  cancelled_ = false;
  state_ = State::LOADING;
  statusText_ = tr(STR_LOADING);
  requestUpdateAndWait();

  Request tpl = manifest_.browse;
  if (activeList_ >= 0) {
    const auto& list = lists_[activeList_];
    if (!list.url.empty()) tpl.url = list.url;
    if (!list.body.empty()) tpl.body = list.body;
  }
  const Vars vars = baseVars();
  const plugincat::Paging paging = plugincat::pagingOf(tpl);
  const Vars itemVars = {{"page", std::to_string(page_)},
                         {"limit", std::to_string(plugincat::limitFor(paging, manifest_.pageSize))},
                         {"offset", std::to_string((page_ - 1) * manifest_.pageSize)}};

  const auto result = http_->fetchToFile(build(tpl, vars, itemVars), TMP_RESPONSE, [this] { return pollCancel(); });
  if (!result.ok()) {
    if (!recoverAuth(result, Retry::Page)) failHttp(result);
    return;
  }
  authRetried_ = false;

  JsonDocument filter;
  const bool filtered = plugincat::buildItemFilter(manifest_, filter);
  JsonDocument doc;
  DeserializationError err;
  {
    FsFile file;
    if (!Storage.openFileForRead("PLG", TMP_RESPONSE, file)) {
      fail(tr(STR_PLUGIN_BAD_RESPONSE), Retry::Page);
      return;
    }
    FileReader reader(file);
    err = filtered ? deserializeJson(doc, reader, DeserializationOption::Filter(filter)) : deserializeJson(doc, reader);
    file.close();
  }
  Storage.remove(TMP_RESPONSE);
  if (err != DeserializationError::Ok) {
    LOG_ERR("PLG", "Catalog parse: %s", err.c_str());
    fail(tr(STR_PLUGIN_BAD_RESPONSE), Retry::Page);
    return;
  }

  std::vector<Item> items;
  bool more = false;
  // A static list holds every row on every page: slice it here. Server-paged requests are sliced by the server.
  const size_t skip = paging == plugincat::Paging::Static ? static_cast<size_t>(page_ - 1) * manifest_.pageSize : 0;
  plugincat::extractItems(doc.as<JsonVariantConst>(), manifest_, items, more, skip,
                          paging == plugincat::Paging::PageLimit);
  buildRows(items, more);
}

void PluginCatalogActivity::loadXml() {
  retry_ = Retry::Page;
  cancelled_ = false;
  state_ = State::LOADING;
  statusText_ = tr(STR_LOADING);
  requestUpdateAndWait();

  Request tpl = manifest_.browse;
  tpl.url = xmlUrl_;
  const auto result = http_->fetchToFile(build(tpl, baseVars(), {}), TMP_RESPONSE, [this] { return pollCancel(); });
  if (!result.ok()) {
    if (!recoverAuth(result, Retry::Page)) failHttp(result);
    return;
  }
  authRetried_ = false;

  plugincat::XmlOptions options;
  options.itemName = manifest_.xmlItem;
  options.containerName = manifest_.xmlContainer;
  options.fields = {manifest_.titlePath, manifest_.authorPath,  manifest_.idPath,
                    manifest_.urlPath,   manifest_.versionPath, manifest_.descriptionPath};
  options.extensions = manifest_.xmlExtensions;
  options.skipSelf = manifest_.xmlSkipSelf;
  options.resolveUrls = manifest_.xmlResolveUrls;
  options.requestUrl = xmlUrl_;
  plugincat::XmlCatalogParser parser(options);

  bool wellFormed = true;
  {
    FsFile file;
    if (!Storage.openFileForRead("PLG", TMP_RESPONSE, file)) {
      fail(tr(STR_PLUGIN_BAD_RESPONSE), Retry::Page);
      return;
    }
    char buf[2048];
    while (wellFormed) {
      const int n = file.read(buf, sizeof(buf));
      if (n <= 0) break;
      wellFormed = parser.feed(buf, static_cast<size_t>(n), false);
      if (parser.truncated()) break;
    }
    if (wellFormed && !parser.truncated()) wellFormed = parser.feed("", 0, true);
    file.close();
  }
  Storage.remove(TMP_RESPONSE);
  if (!wellFormed && parser.entries().empty()) {
    fail(tr(STR_PLUGIN_BAD_RESPONSE), Retry::Page);
    return;
  }

  rows_.clear();
  for (const auto& e : parser.entries()) {
    Row row;
    row.item.title = e.title;
    row.item.author = e.author;
    row.item.id = e.id;
    row.item.url = e.url;
    row.item.version = e.version;
    row.item.description = e.description;
    row.folder = e.folder;
    rows_.push_back(std::move(row));
  }
  selected_ = 0;
  state_ = State::ITEMS;
  requestUpdate();
}

void PluginCatalogActivity::buildRows(const std::vector<Item>& items, bool more) {
  rows_.clear();
  if (page_ > 1) {
    Row prev;
    prev.kind = RowKind::Prev;
    rows_.push_back(std::move(prev));
  }
  for (const auto& item : items) {
    Row row;
    row.item = item;
    annotateInstalled(row);
    rows_.push_back(std::move(row));
  }
  // Plugins with an update waiting go first (feed order kept inside each group). The ranking is per page: the
  // feed is streamed one page at a time, so an update on page 3 is not pulled to page 1.
  {
    const auto first = rows_.begin() + (page_ > 1 ? 1 : 0);
    std::stable_partition(first, rows_.end(), [](const Row& r) { return r.update; });
  }
  if (more) {
    Row next;
    next.kind = RowKind::Next;
    rows_.push_back(std::move(next));
  }
  selected_ = page_ > 1 && rows_.size() > 1 ? 1 : 0;
  state_ = State::ITEMS;
  requestUpdate();
}

std::string PluginCatalogActivity::bundleDirFor(const Item& item) const {
  const std::string sub = plugincat::sanitizeFileName(
      plugincat::substitute(manifest_.bundleSubdir, {{"id", item.id}, {"title", item.title}}), 60);
  const std::string root = manifest_.destDir.empty() ? DEFAULT_PLUGIN_ROOT : manifest_.destDir;
  if (PluginLocations::shouldReuseInstalledBundleDir(root, sub)) {
    const std::string existing = PluginLocations::findPluginDirOnDisk(sub.c_str());
    if (!existing.empty()) return existing;
  }
  return root + "/" + sub;
}

void PluginCatalogActivity::annotateInstalled(Row& row) const {
  if (!manifest_.tracksInstalls()) return;
  const std::string dir = bundleDirFor(row.item);
  std::string text;
  if (!readTextFile(dir + "/manifest.json", text)) {
    row.installed = Storage.exists((dir + "/plugin.js").c_str());
    row.update = row.installed;
    return;
  }
  JsonDocument doc;
  std::string installedVersion;
  if (deserializeJson(doc, text) == DeserializationError::Ok) installedVersion = doc["version"] | "";
  row.installed = true;
  row.update = PluginLocations::isNewerVersion(row.item.version, installedVersion);
}

// ---------------------------------------------------------------- sign-in

std::string PluginCatalogActivity::currentToken() const {
  for (const auto& v : secretVars_) {
    if (v.first == "token") return v.second;
  }
  return {};
}

void PluginCatalogActivity::setToken(const std::string& token) {
  bool found = false;
  for (auto& v : secretVars_) {
    if (v.first == "token") {
      v.second = token;
      found = true;
    }
  }
  if (!found) secretVars_.emplace_back("token", token);
  if (manifest_.tokenFile.empty()) return;
  const std::string json = plugincat::tokenFileJson(manifest_.tokenPath, token);
  if (!json.empty()) Storage.writeFile(manifest_.tokenFile.c_str(), String(json.c_str()));
}

bool PluginCatalogActivity::mintPasswordToken() {
  std::string body;
  const auto r = http_->fetchString(build(manifest_.auth.request, baseVars(), {}), body, MAX_SMALL_RESPONSE);
  if (!r.ok()) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) return false;
  const std::string token = jsonPathString(doc.as<JsonVariantConst>(), manifest_.auth.tokenPath);
  if (token.empty()) return false;
  setToken(token);
  return true;
}

bool PluginCatalogActivity::recoverAuth(const PluginHttp::Result& r, Retry retry) {
  if (r.status != 401 && r.status != 403) return false;
  if (!needsToken()) return false;
  using AuthType = plugincat::Auth::Type;
  if (manifest_.auth.type == AuthType::Password && !authRetried_) {
    authRetried_ = true;
    if (mintPasswordToken()) {
      if (retry == Retry::Lists) {
        loadLists();
      } else {
        loadPage(page_);
      }
      return true;
    }
    fail(tr(STR_PLUGIN_SIGN_IN_FAILED), Retry::None);
    return true;
  }
  if (manifest_.auth.type == AuthType::DeviceCode) {
    setToken("");
    state_ = State::SIGN_IN_PROMPT;
    requestUpdate();
    return true;
  }
  fail(tr(STR_PLUGIN_NOT_SIGNED_IN), Retry::None);
  return true;
}

void PluginCatalogActivity::beginDeviceCode() {
  cancelled_ = false;
  state_ = State::LOADING;
  statusText_ = tr(STR_LOADING);
  requestUpdateAndWait();

  std::string body;
  const auto r = http_->fetchString(build(manifest_.auth.request, baseVars(), {}), body, MAX_SMALL_RESPONSE,
                                    [this] { return pollCancel(); });
  JsonDocument doc;
  if (!r.ok() || deserializeJson(doc, body) != DeserializationError::Ok) {
    fail(tr(STR_PLUGIN_SIGN_IN_FAILED), Retry::None);
    return;
  }
  const auto root = doc.as<JsonVariantConst>();
  const auto& a = manifest_.auth;
  signInCode_ = jsonPathString(root, a.codePath);
  signInUrl_ = jsonPathString(root, a.verifyUrlPath);
  deviceCode_ = jsonPathString(root, a.deviceCodePath);
  if (signInCode_.empty() || deviceCode_.empty()) {
    fail(tr(STR_PLUGIN_SIGN_IN_FAILED), Retry::None);
    return;
  }
  const long interval = atol(jsonPathString(root, a.intervalPath).c_str());
  const long expires = atol(jsonPathString(root, a.expiresPath).c_str());
  pollIntervalMs_ = static_cast<uint32_t>(std::clamp(interval > 0 ? interval : 5L, 2L, 30L)) * 1000;
  signInDeadlineMs_ = millis() + static_cast<uint32_t>(std::clamp(expires > 0 ? expires : 600L, 30L, 1800L)) * 1000;
  nextPollMs_ = millis() + pollIntervalMs_;
  state_ = State::SIGN_IN_CODE;
  requestUpdate();
}

void PluginCatalogActivity::pollDeviceCode() {
  if (static_cast<int32_t>(millis() - signInDeadlineMs_) >= 0) {
    fail(tr(STR_PLUGIN_SIGN_IN_EXPIRED), Retry::None);
    return;
  }
  if (static_cast<int32_t>(millis() - nextPollMs_) < 0) return;
  nextPollMs_ = millis() + pollIntervalMs_;

  Vars vars = baseVars();
  vars.emplace_back("device_code", deviceCode_);
  std::string body;
  const auto r = http_->fetchString(build(manifest_.auth.poll, vars, {}), body, MAX_SMALL_RESPONSE);
  JsonDocument doc;
  const bool parsed = deserializeJson(doc, body) == DeserializationError::Ok;
  const auto root = doc.as<JsonVariantConst>();
  const auto& a = manifest_.auth;
  if (parsed) {
    const std::string token = jsonPathString(root, a.tokenPath);
    if (!token.empty()) {
      setToken(token);
      startBrowsing();
      return;
    }
    const std::string error = jsonPathString(root, a.errorPath);
    if (error == "slow_down") {
      pollIntervalMs_ += 5000;
      return;
    }
    if (error == "expired_token") {
      fail(tr(STR_PLUGIN_SIGN_IN_EXPIRED), Retry::None);
      return;
    }
    if (error == "access_denied") {
      fail(tr(STR_PLUGIN_SIGN_IN_FAILED), Retry::None);
      return;
    }
  }
  (void)r;  // authorization_pending, or a hiccup: keep polling until the code expires
}

// ---------------------------------------------------------------- install

// Details page (catalog.item_details): row metadata, then the item's details_url text when it has one. The
// extra fetch is bounded (24 KiB) and optional: any failure falls back to the metadata alone, and Install /
// Update never depends on it.
void PluginCatalogActivity::showDetails(size_t rowIndex) {
  detailsRow_ = rowIndex;
  const Item& item = rows_[rowIndex].item;
  std::string text;
  auto addLine = [&text](const std::string& label, const std::string& value) {
    if (value.empty()) return;
    text += label.empty() ? value : label + ": " + value;
    text += '\n';
  };
  addLine("", item.title);
  addLine("", item.detailAuthor.empty() ? item.author : item.detailAuthor);
  if (!item.version.empty()) addLine("", std::string("v") + item.version);
  addLine("", item.repository);
  if (!item.description.empty()) text += "\n" + item.description + "\n";

  std::string detailsUrl = item.detailsUrl;
  plugincat::resolveServerPath(detailsUrl, plugincat::substitute(manifest_.browse.url, baseVars()));
  if (detailsUrl.rfind("http", 0) == 0) {
    state_ = State::LOADING;
    statusText_ = tr(STR_LOADING);
    requestUpdateAndWait();
    Request req;
    req.url = detailsUrl;
    std::string body;
    const auto r = http_->fetchString(req, body, plugincat::MAX_DETAILS_BYTES, [this] { return pollCancel(); });
    if (r.ok() && !body.empty()) {
      text += "\n" + plugincat::detailsToText(body, item.detailsFormat);
    }
  }

  detailsLines_.clear();
  const int width = renderer.getScreenWidth() - 2 * UITheme::getInstance().getMetrics().contentSidePadding;
  constexpr size_t kMaxLines = 400;
  size_t pos = 0;
  while (pos <= text.size() && detailsLines_.size() < kMaxLines) {
    size_t nl = text.find('\n', pos);
    if (nl == std::string::npos) nl = text.size();
    const std::string paragraph = text.substr(pos, nl - pos);
    if (paragraph.empty()) {
      detailsLines_.emplace_back();
    } else {
      for (auto& line : renderer.wrappedText(UI_10_FONT_ID, paragraph.c_str(), width, 60)) {
        if (detailsLines_.size() >= kMaxLines) break;
        detailsLines_.push_back(std::move(line));
      }
    }
    pos = nl + 1;
  }
  detailsScroll_ = 0;
  state_ = State::DETAILS;
  requestUpdate();
}

void PluginCatalogActivity::confirmInstall(size_t rowIndex) {
  const Item item = rows_[rowIndex].item;
  const bool update = rows_[rowIndex].update;
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, item.title,
                                             update ? tr(STR_PLUGIN_UPDATE_CONFIRM) : tr(STR_PLUGIN_INSTALL_CONFIRM)),
      [this, item](const ActivityResult& r) {
        if (r.isCancelled) {
          requestUpdate();
          return;
        }
        install(item);
      });
}

void PluginCatalogActivity::onProgress(size_t done, size_t total) {
  progressDone_ = done;
  progressTotal_ = total;
  const uint32_t now = millis();
  if (now - lastUiMs_ >= 2500) {
    lastUiMs_ = now;
    requestUpdate(true);
  }
}

void PluginCatalogActivity::install(const Item& item) {
  cancelled_ = false;
  retry_ = Retry::None;
  progressDone_ = progressTotal_ = 0;
  lastUiMs_ = millis();
  state_ = State::DOWNLOADING;
  statusText_ = item.title;
  requestUpdateAndWait();

  const bool ok = manifest_.isBundle() && !item.files.empty() ? installBundle(item) : installFile(item);
  if (!ok) return;  // fail() already set the error screen
  Storage.remove(TMP_RESPONSE);
  for (auto& row : rows_) {
    if (row.kind == RowKind::Item && row.item.id == item.id && row.item.title == item.title) annotateInstalled(row);
  }
  showMessage(tr(STR_PLUGIN_DONE));
}

bool PluginCatalogActivity::installBundle(const Item& item) {
  if (static_cast<int>(item.files.size()) > MAX_BUNDLE_FILES || item.base.empty()) {
    fail(tr(STR_PLUGIN_FILE_UNSAFE), Retry::None);
    return false;
  }
  for (const auto& f : item.files) {
    if (!safeRelativePath(f)) {
      fail(tr(STR_PLUGIN_FILE_UNSAFE), Retry::None);
      return false;
    }
  }
  const std::string dir = bundleDirFor(item);
  if (!protectedpaths::isPluginPath(dir + "/x")) {
    fail(tr(STR_PLUGIN_FILE_UNSAFE), Retry::None);
    return false;
  }
  Storage.mkdir(dir.c_str(), true);
  const std::string marker = dir + "/" + PluginLocations::INSTALL_MARKER;
  {
    FsFile m;
    if (Storage.openFileForWrite("PLG", marker, m)) m.close();
  }

  const Vars vars = baseVars();
  const Vars itemVars = {{"id", item.id}, {"title", item.title}};
  http_->setBasicAuth(plugincat::substitute(manifest_.dlUser, vars), plugincat::substitute(manifest_.dlPass, vars));

  std::vector<std::string> staged;
  auto cleanup = [&] {
    for (const auto& s : staged) Storage.remove(s.c_str());
  };
  for (const auto& rel : item.files) {
    Request req;
    req.url = joinUrl(item.base, rel);
    req.headers = build(manifest_.download, vars, itemVars).headers;
    const std::string dest = dir + "/" + rel;
    const std::string part = dest + ".new";
    ensureParentDirs(part);
    const auto r = http_->fetchToFile(req, part, [this] { return pollCancel(); },
                                      [this](size_t d, size_t t) { onProgress(d, t); });
    if (!r.ok()) {
      cleanup();
      Storage.remove(marker.c_str());
      http_->setBasicAuth("", "");
      failHttp(r);
      return false;
    }
    staged.push_back(part);
  }
  http_->setBasicAuth("", "");
  for (size_t i = 0; i < staged.size(); i++) {
    const std::string dest = dir + "/" + item.files[i];
    Storage.remove(dest.c_str());
    Storage.rename(staged[i].c_str(), dest.c_str());
  }
  Storage.remove(marker.c_str());
  return true;
}

bool PluginCatalogActivity::installFile(const Item& item) {
  const Vars vars = baseVars();
  const Vars itemVars = {{"id", item.id}, {"title", item.title}, {"author", item.author}, {"url", item.url}};
  Vars nameVars = vars;
  nameVars.insert(nameVars.end(), itemVars.begin(), itemVars.end());

  http_->setBasicAuth(plugincat::substitute(manifest_.dlUser, vars), plugincat::substitute(manifest_.dlPass, vars));
  Request req = build(manifest_.download, vars, itemVars);

  if (!manifest_.dlUrlPath.empty()) {  // one hop: the first response names the file
    std::string body;
    const auto hop = http_->fetchString(req, body, MAX_SMALL_RESPONSE, [this] { return pollCancel(); });
    if (!hop.ok()) {
      http_->setBasicAuth("", "");
      failHttp(hop);
      return false;
    }
    JsonDocument doc;
    std::string url;
    if (deserializeJson(doc, body) == DeserializationError::Ok) url = jsonPathString(doc.as<JsonVariantConst>(), manifest_.dlUrlPath);
    if (url.empty()) {
      http_->setBasicAuth("", "");
      fail(tr(STR_PLUGIN_BAD_RESPONSE), Retry::None);
      return false;
    }
    plugincat::resolveServerPath(url, req.url);  // the hop may answer with a server path
    req.url = url;
    req.method = "GET";
    req.body.clear();
  } else if (req.url.empty()) {
    req.url = item.url;
  }
  if (req.url.empty()) {
    http_->setBasicAuth("", "");
    fail(tr(STR_PLUGIN_BAD_RESPONSE), Retry::None);
    return false;
  }

  const std::string name = plugincat::sanitizeFileName(plugincat::substitute(manifest_.filenameTemplate, nameVars));
  std::string dir = manifest_.destDir;
  for (const auto& v : secretVars_) {  // a dest_dir in the config file (set up on the web page) wins
    if (v.first == "cfg." + manifest_.destDirConfigKey && protectedpaths::isPluginPath(v.second + "/x")) {
      dir = v.second;
      while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
    }
  }
  const std::string dest = dir + "/" + name;
  if (protectedpaths::isSensitivePath(dest.c_str())) {
    http_->setBasicAuth("", "");
    fail(tr(STR_PLUGIN_FILE_UNSAFE), Retry::None);
    return false;
  }
  if (!dir.empty()) Storage.mkdir(dir.c_str(), true);
  const std::string part = dest + ".part";
  const auto r = http_->fetchToFile(req, part, [this] { return pollCancel(); },
                                    [this](size_t d, size_t t) { onProgress(d, t); });
  http_->setBasicAuth("", "");
  if (!r.ok()) {
    failHttp(r);
    return false;
  }
  Storage.remove(dest.c_str());
  if (!Storage.rename(part.c_str(), dest.c_str())) {
    Storage.remove(part.c_str());
    fail(tr(STR_PLUGIN_SD_WRITE_FAILED), Retry::None);
    return false;
  }
  clearBookCache(dest);
  if (!manifest_.sidecarPath.empty()) {
    Vars sideVars = nameVars;
    sideVars.emplace_back("dest", dest);
    {
      // {md5}: MD5 hex of the destination file path (what sidecar readers key on)
      MD5Builder md5;
      md5.begin();
      md5.add(dest.c_str());
      md5.calculate();
      sideVars.emplace_back("md5", std::string(md5.toString().c_str()));
    }
    const std::string sidePath = plugincat::substitute(manifest_.sidecarPath, sideVars);
    const std::string sideBody = plugincat::substitute(manifest_.sidecarBody, sideVars);
    if (protectedpaths::isPluginPath(sidePath) && sideBody.size() <= 2048) {
      Storage.writeFile(sidePath.c_str(), String(sideBody.c_str()));
    }
  }
  const size_t slash = pluginDir_.rfind('/');
  PluginEventBus::emit("book.downloaded", {{"path", dest},
                                           {"title", item.title},
                                           {"plugin", slash == std::string::npos ? pluginDir_ : pluginDir_.substr(slash + 1)}});
  return true;
}

// ---------------------------------------------------------------- input

size_t PluginCatalogActivity::listCount() const {
  return state_ == State::LISTS ? lists_.size() : rows_.size();
}

void PluginCatalogActivity::goBack() {
  if (state_ == State::ITEMS && manifest_.xml && !folderStack_.empty()) {
    xmlUrl_ = folderStack_.back();
    folderStack_.pop_back();
    loadXml();
    return;
  }
  if (state_ == State::ERROR && manifest_.xml && !folderStack_.empty()) {  // a failed folder: step out of it
    xmlUrl_ = folderStack_.back();
    folderStack_.pop_back();
    loadXml();
    return;
  }
  if (state_ == State::ITEMS && !lists_.empty()) {
    state_ = State::LISTS;
    selected_ = std::max(0, activeList_);
    requestUpdate();
    return;
  }
  finish();
}

void PluginCatalogActivity::loop() {
  const bool back = mappedInput.wasReleased(MappedInputManager::Button::Back);
  const bool confirm = mappedInput.wasReleased(MappedInputManager::Button::Confirm);

  switch (state_) {
    case State::CONNECTING:
    case State::WIFI_SELECTION:
    case State::LOADING:
    case State::DOWNLOADING:
      if (back && state_ == State::CONNECTING) finish();
      return;
    case State::SIGN_IN_PROMPT:
      if (back) {
        finish();
      } else if (confirm) {
        beginDeviceCode();
      }
      return;
    case State::SIGN_IN_CODE:
      if (back) {
        state_ = State::SIGN_IN_PROMPT;
        requestUpdate();
      } else {
        pollDeviceCode();
      }
      return;
    case State::DETAILS: {
      if (back) {
        state_ = State::ITEMS;
        requestUpdate();
        return;
      }
      if (confirm) {
        confirmInstall(detailsRow_);
        return;
      }
      const auto& metrics = UITheme::getInstance().getMetrics();
      const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
      const int visible = std::max(
          1, (renderer.getScreenHeight() - top - metrics.buttonHintsHeight - metrics.verticalSpacing) /
                 std::max(1, renderer.getLineHeight(UI_10_FONT_ID)));
      const int maxScroll = std::max(0, static_cast<int>(detailsLines_.size()) - visible);
      buttonNavigator_.onNextRelease([this, visible, maxScroll] {
        detailsScroll_ = std::min(maxScroll, detailsScroll_ + std::max(1, visible - 1));
        requestUpdate();
      });
      buttonNavigator_.onPreviousRelease([this, visible] {
        detailsScroll_ = std::max(0, detailsScroll_ - std::max(1, visible - 1));
        requestUpdate();
      });
      return;
    }
    case State::MESSAGE:
      if (back || confirm) {
        state_ = rows_.empty() ? State::LISTS : State::ITEMS;
        requestUpdate();
      }
      return;
    case State::ERROR:
      if (back) {
        goBack();
      } else if (confirm && retry_ == Retry::Lists) {
        loadLists();
      } else if (confirm && retry_ == Retry::Page) {
        loadPage(page_);
      }
      return;
    default:
      break;
  }

  if (back) {
    goBack();
    return;
  }
  const int count = static_cast<int>(listCount());
  if (count == 0) return;

  auto activate = [this] {
    if (state_ == State::LISTS) {
      openList(static_cast<size_t>(selected_));
      return;
    }
    const Row& row = rows_[selected_];
    if (row.kind == RowKind::Prev) {
      loadPage(page_ - 1);
    } else if (row.kind == RowKind::Next) {
      loadPage(page_ + 1);
    } else if (row.folder) {
      folderStack_.push_back(xmlUrl_);
      xmlUrl_ = plugincat::resolveAgainst(xmlUrl_, row.item.url);
      if (xmlUrl_.empty()) xmlUrl_ = folderStack_.back();
      loadXml();
    } else if (manifest_.tracksInstalls() || !row.item.detailsUrl.empty()) {
      showDetails(static_cast<size_t>(selected_));  // version-tracked catalogs show Details before Install/Update
    } else {
      confirmInstall(static_cast<size_t>(selected_));
    }
  };

  if (confirm) {
    activate();
    return;
  }
  {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int height = renderer.getScreenHeight() - top - metrics.buttonHintsHeight - metrics.verticalSpacing;
    auto touch = TouchListNavigation::handle(mappedInput, selected_, count,
                                              Rect{0, top, renderer.getScreenWidth(), height},
                                              metrics.listWithSubtitleRowHeight);
    if (touch.handled) {
      requestUpdate();
      if (touch.activate) activate();
      return;
    }
  }
  buttonNavigator_.onNextRelease([this, count] {
    selected_ = ButtonNavigator::nextIndex(selected_, count);
    requestUpdate();
  });
  buttonNavigator_.onPreviousRelease([this, count] {
    selected_ = ButtonNavigator::previousIndex(selected_, count);
    requestUpdate();
  });
}

// ---------------------------------------------------------------- render

void PluginCatalogActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, title_.c_str());
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = height - top - metrics.buttonHintsHeight - metrics.verticalSpacing;

  const char* confirmLabel = "";
  switch (state_) {
    case State::LISTS:
      GUI.drawList(renderer, Rect{0, top, width, contentHeight}, static_cast<int>(lists_.size()), selected_,
                   [this](int i) { return lists_[i].title; });
      confirmLabel = tr(STR_OPEN);
      break;
    case State::ITEMS:
      if (rows_.empty()) {
        renderer.drawCenteredText(UI_10_FONT_ID, height / 2, tr(STR_PLUGIN_NO_RESULTS));
      } else {
        GUI.drawList(
            renderer, Rect{0, top, width, contentHeight}, static_cast<int>(rows_.size()), selected_,
            [this](int i) -> std::string {
              switch (rows_[i].kind) {
                case RowKind::Prev: return tr(STR_PLUGIN_PREV_PAGE);
                case RowKind::Next: return tr(STR_PLUGIN_NEXT_PAGE);
                default: return rows_[i].folder ? rows_[i].item.title + "/" : rows_[i].item.title;
              }
            },
            [this](int i) -> std::string {
              if (rows_[i].kind != RowKind::Item) return "";
              const auto& it = rows_[i].item;
              std::string sub = it.author;
              if (!it.version.empty()) sub += (sub.empty() ? "v" : " · v") + it.version;
              return sub.empty() ? it.description : sub;
            },
            nullptr,
            [this](int i) -> std::string {
              if (rows_[i].kind != RowKind::Item) return "";
              if (rows_[i].update) return tr(STR_PLUGIN_UPDATE);
              return rows_[i].installed ? tr(STR_PLUGIN_INSTALLED) : "";
            },
            true);
      }
      confirmLabel = tr(STR_SELECT);
      break;
    case State::DETAILS: {
      const int lineHeight = std::max(1, renderer.getLineHeight(UI_10_FONT_ID));
      const int visible = std::max(1, contentHeight / lineHeight);
      int y = top;
      for (int i = detailsScroll_; i < static_cast<int>(detailsLines_.size()) && i < detailsScroll_ + visible; ++i) {
        if (!detailsLines_[i].empty()) {
          renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, detailsLines_[i].c_str(), true,
                            i == 0 && detailsScroll_ == 0 ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
        }
        y += lineHeight;
      }
      confirmLabel = rows_[detailsRow_].update ? tr(STR_PLUGIN_UPDATE) : tr(STR_PLUGIN_INSTALL);
      break;
    }
    case State::DOWNLOADING: {
      renderer.drawCenteredText(UI_10_FONT_ID, height / 2 - 50, tr(STR_DOWNLOADING));
      const auto lines = renderer.wrappedText(UI_10_FONT_ID, statusText_.c_str(), width - 56, 2);
      int y = height / 2 - 22;
      for (const auto& line : lines) {
        renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str());
        y += 24;
      }
      if (progressTotal_ > 0) {
        GUI.drawProgressBar(renderer, Rect{50, height / 2 + 30, width - 100, 20}, progressDone_, progressTotal_);
      } else if (progressDone_ > 0) {
        char text[32];
        snprintf(text, sizeof(text), "%zu KB", progressDone_ / 1024);
        renderer.drawCenteredText(SMALL_FONT_ID, height / 2 + 36, text);
      }
      break;
    }
    case State::SIGN_IN_PROMPT:
      renderer.drawCenteredText(UI_10_FONT_ID, height / 2, tr(STR_PLUGIN_SIGN_IN_PROMPT));
      confirmLabel = tr(STR_PLUGIN_SIGN_IN);
      break;
    case State::SIGN_IN_CODE: {
      const int qrSize = std::min(width - 120, height / 3);
      int y = top;
      if (!signInUrl_.empty()) {
        QrUtils::drawQrCode(renderer, Rect{(width - qrSize) / 2, y, qrSize, qrSize}, signInUrl_);
        y += qrSize + 12;
        const auto lines = renderer.wrappedText(SMALL_FONT_ID, signInUrl_.c_str(), width - 40, 2);
        for (const auto& line : lines) {
          renderer.drawCenteredText(SMALL_FONT_ID, y, line.c_str());
          y += renderer.getLineHeight(SMALL_FONT_ID) + 2;
        }
        y += 10;
      }
      renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_PLUGIN_ENTER_CODE));
      renderer.drawCenteredText(UI_12_FONT_ID, y + 30, signInCode_.c_str());
      break;
    }
    case State::ERROR:
      renderer.drawCenteredText(UI_10_FONT_ID, height / 2 - 20, tr(STR_ERROR_MSG));
      renderer.drawCenteredText(UI_10_FONT_ID, height / 2 + 10, errorText_.c_str());
      confirmLabel = retry_ == Retry::None ? "" : tr(STR_RETRY);
      break;
    default:
      renderer.drawCenteredText(UI_10_FONT_ID, height / 2, statusText_.c_str());
      if (state_ == State::MESSAGE) confirmLabel = tr(STR_CONFIRM);
      break;
  }
  const bool scrolling = state_ == State::DETAILS;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, scrolling ? tr(STR_DIR_UP) : "",
                                            scrolling ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
