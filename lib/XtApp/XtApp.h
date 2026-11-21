#pragma once
// XtApp host: runs small Lua apps in the dialect of the official Xteink "Licorice" mini-app host
// (manifest.json + index.lua, callbacks on_load/on_draw/on_input/on_tick/on_leave, globals `g` and `ctx`).
//
// This file is hardware independent: drawing goes through Canvas, storage/time/battery through Env, so the same
// code runs on the device (GfxRenderer/HalStorage) and in the native tests (bitmap canvas + in-memory files).
//
// Only unencrypted apps run. Official .xtapp packages are encrypted to a key held by the vendor firmware; nothing
// here tries to read them (see inspectContainer()).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "XtAppUtil.h"

struct lua_State;

namespace xtapp {

struct Manifest {
  std::string appId;
  std::string displayName;
  std::string description;
  std::string version;
  std::string author;
  std::string entry = "index.lua";
  std::string orientation = "portrait";
  int apiMajor = 0;
  int apiMinor = 8;
  std::vector<std::string> permissions;
  bool encrypted = false;  // carries dek_wrap_* fields: an official, vendor-key package
};

// `locale` picks display_names[locale] over display_name. False + err on invalid JSON or a missing app_id.
bool parseManifest(const std::string& json, Manifest& out, std::string* err = nullptr, const char* locale = "en");

struct ContainerInfo {
  bool valid = false;
  bool encrypted = false;     // code section is encrypted (official Licorice package)
  std::string manifestJson;   // plain JSON manifest embedded in the container
};
// `head` = at least the first 128 + manifest-length bytes of an .xtapp file.
ContainerInfo inspectContainer(const std::string& head);

struct Canvas {
  virtual ~Canvas() = default;
  virtual int width() const = 0;
  virtual int height() const = 0;
  virtual void clear(bool black) = 0;
  virtual void fillRect(int x, int y, int w, int h, bool black) = 0;
  virtual void line(int x0, int y0, int x1, int y1, bool black) = 0;
  virtual void pixel(int x, int y, bool black) = 0;
  // y is the TOP of the line box. size: 0 small, 1 medium (default), 2 large.
  virtual void text(int x, int y, const char* s, int size, bool black) = 0;
  virtual int textWidth(const char* s, int size) = 0;
  virtual int lineHeight(int size) = 0;
};

// Reading statistics for ctx.reader (manifest permission "reader"). Seconds per day are kept since the daily file was
// introduced (days() returns what exists); per-book per-day rows are not kept, so book_days() is an empty table.
struct ReaderSummary {
  uint32_t totalSec = 0, todaySec = 0;
  int streakDays = 0, bestStreakDays = 0;
};
struct ReaderDay {
  std::string date;  // YYYY-MM-DD
  uint32_t sec = 0;
};
struct ReaderBook {
  std::string id, title, author, format;
  int percent = -1;  // -1 = unknown (field omitted)
  uint32_t readSec = 0;
  bool finished = false;
};

struct Env {
  virtual ~Env() = default;
  // Files shipped inside the app folder (index.lua, src/*.lua, data/*). `rel` has no leading '/' and no "..".
  virtual bool readPackage(const std::string& rel, std::string& out) = 0;
  // The app's private writable sandbox: flat file names.
  virtual bool readFile(const std::string& name, std::string& out) = 0;
  virtual bool writeFile(const std::string& name, const std::string& data) = 0;
  virtual bool removeFile(const std::string& name) = 0;
  virtual int64_t fileSize(const std::string& name) = 0;  // -1 when missing
  virtual std::vector<std::string> listFiles() = 0;
  virtual bool loadState(std::string& out) = 0;
  virtual bool saveState(const std::string& data) = 0;
  virtual uint32_t millis() = 0;
  virtual int64_t epochSec() = 0;        // UTC seconds, 0 when the clock is not set
  virtual int localOffsetSec() { return 0; }
  virtual int batteryPercent() { return 100; }
  virtual bool charging() { return false; }
  virtual void log(int /*level*/, const std::string& /*msg*/) {}
  // Optional hardware services; the defaults report "not available".
  virtual bool decodeImage(const std::string& /*bytes*/, ImageData& /*out*/, std::string& err) {
    err = "unsupported";
    return false;
  }
  virtual bool qrEncode(const std::string& /*text*/, ImageData& /*out*/) { return false; }  // 1 pixel per module
  virtual bool readerSummary(ReaderSummary& /*out*/) { return false; }
  virtual bool readerBooks(std::vector<ReaderBook>& /*out*/) { return false; }  // recent books with their statistics
  virtual bool readerDays(const std::string& /*from*/, const std::string& /*to*/, std::vector<ReaderDay>& /*out*/) {
    return false;
  }  // dates YYYY-MM-DD, inclusive
  virtual bool readerCurrent(ReaderBook& /*out*/) { return false; }             // the book read last
};

struct Event {
  enum class Type { Key, Pointer };
  Type type = Type::Key;
  std::string key;      // up down left right ok back
  std::string phase;    // key: down|up   pointer: down|move|up|hold
  std::string gesture;  // pointer up: tap|long|swipe_up|swipe_down|swipe_left|swipe_right
  int x = 0, y = 0, downX = 0, downY = 0;
  uint32_t timeMs = 0;
};

struct NetRequest {
  int id = 0;
  std::string url;
  std::string method = "GET";
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;
  uint32_t timeoutMs = 15000;
  std::string saveName;  // non-empty: stream the response body to this file in the app sandbox instead of memory
};

struct NetResponse {
  int status = 0;  // 0 = no HTTP answer (see error)
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;  // names lower-cased
  std::string error;  // "wifi", "timeout", "connect", "too_large", ...
  bool saved = false;
};

struct Limits {
  size_t memoryBytes = 768 * 1024;  // Lua heap budget
  uint32_t callbackMs = 8000;       // per-callback watchdog
};

class App {
 public:
  App(Env& env, Canvas& canvas, Limits limits = {});
  ~App();
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  // Parses the manifest, creates the sandbox, loads the entry script, restores ctx.state and calls on_load.
  bool start(const std::string& manifestJson);

