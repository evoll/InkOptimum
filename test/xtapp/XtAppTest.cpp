#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "XtApp.h"
#include "XtAppJson.h"

using namespace xtapp;

namespace {

struct MemCanvas : Canvas {
  int w = 480, h = 800;
  std::vector<uint8_t> px;
  int texts = 0;
  std::string lastText;
  MemCanvas() : px(static_cast<size_t>(w) * h, 0) {}
  int width() const override { return w; }
  int height() const override { return h; }
  void clear(bool black) override { std::fill(px.begin(), px.end(), black ? 1 : 0); }
  void pixel(int x, int y, bool b) override {
    if (x >= 0 && y >= 0 && x < w && y < h) px[static_cast<size_t>(y) * w + x] = b ? 1 : 0;
  }
  void fillRect(int x, int y, int ww, int hh, bool b) override {
    for (int yy = y; yy < y + hh; ++yy)
      for (int xx = x; xx < x + ww; ++xx) pixel(xx, yy, b);
  }
  void line(int x0, int y0, int x1, int y1, bool b) override {
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
      pixel(x0, y0, b);
      if (x0 == x1 && y0 == y1) break;
      const int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }
  void text(int x, int y, const char* s, int size, bool b) override {
    ++texts;
    lastText = s;
    fillRect(x, y, textWidth(s, size), 2, b);
  }
  int textWidth(const char* s, int) override { return static_cast<int>(strlen(s)) * 12; }
  int lineHeight(int size) override { return 20 + size * 4; }
  int count() const { return static_cast<int>(std::count(px.begin(), px.end(), 1)); }
  bool at(int x, int y) const { return px[static_cast<size_t>(y) * w + x] != 0; }
};

struct MemEnv : Env {
  std::map<std::string, std::string> pkg, files;
  std::string state;
  bool hasState = false;
  uint32_t now = 1000;
  int64_t epoch = 1700000000;  // 2023-11-14 22:13:20 UTC (Tuesday)
  std::vector<std::string> logs;
  bool readPackage(const std::string& r, std::string& o) override {
    auto it = pkg.find(r);
    if (it == pkg.end()) return false;
    o = it->second;
    return true;
  }
  bool readFile(const std::string& n, std::string& o) override {
    auto it = files.find(n);
    if (it == files.end()) return false;
    o = it->second;
    return true;
  }
  bool writeFile(const std::string& n, const std::string& d) override { files[n] = d; return true; }
  bool removeFile(const std::string& n) override { return files.erase(n) > 0; }
  int64_t fileSize(const std::string& n) override {
    auto it = files.find(n);
    return it == files.end() ? -1 : static_cast<int64_t>(it->second.size());
  }
  std::vector<std::string> listFiles() override {
    std::vector<std::string> v;
    for (auto& kv : files) v.push_back(kv.first);
    return v;
  }
  bool loadState(std::string& o) override { o = state; return hasState; }
  bool saveState(const std::string& d) override { state = d; hasState = true; return true; }
  uint32_t millis() override { return now; }
  int64_t epochSec() override { return epoch; }
  void log(int, const std::string& m) override { logs.push_back(m); }
  bool decodeImage(const std::string& b, ImageData& out, std::string& err) override {
    if (b.compare(0, 4, "IMG1") != 0) { err = "decode_failed"; return false; }
    out.w = 2; out.h = 2; out.gray = {0, 255, 255, 0};  // checker
    return true;
  }
  bool qrEncode(const std::string&, ImageData& out) override {
    out.w = out.h = 21;
    out.gray.assign(441, 0);  // all black modules
    return true;
  }
};

const char* kManifest = R"({"app_id":"t","display_name":"T","entry":"index.lua","api":"2.2"})";

struct Fixture {
  MemEnv env;
  MemCanvas cv;
  std::unique_ptr<App> app;
  bool start(const std::string& lua, const std::string& manifest = kManifest, Limits lim = {}) {
    env.pkg["index.lua"] = lua;
    app = std::make_unique<App>(env, cv, lim);
    return app->start(manifest);
  }
};

}  // namespace

