#include "XtApp.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include "XtAppJson.h"
#include "XtAppUtil.h"

namespace xtapp {

// ------------------------------------------------------------------------------------------------ manifest

bool parseManifest(const std::string& json, Manifest& out, std::string* err, const char* locale) {
  JValue root;
  std::string perr;
  if (!parseJson(json, root, &perr) || root.type != JValue::Type::Obj) {
    if (err) *err = "manifest.json: " + (perr.empty() ? std::string("not an object") : perr);
    return false;
  }
  out = Manifest{};
  out.appId = root.str("app_id");
  if (out.appId.empty()) {
    if (err) *err = "manifest.json: app_id missing";
    return false;
  }
  out.displayName = root.str("display_name", root.str("name", out.appId));
  if (const JValue* names = root.get("display_names")) {
    const std::string loc = names->str(locale);
    if (!loc.empty()) out.displayName = loc;
  }
  out.description = root.str("description");
  out.version = root.str("version");
  out.author = root.str("author");
  out.entry = root.str("entry", "index.lua");
  if (const JValue* d = root.get("display")) out.orientation = d->str("orientation", "portrait");
  const std::string api = root.str("api");
  if (!api.empty()) {
    int a = 0, b = 0;
    if (sscanf(api.c_str(), "%d.%d", &a, &b) >= 1) {
      out.apiMajor = a;
      out.apiMinor = b;
    }
  }
  if (const JValue* p = root.get("permissions")) {
    if (p->type == JValue::Type::Arr) {
      for (const auto& v : p->a) {
        if (v.type == JValue::Type::Str) out.permissions.push_back(v.s);
      }
    }
  }
  out.encrypted = root.get("dek_wrap_alg") != nullptr;
  if (const JValue* ls = root.get("lockscreen")) {
    if (ls->type == JValue::Type::Obj) {
      const JValue* en = ls->get("enabled");
      out.lockscreenEnabled = en ? (en->type == JValue::Type::Bool ? en->b : en->type == JValue::Type::Int && en->i != 0)
                                 : true;
      out.lockscreenEntry = ls->str("entry", "lockscreen.lua");
      if (const JValue* iv = ls->get("interval_sec")) {
        const int64_t v = iv->type == JValue::Type::Int ? iv->i : iv->type == JValue::Type::Num ? static_cast<int64_t>(iv->n) : 30;
        out.lockscreenIntervalSec = static_cast<int>(std::clamp<int64_t>(v, 1, 86400));
      }
    }
  }
  return true;
}

ContainerInfo inspectContainer(const std::string& head) {
  ContainerInfo info;
  if (head.size() < 0x80 || head.compare(0, 4, "XTAP") != 0) return info;
  auto u16 = [&](size_t o) { return static_cast<uint32_t>(static_cast<uint8_t>(head[o]) | (static_cast<uint8_t>(head[o + 1]) << 8)); };
  auto u32 = [&](size_t o) { return u16(o) | (u16(o + 2) << 16); };
  if (u16(4) != 1 || u16(6) != 0x80) return info;
  const uint32_t mOff = u32(0x10);
  const uint32_t mLen = u32(0x14);
  if (mOff != 0x80 || mLen == 0 || mLen > 0x2000 || head.size() < static_cast<size_t>(mOff) + mLen) return info;
  info.manifestJson = head.substr(mOff, mLen);
  info.valid = true;
  JValue m;
  info.encrypted = u32(0x34) != 0 || (parseJson(info.manifestJson, m) && m.get("dek_wrap_alg") != nullptr);
  return info;
}

// ------------------------------------------------------------------------------------------------ allocator

namespace {

struct Heap {
  size_t used = 0;
  size_t limit = 0;
};

void* luaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  auto* h = static_cast<Heap*>(ud);
  if (!ptr) osize = 0;
  if (nsize == 0) {
    free(ptr);
    h->used -= osize;
    return nullptr;
  }
  if (h->used - osize + nsize > h->limit) return nullptr;
  void* np = realloc(ptr, nsize);
  if (np) h->used = h->used - osize + nsize;
  return np;
}

constexpr int kNullSentinel = 0;
int g_nullTag;  // address used as ctx.json.null

}  // namespace

// ------------------------------------------------------------------------------------------------ impl

struct App::Impl {
  App* app = nullptr;
  Env* env = nullptr;
  Canvas* cv = nullptr;
  Canvas* baseCanvas = nullptr;  // cv is swapped to a LayerCanvas during layer:draw
  Limits limits;
  Heap heap;
  lua_State* L = nullptr;
  const void* ctxPtr = nullptr;
  const void* gPtr = nullptr;
  uint32_t callStart = 0;
  uint32_t budgetMs = 0;
  std::string toast;
  bool hasToast = false;
  struct Timer {
    int id;
    uint32_t due;
    int ref;
  };
  std::vector<Timer> timers;
  int nextTimer = 1;
  uint32_t lastTick = 0;
  bool confirmPending = false;
  Confirm confirm;
  int confirmRef = LUA_NOREF;
  bool confirmSetsLockscreen = false;  // the pending confirm is the "set as lock screen" question, not a Lua one
  // Lock screen
  bool lockMode = false;
  uint32_t lockInterval = 30;
  bool lockFlush = false;
  std::string openRequest;
  std::map<std::string, bool> loadedModules;
  // ctx.input:text / :edit
  bool textPending = false;    // requested, not yet handed to the host
  bool textInFlight = false;   // handed out, waiting for answerText
  bool textIsEdit = false;
  TextRequest textReq;
  int textFnRef = LUA_NOREF;   // text: fn(ctx, text); edit: on_end
  int textChangeRef = LUA_NOREF;
  // ctx.net
  struct NetJob {
    int id;
    int fnRef;
    bool handedOut;
  };
  std::vector<NetJob> netJobs;
  std::vector<NetRequest> netQueue;  // not yet handed to the host
  std::vector<int> netCancelled;
  int nextNetId = 1;
  // ctx.net:websocket (one socket at a time)
  int wsId = 0;
  int wsRef = LUA_NOREF;  // handlers table
  bool wsHandedOut = false;
  std::vector<App::WsOpen> wsOpenQ;
  std::vector<App::WsOut> wsOutQ;
  int nextWsId = 1;
  size_t imageBytes = 0;  // decoded images alive in Lua (capped: they live outside the Lua heap)
};

namespace {

App::Impl* S(lua_State* L) { return *static_cast<App::Impl**>(lua_getextraspace(L)); }

int argInt(lua_State* L, int i) {
  const lua_Number n = luaL_checknumber(L, i);
  if (std::isnan(n)) return 0;
  if (n > 1e9) return 1000000000;
  if (n < -1e9) return -1000000000;
  return static_cast<int>(std::lround(n));
}
int optInt(lua_State* L, int i, int def) { return lua_isnoneornil(L, i) ? def : argInt(L, i); }

// ---------------------------------------------------------------- JSON <-> Lua

void pushJson(lua_State* L, const JValue& v) {
  switch (v.type) {
    case JValue::Type::Null: lua_pushlightuserdata(L, &g_nullTag); break;
    case JValue::Type::Bool: lua_pushboolean(L, v.b); break;
    case JValue::Type::Int: lua_pushinteger(L, static_cast<lua_Integer>(v.i)); break;
    case JValue::Type::Num: lua_pushnumber(L, v.n); break;
    case JValue::Type::Str: lua_pushlstring(L, v.s.data(), v.s.size()); break;
    case JValue::Type::Arr:
      lua_createtable(L, static_cast<int>(v.a.size()), 0);
      for (size_t i = 0; i < v.a.size(); ++i) {
        pushJson(L, v.a[i]);
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
      }
      break;
    case JValue::Type::Obj:
      lua_createtable(L, 0, static_cast<int>(v.o.size()));
      for (const auto& kv : v.o) {
        pushJson(L, kv.second);
        lua_setfield(L, -2, kv.first.c_str());
      }
      break;
  }
}

constexpr size_t kMaxJson = 256 * 1024;

bool encodeValue(lua_State* L, int idx, std::string& out, int depth) {
  if (out.size() > kMaxJson || depth > 24) return false;
  idx = lua_absindex(L, idx);
  switch (lua_type(L, idx)) {
    case LUA_TNIL: out += "null"; return true;
    case LUA_TBOOLEAN: out += lua_toboolean(L, idx) ? "true" : "false"; return true;
    case LUA_TNUMBER: {
      char buf[40];
      if (lua_isinteger(L, idx)) {
        snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(lua_tointeger(L, idx)));
      } else {
        const double d = lua_tonumber(L, idx);
        if (!std::isfinite(d)) {
          out += "null";
          return true;
        }
        snprintf(buf, sizeof(buf), "%.17g", d);
      }
      out += buf;
      return true;
    }
    case LUA_TSTRING: {
      size_t n;
      const char* s = lua_tolstring(L, idx, &n);
      appendJsonString(out, std::string(s, n));
      return true;
    }
    case LUA_TLIGHTUSERDATA:
      out += "null";
      return true;
    case LUA_TTABLE: {
      const lua_Unsigned n = lua_rawlen(L, idx);
      size_t total = 0;
      lua_pushnil(L);
      while (lua_next(L, idx)) {
        ++total;
        lua_pop(L, 1);
      }
      if (total == 0 || total == n) {
        out += '[';
        for (lua_Unsigned i = 1; i <= n; ++i) {
          if (i > 1) out += ',';
          lua_rawgeti(L, idx, static_cast<lua_Integer>(i));
          const bool ok = encodeValue(L, -1, out, depth + 1);
          lua_pop(L, 1);
          if (!ok) return false;
        }
        out += ']';
        return true;
      }
      out += '{';
      bool first = true;
      lua_pushnil(L);
      while (lua_next(L, idx)) {
        const int kt = lua_type(L, -2);
        const int vt = lua_type(L, -1);
        if ((kt == LUA_TSTRING || kt == LUA_TNUMBER) && vt != LUA_TFUNCTION && vt != LUA_TUSERDATA &&
            vt != LUA_TTHREAD) {
          if (!first) out += ',';
          first = false;
          lua_pushvalue(L, -2);  // tostring on a copy: lua_next needs the original key untouched
          size_t kn;
          const char* ks = lua_tolstring(L, -1, &kn);
          appendJsonString(out, std::string(ks, kn));
          lua_pop(L, 1);
          out += ':';
          if (!encodeValue(L, -1, out, depth + 1)) {
            lua_pop(L, 2);
            return false;
          }
        }
        lua_pop(L, 1);
      }
      out += '}';
      return true;
    }
    default: out += "null"; return true;  // functions, userdata, threads
  }
}

void setFn(lua_State* L, const char* name, lua_CFunction f) {
  lua_pushcfunction(L, f);
  lua_setfield(L, -2, name);
}

// ---------------------------------------------------------------- generic helpers

int fRetNilErr(lua_State* L, const char* e) {
  lua_pushnil(L);
  lua_pushstring(L, e);
  return 2;
}

bool safePackageName(const std::string& n) {
  if (n.empty() || n.size() > 128 || n[0] == '/' || n[0] == '\\') return false;
  if (n.find("..") != std::string::npos || n.find('\\') != std::string::npos) return false;
  return true;
}

