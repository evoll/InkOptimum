#include "ReadingSessions.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace readingsessions {

bool isDocumentId(const std::string& doc) {
  return doc.size() == 32 && doc.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
}

namespace {
// Longest prefix of at most `maxBytes` bytes that does not cut a UTF-8 sequence.
std::string truncateUtf8(const std::string& s, const size_t maxBytes) {
  if (s.size() <= maxBytes) return s;
  size_t n = maxBytes;
  while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
  return s.substr(0, n);
}
}  // namespace

std::string encodeLine(const Session& session) {
  if (!isDocumentId(session.doc)) return "";
  JsonDocument doc;
  doc["doc"] = session.doc;
  doc["start"] = session.start;
  doc["secs"] = session.secs;
  doc["tz"] = session.tz;
  doc["from"] = session.from;
  doc["to"] = session.to;
  doc["title"] = truncateUtf8(session.title, MAX_TITLE_BYTES);
  doc["path"] = session.path;
  std::string line;
  serializeJson(doc, line);
  if (line.size() > MAX_LINE) {
    // upload skips lines longer than its buffer, so a very long path is left out.
    doc.remove("path");
    line.clear();
    serializeJson(doc, line);
  }
  return line.size() > MAX_LINE ? "" : line;
}

bool decodeLine(const std::string& line, Session& out) {
  JsonDocument doc;
  if (deserializeJson(doc, line) != DeserializationError::Ok || !doc.is<JsonObject>()) return false;
  out = Session();
  out.doc = doc["doc"] | "";
  out.start = doc["start"] | static_cast<int64_t>(0);
  out.secs = doc["secs"] | static_cast<uint32_t>(0);
  out.tz = std::clamp(doc["tz"] | 0, -720, 840);
  out.from = doc["from"] | 0;
  out.to = doc["to"] | 0;
  out.title = doc["title"] | "";
  out.path = doc["path"] | "";
  return true;
}

bool uploadable(const Session& s, const int64_t now) {
  if (!isDocumentId(s.doc)) return false;
  if (s.start < MIN_START || s.secs < MIN_SECONDS || s.secs > MAX_SECONDS) return false;
  // A session dated in the future was measured with a wrong clock; it stays in the log only.
  if (now > 0 && s.start + static_cast<int64_t>(s.secs) > now + MAX_FUTURE_SECONDS) return false;
  return true;
}

std::string sessionId(const int64_t start, const uint8_t mac[6]) {
  char buf[48];
  snprintf(buf, sizeof(buf), "%lld-%02x%02x%02x%02x%02x%02x", static_cast<long long>(start), mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
  return buf;
}

std::string buildBatch(const std::string& deviceId, const std::vector<Session>& sessions, const uint8_t mac[6],
                       const int64_t now, std::vector<std::string>& ids) {
  ids.clear();
  JsonDocument doc;
  doc["device_id"] = deviceId;
  JsonArray items = doc["items"].to<JsonArray>();
  std::vector<int64_t> starts;
  for (const auto& s : sessions) {
    if (ids.size() >= BATCH_SIZE) break;
    if (!uploadable(s, now)) continue;
    if (std::find(starts.begin(), starts.end(), s.start) != starts.end()) continue;
    starts.push_back(s.start);
    const std::string id = sessionId(s.start, mac);
    ids.push_back(id);
    JsonObject item = items.add<JsonObject>();
    item["session_id"] = id;
    item["document"] = s.doc;
    item["started_at"] = s.start;
    item["ended_at"] = s.start + static_cast<int64_t>(s.secs);
    item["seconds"] = s.secs;
    item["utc_offset_minutes"] = s.tz;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool allAccepted(const std::string& responseBody, const std::vector<std::string>& ids) {
  JsonDocument doc;
  if (deserializeJson(doc, responseBody) != DeserializationError::Ok) return false;
  const JsonArrayConst accepted = doc["accepted_session_ids"].as<JsonArrayConst>();
  for (const auto& id : ids) {
    bool found = false;
    for (JsonVariantConst v : accepted) {
      if (id == (v | "")) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

}  // namespace readingsessions