TEST(XtAppJson, ParsesAndRoundTripsTypes) {
  JValue v;
  ASSERT_TRUE(parseJson(R"({"a":[1,2.5,"xé😀"],"b":true,"c":null,"i":9007199254740993})", v));
  ASSERT_EQ(v.type, JValue::Type::Obj);
  EXPECT_EQ(v.get("a")->a[0].type, JValue::Type::Int);
  EXPECT_EQ(v.get("a")->a[1].type, JValue::Type::Num);
  EXPECT_EQ(v.get("a")->a[2].s, "x\xc3\xa9\xf0\x9f\x98\x80");
  EXPECT_EQ(v.get("i")->i, 9007199254740993LL);
  EXPECT_FALSE(parseJson("{\"a\":}", v));
  EXPECT_FALSE(parseJson("[1,2", v));
  EXPECT_FALSE(parseJson(std::string(40, '[') + std::string(40, ']'), v));
}

TEST(XtAppManifest, ParsesFieldsAndLocale) {
  Manifest m;
  ASSERT_TRUE(parseManifest(
      R"({"app_id":"fbreader","display_name":"FB2 Reader","display_names":{"ru":"Читалка"},"api":"2.17","author":"K","permissions":["reader"]})",
      m, nullptr, "ru"));
  EXPECT_EQ(m.displayName, "Читалка");
  EXPECT_EQ(m.apiMajor, 2);
  EXPECT_EQ(m.apiMinor, 17);
  EXPECT_FALSE(m.encrypted);
  ASSERT_EQ(m.permissions.size(), 1u);
  std::string err;
  EXPECT_FALSE(parseManifest(R"({"display_name":"x"})", m, &err));
}

TEST(XtAppContainer, DetectsOfficialEncryptedPackage) {
  const std::string manifest = R"({"app_id":"x","dek_wrap_alg":"P256-ECDH-HKDF-SHA256-AESKW"})";
  std::string f(0x80, '\0');
  f.replace(0, 4, "XTAP");
  f[4] = 1;
  f[6] = static_cast<char>(0x80);
  f[0x10] = static_cast<char>(0x80);
  f[0x14] = static_cast<char>(manifest.size());
  f += manifest;
  const ContainerInfo ci = inspectContainer(f);
  ASSERT_TRUE(ci.valid);
  EXPECT_TRUE(ci.encrypted);
  EXPECT_EQ(ci.manifestJson, manifest);
  EXPECT_FALSE(inspectContainer("XTAPxxxx").valid);

  Fixture fx;
  EXPECT_FALSE(fx.start("function on_draw() end", manifest));
  EXPECT_NE(fx.app->error().find("encrypted"), std::string::npos);
}