bool safeFileName(const std::string& n) {
  if (n.empty() || n.size() > 64 || n[0] == '.') return false;
  for (const char c : n) {
    if (!(isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-')) return false;
  }
  return true;
}

// ---------------------------------------------------------------- ctx top-level functions (method or plain call)

int ctxOff(lua_State* L) {
  return (lua_istable(L, 1) && lua_topointer(L, 1) == S(L)->ctxPtr) ? 1 : 0;
}

int cInvalidate(lua_State* L) {
  const int off = ctxOff(L);
  const char* mode = lua_isstring(L, off + 1) ? lua_tostring(L, off + 1) : "";
  S(L)->app->redraw_ = true;
  if (!strcmp(mode, "full")) S(L)->app->full_ = true;
  return 0;
}
int cFullRefresh(lua_State* L) {  // ctx.display:full_refresh()
  S(L)->app->redraw_ = true;
  S(L)->app->full_ = true;
  return 0;
}
int cNoop(lua_State*) { return 0; }
int cRetFalse(lua_State* L) {
  lua_pushboolean(L, 0);
  return 1;
}
int cQuit(lua_State* L) {
  S(L)->app->quit_ = true;
  return 0;
}
int cSetTick(lua_State* L) {
  const int off = ctxOff(L);
  const int ms = optInt(L, off + 1, 0);
  S(L)->app->tickMs_ = ms <= 0 ? 0 : static_cast<uint32_t>(std::max(ms, 100));
  return 0;
}
int cSetTickRate(lua_State* L) {
  const int off = ctxOff(L);
  const char* r = luaL_optstring(L, off + 1, "normal");
  uint32_t ms = 100;
  if (!strcmp(r, "realtime")) ms = 33;
  else if (!strcmp(r, "high")) ms = 50;
  else if (!strcmp(r, "low")) ms = 500;
  else if (!strcmp(r, "idle")) ms = 2000;
  S(L)->app->tickMs_ = std::max<uint32_t>(ms, 100);
  return 0;
}
int cAfter(lua_State* L) {
  const int off = ctxOff(L);
  const int ms = std::max(0, argInt(L, off + 1));
  luaL_checktype(L, off + 2, LUA_TFUNCTION);
  lua_pushvalue(L, off + 2);
  auto* s = S(L);
  if (s->timers.size() >= 32) return luaL_error(L, "too many timers");
  const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  const int id = s->nextTimer++;
  s->timers.push_back({id, s->env->millis() + static_cast<uint32_t>(ms), ref});
  lua_pushinteger(L, id);
  return 1;
}
int cCancel(lua_State* L) {
  const int off = ctxOff(L);
  const int id = argInt(L, off + 1);
  auto* s = S(L);
  for (size_t i = 0; i < s->timers.size(); ++i) {
    if (s->timers[i].id == id) {
      luaL_unref(L, LUA_REGISTRYINDEX, s->timers[i].ref);
      s->timers.erase(s->timers.begin() + static_cast<long>(i));
      break;
    }
  }
  return 0;
}

bool saveStateNow(lua_State* L) {
  auto* s = S(L);
  lua_getglobal(L, "ctx");
  lua_getfield(L, -1, "state");
  std::string json;
  const bool ok = lua_istable(L, -1) && encodeValue(L, -1, json, 0) && json.size() <= kMaxJson;
  lua_pop(L, 2);
  return ok && s->env->saveState(json);
}
int cSave(lua_State* L) {
  lua_pushboolean(L, saveStateNow(L));
  return 1;
}

// ---------------------------------------------------------------- clock / sys / device

struct Civil {
  int year, month, day, hour, minute, second, weekday;
};
Civil civil(int64_t t) {
  int64_t days = t / 86400;
  int64_t rem = t % 86400;
  if (rem < 0) {
    rem += 86400;
    --days;
  }
  Civil c{};
  c.hour = static_cast<int>(rem / 3600);
  c.minute = static_cast<int>((rem % 3600) / 60);
  c.second = static_cast<int>(rem % 60);
  c.weekday = static_cast<int>(((days % 7) + 11) % 7);  // 1970-01-01 was a Thursday (4)
  const int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t y = yoe + era * 400;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  c.day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  c.month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  if (c.month <= 2) ++y;
  c.year = static_cast<int>(y);
  return c;
}

void pushClock(lua_State* L) {
  auto* s = S(L);
  const int64_t utc = s->env->epochSec();
  const int64_t local = utc + s->env->localOffsetSec();
  const Civil c = civil(utc > 0 ? local : 0);
  lua_createtable(L, 0, 12);
  auto setI = [&](const char* k, lua_Integer v) {
    lua_pushinteger(L, v);
    lua_setfield(L, -2, k);
  };
  setI("hour", c.hour);
  setI("minute", c.minute);
  setI("second", c.second);
  setI("year", c.year);
  setI("month", c.month);
  setI("day", c.day);
  setI("weekday", c.weekday);
  setI("epoch_sec", utc);
  setI("local_sec", local);
  lua_pushstring(L, "dmy");
  lua_setfield(L, -2, "date_order");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "hour12");
  lua_pushboolean(L, utc > 0);
  lua_setfield(L, -2, "valid");
}
int cClockNow(lua_State* L) {  // ctx.clock:now() and ctx.sys.clock()
  pushClock(L);
  return 1;
}
int cMillis(lua_State* L) {
  lua_pushinteger(L, S(L)->env->millis());
  return 1;
}
int cEpoch(lua_State* L) {
  lua_pushinteger(L, S(L)->env->epochSec());
  return 1;
}
int cLocalSec(lua_State* L) {
  lua_pushinteger(L, S(L)->env->epochSec() + S(L)->env->localOffsetSec());
  return 1;
}
int cBatteryNum(lua_State* L) {
  lua_pushinteger(L, S(L)->env->batteryPercent());
  return 1;
}
int cCharging(lua_State* L) {
  lua_pushboolean(L, S(L)->env->charging());
  return 1;
}
int cDeviceBattery(lua_State* L) {  // ctx.device:battery()
  lua_createtable(L, 0, 4);
  lua_pushinteger(L, S(L)->env->batteryPercent());
  lua_setfield(L, -2, "percent");
  lua_pushboolean(L, S(L)->env->charging());
  lua_setfield(L, -2, "charging");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "voltage_mv");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "temperature_c");
  return 1;
}

// ---------------------------------------------------------------- log, gc, budget, dialogs

int cLog(lua_State* L) {
  const int off = lua_istable(L, 1) ? 1 : 0;
  const char* m = lua_isstring(L, off + 1) ? lua_tostring(L, off + 1) : "";
  S(L)->env->log(lua_tointeger(L, lua_upvalueindex(1)), m);
  return 0;
}
int cPrint(lua_State* L) {
  std::string line;
  const int n = lua_gettop(L);
  for (int i = 1; i <= n; ++i) {
    size_t l;
    const char* s = luaL_tolstring(L, i, &l);
    if (i > 1) line += '\t';
    line.append(s, l);
    lua_pop(L, 1);
  }
  S(L)->env->log(1, line);
  return 0;
}
int cGcCollect(lua_State* L) {
  lua_gc(L, LUA_GCCOLLECT);
  return 0;
}
int cGcCount(lua_State* L) {
  lua_pushinteger(L, static_cast<lua_Integer>(S(L)->heap.used));
  return 1;
}
int cBudgetExtend(lua_State* L) {
  const int ms = std::max(0, optInt(L, 2, 0));
  auto* s = S(L);
  s->budgetMs = std::min<uint32_t>(s->budgetMs + static_cast<uint32_t>(ms), 60000);
  return 0;
}
int cToast(lua_State* L) {  // ctx.dialog:toast(text)
  auto* s = S(L);
  s->toast = luaL_optstring(L, 2, "");
  s->hasToast = true;
  s->app->redraw_ = true;
  return 0;
}
int cConfirm(lua_State* L) {  // ctx.dialog:confirm({title=, text=}, fn(ctx, bool))
  auto* s = S(L);
  luaL_checktype(L, 3, LUA_TFUNCTION);
  s->confirm = {};
  if (lua_istable(L, 2)) {
    lua_getfield(L, 2, "title");
    if (lua_isstring(L, -1)) s->confirm.title = lua_tostring(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "text");
    if (lua_isstring(L, -1)) s->confirm.text = lua_tostring(L, -1);
    lua_pop(L, 1);
  } else if (lua_isstring(L, 2)) {
    s->confirm.text = lua_tostring(L, 2);
  }
  if (s->confirmRef != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, s->confirmRef);
  lua_pushvalue(L, 3);
  s->confirmRef = luaL_ref(L, LUA_REGISTRYINDEX);
  s->confirmPending = true;
  s->app->redraw_ = true;
  return 0;
}

// ---------------------------------------------------------------- system, lock screen
// Both `ctx.system.f()` and `ctx.system:f()` styles are accepted: a leading table argument is the receiver.
int firstArg(lua_State* L) { return lua_istable(L, 1) ? 2 : 1; }

int cLockSetDefault(lua_State* L) {  // ctx.lockscreen:set_default() / ctx.system.set_as_lockscreen_app()
  auto* s = S(L);
  if (!s->app->manifest_.lockscreenEnabled) return luaL_error(L, "this app declares no lockscreen");
  s->confirm = {};
  s->confirm.title = "Set as lock screen";
  s->confirm.text = "Show \"" + s->app->manifest_.displayName + "\" when the device locks? Change it back in Settings > Sleep screen.";
  if (s->confirmRef != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, s->confirmRef);
  s->confirmRef = LUA_NOREF;
  s->confirmSetsLockscreen = true;
  s->confirmPending = true;
  s->app->redraw_ = true;
  return 0;
}
int cLockIsDefault(lua_State* L) {
  auto* s = S(L);
  lua_pushboolean(L, s->env->lockscreenApp() == s->app->manifest_.appId);
  return 1;
}
int cLockClearDefault(lua_State* L) {
  auto* s = S(L);
  if (s->env->lockscreenApp() == s->app->manifest_.appId) s->env->setLockscreenApp("");
  lua_pushboolean(L, s->env->lockscreenApp() != s->app->manifest_.appId);
  return 1;
}
int cSysCanOpen(lua_State* L) {
  const char* id = luaL_checkstring(L, firstArg(L));
  lua_pushboolean(L, S(L)->env->appInstalled(id));
  return 1;
}
int cSysOpen(lua_State* L) {
  auto* s = S(L);
  const char* id = luaL_checkstring(L, firstArg(L));
  if (!s->env->appInstalled(id)) return fRetNilErr(L, "not_found");
  s->openRequest = id;
  s->app->quit_ = true;  // the host opens it once this app is left
  lua_pushboolean(L, 1);
  return 1;
}
int cLockSetInterval(lua_State* L) {  // ctx.lock:set_interval(sec)
  auto* s = S(L);
  const int sec = argInt(L, lua_istable(L, 1) ? 2 : 1);
  s->lockInterval = static_cast<uint32_t>(std::clamp(sec, 1, 86400));
  return 0;
}
int cLockFlushOnce(lua_State* L) {
  S(L)->lockFlush = true;
  return 0;
}

// ---------------------------------------------------------------- fonts, screen

struct FontDef {
  const char* name;
  int size;
};
constexpr FontDef kFonts[3] = {{"small", 0}, {"medium", 1}, {"large", 2}};

int fontSizeOf(lua_State* L, int idx) {
  if (lua_istable(L, idx)) {
    lua_getfield(L, idx, "_sz");
    const int v = lua_isinteger(L, -1) ? static_cast<int>(lua_tointeger(L, -1)) : 1;
    lua_pop(L, 1);
    return std::clamp(v, 0, 2);
  }
  return 1;
}
int optFontSize(lua_State* L, int optsIdx) {
  if (!lua_istable(L, optsIdx)) return 1;
  lua_getfield(L, optsIdx, "font");
  const int sz = lua_istable(L, -1) ? fontSizeOf(L, -1) : 1;
  lua_pop(L, 1);
  return sz;
}
int cFontMeasure(lua_State* L) {  // font:measure(s)
  const int sz = fontSizeOf(L, 1);
  lua_pushinteger(L, S(L)->cv->textWidth(luaL_optstring(L, 2, ""), sz));
  return 1;
}
int cFontsSize(lua_State* L) {  // ctx.fonts:size(n)
  const int n = optInt(L, 2, 24);
  const int pick = n <= 20 ? 0 : (n <= 27 ? 1 : 2);
  lua_getglobal(L, "ctx");
  lua_getfield(L, -1, "fonts");
  lua_getfield(L, -1, kFonts[pick].name);
  return 1;
}
int cFontsHandle(lua_State* L) {
  lua_getglobal(L, "ctx");
  lua_getfield(L, -1, "fonts");
  lua_getfield(L, -1, "medium");
  return 1;
}

// ---------------------------------------------------------------- storage (ctx.storage / ctx.files)

constexpr size_t kMaxAppFile = 2 * 1024 * 1024;
constexpr size_t kMaxAppFiles = 512;

