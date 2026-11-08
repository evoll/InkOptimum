#include "PluginEvents.h"

#include <ArduinoJson.h>

#include <cstring>

namespace pluginevents {
namespace {

const char* const KNOWN[] = {"reader.open", "reader.exit", "reader.session", "book.downloaded", "sleep.enter"};

}  // namespace

bool isKnownEvent(const std::string& name) {
  for (const char* k : KNOWN) {
    if (name == k) return true;
  }
  return false;
}

std::vector<Handler> parseHandlers(const std::string& deviceJson) {
  std::vector<Handler> out;
  JsonDocument doc;
  if (deserializeJson(doc, deviceJson) != DeserializationError::Ok) return out;
  JsonObjectConst events = doc["events"].as<JsonObjectConst>();
  for (JsonPairConst kv : events) {
    Handler h;
    h.event = kv.key().c_str();
    if (!isKnownEvent(h.event)) continue;
    JsonVariantConst node = kv.value();
    JsonVariantConst req = node["request"];
    JsonVariantConst dl = node["download"];
    JsonVariantConst src = req.is<JsonObjectConst>() ? req : dl;
    if (!src.is<JsonObjectConst>()) continue;
    h.isDownload = !req.is<JsonObjectConst>();
    h.request.url = src["url"] | "";
    h.request.method = src["method"] | (h.isDownload ? "GET" : "POST");
    h.request.body = src["body"] | "";
    JsonVariantConst headers = src["headers"];
    if (headers.is<JsonObjectConst>()) {
      for (JsonPairConst hv : headers.as<JsonObjectConst>()) {
        if (hv.value().is<const char*>()) h.request.headers.emplace_back(hv.key().c_str(), hv.value().as<const char*>());
      }
    }
    h.dest = src["dest"] | "";
    h.toast = node["toast"] | "";
    h.connect = node["connect"] | false;
    // A wrong-typed filter drops the handler: it must not silently turn the filter off.
    if (!node["require_meta"].isNull()) {
      if (!node["require_meta"].is<const char*>() || std::string(node["require_meta"].as<const char*>()).empty()) continue;
      h.requireMeta = node["require_meta"].as<const char*>();
    }
    if (h.request.url.empty() || (h.isDownload && h.dest.empty())) continue;
    out.push_back(std::move(h));
  }
  return out;
}

bool sidecarHasKey(const std::string& sidecarJson, const std::string& key) {
  JsonDocument doc;
  if (key.empty() || deserializeJson(doc, sidecarJson) != DeserializationError::Ok || !doc.is<JsonObject>()) return false;
  JsonVariantConst v = doc[key];
  if (v.is<const char*>()) return v.as<const char*>()[0] != '\0';
  if (v.is<long long>()) return true;  // stored as an integer; decimals and booleans do not match
  return false;
}

std::string encodeLine(const Event& event) {
  JsonDocument doc;
  doc["id"] = event.id;
  doc["e"] = event.name;
  doc["ts"] = event.ts;
  JsonObject vars = doc["vars"].to<JsonObject>();
  for (const auto& v : event.vars) vars[v.first] = v.second;
  std::string line;
  serializeJson(doc, line);
  return line.size() > MAX_LINE ? std::string() : line;
}

bool decodeLine(const std::string& line, Event& event) {
  JsonDocument doc;
  if (deserializeJson(doc, line) != DeserializationError::Ok || !doc["e"].is<const char*>() ||
      !doc["id"].is<const char*>()) {
    return false;
  }
  event = Event{};
  event.id = doc["id"].as<const char*>();
  event.name = doc["e"].as<const char*>();
  event.ts = doc["ts"] | 0L;
  for (JsonPairConst kv : doc["vars"].as<JsonObjectConst>()) {
    event.vars.emplace_back(kv.key().c_str(), kv.value().is<const char*>() ? kv.value().as<const char*>() : "");
  }
  return true;
}

void appendToOutbox(std::string& outbox, const std::string& line) {
  if (line.empty()) return;
  if (outbox.size() + line.size() + 1 > MAX_OUTBOX) outbox.clear();
  outbox += line;
  outbox += '\n';
}

plugincat::Vars eventVars(const Event& event, const plugincat::Vars& extra) {
  plugincat::Vars vars = extra;
  for (const auto& v : event.vars) vars.emplace_back("event." + v.first, v.second);
  vars.emplace_back("event.ts", std::to_string(event.ts));
  vars.emplace_back("event.id", event.id);
  return vars;
}

int drain(std::string& outbox, const std::vector<Handler>& handlers, const Deliver& deliver, int maxHandlers) {
  std::vector<std::string> lines;
  size_t pos = 0;
  while (pos < outbox.size()) {
    size_t nl = outbox.find('\n', pos);
    if (nl == std::string::npos) nl = outbox.size();
    if (nl > pos) lines.push_back(outbox.substr(pos, nl - pos));
    pos = nl + 1;
  }

  int ran = 0;
  size_t done = 0;
  for (; done < lines.size(); done++) {
    Event event;
    const Handler* handler = nullptr;
    if (decodeLine(lines[done], event)) {
      for (const auto& h : handlers) {
        if (h.event == event.name) {
          handler = &h;
          break;
        }
      }
    }
    if (!handler) continue;  // corrupt or orphaned: counts as delivered
    if (ran >= maxHandlers) break;
    ran++;
    if (deliver(*handler, event) != Delivery::Delivered) break;
  }

  std::string rest;
  for (size_t i = done; i < lines.size(); i++) {
    rest += lines[i];
    rest += '\n';
  }
  outbox = rest;
  return ran;
}

}  // namespace pluginevents