TEST(XtApp, NetworkRequestRoundTrip) {
  Fixture fx;
  ASSERT_TRUE(fx.start(R"(
    result = '-'
    job = ctx.net:request({url = 'https://example.org/a?b=1', method = 'post', body = 'x=1',
                           headers = {['X-Key'] = 'v', ['Bad Name'] = 'no'}, timeout_ms = 5000}, function(c, r)
      result = r.status .. ':' .. tostring(r.ok) .. ':' .. r.body .. ':' .. r.headers['content-type']
    end)
    assert(job and job.id and job.cancel)
    local none, why = ctx.net:request({url = 'ftp://x'}, function() end)
    assert(none == nil and why == 'invalid_argument')
    assert(not pcall(ctx.net.request, ctx.net, {}, function() end))   -- missing url
    ctx.net:keep_wifi(300000)
    function on_draw() g:text(0, 0, result) end
  )", R"({"app_id":"n","permissions":["net"]})"));
  ASSERT_FALSE(fx.app->failed()) << fx.app->error();
  EXPECT_EQ(fx.app->keepWifiMs(), 300000u);
  NetRequest r;
  ASSERT_TRUE(fx.app->takeNetRequest(r));
  EXPECT_EQ(r.method, "POST");
  EXPECT_EQ(r.body, "x=1");
  EXPECT_EQ(r.timeoutMs, 5000u);
  ASSERT_EQ(r.headers.size(), 1u);  // invalid header name dropped
  EXPECT_EQ(r.headers[0].first, "X-Key");
  EXPECT_FALSE(fx.app->takeNetRequest(r));
  NetResponse resp;
  resp.status = 201;
  resp.body = "ok!";
  resp.headers = {{"content-type", "text/plain"}};
  fx.app->netDone(r.id, std::move(resp));
  fx.app->draw();
  EXPECT_EQ(fx.cv.lastText, "201:true:ok!:text/plain");
}

TEST(XtApp, NetworkCancelDropsTheCallback) {
  Fixture fx;
  ASSERT_TRUE(fx.start(R"(
    fired = false
    job = ctx.net:request({url = 'http://example.org/'}, function() fired = true end)
    function on_input(c, ev) job:cancel() return true end
    function on_draw() g:text(0, 0, tostring(fired)) end
  )"));
  NetRequest r;
  ASSERT_TRUE(fx.app->takeNetRequest(r));  // already handed to the host
  Event k;
  k.key = "ok";
  k.phase = "down";
  fx.app->input(k);
  int cid = 0;
  ASSERT_TRUE(fx.app->takeNetCancel(cid));
  EXPECT_EQ(cid, r.id);
  fx.app->netDone(r.id, NetResponse{});  // late result is ignored
  fx.app->draw();
  EXPECT_EQ(fx.cv.lastText, "false");
}

TEST(XtApp, NetworkSaveOptionAndLimits) {
  Fixture fx;
  ASSERT_TRUE(fx.start(R"(
    for i = 1, 4 do assert(ctx.net:request({url = 'http://e.org/' .. i}, function() end)) end
    local j, why = ctx.net:request({url = 'http://e.org/5'}, function() end)
    assert(j == nil and why == 'busy')
    ctx.net:request({url = 'http://e.org/f', save = {to = 'files', name = 'a.bin'}}, function() end)
  )"));
  EXPECT_TRUE(fx.app->failed() == false) << fx.app->error();
}

TEST(XtApp, DrawsThroughG) {
  Fixture fx;
  ASSERT_TRUE(fx.start(R"(
    function on_draw(c, gg)
      gg:clear(0)
      g:rect(10, 10, 20, 20)
      g:rect(100, 100, 10, 10, true)
      g:line(0, 0, 50, 0)
      g:circle(300, 300, 5, true)
      g:text(5, 5, 'hi')
      local w, h = g:size()
      assert(w == 480 and h == 800)
    end
  )"));
  fx.app->draw();
  ASSERT_FALSE(fx.app->failed()) << fx.app->error();
  EXPECT_TRUE(fx.cv.at(10, 10));
  EXPECT_FALSE(fx.cv.at(15, 15));    // outline only
  EXPECT_TRUE(fx.cv.at(105, 105));   // filled
  EXPECT_TRUE(fx.cv.at(300, 300));
  EXPECT_TRUE(fx.cv.at(25, 0));
  EXPECT_EQ(fx.cv.lastText, "hi");
}

TEST(XtApp, ClearBlackWhenNoArg) {
  Fixture fx;
  ASSERT_TRUE(fx.start("function on_draw() g:clear() end"));
  fx.app->draw();
  EXPECT_EQ(fx.cv.count(), 480 * 800);
}

TEST(XtApp, InputEventsAndInvalidate) {
  Fixture fx;
  ASSERT_TRUE(fx.start(R"(
    local n = 0
    function on_input(c, ev)
      if ev.type == 'key' and ev.key == 'ok' and ev.phase == 'down' then n = n + 1; ctx:invalidate('full'); return true end
      if ev.type == 'pointer' and ev.gesture == 'tap' then n = n + 100; ctx:invalidate(); return true end
      return false
    end
    function on_draw() g:text(0, 0, tostring(n)) end
  )"));
  bool full = true;
  EXPECT_TRUE(fx.app->consumeRedraw(&full));  // first frame
  EXPECT_FALSE(fx.app->consumeRedraw(&full));
  Event k;
  k.key = "ok";
  k.phase = "down";
  EXPECT_TRUE(fx.app->input(k));
  EXPECT_TRUE(fx.app->consumeRedraw(&full));
  EXPECT_TRUE(full);
  Event up = k;
  up.key = "back";
  EXPECT_FALSE(fx.app->input(up));
  Event t;
  t.type = Event::Type::Pointer;
  t.gesture = "tap";
  t.phase = "up";
  EXPECT_TRUE(fx.app->input(t));
  EXPECT_TRUE(fx.app->consumeRedraw(&full));
  EXPECT_FALSE(full);
  fx.app->draw();
  EXPECT_EQ(fx.cv.lastText, "101");
}

TEST(XtApp, Api08GetsTouchEvents) {
  Fixture fx;
  ASSERT_TRUE(fx.start(R"(
    got = nil
    function on_input(c, ev) got = ev.type .. ':' .. ev.gesture; return true end
    function on_draw() g:text(0, 0, got or '-') end
  )", R"({"app_id":"o","api":"0.8"})"));
  Event t;
  t.type = Event::Type::Pointer;
  t.gesture = "swipe_left";
  t.phase = "up";
  fx.app->input(t);
  fx.app->draw();
  EXPECT_EQ(fx.cv.lastText, "touch:swipe_left");
}

TEST(XtApp, StatePersistsAcrossRuns) {
  Fixture a;
  ASSERT_TRUE(a.start(R"(
    function on_load(c) c.state.count = (c.state.count or 0) + 1; c.state.board = {1,2,3}; c.state.name = 'é' end
  )"));
  a.app->leave();
  ASSERT_TRUE(a.env.hasState);
  Fixture b;
  b.env.state = a.env.state;
  b.env.hasState = true;
  ASSERT_TRUE(b.start(R"(
    function on_load(c) assert(c.state.count == 1, 'count'); assert(#c.state.board == 3); assert(c.state.name == 'é') end
  )"));
  EXPECT_FALSE(b.app->failed()) << b.app->error();
}

TEST(XtApp, StorageAndJsonHelpers) {
  Fixture fx;
  ASSERT_TRUE(fx.start(R"(
    local s = ctx.storage
    assert(s:write('a.txt', 'hello'))
    assert(s:read('a.txt') == 'hello')
    assert(s:exists('a.txt') and s:size('a.txt') == 5)
    local v, e = s:read('missing'); assert(v == nil and e == 'not_found')
    local ok, e2 = s:write('../x', 'y'); assert(ok == nil and e2 == 'invalid_argument')
    assert(#s:list() == 1)
    assert(s:remove('a.txt') and not s:exists('a.txt'))
    local t = ctx.json:decode('{"a":[1,2,{"b":null}],"s":"x"}')
    assert(t.a[2] == 2 and t.a[3].b == ctx.json.null and t.s == 'x')
    assert(ctx.json:encode({1,2,3}) == '[1,2,3]')
    assert(ctx.json:decode('{') == nil)
    assert(ctx.base64:encode('hello!?') == 'aGVsbG8hPw==')
    assert(ctx.base64:decode('aGVsbG8hPw==') == 'hello!?')
    assert(ctx.hex:encode('AB') == '4142' and ctx.hex:decode('4142') == 'AB')
    assert(ctx.url:encode('a b&') == 'a%20b%26')
  )"));
  EXPECT_FALSE(fx.app->failed()) << fx.app->error();
}

TEST(XtApp, ClockFieldsAreCivil) {
  Fixture fx;
  fx.env.epoch = 1700000000;  // Tue 2023-11-14 22:13:20 UTC
  ASSERT_TRUE(fx.start(R"(
    local c = ctx.clock:now()
    assert(c.year == 2023 and c.month == 11 and c.day == 14, 'date')
    assert(c.hour == 22 and c.minute == 13 and c.second == 20, 'time')
    assert(c.weekday == 2, 'weekday ' .. c.weekday)
    local s = ctx.sys.clock()
    assert(s.year == 2023)
    assert(ctx.sys.battery() == 100)
  )"));
  EXPECT_FALSE(fx.app->failed()) << fx.app->error();
}

TEST(XtApp, TimersAndTick) {
  Fixture fx;
  ASSERT_TRUE(fx.start(R"(
    fired = 0; ticks = 0
    ctx:after(500, function(c) fired = fired + 1; c:invalidate() end)
    ctx:set_tick(200)
    function on_tick(c, n) ticks = ticks + 1 end
    function on_draw() g:text(0, 0, fired .. ':' .. (ticks > 0 and 'T' or 't')) end
  )"));
  EXPECT_TRUE(fx.app->hasTickHandler());
  fx.env.now += 100;
  fx.app->tick();
  fx.app->draw();
  EXPECT_EQ(fx.cv.lastText, "0:t");
  fx.env.now += 600;
  fx.app->tick();
  fx.app->draw();
  EXPECT_EQ(fx.cv.lastText, "1:T");
  fx.env.now += 600;
  fx.app->tick();
  fx.app->draw();
  EXPECT_EQ(fx.cv.lastText, "1:T");  // one-shot
}

TEST(XtApp, RequireLoadsPackageModules) {
  Fixture fx;
  fx.env.pkg["src/util.lua"] = "local M = {} function M.double(x) return x * 2 end return M";
  ASSERT_TRUE(fx.start("local u = require('util'); assert(u.double(4) == 8); assert(require('util') == u)"));
  EXPECT_FALSE(fx.app->failed()) << fx.app->error();
}

TEST(XtApp, SandboxHidesDangerousGlobals) {
  Fixture fx;
  ASSERT_TRUE(fx.start(R"(
    assert(io == nil and os == nil and debug == nil and package == nil)
    assert(dofile == nil and loadfile == nil and collectgarbage == nil and string.dump == nil)
  )"));
  EXPECT_FALSE(fx.app->failed()) << fx.app->error();
}

TEST(XtApp, ErrorsAreReportedNotThrown) {
  Fixture fx;
  ASSERT_TRUE(fx.start("function on_draw() error('boom') end"));
  fx.app->draw();
  EXPECT_TRUE(fx.app->failed());
  EXPECT_NE(fx.app->error().find("boom"), std::string::npos);
  Fixture bad;
  EXPECT_FALSE(bad.start("this is not lua"));
  EXPECT_NE(bad.app->error().find("syntax"), std::string::npos);
}

TEST(XtApp, WatchdogStopsRunawayScripts) {
  Fixture fx;
  // Each millis() call advances the fake clock, so the instruction hook sees the budget run out.
  struct TickEnv : MemEnv {
    uint32_t millis() override { return now += 50; }
  } env;
  env.pkg["index.lua"] = "function on_draw() while true do end end";
  MemCanvas cv;
  Limits lim;
  lim.callbackMs = 1000;
  App app(env, cv, lim);
  ASSERT_TRUE(app.start(kManifest));
  app.draw();
  EXPECT_TRUE(app.failed());
  EXPECT_NE(app.error().find("time budget"), std::string::npos);
}

TEST(XtApp, MemoryLimitIsEnforced) {
  Fixture fx;
  Limits lim;
  lim.memoryBytes = 256 * 1024;
  ASSERT_TRUE(fx.start("function on_draw() local t = {} for i = 1, 1e7 do t[i] = {i} end end", kManifest, lim));
  fx.app->draw();
  EXPECT_TRUE(fx.app->failed());
  EXPECT_NE(fx.app->error().find("memory"), std::string::npos);
}

TEST(XtApp, ConfirmDialogRoundTrip) {
  Fixture fx;
  ASSERT_TRUE(fx.start(R"(
    answer = nil
    function on_load(c) c.dialog:confirm({title='T', text='Sure?'}, function(cc, yes) answer = yes end) end
    function on_draw() g:text(0, 0, tostring(answer)) end
  )"));
  App::Confirm c;
  ASSERT_TRUE(fx.app->pendingConfirm(c));
  EXPECT_EQ(c.text, "Sure?");
  fx.app->answerConfirm(true);
  fx.app->draw();
  EXPECT_EQ(fx.cv.lastText, "true");
  EXPECT_FALSE(fx.app->pendingConfirm(c));
}

// ---- corpus: every open-source community app must load, draw and survive a scripted session.
// Enabled by XTAPP_CORPUS=<dir with one folder per app>; skipped otherwise.
TEST(XtAppCorpus, CommunityAppsRunWithoutLuaErrors) {
  const char* root = getenv("XTAPP_CORPUS");
  if (!root) GTEST_SKIP() << "set XTAPP_CORPUS to run";
  namespace fs = std::filesystem;
  int ran = 0, skipped = 0;
  for (const auto& e : fs::directory_iterator(root)) {
    if (!e.is_directory() || !fs::exists(e.path() / "manifest.json") || !fs::exists(e.path() / "index.lua")) continue;
    MemEnv env;
    for (const auto& f : fs::recursive_directory_iterator(e.path())) {
      if (!f.is_regular_file() || f.file_size() > 2 * 1024 * 1024) continue;
      const std::string rel = fs::relative(f.path(), e.path()).generic_string();
      std::ifstream in(f.path(), std::ios::binary);
      std::stringstream ss;
      ss << in.rdbuf();
      env.pkg[rel] = ss.str();
    }
    MemCanvas cv;
    App app(env, cv);
    const std::string name = e.path().filename().string();
    if (!app.start(env.pkg["manifest.json"])) {
      const std::string err = app.error();
      if (err.find("network") != std::string::npos) {
        ++skipped;
        continue;
      }
      ADD_FAILURE() << name << ": start failed: " << err;
      continue;
    }
    bool bad = false;
    auto check = [&](const char* what) {
      if (app.failed() && !bad) {
        bad = true;
        ADD_FAILURE() << name << " (" << what << "): " << app.error();
      }
    };
    int netServed = 0;
    auto serveNet = [&]() {
      NetRequest nr;
      while (app.takeNetRequest(nr)) {
        NetResponse resp;
        if (netServed++ % 2 == 0) {  // alternate a JSON answer and a transport failure
          resp.status = 200;
          resp.body = "{\"items\":[],\"results\":[],\"data\":{}}";
          resp.headers = {{"content-type", "application/json"}};
        } else {
          resp.error = "timeout";
        }
        app.netDone(nr.id, std::move(resp));
      }
      std::string t;
      app.takeToast(t);
    };
    app.draw();
    check("first draw");
    serveNet();
    check("net");
    const char* keys[] = {"down", "ok", "up", "right", "left", "ok", "back"};
    for (int round = 0; round < 2 && !bad; ++round) {
      for (const char* k : keys) {
        Event ev;
        ev.key = k;
        ev.phase = "down";
        app.input(ev);
        ev.phase = "up";
        app.input(ev);
        check(k);
        env.now += 400;
        app.tick();
        check("tick");
        serveNet();
        check("net");
        if (app.consumeRedraw(nullptr)) app.draw();
        check("draw");
        if (bad) break;
      }
      for (const char* g : {"tap", "swipe_left", "swipe_up", "swipe_right", "swipe_down", "long"}) {
        Event ev;
        ev.type = Event::Type::Pointer;
        ev.phase = "up";
        ev.gesture = g;
        ev.x = 240;
        ev.y = 400;
        ev.downX = 100;
        ev.downY = 300;
        app.input(ev);
        check(g);
        if (app.consumeRedraw(nullptr)) app.draw();
        check("draw");
      }
    }
    app.leave();
    check("leave");
    ++ran;
  }
  RecordProperty("ran", ran);
  std::printf("[ corpus ] ran %d apps, skipped %d\n", ran, skipped);
  EXPECT_GT(ran, 0);
}

TEST(XtApp, TextInputRoundTrip) {
  Fixture fx;
  ASSERT_TRUE(fx.start(R"(
    got = nil
    function on_input(c, ev)
      if ev.key == 'ok' then c.input:text({title = 'Name', text = 'abc', max_bytes = 8}, function(cc, s) got = s end) end
      return true
    end
    function on_draw() g:text(0, 0, tostring(got)) end
  )"));
  Event k;
  k.key = "ok";
  k.phase = "down";
  fx.app->input(k);
  App::TextRequest r;
  ASSERT_TRUE(fx.app->takeTextRequest(r));
  EXPECT_EQ(r.title, "Name");
  EXPECT_EQ(r.text, "abc");
  EXPECT_EQ(r.maxBytes, 8u);
  EXPECT_FALSE(fx.app->takeTextRequest(r));  // handed out once
  fx.app->answerText(true, "xyz");
  fx.app->draw();
  EXPECT_EQ(fx.cv.lastText, "xyz");
  // cancel: callback not called
  fx.app->input(k);
  ASSERT_TRUE(fx.app->takeTextRequest(r));
  fx.app->answerText(false, "");
  fx.app->draw();
  EXPECT_EQ(fx.cv.lastText, "xyz");
}

TEST(XtApp, EditCallsOnChangeThenOnEnd) {
  Fixture fx;
  ASSERT_TRUE(fx.start(R"(
    log = ''
    function on_input(c, ev)
      h = c.input:edit({text = 'q', on_change = function(cc, t) log = log .. 'c:' .. t .. ';' end,
                        on_end = function(cc, t, why) log = log .. 'e:' .. t .. ':' .. why end})
      return true
    end
    function on_draw() g:text(0, 0, log) end
  )"));
  Event k;
  k.key = "ok";
  k.phase = "down";
  fx.app->input(k);
  App::TextRequest r;
  ASSERT_TRUE(fx.app->takeTextRequest(r));
  EXPECT_EQ(r.text, "q");
  fx.app->answerText(true, "hello");
  fx.app->draw();
  ASSERT_FALSE(fx.app->failed()) << fx.app->error();
  EXPECT_EQ(fx.cv.lastText, "c:hello;e:hello:enter");
}

TEST(XtAppUtil, HashesMatchKnownVectors) {
  auto hex = [](const std::string& b) {
    std::string o;
    char t[3];
    for (unsigned char c : b) { snprintf(t, 3, "%02x", c); o += t; }
    return o;
  };
  EXPECT_EQ(hex(sha1("abc")), "a9993e364706816aba3e25717850c26c9cd0d89d");
  EXPECT_EQ(hex(sha256("abc")), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(hex(sha256("")), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(XtAppUtil, InflateHandlesStoredFixedAndDynamic) {
  std::string out;
  // stored block "hello"
  const std::string stored("\x01\x05\x00\xfa\xff" "hello", 10);
  ASSERT_TRUE(inflateData(stored, out, 100));
  EXPECT_EQ(out, "hello");
  // zlib of "aaaaaaaaaaaaaaaaaaaa" (fixed huffman, with back-reference): python zlib.compress
  const unsigned char z[] = {0x78, 0x9c, 0x4b, 0x4c, 0xc4, 0x04, 0x00, 0x4f, 0xa6, 0x07, 0x95};
  ASSERT_TRUE(inflateData(std::string(reinterpret_cast<const char*>(z), sizeof(z)), out, 100));
  EXPECT_EQ(out, std::string(20, 'a'));
  EXPECT_FALSE(inflateData(std::string(reinterpret_cast<const char*>(z), sizeof(z)), out, 5));  // output cap
  // zlib level 9 of a longer text: dynamic Huffman block
  const unsigned char dyn[] = {0x78, 0xda, 0xed, 0x8c, 0x57, 0x76, 0x82, 0x40, 0x14, 0x40, 0xb7, 0xf2, 0xac, 0x51, 0xa3, 0x18, 0x7b, 0xec, 0x25, 0xb1, 0xd7, 0x44, 0xec, 0x15, 0x70, 0x80, 0x51, 0x60, 0xa4, 0xab, 0xab, 0x77, 0xb6, 0xe1, 0x39, 0x7e, 0xdf, 0xc2, 0xca, 0x8, 0x74, 0x1b, 0xb, 0x17, 0xe0, 0xd, 0xe2, 0x6a, 0x20, 0x92, 0x1b, 0x9c, 0x6d, 0xf5, 0x6a, 0x2, 0x71, 0x90, 0x1, 0x16, 0xc5, 0xa, 0xf7, 0xb8, 0xc3, 0x89, 0x48, 0xc, 0x4c, 0x38, 0xea, 0xa9, 0x77, 0xe0, 0xa9, 0xe4, 0x62, 0x4b, 0x6, 0x11, 0x3b, 0x88, 0xa2, 0x7, 0xd2, 0x40, 0xc1, 0xba, 0x4d, 0xc, 0xda, 0x4a, 0x26, 0x3, 0xec, 0xfb, 0xfa, 0x42, 0x57, 0xf0, 0x78, 0x7d, 0xfe, 0x40, 0xf0, 0x23, 0x14, 0x8e, 0x7c, 0x46, 0x63, 0x4c, 0xfc, 0x2b, 0x91, 0x4c, 0xa5, 0x33, 0xd9, 0xdc, 0x77, 0xbe, 0x50, 0x2c, 0x95, 0x2b, 0xd5, 0x5a, 0xbd, 0xf1, 0xf3, 0xdb, 0x6c, 0xb5, 0x3b, 0xdd, 0x5e, 0x7f, 0x30, 0x1c, 0x8d, 0x27, 0x7f, 0xff, 0x53, 0x76, 0x36, 0x5f, 0x2c, 0x57, 0xeb, 0xcd, 0x76, 0xb7, 0x3f, 0x1c, 0x39, 0x5e, 0x38, 0x21, 0x51, 0x92, 0xf1, 0xf9, 0xa2, 0xa8, 0x1a, 0xb9, 0xea, 0x86, 0x69, 0xd9, 0x8e, 0xfb, 0x4, 0x98, 0x3a, 0xd3, 0x29};
  std::string expect;
  for (int i = 0; i < 6; ++i) expect += "The quick brown fox jumps over the lazy dog. Pack my box with five dozen liquor jugs. ";
  for (int c = 32; c < 120; ++c) expect += static_cast<char>(c);
  ASSERT_TRUE(inflateData(std::string(reinterpret_cast<const char*>(dyn), sizeof(dyn)), out, 4096));
  EXPECT_EQ(out, expect);
  EXPECT_FALSE(inflateData("\x07garbage", out, 100));
}

TEST(XtApp, ImagesQrHashZlibFromLua) {
  Fixture fx;
  ASSERT_TRUE(fx.start(R"(
    local img, e = ctx.image:decode('IMG1....')
    assert(img and img.width == 2 and img.height == 2, 'decode')
    local bad, why = ctx.image:decode('nope'); assert(bad == nil and why == 'decode_failed')
    qr = ctx.qr:encode('https://example.org')
    assert(qr.width == 21)
    assert(#ctx.hash:sha1('abc') == 20 and #ctx.hash:sha256('abc') == 32)
    assert(ctx.hex:encode(ctx.hash:sha1('abc')) == 'a9993e364706816aba3e25717850c26c9cd0d89d')
    local z = ctx.zlib:decompress('\x78\x9c\x4b\x4c\xc4\x04\x00\x4f\xa6\x07\x95')
    assert(z == string.rep('a', 20), 'zlib')
    function on_draw(c, gg)
      gg:clear(0)
      gg:image(qr, 10, 10, {w = 42, h = 42})   -- black block
      gg:image(img, 100, 100, {w = 8, h = 8})  -- checker, scaled x4
      img:release()
      gg:image(img, 0, 0)                      -- released: draws nothing, no error
    end
  )"));
  fx.app->draw();
  ASSERT_FALSE(fx.app->failed()) << fx.app->error();
  EXPECT_TRUE(fx.cv.at(20, 20));    // qr block black
  EXPECT_TRUE(fx.cv.at(101, 101));  // checker top-left black
  EXPECT_FALSE(fx.cv.at(105, 101)); // checker top-right white
}

TEST(XtApp, LayersDrawOffscreenThenBlit) {
  Fixture fx;
  ASSERT_TRUE(fx.start(R"(
    layer = ctx.layers:create(40, 40)
    assert(layer.width == 40 and layer.height == 40)
    layer:draw(function(lg)
      lg:clear(0)
      lg:rect(5, 5, 10, 10, true)
      lg:line(0, 39, 39, 39)
      local w, h = lg:size(); assert(w == 40 and h == 40, 'layer size')
    end)
    assert(not pcall(function() layer:draw(function() layer:draw(function() end) end) end), 'nested')
    function on_draw(c, gg)
      gg:clear(0)
      gg:image(layer, 100, 100)
      gg:rect(300, 300, 4, 4, true)   -- main canvas again after draw()
      layer:release()
    end
  )"));
  fx.app->draw();
  ASSERT_FALSE(fx.app->failed()) << fx.app->error();
  EXPECT_TRUE(fx.cv.at(110, 110));    // filled square, offset by the blit
  EXPECT_FALSE(fx.cv.at(130, 120));   // white area
  EXPECT_TRUE(fx.cv.at(120, 139));    // bottom line
  EXPECT_TRUE(fx.cv.at(301, 301));
  EXPECT_FALSE(fx.cv.at(10, 10));     // nothing leaked to the main canvas
}

TEST(XtApp, ReaderStatisticsNeedThePermission) {
  struct ReaderEnv : MemEnv {
    bool readerSummary(ReaderSummary& o) override {
      o.totalSec = 7200;
      o.todaySec = 0;
      o.streakDays = 3;
      o.bestStreakDays = 9;
      return true;
    }
    bool readerBooks(std::vector<ReaderBook>& o) override {
      ReaderBook b;
      b.id = "/b/one.epub";
      b.title = "One";
      b.author = "A";
      b.format = "epub";
      b.readSec = 600;
      b.finished = true;
      o.push_back(b);
      return true;
    }
    bool readerCurrent(ReaderBook& o) override {
      o.id = "/b/one.epub";
      o.title = "One";
      o.percent = 42;
      return true;
    }
  };
  const char* lua = R"(
    local r = ctx.reader
    assert(r, 'reader table')
    local s = r:summary()
    assert(s.total_sec == 7200 and s.streak_days == 3 and s.best_streak_days == 9 and s.today_sec == 0)
    local b = r:books()
    assert(#b == 1 and b[1].title == 'One' and b[1].finished == true and b[1].read_sec == 600 and b[1].percent == nil)
    assert(#r:all_books() == 1)
    local c = r:current()
    assert(c.percent == 42 and c.id == '/b/one.epub')
    assert(next(r:days('2026-11-01', '2026-11-14')) == nil and next(r:book_days('a', 'b')) == nil)
    assert(select(2, r:cover('x')) == 'not_found')
  )";
  {
    ReaderEnv env;
    MemCanvas cv;
    env.pkg["index.lua"] = lua;
    App app(env, cv, {});
    ASSERT_TRUE(app.start(R"({"app_id":"t","display_name":"T","entry":"index.lua","api":"2.2","permissions":["reader"]})"))
        << app.error();
    EXPECT_FALSE(app.failed()) << app.error();
  }
  Fixture fx;  // without the permission ctx.reader does not exist
  ASSERT_TRUE(fx.start("assert(ctx.reader == nil)"));
  EXPECT_FALSE(fx.app->failed()) << fx.app->error();
}