int cStRead(lua_State* L) {
  const std::string name = luaL_checkstring(L, 2);
  if (!safeFileName(name)) return fRetNilErr(L, "invalid_argument");
  std::string data;
  if (!S(L)->env->readFile(name, data)) return fRetNilErr(L, "not_found");
  lua_pushlstring(L, data.data(), data.size());
  return 1;
}
int cStWrite(lua_State* L) {
  const std::string name = luaL_checkstring(L, 2);
  size_t n;
  const char* d = luaL_checklstring(L, 3, &n);
  if (!safeFileName(name)) return fRetNilErr(L, "invalid_argument");
  if (n > kMaxAppFile) return fRetNilErr(L, "too_large");
  auto* s = S(L);
  if (s->env->fileSize(name) < 0 && s->env->listFiles().size() >= kMaxAppFiles) return fRetNilErr(L, "quota");
  if (!s->env->writeFile(name, std::string(d, n))) return fRetNilErr(L, "io_error");
  lua_pushboolean(L, 1);
  return 1;
}
int cStExists(lua_State* L) {
  const std::string name = luaL_checkstring(L, 2);
  lua_pushboolean(L, safeFileName(name) && S(L)->env->fileSize(name) >= 0);
  return 1;
}
int cStSize(lua_State* L) {
  const std::string name = luaL_checkstring(L, 2);
  const int64_t sz = safeFileName(name) ? S(L)->env->fileSize(name) : -1;
  if (sz < 0) return fRetNilErr(L, "not_found");
  lua_pushinteger(L, sz);
  return 1;
}
int cStRemove(lua_State* L) {
  const std::string name = luaL_checkstring(L, 2);
  if (!safeFileName(name)) return fRetNilErr(L, "invalid_argument");
  if (S(L)->env->fileSize(name) < 0) return fRetNilErr(L, "not_found");
  lua_pushboolean(L, S(L)->env->removeFile(name));
  return 1;
}
int cStList(lua_State* L) {
  const auto names = S(L)->env->listFiles();
  lua_createtable(L, static_cast<int>(names.size()), 0);
  int i = 1;
  for (const auto& n : names) {
    lua_pushstring(L, n.c_str());
    lua_rawseti(L, -2, i++);
  }
  return 1;
}
int cStUsage(lua_State* L) {
  auto* s = S(L);
  const auto names = s->env->listFiles();
  int64_t bytes = 0;
  for (const auto& n : names) bytes += std::max<int64_t>(0, s->env->fileSize(n));
  lua_createtable(L, 0, 4);
  lua_pushinteger(L, bytes);
  lua_setfield(L, -2, "bytes");
  lua_pushinteger(L, static_cast<lua_Integer>(names.size()));
  lua_setfield(L, -2, "files");
  lua_pushinteger(L, 128 * 1024 * 1024);
  lua_setfield(L, -2, "max_bytes");
  lua_pushinteger(L, kMaxAppFiles);
  lua_setfield(L, -2, "max_files");
  return 1;
}

// ---------------------------------------------------------------- package / data (read-only app files)

bool readPackageFile(lua_State* L, const std::string& name, std::string& out) {
  auto* s = S(L);
  if (!safePackageName(name)) return false;
  if (s->env->readPackage(name, out)) return true;
  return s->env->readPackage("data/" + name, out);
}
int cPkgRead(lua_State* L) {
  const std::string name = luaL_checkstring(L, 2);
  if (!safePackageName(name)) return fRetNilErr(L, "invalid_name");
  std::string d;
  if (!readPackageFile(L, name, d)) return fRetNilErr(L, "not_found");
  lua_pushlstring(L, d.data(), d.size());
  return 1;
}
int cPkgExists(lua_State* L) {
  std::string d;
  lua_pushboolean(L, readPackageFile(L, luaL_checkstring(L, 2), d));
  return 1;
}
int cPkgSize(lua_State* L) {
  std::string d;
  if (!readPackageFile(L, luaL_checkstring(L, 2), d)) return fRetNilErr(L, "not_found");
  lua_pushinteger(L, static_cast<lua_Integer>(d.size()));
  return 1;
}

// ---------------------------------------------------------------- json / encoding / misc

int cJsonDecode(lua_State* L) {
  size_t n;
  const char* s = luaL_checklstring(L, 2, &n);
  JValue v;
  std::string err;
  if (n > kMaxJson || !parseJson(std::string(s, n), v, &err)) return fRetNilErr(L, err.empty() ? "too_large" : err.c_str());
  pushJson(L, v);
  return 1;
}
int cJsonEncode(lua_State* L) {
  luaL_checkany(L, 2);
  std::string out;
  if (!encodeValue(L, 2, out, 0)) return fRetNilErr(L, "too_large_or_deep");
  lua_pushlstring(L, out.data(), out.size());
  return 1;
}

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int cB64Encode(lua_State* L) {
  size_t n;
  const unsigned char* s = reinterpret_cast<const unsigned char*>(luaL_checklstring(L, 2, &n));
  std::string o;
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t v = (s[i] << 16) | (i + 1 < n ? s[i + 1] << 8 : 0) | (i + 2 < n ? s[i + 2] : 0);
    o += kB64[(v >> 18) & 63];
    o += kB64[(v >> 12) & 63];
    o += i + 1 < n ? kB64[(v >> 6) & 63] : '=';
    o += i + 2 < n ? kB64[v & 63] : '=';
  }
  lua_pushlstring(L, o.data(), o.size());
  return 1;
}
int cB64Decode(lua_State* L) {
  size_t n;
  const char* s = luaL_checklstring(L, 2, &n);
  std::string o;
  uint32_t acc = 0;
  int bits = 0;
  for (size_t i = 0; i < n; ++i) {
    const char* p = strchr(kB64, s[i]);
    if (s[i] == '=' || s[i] == '\n' || s[i] == '\r' || s[i] == ' ') continue;
    if (!p || s[i] == 0) return fRetNilErr(L, "invalid_argument");
    acc = (acc << 6) | static_cast<uint32_t>(p - kB64);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      o += static_cast<char>((acc >> bits) & 0xFF);
    }
  }
  lua_pushlstring(L, o.data(), o.size());
  return 1;
}
int cHexEncode(lua_State* L) {
  size_t n;
  const unsigned char* s = reinterpret_cast<const unsigned char*>(luaL_checklstring(L, 2, &n));
  std::string o;
  char b[3];
  for (size_t i = 0; i < n; ++i) {
    snprintf(b, sizeof(b), "%02x", s[i]);
    o += b;
  }
  lua_pushlstring(L, o.data(), o.size());
  return 1;
}
int cHexDecode(lua_State* L) {
  size_t n;
  const char* s = luaL_checklstring(L, 2, &n);
  if (n % 2) return fRetNilErr(L, "invalid_argument");
  std::string o;
  for (size_t i = 0; i < n; i += 2) {
    char b[3] = {s[i], s[i + 1], 0};
    char* e;
    const long v = strtol(b, &e, 16);
    if (*e) return fRetNilErr(L, "invalid_argument");
    o += static_cast<char>(v);
  }
  lua_pushlstring(L, o.data(), o.size());
  return 1;
}
int cUrlEncode(lua_State* L) {
  size_t n;
  const unsigned char* s = reinterpret_cast<const unsigned char*>(luaL_checklstring(L, 2, &n));
  std::string o;
  for (size_t i = 0; i < n; ++i) {
    if (isalnum(s[i]) || s[i] == '-' || s[i] == '_' || s[i] == '.' || s[i] == '~') {
      o += static_cast<char>(s[i]);
    } else {
      char b[4];
      snprintf(b, sizeof(b), "%%%02X", s[i]);
      o += b;
    }
  }
  lua_pushlstring(L, o.data(), o.size());
  return 1;
}
int cUrlDecode(lua_State* L) {
  size_t n;
  const char* s = luaL_checklstring(L, 2, &n);
  std::string o;
  for (size_t i = 0; i < n; ++i) {
    if (s[i] == '%' && i + 2 < n + 0 && isxdigit(static_cast<unsigned char>(s[i + 1])) &&
        isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      char b[3] = {s[i + 1], s[i + 2], 0};
      o += static_cast<char>(strtol(b, nullptr, 16));
      i += 2;
    } else if (s[i] == '+') {
      o += ' ';
    } else {
      o += s[i];
    }
  }
  lua_pushlstring(L, o.data(), o.size());
  return 1;
}
int cRandomBytes(lua_State* L) {
  const int n = std::clamp(optInt(L, 2, 16), 0, 4096);
  std::string o;
  for (int i = 0; i < n; ++i) o += static_cast<char>(rand() & 0xFF);
  lua_pushlstring(L, o.data(), o.size());
  return 1;
}
int cRandomUuid(lua_State* L) {
  char b[40];
  snprintf(b, sizeof(b), "%08x-%04x-4%03x-%04x-%08x%04x", static_cast<unsigned>(rand()), rand() & 0xFFFF, rand() & 0xFFF,
           0x8000 | (rand() & 0x3FFF), static_cast<unsigned>(rand()), rand() & 0xFFFF);
  lua_pushstring(L, b);
  return 1;
}

// ---------------------------------------------------------------- require

int cRequire(lua_State* L) {
  auto* s = S(L);
  const std::string name = luaL_checkstring(L, 1);
  lua_getfield(L, LUA_REGISTRYINDEX, "_XTLOADED");
  lua_getfield(L, -1, name.c_str());
  if (!lua_isnil(L, -1)) return 1;
  lua_pop(L, 1);
  std::string rel = name;
  std::replace(rel.begin(), rel.end(), '.', '/');
  std::string src;
  std::string used;
  for (const std::string& cand : {rel + ".lua", rel + "/init.lua", "src/" + rel + ".lua"}) {
    if (safePackageName(cand) && s->env->readPackage(cand, src)) {
      used = cand;
      break;
    }
  }
  if (used.empty()) return luaL_error(L, "module '%s' not found in the app package", name.c_str());
  if (luaL_loadbufferx(L, src.data(), src.size(), ("@" + used).c_str(), "t") != LUA_OK) return lua_error(L);
  lua_pushstring(L, name.c_str());
  lua_call(L, 1, 1);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_pushboolean(L, 1);
  }
  lua_pushvalue(L, -1);
  lua_setfield(L, -3, name.c_str());
  return 1;
}

// ---------------------------------------------------------------- drawing (g)

bool blackArg(lua_State* L, int idx) {  // optional colour arg: 0 / false = white
  if (lua_isnoneornil(L, idx)) return true;
  if (lua_isboolean(L, idx)) return lua_toboolean(L, idx);
  if (lua_isnumber(L, idx)) return lua_tonumber(L, idx) != 0;
  return true;
}
bool fillArg(lua_State* L, int idx) {
  if (lua_isboolean(L, idx)) return lua_toboolean(L, idx);
  if (lua_istable(L, idx)) {
    lua_getfield(L, idx, "fill");
    const bool f = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return f;
  }
  return false;
}

void hline(Canvas& c, int x0, int x1, int y, bool black) {
  if (x1 < x0) std::swap(x0, x1);
  c.fillRect(x0, y, x1 - x0 + 1, 1, black);
}

void drawCircle(Canvas& c, int cx, int cy, int r, bool fill, bool black) {
  if (r <= 0) {
    c.pixel(cx, cy, black);
    return;
  }
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    if (fill) {
      hline(c, cx - x, cx + x, cy + y, black);
      if (y) hline(c, cx - x, cx + x, cy - y, black);
      hline(c, cx - y, cx + y, cy + x, black);
      hline(c, cx - y, cx + y, cy - x, black);
    } else {
      c.pixel(cx + x, cy + y, black);
      c.pixel(cx - x, cy + y, black);
      c.pixel(cx + x, cy - y, black);
      c.pixel(cx - x, cy - y, black);
      c.pixel(cx + y, cy + x, black);
      c.pixel(cx - y, cy + x, black);
      c.pixel(cx + y, cy - x, black);
      c.pixel(cx - y, cy - x, black);
    }
    ++y;
    if (err < 0) {
      err += 2 * y + 1;
    } else {
      --x;
      err += 2 * (y - x) + 1;
    }
  }
}

void drawArc(Canvas& c, int cx, int cy, int r, double a0, double a1, bool black) {
  if (a1 < a0) std::swap(a0, a1);
  if (a1 - a0 > 360) a1 = a0 + 360;
  const double step = std::max(2.0, 360.0 / std::max(8, r * 2));
  int px = 0, py = 0;
  bool have = false;
  for (double a = a0;; a += step) {
    const double aa = std::min(a, a1);
    const double rad = aa * 3.14159265358979 / 180.0;
    const int x = cx + static_cast<int>(std::lround(std::cos(rad) * r));
    const int y = cy + static_cast<int>(std::lround(std::sin(rad) * r));
    if (have) c.line(px, py, x, y, black);
    px = x;
    py = y;
    have = true;
    if (aa >= a1) break;
  }
}