  void draw();                      // on_draw(ctx, g)
  bool input(const Event& ev);      // on_input(ctx, ev); true when the app claimed the event
  void tick();                      // due ctx:after timers and on_tick
  void leave();                     // on_leave, then ctx.state is saved

  bool consumeRedraw(bool* full);   // true once per request (invalidate / display:full_refresh)
  bool quitRequested() const { return quit_; }
  bool failed() const { return failed_; }
  const std::string& error() const { return error_; }
  const Manifest& manifest() const { return manifest_; }
  uint32_t tickIntervalMs() const { return tickMs_; }
  bool hasTickHandler() const { return hasTick_; }
  bool takeToast(std::string& out);

  struct Confirm {
    std::string title, text;
  };
  bool pendingConfirm(Confirm& out) const;
  void answerConfirm(bool yes);

  // Text entry requested by ctx.input:text / ctx.input:edit. takeTextRequest() hands each request out once; the host
  // shows its keyboard and reports back with answerText() (ok=false: cancelled).
  struct TextRequest {
    std::string title;
    std::string text;
    size_t maxChars = 0;  // 0 = unlimited
    size_t maxBytes = 0;
  };
  bool takeTextRequest(TextRequest& out);
  void answerText(bool ok, const std::string& text);

  // ctx.net:request: the host runs the request (connecting Wi-Fi first) and calls netDone() with the outcome.
  // takeNetRequest() hands each request out once. cancelled jobs (job:cancel()) are reported by takeNetCancel().
  bool takeNetRequest(NetRequest& out);
  bool takeNetCancel(int& id);
  void netDone(int id, NetResponse&& resp);
  uint32_t keepWifiMs() const { return keepWifiMs_; }  // last ctx.net:keep_wifi(ms) argument

  // --- used by the Lua bindings (public to keep the C callbacks simple) ---
  struct Impl;
  Impl* impl() { return impl_.get(); }

 // State below is touched by the Lua binding functions in XtApp.cpp; treat it as private.
  std::unique_ptr<Impl> impl_;
  Manifest manifest_;
  bool quit_ = false;
  bool failed_ = false;
  bool redraw_ = false;
  bool full_ = false;
  bool hasTick_ = false;
  uint32_t tickMs_ = 0;
  uint32_t keepWifiMs_ = 0;
  std::string error_;
};

}  // namespace xtapp
