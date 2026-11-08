#include "TlsPolicy.h"

#include <ArduinoJson.h>

#include <algorithm>

namespace tlspolicy {

std::string hostKey(const std::string& host, const uint16_t port) {
  std::string key = host;
  std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
  key += ':';
  key += std::to_string(port);
  return key;
}

std::string toHex(const uint8_t* bytes, const size_t len) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    out += digits[bytes[i] >> 4];
    out += digits[bytes[i] & 15];
  }
  return out;
}

bool fromHex(const std::string& hex, uint8_t* out, const size_t len) {
  if (hex.size() != len * 2) return false;
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < len; i++) {
    const int hi = nibble(hex[2 * i]);
    const int lo = nibble(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>(hi << 4 | lo);
  }
  return true;
}

bool PinStore::load(const std::string& json) {
  pins_.clear();
  dirty_ = false;
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return false;
  for (JsonVariantConst p : doc["pins"].as<JsonArrayConst>()) {
    const std::string key = p["h"] | "";
    std::string fp = p["fp"] | "";
    uint8_t raw[FINGERPRINT_BYTES];
    if (key.empty() || !fromHex(fp, raw, sizeof(raw))) continue;
    fp = toHex(raw, sizeof(raw));  // normalised to lower case
    if (pins_.size() < MAX_PINS) pins_.push_back({key, fp});
  }
  return true;
}

std::string PinStore::toJson() const {
  JsonDocument doc;
  JsonArray arr = doc["pins"].to<JsonArray>();
  for (const auto& pin : pins_) {
    JsonObject o = arr.add<JsonObject>();
    o["h"] = pin.key;
    o["fp"] = pin.fp;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

Verdict PinStore::check(const std::string& key, const std::string& fingerprintHex) const {
  for (const auto& pin : pins_) {
    if (pin.key == key) return pin.fp == fingerprintHex ? Verdict::Match : Verdict::Mismatch;
  }
  return Verdict::NewHost;
}

void PinStore::set(const std::string& key, const std::string& fingerprintHex) {
  dirty_ = true;
  for (auto& pin : pins_) {
    if (pin.key == key) {
      pin.fp = fingerprintHex;
      return;
    }
  }
  if (pins_.size() >= MAX_PINS) pins_.erase(pins_.begin());
  pins_.push_back({key, fingerprintHex});
}

bool PinStore::forget(const std::string& key) {
  const auto it = std::remove_if(pins_.begin(), pins_.end(), [&](const Pin& p) { return p.key == key; });
  if (it == pins_.end()) return false;
  pins_.erase(it, pins_.end());
  dirty_ = true;
  return true;
}

Decision decideTofu(PinStore& store, const std::string& key, const std::string& fingerprintHex) {
  if (fingerprintHex.empty()) return Decision::Accept;
  switch (store.check(key, fingerprintHex)) {
    case Verdict::Match:
      return Decision::Accept;
    case Verdict::Mismatch:
      return Decision::Reject;
    case Verdict::NewHost:
      store.set(key, fingerprintHex);
      return Decision::AcceptAndSave;
  }
  return Decision::Reject;
}

}  // namespace tlspolicy