bool readPoints(lua_State* L, int idx, std::vector<int>& xs, std::vector<int>& ys) {
  if (!lua_istable(L, idx)) return false;
  const lua_Unsigned n = lua_rawlen(L, idx);
  if (n > 4096) return false;
  lua_rawgeti(L, idx, 1);
  const bool nested = lua_istable(L, -1);
  lua_pop(L, 1);
  if (nested) {
    for (lua_Unsigned i = 1; i <= n; ++i) {
      lua_rawgeti(L, idx, static_cast<lua_Integer>(i));
      lua_rawgeti(L, -1, 1);
      lua_rawgeti(L, -2, 2);
      if (lua_isnumber(L, -2) && lua_isnumber(L, -1)) {
        xs.push_back(static_cast<int>(lua_tonumber(L, -2)));
        ys.push_back(static_cast<int>(lua_tonumber(L, -1)));
      } else {
        lua_pop(L, 3);
        lua_getfield(L, -1, "x");
        lua_getfield(L, -2, "y");
        xs.push_back(static_cast<int>(lua_tonumber(L, -2)));
        ys.push_back(static_cast<int>(lua_tonumber(L, -1)));
      }
      lua_pop(L, 3);
    }
  } else {
    for (lua_Unsigned i = 1; i + 1 <= n; i += 2) {
      lua_rawgeti(L, idx, static_cast<lua_Integer>(i));
      lua_rawgeti(L, idx, static_cast<lua_Integer>(i + 1));
      xs.push_back(static_cast<int>(lua_tonumber(L, -2)));
      ys.push_back(static_cast<int>(lua_tonumber(L, -1)));
      lua_pop(L, 2);
    }
  }
  return true;
}

int gClear(lua_State* L) {
  S(L)->cv->clear(lua_isnoneornil(L, 2) ? true : blackArg(L, 2));
  return 0;
}
int gRect(lua_State* L) {
  Canvas& c = *S(L)->cv;
  const int x = argInt(L, 2), y = argInt(L, 3), w = argInt(L, 4), h = argInt(L, 5);
  if (w <= 0 || h <= 0) return 0;
  const bool black = true;
  if (fillArg(L, 6)) {
    c.fillRect(x, y, w, h, black);
    return 0;
  }
  c.fillRect(x, y, w, 1, black);
  if (h > 1) c.fillRect(x, y + h - 1, w, 1, black);
  if (h > 2) {
    c.fillRect(x, y + 1, 1, h - 2, black);
    if (w > 1) c.fillRect(x + w - 1, y + 1, 1, h - 2, black);
  }
  return 0;
}
int gRoundRect(lua_State* L) {
  Canvas& c = *S(L)->cv;
  const int x = argInt(L, 2), y = argInt(L, 3), w = argInt(L, 4), h = argInt(L, 5);
  int r = optInt(L, 6, 6);
  if (w <= 0 || h <= 0) return 0;
  r = std::clamp(r, 0, std::min(w, h) / 2);
  const bool fill = fillArg(L, 7);
  if (fill) {
    c.fillRect(x, y + r, w, h - 2 * r, true);
    c.fillRect(x + r, y, w - 2 * r, r, true);
    c.fillRect(x + r, y + h - r, w - 2 * r, r, true);
    drawCircle(c, x + r, y + r, r, true, true);
    drawCircle(c, x + w - 1 - r, y + r, r, true, true);
    drawCircle(c, x + r, y + h - 1 - r, r, true, true);
    drawCircle(c, x + w - 1 - r, y + h - 1 - r, r, true, true);
    return 0;
  }
  c.line(x + r, y, x + w - 1 - r, y, true);
  c.line(x + r, y + h - 1, x + w - 1 - r, y + h - 1, true);
  c.line(x, y + r, x, y + h - 1 - r, true);
  c.line(x + w - 1, y + r, x + w - 1, y + h - 1 - r, true);
  drawArc(c, x + r, y + r, r, 180, 270, true);
  drawArc(c, x + w - 1 - r, y + r, r, 270, 360, true);
  drawArc(c, x + w - 1 - r, y + h - 1 - r, r, 0, 90, true);
  drawArc(c, x + r, y + h - 1 - r, r, 90, 180, true);
  return 0;
}
int gLine(lua_State* L) {
  S(L)->cv->line(argInt(L, 2), argInt(L, 3), argInt(L, 4), argInt(L, 5), blackArg(L, 6));
  return 0;
}
int gCircle(lua_State* L) {
  drawCircle(*S(L)->cv, argInt(L, 2), argInt(L, 3), argInt(L, 4), fillArg(L, 5), true);
  return 0;
}
int gArc(lua_State* L) {
  drawArc(*S(L)->cv, argInt(L, 2), argInt(L, 3), argInt(L, 4), luaL_checknumber(L, 5), luaL_checknumber(L, 6), true);
  return 0;
}
int gText(lua_State* L) {
  const int x = argInt(L, 2), y = argInt(L, 3);
  const char* s = luaL_tolstring(L, 4, nullptr);
  const int sz = optFontSize(L, 5);
  bool black = true;
  if (lua_istable(L, 5)) {
    lua_getfield(L, 5, "color");
    if (!lua_isnil(L, -1)) black = lua_isboolean(L, -1) ? lua_toboolean(L, -1) : lua_tonumber(L, -1) != 0;
    lua_pop(L, 1);
  }
  S(L)->cv->text(x, y, s, sz, black);
  return 0;
}
int gTextBox(lua_State* L) {
  // g:text_box(x, y, w, [h,] text [, opts]) -> height used
  Canvas& c = *S(L)->cv;
  int nums[4];
  int cnt = 0, i = 2;
  while (cnt < 4 && lua_isnumber(L, i) && lua_type(L, i) == LUA_TNUMBER) nums[cnt++] = argInt(L, i++);
  if (cnt < 3) return luaL_error(L, "text_box: x, y, width expected");
  const char* text = luaL_tolstring(L, i, nullptr);
  const int sz = optFontSize(L, i + 1);
  const int x = nums[0], y = nums[1], w = nums[2];
  const int maxH = cnt == 4 ? nums[3] : 100000;
  const int lh = c.lineHeight(sz);
  int cy = y;
  std::string line;
  auto flush = [&]() {
    if (cy + lh - y <= maxH) c.text(x, cy, line.c_str(), sz, true);
    cy += lh;
    line.clear();
  };
  std::string word;
  auto addWord = [&]() {
    if (word.empty()) return;
    const std::string cand = line.empty() ? word : line + " " + word;
    if (!line.empty() && c.textWidth(cand.c_str(), sz) > w) {
      flush();
      line = word;
    } else {
      line = cand;
    }
    word.clear();
  };
  for (const char* p = text; *p; ++p) {
    if (*p == '\n') {
      addWord();
      flush();
    } else if (*p == ' ') {
      addWord();
    } else {
      word += *p;
    }
  }
  addWord();
  if (!line.empty()) flush();
  lua_pushinteger(L, cy - y);
  return 1;
}
int gSize(lua_State* L) {
  lua_pushinteger(L, S(L)->cv->width());
  lua_pushinteger(L, S(L)->cv->height());
  return 2;
}
int gMeasure(lua_State* L) {
  const char* s = luaL_tolstring(L, 2, nullptr);
  const int sz = optFontSize(L, 3);
  lua_pushinteger(L, S(L)->cv->textWidth(s, sz));
  lua_pushinteger(L, S(L)->cv->lineHeight(sz));
  return 2;
}
int gPolygon(lua_State* L) {
  Canvas& c = *S(L)->cv;
  std::vector<int> xs, ys;
  if (!readPoints(L, 2, xs, ys) || xs.size() < 2) return 0;
  const bool fill = fillArg(L, 3);
  const size_t n = xs.size();
  if (fill && n >= 3) {
    const int y0 = *std::min_element(ys.begin(), ys.end());
    const int y1 = *std::max_element(ys.begin(), ys.end());
    for (int y = std::max(0, y0); y <= std::min(c.height() - 1, y1); ++y) {
      std::vector<int> cross;
      for (size_t i = 0; i < n; ++i) {
        const size_t j = (i + 1) % n;
        const int ya = ys[i], yb = ys[j];
        if ((ya <= y && yb > y) || (yb <= y && ya > y)) {
          cross.push_back(xs[i] + (y - ya) * (xs[j] - xs[i]) / (yb - ya));
        }
      }
      std::sort(cross.begin(), cross.end());
      for (size_t k = 0; k + 1 < cross.size(); k += 2) hline(c, cross[k], cross[k + 1], y, true);
    }
  }
  for (size_t i = 0; i < n; ++i) {
    const size_t j = (i + 1) % n;
    c.line(xs[i], ys[i], xs[j], ys[j], true);
  }
  return 0;
}
int gPolyline(lua_State* L) {
  std::vector<int> xs, ys;
  if (!readPoints(L, 2, xs, ys)) return 0;
  for (size_t i = 0; i + 1 < xs.size(); ++i) S(L)->cv->line(xs[i], ys[i], xs[i + 1], ys[i + 1], true);
  return 0;
}
int gPattern(lua_State* L) {  // pattern_rect / fill_pattern: 50% dither fill of x, y, w, h
  Canvas& c = *S(L)->cv;
  const int x = argInt(L, 2), y = argInt(L, 3), w = argInt(L, 4), h = argInt(L, 5);
  for (int yy = std::max(0, y); yy < y + h && yy < c.height(); ++yy) {
    for (int xx = x + ((yy - y) & 1); xx < x + w; xx += 2) c.pixel(xx, yy, true);
  }
  return 0;
}


// ---------------------------------------------------------------- text input

void releaseTextRefs(lua_State* L, App::Impl* s) {
  if (s->textFnRef != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, s->textFnRef);
  if (s->textChangeRef != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, s->textChangeRef);
  s->textFnRef = s->textChangeRef = LUA_NOREF;
}

void readTextOpts(lua_State* L, int idx, App::TextRequest& r) {
  r = {};
  if (!lua_istable(L, idx)) return;
  auto str = [&](const char* k, std::string& dst) {
    lua_getfield(L, idx, k);
    if (lua_isstring(L, -1)) dst = lua_tostring(L, -1);
    lua_pop(L, 1);
  };
  auto num = [&](const char* k, size_t& dst) {
    lua_getfield(L, idx, k);
    if (lua_isnumber(L, -1) && lua_tonumber(L, -1) > 0) dst = static_cast<size_t>(lua_tonumber(L, -1));
    lua_pop(L, 1);
  };
  str("title", r.title);
  str("text", r.text);
  num("max_chars", r.maxChars);
  num("max_bytes", r.maxBytes);
}

int cInputText(lua_State* L) {  // ctx.input:text({title=, text=, max_chars=, max_bytes=}, fn(ctx, text))
  auto* s = S(L);
  luaL_checktype(L, 3, LUA_TFUNCTION);
  if (s->textPending || s->textInFlight) return luaL_error(L, "a text editor is already open");
  readTextOpts(L, 2, s->textReq);
  lua_pushvalue(L, 3);
  s->textFnRef = luaL_ref(L, LUA_REGISTRYINDEX);
  s->textIsEdit = false;
  s->textPending = true;
  return 0;
}

int cEditSetText(lua_State* L) {  // handle:set_text(t)
  auto* s = S(L);
  if (lua_isstring(L, 2)) s->textReq.text = lua_tostring(L, 2);
  lua_pushvalue(L, 1);
  lua_pushstring(L, s->textReq.text.c_str());
  lua_setfield(L, -2, "text");
  lua_pop(L, 1);
  return 0;
}
int cEditClose(lua_State* L) {  // handle:close()
  auto* s = S(L);
  if (s->textPending) {
    s->textPending = false;
    releaseTextRefs(L, s);
  }
  return 0;
}
int cInputEdit(lua_State* L) {  // ctx.input:edit({text=, on_change=, on_end=}) -> handle
  auto* s = S(L);
  if (s->textPending || s->textInFlight) return luaL_error(L, "a text editor is already open");
  readTextOpts(L, 2, s->textReq);
  if (lua_istable(L, 2)) {
    lua_getfield(L, 2, "on_end");
    if (lua_isfunction(L, -1)) s->textFnRef = luaL_ref(L, LUA_REGISTRYINDEX);
    else lua_pop(L, 1);
    lua_getfield(L, 2, "on_change");
    if (lua_isfunction(L, -1)) s->textChangeRef = luaL_ref(L, LUA_REGISTRYINDEX);
    else lua_pop(L, 1);
  }
  s->textIsEdit = true;
  s->textPending = true;
  lua_createtable(L, 0, 4);
  lua_pushstring(L, s->textReq.text.c_str());
  lua_setfield(L, -2, "text");
  setFn(L, "set_text", cEditSetText);
  setFn(L, "close", cEditClose);
  return 1;
}


