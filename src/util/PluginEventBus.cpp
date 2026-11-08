#include "PluginEventBus.h"

#include <ArduinoJson.h>
#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PluginEvents.h>
#include <PluginItems.h>
#include <PluginRequest.h>
#include <ProtectedPaths.h>
#include <WiFi.h>
#include <esp_random.h>

#include <ctime>
#include <vector>

#include "InkMODSettings.h"
#include "WifiCredentialStore.h"
#include "util/BookCacheUtils.h"
#include "util/PluginHttp.h"
#include "util/PluginLocations.h"

namespace {

using pluginevents::Delivery;

struct Subscription {
  std::string name;
  std::string dir;
  std::string outbox;  // events.jsonl
  std::vector<pluginevents::Handler> handlers;
  plugincat::DeviceManifest manifest;
};

std::vector<Subscription> subscriptions;
uint32_t eventCounter = 0;
uint32_t bootNonce = 0;
bool draining = false;
PluginEventBus::ToastSink toastSink = nullptr;

constexpr size_t SMALL_FILE = 2048;

bool readSmall(const std::string& path, std::string& out) {
  if (path.empty() || !Storage.exists(path.c_str())) return false;
  const String s = Storage.readFile(path.c_str());
  if (s.length() > 8192) return false;
  out.assign(s.c_str(), s.length());
  return true;
}

// Does `sub` want this event? A handler with require_meta wants it only for a book whose sidecar has the key.
bool wantsEvent(const Subscription& sub, const pluginevents::Event& event) {
  for (const auto& h : sub.handlers) {
    if (h.event != event.name) continue;
    if (h.requireMeta.empty()) return true;
    std::string book;
    for (const auto& v : event.vars) {
      if (v.first == "book" || v.first == "path") book = v.second;
    }
    std::string text;
    if (book.empty() || !readSmall(book + ".meta.json", text) || text.size() > SMALL_FILE) return false;
    return pluginevents::sidecarHasKey(text, h.requireMeta);
  }
  return false;
}

bool isBookFile(const std::string& path) {
  const size_t dot = path.rfind('.');
  if (dot == std::string::npos) return false;
  std::string ext = path.substr(dot);
  for (auto& c : ext) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return ext == ".epub" || ext == ".txt" || ext == ".md" || ext == ".xtc" || ext == ".fb2" || ext == ".fb3";
}

plugincat::Vars secretVars(const Subscription& sub) {
  plugincat::Vars vars;
  std::string text;
  JsonDocument doc;
  if (readSmall(sub.manifest.tokenFile, text) && deserializeJson(doc, text) == DeserializationError::Ok) {
    vars.emplace_back("token", plugincat::variantToString(plugincat::resolvePath(doc.as<JsonVariantConst>(),
                                                                                   sub.manifest.tokenPath)));
  } else {
    vars.emplace_back("token", "");
  }
  JsonDocument cfg;
  if (readSmall(sub.manifest.configFile, text) && deserializeJson(cfg, text) == DeserializationError::Ok &&
      cfg.is<JsonObject>()) {
    for (JsonPairConst kv : cfg.as<JsonObjectConst>()) {
      vars.emplace_back(std::string("cfg.") + kv.key().c_str(), plugincat::variantToString(kv.value()));
    }
  }
  vars.emplace_back("firmware_family", plugincat::identity::FAMILY);
  vars.emplace_back("plugin_api", std::to_string(plugincat::identity::PLUGIN_API));
  return vars;
}

// {meta.KEY}: the sidecar of the event's book, flat values only.
void addMeta(const pluginevents::Event& event, plugincat::Vars& item) {
  std::string book;
  for (const auto& v : event.vars) {
    if (v.first == "book" || v.first == "path") book = v.second;
  }
  std::string text;
  if (book.empty() || !readSmall(book + ".meta.json", text) || text.size() > SMALL_FILE) return;
  JsonDocument doc;
  if (deserializeJson(doc, text) != DeserializationError::Ok || !doc.is<JsonObject>()) return;
  for (JsonPairConst kv : doc.as<JsonObjectConst>()) {
    if (kv.value().isNull() || kv.value().is<JsonObjectConst>() || kv.value().is<JsonArrayConst>()) continue;
    item.emplace_back(std::string("meta.") + kv.key().c_str(), plugincat::variantToString(kv.value()));
  }
}

Delivery deliverOne(PluginHttp& http, const Subscription& sub, const pluginevents::Handler& handler,
                    const pluginevents::Event& event) {
  const plugincat::Vars raw = secretVars(sub);
  plugincat::Vars item = pluginevents::eventVars(event, {});
  addMeta(event, item);
  const plugincat::Request req = plugincat::substituteRequest(handler.request, raw, item);

  if (!handler.isDownload) {
    std::string ignored;
    const auto r = http.fetchString(req, ignored, pluginevents::MAX_ACK_BODY);
    // Any 2xx is an acknowledgement; a body past the cap is discarded, not an error.
    if (r.status < 200 || r.status >= 300) return Delivery::Failed;
    if (!handler.toast.empty() && toastSink) {
      // The toast may use the same variables as the request; it is shown only after the request succeeded.
      plugincat::Vars all = raw;
      all.insert(all.end(), item.begin(), item.end());
      const std::string text = plugincat::substitute(handler.toast, all);
      if (!text.empty()) toastSink(text.substr(0, 120).c_str());
    }
    return Delivery::Delivered;
  }

  plugincat::Vars all = raw;
  all.insert(all.end(), item.begin(), item.end());
  const std::string dest = plugincat::substitute(handler.dest, all);
  if (!protectedpaths::isPluginPath(dest) || dest.back() == '/') return Delivery::Delivered;  // unusable: drop
  const std::string part = dest + ".part";
  for (size_t i = 1; i < dest.size(); i++) {
    if (dest[i] == '/') Storage.mkdir(dest.substr(0, i).c_str(), true);
  }
  const auto r = http.fetchToFile(req, part, nullptr, nullptr, pluginevents::MAX_DOWNLOAD);
  if (!r.ok()) return Delivery::Failed;
  Storage.remove(dest.c_str());
  if (!Storage.rename(part.c_str(), dest.c_str())) {
    Storage.remove(part.c_str());
    return Delivery::Failed;
  }
  if (isBookFile(dest)) clearBookCache(dest);
  return Delivery::Delivered;
}

}  // namespace

