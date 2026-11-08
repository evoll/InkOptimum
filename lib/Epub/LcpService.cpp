#include "LcpService.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstdlib>

namespace lcpservice {

std::string toHex(const uint8_t* data, const size_t len) {
  static const char* H = "0123456789abcdef";
  std::string out;
  out.resize(len * 2);
  for (size_t i = 0; i < len; i++) {
    out[i * 2] = H[data[i] >> 4];
    out[i * 2 + 1] = H[data[i] & 15];
  }
  return out;
}

bool fromHex(const std::string& hex, uint8_t* out, const size_t len) {
  if (hex.size() != len * 2) return false;
  auto nib = [](const char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < len; i++) {
    const int hi = nib(hex[i * 2]);
    const int lo = nib(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>(hi << 4 | lo);
  }
  return true;
}

std::string buildEnrollRequest(const std::string& releaseKey, const std::string& pubkeyB64) {
  JsonDocument doc;
  doc["release_key"] = releaseKey;
  doc["pubkey"] = pubkeyB64;
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool parseEnrollResponse(const std::string& json, std::string* deviceId) {
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return false;
  const char* id = doc["device_id"] | "";
  if (!*id || std::char_traits<char>::length(id) > 64) return false;
  *deviceId = id;
  return true;
}

std::string buildUnlockRequest(const std::string& deviceId, const std::string& userKeyHex,
                               const std::string& licenseText) {
  // device_id and the hex key are produced by us / validated hex, but quote them through ArduinoJson anyway.
  JsonDocument head;
  head["device_id"] = deviceId;
  head["user_key"] = userKeyHex;
  std::string out;
  serializeJson(head, out);
  out.pop_back();  // drop the closing brace and append the verbatim license object
  out += ",\"license\":";
  out += licenseText;
  out += '}';
  return out;
}

bool parseUnlockResponse(const std::string& json, WrappedKey* wrapped, int64_t* expiresAt) {
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return false;
  JsonObjectConst key = doc["content_key"].as<JsonObjectConst>();
  if (key.isNull()) return false;
  wrapped->epk = key["epk"] | "";
  wrapped->iv = key["iv"] | "";
  wrapped->ct = key["ct"] | "";
  wrapped->tag = key["tag"] | "";
  if (wrapped->epk.empty() || wrapped->iv.empty() || wrapped->ct.empty() || wrapped->tag.empty()) return false;
  const int64_t expires = doc["expires"] | static_cast<int64_t>(0);
  *expiresAt = expires > 0 ? expires : 0;
  return true;
}

UnlockOutcome classifyUnlockStatus(const int s) {
  if (s == 401) return UnlockOutcome::NotEnrolled;
  if (s == 403) return UnlockOutcome::WrongPassphrase;
  if (s == 422) return UnlockOutcome::Unsupported;
  if (s >= 200 && s < 300) return UnlockOutcome::Ok;
  return UnlockOutcome::Failed;
}

}  // namespace lcpservice