// ---------------------------------------------------------------- images, QR, zlib, hash

constexpr size_t kMaxImageBytes = 1536 * 1024;
const char kImageMeta[] = "xtapp.image";

struct ImageBox {
  ImageData* img;
  App::Impl* owner;
  bool layer;
};

ImageBox* checkImage(lua_State* L, int idx) {
  auto* b = static_cast<ImageBox*>(luaL_testudata(L, idx, kImageMeta));
  return (b && b->img) ? b : nullptr;
}
void freeImage(ImageBox* b) {
  if (!b->img) return;
  b->owner->imageBytes -= std::min(b->owner->imageBytes, b->img->gray.size());
  delete b->img;
  b->img = nullptr;
}
int cImageGc(lua_State* L) {
  freeImage(static_cast<ImageBox*>(lua_touserdata(L, 1)));
  return 0;
}
int cImageRelease(lua_State* L) {
  if (auto* b = static_cast<ImageBox*>(luaL_testudata(L, 1, kImageMeta))) freeImage(b);
  return 0;
}
int cLayerDraw(lua_State* L);

int cImageIndex(lua_State* L) {
  auto* b = static_cast<ImageBox*>(luaL_checkudata(L, 1, kImageMeta));
  const char* k = luaL_checkstring(L, 2);
  if (!strcmp(k, "release")) {
    lua_pushcfunction(L, cImageRelease);
  } else if (b->layer && !strcmp(k, "draw")) {
    lua_pushcfunction(L, cLayerDraw);
  } else if (b->img && (!strcmp(k, "width") || !strcmp(k, "w"))) {
    lua_pushinteger(L, b->img->w);
  } else if (b->img && (!strcmp(k, "height") || !strcmp(k, "h"))) {
    lua_pushinteger(L, b->img->h);
  } else {
    lua_pushnil(L);
  }
  return 1;
}
// Moves `img` into a new userdata on top of the stack (or pushes nil, err).
int pushImage(lua_State* L, ImageData&& img) {
  auto* s = S(L);
  const size_t bytes = img.gray.size();
  if (s->imageBytes + bytes > kMaxImageBytes) return fRetNilErr(L, "out_of_memory");
  auto* b = static_cast<ImageBox*>(lua_newuserdatauv(L, sizeof(ImageBox), 0));
  b->img = new (std::nothrow) ImageData(std::move(img));
  b->owner = s;
  b->layer = false;
  if (!b->img) {
    lua_pop(L, 1);
    return fRetNilErr(L, "out_of_memory");
  }
  s->imageBytes += bytes;
  if (luaL_newmetatable(L, kImageMeta)) {
    lua_pushcfunction(L, cImageGc);
    lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, cImageIndex);
    lua_setfield(L, -2, "__index");
  }
  lua_setmetatable(L, -2);
  return 1;
}


// Offscreen drawing target for ctx.layers: 1-bit content stored as gray 0 / 255. Text cannot be rendered offscreen by
// the host fonts, so text drawn into a layer is skipped (measuring still works through the real canvas).
struct LayerCanvas final : Canvas {
  Canvas& base;
  ImageData& img;
  LayerCanvas(Canvas& b, ImageData& i) : base(b), img(i) {}
  int width() const override { return img.w; }
  int height() const override { return img.h; }
  void clear(bool black) override { std::fill(img.gray.begin(), img.gray.end(), black ? 0 : 255); }
  void pixel(int x, int y, bool black) override {
    if (x >= 0 && y >= 0 && x < img.w && y < img.h) img.gray[static_cast<size_t>(y) * img.w + x] = black ? 0 : 255;
  }
  void fillRect(int x, int y, int w, int h, bool black) override {
    const int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(img.w, x + w), y1 = std::min(img.h, y + h);
    for (int yy = y0; yy < y1; ++yy) {
      std::fill(img.gray.begin() + static_cast<long>(yy) * img.w + x0, img.gray.begin() + static_cast<long>(yy) * img.w + x1,
                black ? 0 : 255);
    }
  }
  void line(int x0, int y0, int x1, int y1, bool black) override {
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int guard = 0; guard < 100000; ++guard) {
      pixel(x0, y0, black);
      if (x0 == x1 && y0 == y1) break;
      const int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }
  void text(int, int, const char*, int, bool) override {}
  int textWidth(const char* s, int size) override { return base.textWidth(s, size); }
  int lineHeight(int size) override { return base.lineHeight(size); }
};

int cLayerDraw(lua_State* L) {  // layer:draw(fn(g))
  auto* b = static_cast<ImageBox*>(luaL_checkudata(L, 1, kImageMeta));
  luaL_checktype(L, 2, LUA_TFUNCTION);
  if (!b->img) return luaL_error(L, "layer released");
  auto* s = S(L);
  if (s->cv != s->baseCanvas) return luaL_error(L, "layer:draw cannot be nested");
  LayerCanvas lc(*s->baseCanvas, *b->img);
  s->cv = &lc;
  lua_pushvalue(L, 2);
  lua_getglobal(L, "g");
  const int rc = lua_pcall(L, 1, 0, 0);
  s->cv = s->baseCanvas;
  if (rc != LUA_OK) return lua_error(L);
  return 0;
}

int cLayersCreate(lua_State* L) {  // ctx.layers:create(w, h)
  const int w = argInt(L, 2), h = argInt(L, 3);
  if (w < 1 || h < 1 || w > 2048 || h > 2048) return fRetNilErr(L, "invalid_argument");
  ImageData img;
  img.w = w;
  img.h = h;
  img.gray.assign(static_cast<size_t>(w) * h, 255);
  const int r = pushImage(L, std::move(img));
  if (r == 1) static_cast<ImageBox*>(lua_touserdata(L, -1))->layer = true;
  return r;
}

int cImageDecode(lua_State* L) {  // ctx.image:decode(bytes [, opts])
  size_t n;
  const char* d = luaL_checklstring(L, 2, &n);
  if (n == 0 || n > 4 * 1024 * 1024) return fRetNilErr(L, "invalid_argument");
  ImageData img;
  std::string err;
  if (!S(L)->env->decodeImage(std::string(d, n), img, err)) return fRetNilErr(L, err.empty() ? "decode_failed" : err.c_str());
  if (img.w <= 0 || img.h <= 0 || img.gray.size() < static_cast<size_t>(img.w) * img.h) return fRetNilErr(L, "decode_failed");
  return pushImage(L, std::move(img));
}
int cQrEncode(lua_State* L) {  // ctx.qr:encode(text)
  size_t n;
  const char* d = luaL_checklstring(L, 2, &n);
  ImageData img;
  if (n == 0 || n > 2953 || !S(L)->env->qrEncode(std::string(d, n), img)) return fRetNilErr(L, "invalid_argument");
  return pushImage(L, std::move(img));
}
int cZlibDecompress(lua_State* L) {  // ctx.zlib:decompress(bytes [, max_bytes])
  size_t n;
  const char* d = luaL_checklstring(L, 2, &n);
  const size_t cap = static_cast<size_t>(std::clamp(optInt(L, 3, 1024 * 1024), 1, 4 * 1024 * 1024));
  std::string out;
  if (!inflateData(std::string(d, n), out, cap)) return fRetNilErr(L, "decompress_failed");
  lua_pushlstring(L, out.data(), out.size());
  return 1;
}
void pushReaderBook(lua_State* L, const ReaderBook& b) {
  lua_createtable(L, 0, 8);
  auto str = [L](const char* k, const std::string& v) {
    lua_pushlstring(L, v.data(), v.size());
    lua_setfield(L, -2, k);
  };
  str("id", b.id);
  str("title", b.title);
  str("author", b.author);
  str("format", b.format);
  if (b.percent >= 0) {
    lua_pushinteger(L, b.percent);
    lua_setfield(L, -2, "percent");
  }
  lua_pushinteger(L, static_cast<lua_Integer>(b.readSec));
  lua_setfield(L, -2, "read_sec");
  lua_pushboolean(L, b.finished);
  lua_setfield(L, -2, "finished");
}
int cReaderSummary(lua_State* L) {  // ctx.reader:summary() -> {total_sec, today_sec, streak_days, best_streak_days}
  ReaderSummary sm;
  if (!S(L)->env->readerSummary(sm)) return fRetNilErr(L, "unavailable");
  lua_createtable(L, 0, 4);
  lua_pushinteger(L, static_cast<lua_Integer>(sm.totalSec));
  lua_setfield(L, -2, "total_sec");
  lua_pushinteger(L, static_cast<lua_Integer>(sm.todaySec));
  lua_setfield(L, -2, "today_sec");
  lua_pushinteger(L, sm.streakDays);
  lua_setfield(L, -2, "streak_days");
  lua_pushinteger(L, sm.bestStreakDays);
  lua_setfield(L, -2, "best_streak_days");
  return 1;
}
int cReaderBooks(lua_State* L) {  // ctx.reader:books() / all_books() -> {{id, title, author, format, read_sec, finished}}
  std::vector<ReaderBook> v;
  if (!S(L)->env->readerBooks(v)) return fRetNilErr(L, "unavailable");
  lua_createtable(L, static_cast<int>(v.size()), 0);
  int i = 1;
  for (const auto& b : v) {
    pushReaderBook(L, b);
    lua_rawseti(L, -2, i++);
  }
  return 1;
}
int cReaderCurrent(lua_State* L) {  // ctx.reader:current() -> book | nil
  ReaderBook b;
  if (!S(L)->env->readerCurrent(b)) {
    lua_pushnil(L);
    return 1;
  }
  pushReaderBook(L, b);
  return 1;
}
int cReaderDays(lua_State* L) {  // ctx.reader:days(from, to) -> {{date = 'YYYY-MM-DD', sec}} ascending
  size_t fl = 0, tl = 0;
  const char* from = luaL_checklstring(L, 2, &fl);
  const char* to = luaL_checklstring(L, 3, &tl);
  std::vector<ReaderDay> v;
  if (!S(L)->env->readerDays(std::string(from, fl), std::string(to, tl), v)) {
    lua_newtable(L);
    return 1;
  }
  lua_createtable(L, static_cast<int>(v.size()), 0);
  int i = 1;
  for (const auto& d : v) {
    lua_createtable(L, 0, 2);
    lua_pushlstring(L, d.date.data(), d.date.size());
    lua_setfield(L, -2, "date");
    lua_pushinteger(L, static_cast<lua_Integer>(d.sec));
    lua_setfield(L, -2, "sec");
    lua_rawseti(L, -2, i++);
  }
  return 1;
}
int cReaderEmpty(lua_State* L) {  // book_days(): per-book per-day rows are not recorded here
  lua_newtable(L);
  return 1;
}
int cReaderNoCover(lua_State* L) { return fRetNilErr(L, "not_found"); }
int cHashSha1(lua_State* L) {
  size_t n;
  const char* d = luaL_checklstring(L, 2, &n);
  const std::string h = sha1(std::string(d, n));
  lua_pushlstring(L, h.data(), h.size());
  return 1;
}
int cHashSha256(lua_State* L) {
  size_t n;
  const char* d = luaL_checklstring(L, 2, &n);
  const std::string h = sha256(std::string(d, n));
  lua_pushlstring(L, h.data(), h.size());
  return 1;
}
int gImage(lua_State* L) {  // g:image(img, x, y [, {w=, h=}])
  ImageBox* b = checkImage(L, 2);
  if (!b) return 0;
  const int x = argInt(L, 3), y = argInt(L, 4);
  int w = b->img->w, h = b->img->h;
  if (lua_istable(L, 5)) {
    lua_getfield(L, 5, "w");
    if (lua_isnumber(L, -1)) w = static_cast<int>(lua_tonumber(L, -1));
    lua_pop(L, 1);
    lua_getfield(L, 5, "h");
    if (lua_isnumber(L, -1)) h = static_cast<int>(lua_tonumber(L, -1));
    lua_pop(L, 1);
  }
  blitImage(*S(L)->cv, *b->img, x, y, w, h);
  return 0;
}


// ---------------------------------------------------------------- network (ctx.net)

constexpr size_t kMaxNetJobs = 4;