namespace PluginEventBus {

void init() {
  subscriptions.clear();
  bootNonce = esp_random();
  if (!SETTINGS.pluginsEnabled) return;
  for (const auto& entry : PluginLocations::scanPlugins()) {
    if (!entry.hasDevice) continue;
    if (entry.name.size() > pluginevents::MAX_PLUGIN_NAME || entry.dir.size() > pluginevents::MAX_PLUGIN_PATH) {
      LOG_INF("PEV", "Plugin %s not subscribed: name or path too long", entry.name.c_str());
      continue;
    }
    if (static_cast<int>(subscriptions.size()) >= pluginevents::MAX_SUBSCRIBED) break;
    std::string json;
    if (!readSmall(entry.dir + "/device.json", json)) continue;
    Subscription sub;
    sub.name = entry.name;
    sub.dir = entry.dir;
    sub.outbox = entry.dir + "/events.jsonl";
    sub.handlers = pluginevents::parseHandlers(json);
    if (sub.handlers.empty() || !plugincat::parseDeviceManifest(json, entry.dir, sub.manifest)) continue;
    subscriptions.push_back(std::move(sub));
  }
  LOG_INF("PEV", "%u plugin(s) subscribed to events", static_cast<unsigned>(subscriptions.size()));
}

void emit(const char* name, const plugincat::Vars& vars) {
  if (subscriptions.empty() || !SETTINGS.pluginsEnabled || !pluginevents::isKnownEvent(name)) return;
  pluginevents::Event event;
  char id[24];
  snprintf(id, sizeof(id), "%08lx-%lu", static_cast<unsigned long>(bootNonce), static_cast<unsigned long>(++eventCounter));
  event.id = id;
  event.name = name;
  const time_t now = time(nullptr);
  event.ts = now > 1600000000 ? static_cast<long>(now) : 0;
  event.vars = vars;
  const std::string line = pluginevents::encodeLine(event);
  if (line.empty()) return;

  for (const auto& sub : subscriptions) {
    if (!wantsEvent(sub, event)) continue;
    std::string box;
    if (Storage.exists(sub.outbox.c_str())) {
      const String s = Storage.readFile(sub.outbox.c_str());
      box.assign(s.c_str(), s.length());
    }
    pluginevents::appendToOutbox(box, line);
    Storage.writeFile(sub.outbox.c_str(), String(box.c_str()));
  }
}

void setToastSink(ToastSink sink) { toastSink = sink; }

void drainIfOnline() {
  if (subscriptions.empty() || draining || !SETTINGS.pluginsEnabled) return;
  if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) return;
  draining = true;
  std::unique_ptr<PluginHttp> http;
  for (auto& sub : subscriptions) {
    if (!Storage.exists(sub.outbox.c_str())) continue;
    const String s = Storage.readFile(sub.outbox.c_str());
    std::string box(s.c_str(), s.length());
    if (box.empty()) continue;
    if (!http) http = std::make_unique<PluginHttp>();
    const std::string before = box;
    pluginevents::drain(box, sub.handlers, [&](const pluginevents::Handler& h, const pluginevents::Event& e) {
      return deliverOne(*http, sub, h, e);
    });
    if (box == before) continue;
    if (box.empty()) {
      Storage.remove(sub.outbox.c_str());
    } else {
      Storage.writeFile(sub.outbox.c_str(), String(box.c_str()));
    }
  }
  draining = false;
}

namespace {
// True when some outbox holds an event whose handler asked for `connect`.
bool connectWanted() {
  for (const auto& sub : subscriptions) {
    if (!Storage.exists(sub.outbox.c_str())) continue;
    const String s = Storage.readFile(sub.outbox.c_str());
    const std::string box(s.c_str(), s.length());
    size_t pos = 0;
    while (pos < box.size()) {
      size_t nl = box.find('\n', pos);
      if (nl == std::string::npos) nl = box.size();
      pluginevents::Event event;
      if (pluginevents::decodeLine(box.substr(pos, nl - pos), event)) {
        for (const auto& h : sub.handlers) {
          if (h.connect && h.event == event.name) return true;
        }
      }
      pos = nl + 1;
    }
  }
  return false;
}
}  // namespace

void drainAtSleep(uint16_t batteryPercent, const char* connectingText) {
  if (subscriptions.empty() || draining || !SETTINGS.pluginsEnabled) return;
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    drainIfOnline();
    return;
  }
  if (batteryPercent < 20 || !connectWanted()) return;

  WIFI_STORE.loadFromFile();
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  const auto* cred = lastSsid.empty() ? nullptr : WIFI_STORE.findCredential(lastSsid);
  if (!cred) return;
  const std::string ssid = cred->ssid;
  const std::string password = cred->password;

  if (toastSink && connectingText) toastSink(connectingText);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  const uint32_t started = millis();
  while (millis() - started < 10000) {  // sleep must never be held up longer than this
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) break;
    delay(100);
  }
  const bool online = WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
  if (online) drainIfOnline();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

}  // namespace PluginEventBus