int cNetCancel(lua_State* L) {  // job:cancel()
  auto* s = S(L);
  lua_getfield(L, 1, "id");
  const int id = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  for (size_t i = 0; i < s->netJobs.size(); ++i) {
    if (s->netJobs[i].id != id) continue;
    luaL_unref(L, LUA_REGISTRYINDEX, s->netJobs[i].fnRef);
    const bool out = s->netJobs[i].handedOut;
    s->netJobs.erase(s->netJobs.begin() + static_cast<long>(i));
    if (out) {
      s->netCancelled.push_back(id);
    } else {
      s->netQueue.erase(std::remove_if(s->netQueue.begin(), s->netQueue.end(),
                                       [id](const NetRequest& r) { return r.id == id; }),
                        s->netQueue.end());
    }
    break;
  }
  return 0;
}

bool validUrl(const std::string& u) {
  if (u.size() < 8 || u.size() > 2048) return false;
  if (u.compare(0, 7, "http://") != 0 && u.compare(0, 8, "https://") != 0) return false;
  for (const unsigned char c : u) {
    if (c <= 0x20 || c == 0x7F) return false;
  }
  return true;
}

int cNetRequest(lua_State* L) {  // ctx.net:request({url, method, body, headers, timeout_ms, save}, fn) -> job | nil, err
  auto* s = S(L);
  luaL_checktype(L, 2, LUA_TTABLE);
  luaL_checktype(L, 3, LUA_TFUNCTION);
  lua_getfield(L, 2, "url");
  if (!lua_isstring(L, -1)) return luaL_error(L, "bad extra argument (string expected)");
  NetRequest r;
  r.url = lua_tostring(L, -1);
  lua_pop(L, 1);
  if (!validUrl(r.url)) return fRetNilErr(L, "invalid_argument");
  lua_getfield(L, 2, "method");
  if (lua_isstring(L, -1)) {
    r.method = lua_tostring(L, -1);
    for (auto& c : r.method) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  }
  lua_pop(L, 1);
  if (r.method != "GET" && r.method != "POST" && r.method != "PUT" && r.method != "DELETE" && r.method != "PATCH" &&
      r.method != "HEAD") {
    return fRetNilErr(L, "invalid_argument");
  }
  lua_getfield(L, 2, "body");
  if (lua_isstring(L, -1)) {
    size_t n;
    const char* b = lua_tolstring(L, -1, &n);
    if (n > 256 * 1024) return fRetNilErr(L, "too_large");
    r.body.assign(b, n);
  }
  lua_pop(L, 1);
  lua_getfield(L, 2, "timeout_ms");
  if (lua_isnumber(L, -1)) r.timeoutMs = static_cast<uint32_t>(std::clamp(lua_tonumber(L, -1), 1000.0, 120000.0));
  lua_pop(L, 1);
  lua_getfield(L, 2, "headers");
  if (lua_istable(L, -1)) {
    lua_pushnil(L);
    while (lua_next(L, -2)) {
      if (lua_type(L, -2) == LUA_TSTRING && (lua_type(L, -1) == LUA_TSTRING || lua_type(L, -1) == LUA_TNUMBER)) {
        const std::string k = lua_tostring(L, -2);
        bool ok = !k.empty() && k.size() < 64;
        for (const char c : k) ok = ok && (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_');
        const std::string v = lua_tostring(L, -1);
        if (ok && v.find_first_of("\r\n") == std::string::npos && v.size() < 4096 && r.headers.size() < 24) {
          r.headers.emplace_back(k, v);
        }
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
  lua_getfield(L, 2, "save");
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "name");
    if (lua_isstring(L, -1) && safeFileName(lua_tostring(L, -1))) r.saveName = lua_tostring(L, -1);
    else if (lua_isstring(L, -1)) return fRetNilErr(L, "invalid_argument");
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  if (s->netJobs.size() >= kMaxNetJobs) return fRetNilErr(L, "busy");
  r.id = s->nextNetId++;
  lua_pushvalue(L, 3);
  const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  s->netJobs.push_back({r.id, ref, false});
  s->netQueue.push_back(std::move(r));
  lua_createtable(L, 0, 2);
  lua_pushinteger(L, s->netJobs.back().id);
  lua_setfield(L, -2, "id");
  setFn(L, "cancel", cNetCancel);
  return 1;
}

int cWsSend(lua_State* L) {  // ws:send(text) -> true | nil, err
  auto* s = S(L);
  luaL_checktype(L, 1, LUA_TTABLE);
  size_t n;
  const char* d = luaL_checklstring(L, 2, &n);
  lua_getfield(L, 1, "id");
  const int id = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  if (id != s->wsId || s->wsRef == LUA_NOREF) return fRetNilErr(L, "closed");
  if (n > 64 * 1024) return fRetNilErr(L, "too_large");
  if (s->wsOutQ.size() >= 32) return fRetNilErr(L, "busy");
  s->wsOutQ.push_back({id, false, std::string(d, n)});
  lua_pushboolean(L, 1);
  return 1;
}

int cWsClose(lua_State* L) {  // ws:close()
  auto* s = S(L);
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_getfield(L, 1, "id");
  const int id = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  if (id != s->wsId || s->wsRef == LUA_NOREF) return 0;
  s->wsOutQ.push_back({id, true, ""});
  return 0;
}

bool validWsUrl(const std::string& u) {
  if (u.size() < 6 || u.size() > 2048) return false;
  if (u.compare(0, 5, "ws://") != 0 && u.compare(0, 6, "wss://") != 0) return false;
  for (const unsigned char c : u) {
    if (c <= 0x20 || c == 0x7F) return false;
  }
  return true;
}

int cNetWebsocket(lua_State* L) {  // ctx.net:websocket({url, timeout_ms}, handlers) -> ws | nil, err
  auto* s = S(L);
  luaL_checktype(L, 2, LUA_TTABLE);
  luaL_checktype(L, 3, LUA_TTABLE);
  lua_getfield(L, 2, "url");
  if (!lua_isstring(L, -1)) return luaL_error(L, "bad extra argument (string expected)");
  App::WsOpen o;
  o.url = lua_tostring(L, -1);
  lua_pop(L, 1);
  if (!validWsUrl(o.url)) return fRetNilErr(L, "invalid_argument");
  lua_getfield(L, 2, "timeout_ms");
  if (lua_isnumber(L, -1)) o.timeoutMs = static_cast<uint32_t>(std::clamp(lua_tonumber(L, -1), 1000.0, 120000.0));
  lua_pop(L, 1);
  if (s->wsRef != LUA_NOREF) return fRetNilErr(L, "busy");
  o.id = s->nextWsId++;
  lua_pushvalue(L, 3);
  s->wsRef = luaL_ref(L, LUA_REGISTRYINDEX);
  s->wsId = o.id;
  s->wsHandedOut = false;
  s->wsOpenQ.push_back(o);
  lua_createtable(L, 0, 3);
  lua_pushinteger(L, o.id);
  lua_setfield(L, -2, "id");
  setFn(L, "send", cWsSend);
  setFn(L, "close", cWsClose);
  return 1;
}

int cNetKeepWifi(lua_State* L) {  // ctx.net:keep_wifi(ms)
  if (!lua_isnumber(L, 2)) return luaL_error(L, "bad argument #1 to 'keep_wifi' (number expected)");
  S(L)->app->keepWifiMs_ = static_cast<uint32_t>(std::clamp(lua_tonumber(L, 2), 0.0, 3600000.0));
  return 0;
}

// ---------------------------------------------------------------- hook

void budgetHook(lua_State* L, lua_Debug*) {
  auto* s = S(L);
  if (s->env->millis() - s->callStart > s->budgetMs) luaL_error(L, "script exceeded its time budget");
}

int msgHandler(lua_State* L) {
  const char* m = lua_tostring(L, 1);
  luaL_traceback(L, L, m ? m : "(error object)", 1);
  return 1;
}

const char kPrelude[] = R"LUA(
local ctx = ...
ctx.i18n = { locale = 'en', t = function(_, k) return k end }
ctx.perf = { info = function() return {} end }
local function open_text(self, name)
  local s = self:read(name)
  if not s then return nil, 'not_found' end
  local pos = 1
  local r = {}
  function r:read_line()
    if pos > #s then return nil end
    local e = s:find('\n', pos, true)
    local line
    if e then line = s:sub(pos, e - 1); pos = e + 1 else line = s:sub(pos); pos = #s + 1 end
    if line:sub(-1) == '\r' then line = line:sub(1, -2) end
    return line
  end
  function r:seek(p) pos = (p or 0) + 1 end
  function r:tell() return pos - 1 end
  function r:close() s = nil end
  return r
end
ctx.package.open_text = open_text
ctx.data.open_text = open_text
)LUA";

}  // namespace

// ------------------------------------------------------------------------------------------------ App

App::App(Env& env, Canvas& canvas, Limits limits) : impl_(new Impl) {
  impl_->app = this;
  impl_->env = &env;
  impl_->cv = &canvas;
  impl_->baseCanvas = &canvas;
  impl_->limits = limits;
  impl_->heap.limit = limits.memoryBytes;
  impl_->budgetMs = limits.callbackMs;
}

App::~App() {
  if (impl_->L) lua_close(impl_->L);
}

namespace {

lua_State* newSandbox(App::Impl* im) {
  lua_State* L = lua_newstate(luaAlloc, &im->heap);
  if (!L) return nullptr;
  *static_cast<App::Impl**>(lua_getextraspace(L)) = im;
  luaL_requiref(L, "_G", luaopen_base, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
  lua_pop(L, 1);
  // Sandbox: no file loading, no GC control from scripts (ctx.gc instead), no bytecode.
  for (const char* n : {"dofile", "loadfile", "collectgarbage"}) {
    lua_pushnil(L);
    lua_setglobal(L, n);
  }
  lua_getglobal(L, "string");
  lua_pushnil(L);
  lua_setfield(L, -2, "dump");
  lua_pop(L, 1);
  lua_pushcfunction(L, cPrint);
  lua_setglobal(L, "print");
  lua_pushcfunction(L, cRequire);
  lua_setglobal(L, "require");
  lua_newtable(L);
  lua_setfield(L, LUA_REGISTRYINDEX, "_XTLOADED");
  return L;
}

void buildGlobals(lua_State* L, App::Impl* im, const Manifest& m, const std::string& stateJson) {
  // ---- g
  lua_newtable(L);
  setFn(L, "clear", gClear);
  setFn(L, "rect", gRect);
  setFn(L, "round_rect", gRoundRect);
  setFn(L, "line", gLine);
  setFn(L, "circle", gCircle);
  setFn(L, "arc", gArc);
  setFn(L, "text", gText);
  setFn(L, "text_box", gTextBox);
  setFn(L, "size", gSize);
  setFn(L, "measure", gMeasure);
  setFn(L, "measure_text", gMeasure);
  setFn(L, "polygon", gPolygon);
  setFn(L, "polyline", gPolyline);
  setFn(L, "pattern_rect", gPattern);
  setFn(L, "fill_pattern", gPattern);
  setFn(L, "image", gImage);
  setFn(L, "layer", cNoop);
  im->gPtr = lua_topointer(L, -1);
  lua_setglobal(L, "g");

  // ---- ctx
  lua_newtable(L);
  im->ctxPtr = lua_topointer(L, -1);
  setFn(L, "invalidate", cInvalidate);
  setFn(L, "request_refresh", cInvalidate);
  setFn(L, "quit", cQuit);
  setFn(L, "set_tick", cSetTick);
  setFn(L, "set_tick_rate", cSetTickRate);
  setFn(L, "after", cAfter);
  setFn(L, "cancel", cCancel);
  setFn(L, "defer_full_refresh", cNoop);
  setFn(L, "save_state", cSave);
  setFn(L, "save", cSave);

  lua_createtable(L, 0, 2);
  lua_pushinteger(L, m.apiMajor >= 2 ? m.apiMajor : 2);
  lua_setfield(L, -2, "major");
  lua_pushinteger(L, m.apiMajor >= 2 ? m.apiMinor : 9);
  lua_setfield(L, -2, "minor");
  lua_setfield(L, -2, "api");

  lua_newtable(L);  // capabilities: what this host does NOT do is explicit
  for (const char* k : {"dialog", "ui_components", "display_full_refresh", "keyboard", "text_input", "image_decode", "qr", "net", "layers", "reader"}) {
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, k);
  }
  for (const char* k : {"image_transform", "pattern", "ttf", "frontlight", "ui", "ui_ttf_fonts",
                        "ui_image_transform"}) {
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, k);
  }
  lua_pushboolean(L, m.lockscreenEnabled ? 1 : 0);
  lua_setfield(L, -2, "lockscreen");
  lua_setfield(L, -2, "capabilities");

  const int sw = im->cv->width(), sh = im->cv->height();
  lua_newtable(L);
  lua_pushinteger(L, sw);
  lua_setfield(L, -2, "width");
  lua_pushinteger(L, sh);
  lua_setfield(L, -2, "height");
  lua_pushinteger(L, std::min(sw, sh));
  lua_setfield(L, -2, "short_side");
  lua_pushinteger(L, std::max(sw, sh));
  lua_setfield(L, -2, "long_side");
  lua_pushboolean(L, sh >= sw);
  lua_setfield(L, -2, "is_portrait");
  lua_pushboolean(L, sh < sw);
  lua_setfield(L, -2, "is_landscape");
  lua_pushstring(L, sh >= sw ? "portrait" : "landscape");
  lua_setfield(L, -2, "orientation");
  setFn(L, "set_orientation", cRetFalse);
  lua_setfield(L, -2, "screen");

  lua_newtable(L);  // fonts
  for (const auto& f : kFonts) {
    lua_newtable(L);
    lua_pushstring(L, f.name);
    lua_setfield(L, -2, "name");
    lua_pushinteger(L, f.size);
    lua_setfield(L, -2, "_sz");
    lua_pushinteger(L, im->cv->lineHeight(f.size));
    lua_setfield(L, -2, "line_height");
    lua_pushinteger(L, im->cv->lineHeight(f.size) * 4 / 5);
    lua_setfield(L, -2, "ascent");
    setFn(L, "measure", cFontMeasure);
    lua_setfield(L, -2, f.name);
  }
  setFn(L, "size", cFontsSize);
  setFn(L, "handle", cFontsHandle);
  lua_setfield(L, -2, "fonts");

  lua_newtable(L);  // clock
  setFn(L, "now", cClockNow);
  setFn(L, "millis", cMillis);
  lua_setfield(L, -2, "clock");

  lua_newtable(L);  // sys (plain functions, like the stock host)
  setFn(L, "battery", cBatteryNum);
  setFn(L, "charging", cCharging);
  setFn(L, "millis", cMillis);
  setFn(L, "uptime_ms", cMillis);
  setFn(L, "epoch_sec", cEpoch);
  setFn(L, "local_sec", cLocalSec);
  setFn(L, "clock", cClockNow);
  lua_setfield(L, -2, "sys");

  lua_newtable(L);  // device
  setFn(L, "battery", cDeviceBattery);
  setFn(L, "keep_awake", cNoop);
  lua_pushstring(L, "InkOptimum");
  lua_setfield(L, -2, "model");
  lua_pushstring(L, "inkoptimum-xtapp-host");
  lua_setfield(L, -2, "firmware");
  lua_setfield(L, -2, "device");

  lua_newtable(L);  // display
  setFn(L, "full_refresh", cFullRefresh);
  setFn(L, "defer_auto_full", cNoop);
  lua_setfield(L, -2, "display");

  lua_newtable(L);  // log
  for (int lvl = 0; lvl < 3; ++lvl) {
    lua_pushinteger(L, lvl);
    lua_pushcclosure(L, cLog, 1);
    lua_setfield(L, -2, lvl == 0 ? "info" : lvl == 1 ? "warn" : "error");
  }
  lua_setfield(L, -2, "log");

  lua_newtable(L);  // gc
  setFn(L, "collect", cGcCollect);
  setFn(L, "count", cGcCount);
  lua_setfield(L, -2, "gc");

  lua_newtable(L);
  setFn(L, "extend", cBudgetExtend);
  lua_setfield(L, -2, "budget");

  lua_newtable(L);  // dialog
  setFn(L, "toast", cToast);
  setFn(L, "confirm", cConfirm);
  lua_setfield(L, -2, "dialog");

  lua_newtable(L);  // system
  setFn(L, "can_open", cSysCanOpen);
  setFn(L, "open", cSysOpen);
  if (m.lockscreenEnabled && !im->lockMode) setFn(L, "set_as_lockscreen_app", cLockSetDefault);
  lua_setfield(L, -2, "system");

  if (m.lockscreenEnabled && !im->lockMode) {  // an app that declares a lockscreen block gets ctx.lockscreen
    lua_newtable(L);
    setFn(L, "set_default", cLockSetDefault);
    setFn(L, "is_default", cLockIsDefault);
    setFn(L, "clear_default", cLockClearDefault);
    setFn(L, "set_wallpaper", cLockSetDefault);
    setFn(L, "is_wallpaper", cLockIsDefault);
    setFn(L, "clear_wallpaper", cLockClearDefault);
    lua_setfield(L, -2, "lockscreen");
  }
  if (im->lockMode) {  // the lockscreen entry gets ctx.lock instead
    lua_newtable(L);
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "elapsed_sec");
    lua_pushinteger(L, static_cast<lua_Integer>(im->lockInterval));
    lua_setfield(L, -2, "interval_sec");
    lua_pushstring(L, "idle");
    lua_setfield(L, -2, "reason");
    lua_pushstring(L, "");
    lua_setfield(L, -2, "base_page");
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "clear_background");
    setFn(L, "set_interval", cLockSetInterval);
    setFn(L, "flush_once", cLockFlushOnce);
    lua_setfield(L, -2, "lock");
  }

  lua_newtable(L);  // input
  lua_createtable(L, 0, 2);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "keys");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "touch");
  lua_setfield(L, -2, "caps");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "keys");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "pointer");
  setFn(L, "text", cInputText);
  setFn(L, "edit", cInputEdit);
  lua_setfield(L, -2, "input");

  for (const char* tn : {"storage", "files"}) {
    lua_newtable(L);
    setFn(L, "read", cStRead);
    setFn(L, "write", cStWrite);
    setFn(L, "exists", cStExists);
    setFn(L, "size", cStSize);
    setFn(L, "remove", cStRemove);
    setFn(L, "list", cStList);
    setFn(L, "usage", cStUsage);
    lua_pushstring(L, ("/AppFiles/" + m.appId).c_str());
    lua_setfield(L, -2, "path");
    lua_setfield(L, -2, tn);
  }
  for (const char* tn : {"package", "data"}) {
    lua_newtable(L);
    setFn(L, "read", cPkgRead);
    setFn(L, "read_text", cPkgRead);
    setFn(L, "exists", cPkgExists);
    setFn(L, "size", cPkgSize);
    lua_setfield(L, -2, tn);
  }

  lua_newtable(L);
  setFn(L, "decode", cImageDecode);
  lua_setfield(L, -2, "image");
  lua_newtable(L);
  setFn(L, "encode", cQrEncode);
  lua_setfield(L, -2, "qr");
  lua_newtable(L);
  setFn(L, "decompress", cZlibDecompress);
  setFn(L, "inflate", cZlibDecompress);
  lua_setfield(L, -2, "zlib");
  lua_newtable(L);
  setFn(L, "sha1", cHashSha1);
  setFn(L, "sha256", cHashSha256);
  lua_setfield(L, -2, "hash");

  lua_newtable(L);
  setFn(L, "create", cLayersCreate);
  lua_setfield(L, -2, "layers");

  if (std::find(m.permissions.begin(), m.permissions.end(), "reader") != m.permissions.end()) {
    lua_newtable(L);
    setFn(L, "summary", cReaderSummary);
    setFn(L, "books", cReaderBooks);
    setFn(L, "all_books", cReaderBooks);
    setFn(L, "current", cReaderCurrent);
    setFn(L, "days", cReaderDays);
    setFn(L, "book_days", cReaderEmpty);
    setFn(L, "cover", cReaderNoCover);
    lua_setfield(L, -2, "reader");
  }

  lua_newtable(L);
  setFn(L, "request", cNetRequest);
  setFn(L, "keep_wifi", cNetKeepWifi);
  setFn(L, "websocket", cNetWebsocket);
  lua_setfield(L, -2, "net");

  lua_newtable(L);  // json
  setFn(L, "decode", cJsonDecode);
  setFn(L, "encode", cJsonEncode);
  lua_pushlightuserdata(L, &g_nullTag);
  lua_setfield(L, -2, "null");
  lua_setfield(L, -2, "json");
  lua_newtable(L);
  setFn(L, "encode", cB64Encode);
  setFn(L, "decode", cB64Decode);
  lua_setfield(L, -2, "base64");
  lua_newtable(L);
  setFn(L, "encode", cHexEncode);
  setFn(L, "decode", cHexDecode);
  lua_setfield(L, -2, "hex");
  lua_newtable(L);
  setFn(L, "encode", cUrlEncode);
  setFn(L, "decode", cUrlDecode);
  lua_setfield(L, -2, "url");
  lua_newtable(L);
  setFn(L, "uuid", cRandomUuid);
  setFn(L, "bytes", cRandomBytes);
  lua_setfield(L, -2, "random");

  // ctx.state: restored table, or empty
  JValue st;
  if (!stateJson.empty() && parseJson(stateJson, st) && st.type == JValue::Type::Obj) {
    pushJson(L, st);
  } else {
    lua_newtable(L);
  }
  lua_setfield(L, -2, "state");

  lua_pushvalue(L, -1);
  lua_setglobal(L, "ctx");

  // prelude (open_text, i18n, perf)
  if (luaL_loadstring(L, kPrelude) == LUA_OK) {
    lua_pushvalue(L, -2);
    lua_pcall(L, 1, 0, 0);
  } else {
    lua_pop(L, 1);
  }
  lua_pop(L, 1);  // ctx
  (void)kNullSentinel;
}

}  // namespace

bool App::start(const std::string& manifestJson, const Mode mode) {
  std::string err;
  if (!parseManifest(manifestJson, manifest_, &err)) {
    error_ = err;
    failed_ = true;
    return false;
  }
  if (mode == Mode::Lockscreen) {
    if (!manifest_.lockscreenEnabled) {
      error_ = "The app declares no lockscreen block";
      failed_ = true;
      return false;
    }
    manifest_.entry = manifest_.lockscreenEntry;
    impl_->lockMode = true;
    impl_->lockInterval = static_cast<uint32_t>(manifest_.lockscreenIntervalSec);
  }
  if (manifest_.encrypted) {
    error_ = "Official encrypted .xtapp package: its code is locked to the vendor firmware key and cannot run here.";
    failed_ = true;
    return false;
  }
  std::string src;
  if (!safePackageName(manifest_.entry) || !impl_->env->readPackage(manifest_.entry, src)) {
    error_ = "Entry script not found: " + manifest_.entry;
    failed_ = true;
    return false;
  }
  lua_State* L = newSandbox(impl_.get());
  if (!L) {
    error_ = "Out of memory creating the Lua state";
    failed_ = true;
    return false;
  }
  impl_->L = L;
  std::string state;
  impl_->env->loadState(state);
  buildGlobals(L, impl_.get(), manifest_, state);
  lua_sethook(L, budgetHook, LUA_MASKCOUNT, 20000);

  auto fail = [&](const char* what) {
    error_ = std::string(what) + ": " + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "(no message)");
    lua_pop(L, 1);
    failed_ = true;
    return false;
  };
  impl_->callStart = impl_->env->millis();
  if (luaL_loadbufferx(L, src.data(), src.size(), ("@" + manifest_.entry).c_str(), "t") != LUA_OK) return fail("syntax");
  lua_pushcfunction(L, msgHandler);
  lua_insert(L, -2);
  if (lua_pcall(L, 0, 0, -2) != LUA_OK) {
    lua_remove(L, -2);
    return fail("load");
  }
  lua_pop(L, 1);

  for (const char* cb : {"on_load", "on_enter"}) {
    lua_getglobal(L, cb);
    if (!lua_isfunction(L, -1)) {
      lua_pop(L, 1);
      continue;
    }
    lua_pushcfunction(L, msgHandler);
    lua_insert(L, -2);
    lua_getglobal(L, "ctx");
    impl_->callStart = impl_->env->millis();
    if (lua_pcall(L, 1, 0, -3) != LUA_OK) {
      lua_remove(L, -2);
      return fail(cb);
    }
    lua_pop(L, 1);
    break;
  }
  lua_getglobal(L, "on_tick");
  hasTick_ = lua_isfunction(L, -1);
  lua_pop(L, 1);
  if (hasTick_ && tickMs_ == 0) tickMs_ = 1000;
  redraw_ = true;
  impl_->lastTick = impl_->env->millis();
  return true;
}

namespace {
// Calls a global callback. Returns 1 = called OK, 0 = not defined, -1 = error (recorded). Pushes `nret` results.
int callGlobal(App* app, App::Impl* im, const char* name, int nExtra, const std::function<void(lua_State*)>& pushExtra,
               int nret, std::string& err) {
  lua_State* L = im->L;
  lua_getglobal(L, name);
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_pushcfunction(L, msgHandler);
  lua_insert(L, -2);
  lua_getglobal(L, "ctx");
  if (pushExtra) pushExtra(L);
  im->callStart = im->env->millis();
  im->budgetMs = im->limits.callbackMs;
  if (lua_pcall(L, 1 + nExtra, nret, -(2 + nExtra + 1)) != LUA_OK) {
    err = std::string(name) + ": " + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "(error)");
    lua_pop(L, 2);
    (void)app;
    return -1;
  }
  lua_remove(L, -(nret + 1));  // message handler
  return 1;
}
}  // namespace

void App::draw() {
  if (failed_ || !impl_->L) return;
  lua_State* L = impl_->L;
  std::string err;
  const int r = callGlobal(
      this, impl_.get(), "on_draw", 1, [](lua_State* LL) { lua_getglobal(LL, "g"); }, 0, err);
  if (r < 0) {
    error_ = err;
    failed_ = true;
  }
  (void)L;
}

bool App::input(const Event& ev) {
  if (failed_ || !impl_->L) return false;
  lua_State* L = impl_->L;
  std::string err;
  const int r = callGlobal(
      this, impl_.get(), "on_input", 1,
      [&](lua_State* LL) {
        lua_createtable(LL, 0, 10);
        auto setS = [&](const char* k, const std::string& v) {
          if (v.empty()) return;
          lua_pushstring(LL, v.c_str());
          lua_setfield(LL, -2, k);
        };
        auto setI = [&](const char* k, lua_Integer v) {
          lua_pushinteger(LL, v);
          lua_setfield(LL, -2, k);
        };
        if (ev.type == Event::Type::Key) {
          setS("type", "key");
          setS("key", ev.key);
          setS("phase", ev.phase);
          setS("state", ev.phase);
        } else if (manifest_.apiMajor >= 2) {
          setS("type", "pointer");
          setS("phase", ev.phase);
          setS("gesture", ev.gesture);
          setI("x", ev.x);
          setI("y", ev.y);
          setI("down_x", ev.downX);
          setI("down_y", ev.downY);
        } else {  // API 0.8: one touch event per finished gesture
          setS("type", "touch");
          setS("gesture", ev.gesture);
          setI("x", ev.x);
          setI("y", ev.y);
        }
        setI("time_ms", ev.timeMs);
      },
      1, err);
  if (r < 0) {
    error_ = err;
    failed_ = true;
    return false;
  }
  if (r == 0) return false;
  const bool claimed = lua_toboolean(L, -1);
  lua_pop(L, 1);
  return claimed;
}

void App::tick() {
  if (failed_ || !impl_->L) return;
  lua_State* L = impl_->L;
  const uint32_t now = impl_->env->millis();
  // timers
  for (size_t i = 0; i < impl_->timers.size();) {
    if (static_cast<int32_t>(now - impl_->timers[i].due) >= 0) {
      const int ref = impl_->timers[i].ref;
      impl_->timers.erase(impl_->timers.begin() + static_cast<long>(i));
      lua_pushcfunction(L, msgHandler);
      lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
      luaL_unref(L, LUA_REGISTRYINDEX, ref);
      lua_getglobal(L, "ctx");
      impl_->callStart = now;
      impl_->budgetMs = impl_->limits.callbackMs;
      if (lua_pcall(L, 1, 0, -3) != LUA_OK) {
        error_ = std::string("timer: ") + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "(error)");
        failed_ = true;
        lua_pop(L, 2);
        return;
      }
      lua_pop(L, 1);
      i = 0;  // list may have changed
    } else {
      ++i;
    }
  }
  if (hasTick_ && tickMs_ && now - impl_->lastTick >= tickMs_) {
    impl_->lastTick = now;
    std::string err;
    const uint32_t ms = tickMs_;
    const int r = callGlobal(
        this, impl_.get(), "on_tick", 1, [ms](lua_State* LL) { lua_pushinteger(LL, ms); }, 0, err);
    if (r < 0) {
      error_ = err;
      failed_ = true;
    }
  }
}

void App::leave() {
  if (!impl_->L) return;
  if (!failed_) {
    std::string err;
    if (callGlobal(this, impl_.get(), "on_leave", 0, nullptr, 0, err) < 0) {
      error_ = err;
      failed_ = true;
    }
  }
  saveStateNow(impl_->L);
}

bool App::consumeRedraw(bool* full) {
  const bool r = redraw_;
  if (full) *full = full_;
  redraw_ = false;
  full_ = false;
  return r;
}

bool App::takeToast(std::string& out) {
  if (!impl_->hasToast) return false;
  out = impl_->toast;
  impl_->hasToast = false;
  return true;
}

bool App::pendingConfirm(Confirm& out) const {
  if (!impl_->confirmPending) return false;
  out = impl_->confirm;
  return true;
}

uint32_t App::lockIntervalSec() const { return impl_->lockInterval; }
bool App::takeLockFlush() {
  const bool f = impl_->lockFlush;
  impl_->lockFlush = false;
  return f;
}
bool App::takeOpenRequest(std::string& appId) {
  if (impl_->openRequest.empty()) return false;
  appId = std::move(impl_->openRequest);
  impl_->openRequest.clear();
  return true;
}

void App::answerConfirm(bool yes) {
  if (impl_->confirmSetsLockscreen) {
    impl_->confirmSetsLockscreen = false;
    impl_->confirmPending = false;
    if (yes) impl_->env->setLockscreenApp(manifest_.appId);
    redraw_ = true;
    return;
  }
  if (!impl_->confirmPending || !impl_->L || failed_) {
    impl_->confirmPending = false;
    return;
  }
  lua_State* L = impl_->L;
  impl_->confirmPending = false;
  const int ref = impl_->confirmRef;
  impl_->confirmRef = LUA_NOREF;
  if (ref == LUA_NOREF) return;
  lua_pushcfunction(L, msgHandler);
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  luaL_unref(L, LUA_REGISTRYINDEX, ref);
  lua_getglobal(L, "ctx");
  lua_pushboolean(L, yes);
  impl_->callStart = impl_->env->millis();
  impl_->budgetMs = impl_->limits.callbackMs;
  if (lua_pcall(L, 2, 0, -4) != LUA_OK) {
    error_ = std::string("confirm: ") + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "(error)");
    failed_ = true;
    lua_pop(L, 2);
    return;
  }
  lua_pop(L, 1);
  redraw_ = true;
}


bool App::takeTextRequest(TextRequest& out) {
  if (!impl_->textPending) return false;
  impl_->textPending = false;
  impl_->textInFlight = true;
  out = impl_->textReq;
  return true;
}

void App::answerText(bool ok, const std::string& text) {
  if (!impl_->textInFlight || !impl_->L || failed_) {
    impl_->textInFlight = false;
    return;
  }
  lua_State* L = impl_->L;
  impl_->textInFlight = false;
  const int fnRef = impl_->textFnRef;
  const int changeRef = impl_->textChangeRef;
  impl_->textFnRef = impl_->textChangeRef = LUA_NOREF;
  auto call = [&](int ref, const char* what, int extra) {
    lua_pushcfunction(L, msgHandler);
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_getglobal(L, "ctx");
    lua_pushstring(L, text.c_str());
    if (extra) lua_pushstring(L, "enter");
    impl_->callStart = impl_->env->millis();
    impl_->budgetMs = impl_->limits.callbackMs;
    if (lua_pcall(L, 2 + extra, 0, -(4 + extra)) != LUA_OK) {
      error_ = std::string(what) + ": " + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "(error)");
      failed_ = true;
      lua_pop(L, 2);
      return;
    }
    lua_pop(L, 1);
  };
  if (ok && !impl_->textIsEdit && fnRef != LUA_NOREF) call(fnRef, "input:text", 0);
  if (impl_->textIsEdit && ok) {
    if (changeRef != LUA_NOREF && !failed_) call(changeRef, "input:edit on_change", 0);
    if (fnRef != LUA_NOREF && !failed_) call(fnRef, "input:edit on_end", 1);
  }
  if (fnRef != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, fnRef);
  if (changeRef != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, changeRef);
  redraw_ = true;
}


bool App::takeNetRequest(NetRequest& out) {
  if (impl_->netQueue.empty()) return false;
  out = std::move(impl_->netQueue.front());
  impl_->netQueue.erase(impl_->netQueue.begin());
  for (auto& j : impl_->netJobs) {
    if (j.id == out.id) j.handedOut = true;
  }
  return true;
}

bool App::takeNetCancel(int& id) {
  if (impl_->netCancelled.empty()) return false;
  id = impl_->netCancelled.back();
  impl_->netCancelled.pop_back();
  return true;
}

bool App::takeWsOpen(WsOpen& out) {
  if (impl_->wsOpenQ.empty()) return false;
  out = impl_->wsOpenQ.front();
  impl_->wsOpenQ.erase(impl_->wsOpenQ.begin());
  impl_->wsHandedOut = true;
  return true;
}

bool App::takeWsOut(WsOut& out) {
  if (impl_->wsOutQ.empty()) return false;
  out = std::move(impl_->wsOutQ.front());
  impl_->wsOutQ.erase(impl_->wsOutQ.begin());
  return true;
}

void App::wsEvent(int id, const std::string& type, const std::string& data) {
  if (failed_ || !impl_->L || id != impl_->wsId || impl_->wsRef == LUA_NOREF) return;
  lua_State* L = impl_->L;
  const bool last = type == "close" || type == "error";
  const int ref = impl_->wsRef;
  if (last) {  // the socket is gone after this event: free the slot first so the handler may open a new one
    impl_->wsRef = LUA_NOREF;
    impl_->wsId = 0;
    impl_->wsOutQ.clear();
  }
  lua_pushcfunction(L, msgHandler);
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  lua_getfield(L, -1, ("on_" + type).c_str());
  lua_remove(L, -2);
  if (last) luaL_unref(L, LUA_REGISTRYINDEX, ref);
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    return;
  }
  lua_getglobal(L, "ctx");
  lua_pushlstring(L, data.data(), data.size());
  impl_->callStart = impl_->env->millis();
  impl_->budgetMs = impl_->limits.callbackMs;
  if (lua_pcall(L, 2, 0, -4) != LUA_OK) {
    error_ = std::string("websocket callback: ") + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "(error)");
    failed_ = true;
    lua_pop(L, 2);
    return;
  }
  lua_pop(L, 1);
  redraw_ = true;
}

void App::netDone(int id, NetResponse&& resp) {
  if (failed_ || !impl_->L) return;
  lua_State* L = impl_->L;
  int ref = LUA_NOREF;
  for (size_t i = 0; i < impl_->netJobs.size(); ++i) {
    if (impl_->netJobs[i].id == id) {
      ref = impl_->netJobs[i].fnRef;
      impl_->netJobs.erase(impl_->netJobs.begin() + static_cast<long>(i));
      break;
    }
  }
  if (ref == LUA_NOREF) return;  // cancelled meanwhile
  lua_pushcfunction(L, msgHandler);
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  luaL_unref(L, LUA_REGISTRYINDEX, ref);
  lua_getglobal(L, "ctx");
  lua_createtable(L, 0, 6);
  lua_pushinteger(L, resp.status);
  lua_setfield(L, -2, "status");
  lua_pushboolean(L, resp.status >= 200 && resp.status < 300);
  lua_setfield(L, -2, "ok");
  if (!resp.saved) {
    lua_pushlstring(L, resp.body.data(), resp.body.size());
    lua_setfield(L, -2, "body");
  } else {
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "saved");
  }
  lua_createtable(L, 0, static_cast<int>(resp.headers.size()));
  for (const auto& h : resp.headers) {
    lua_pushlstring(L, h.second.data(), h.second.size());
    lua_setfield(L, -2, h.first.c_str());
  }
  lua_setfield(L, -2, "headers");
  if (!resp.error.empty()) {
    lua_pushstring(L, resp.error.c_str());
    lua_setfield(L, -2, "error");
  }
  impl_->callStart = impl_->env->millis();
  impl_->budgetMs = impl_->limits.callbackMs;
  if (lua_pcall(L, 2, 0, -4) != LUA_OK) {
    error_ = std::string("net callback: ") + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "(error)");
    failed_ = true;
    lua_pop(L, 2);
    return;
  }
  lua_pop(L, 1);
  redraw_ = true;
}

}  // namespace xtapp
