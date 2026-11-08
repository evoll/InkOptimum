#include "LcpDeviceStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <LcpLicense.h>
#include <LcpService.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <Util.h>
#include <WolfsslCrypto.h>
#include <base64.h>

#include <algorithm>
#include <cstring>

#include "network/HttpDownloader.h"

// Injected from CI secrets into official release builds; self-compiled firmware carries the empty default
// and does everything except service-backed LCP.
#ifndef FREEINK_LCP_RELEASE_KEY
#define FREEINK_LCP_RELEASE_KEY ""
#endif

namespace lcpdevice {

namespace {

constexpr char DEVICE_FILE[] = "/.inkmod/lcp_device.json";

struct Identity {
  std::string deviceId;
  std::string privHex;  // 64 hex chars
  bool loaded = false;
  bool valid() const { return !deviceId.empty() && privHex.size() == 64; }
};

Identity& identity() {
  static Identity instance;
  return instance;
}

void loadOnce() {
  Identity& id = identity();
  if (id.loaded) return;
  id.loaded = true;
  const String json = Storage.readFile(DEVICE_FILE);
  if (json.isEmpty()) return;
  JsonDocument doc;
  if (deserializeJson(doc, json.c_str()) != DeserializationError::Ok) return;
  id.deviceId = doc["device_id"] | "";
  bool ok = false;
  id.privHex = obfuscation::deobfuscateFromBase64(doc["priv_obf"] | "", &ok);
  if (!ok || id.privHex.size() != 64) id.privHex.clear();
}

bool saveIdentity() {
  Identity& id = identity();
  JsonDocument doc;
  doc["device_id"] = id.deviceId;
  doc["priv_obf"] = obfuscation::obfuscateToBase64(id.privHex);
  String out;
  serializeJson(doc, out);
  Storage.mkdir("/.inkmod");
  return Storage.writeFile(DEVICE_FILE, out);
}

}  // namespace

bool available() {
  static constexpr char RELEASE_KEY[] = FREEINK_LCP_RELEASE_KEY;
  loadOnce();
  return RELEASE_KEY[0] != '\0' || identity().valid();
}

bool canEnroll() {
  static constexpr char RELEASE_KEY[] = FREEINK_LCP_RELEASE_KEY;
  return RELEASE_KEY[0] != '\0';
}

bool enrolled() {
  loadOnce();
  return identity().valid();
}

bool ensureEnrolled(std::string& deviceId, bool& notEnrollable) {
  loadOnce();
  Identity& id = identity();
  notEnrollable = false;
  if (id.valid()) {
    deviceId = id.deviceId;
    return true;
  }
  static constexpr char RELEASE_KEY[] = FREEINK_LCP_RELEASE_KEY;
  if (RELEASE_KEY[0] == '\0') {
    notEnrollable = true;
    return false;
  }

  freeink::content::WolfsslCrypto crypto;
  uint8_t priv[32];
  uint8_t pub[32];
  if (!crypto.x25519MakeKey(priv, pub)) {
    LOG_ERR("LCPDEV", "Keygen failed");
    return false;
  }
  const std::string request = lcpservice::buildEnrollRequest(RELEASE_KEY, base64::encode(pub, sizeof(pub)).c_str());
  std::string response;
  int status = 0;
  if (!HttpDownloader::postForm(lcpservice::ENROLL_URL, request, response, &status) ||
      !lcpservice::parseEnrollResponse(response, &deviceId)) {
    LOG_ERR("LCPDEV", "Enroll failed (status %d)", status);
    memset(priv, 0, sizeof(priv));
    return false;
  }
  id.deviceId = deviceId;
  id.privHex = lcpservice::toHex(priv, sizeof(priv));
  memset(priv, 0, sizeof(priv));
  if (!saveIdentity()) {
    LOG_ERR("LCPDEV", "Cannot persist device identity");
    id.deviceId.clear();
    id.privHex.clear();
    return false;
  }
  LOG_INF("LCPDEV", "Enrolled as %s", deviceId.c_str());
  return true;
}

void forget() {
  loadOnce();
  Identity& id = identity();
  id.deviceId.clear();
  id.privHex.clear();
  Storage.remove(DEVICE_FILE);
}

bool unwrapContentKey(const std::string& epkB64, const std::string& ivB64, const std::string& ctB64,
                      const std::string& tagB64, uint8_t out[32]) {
  using freeink::content::base64Decode;
  uint8_t epk[32];
  uint8_t iv[12];
  uint8_t ct[32];
  uint8_t tag[16];
  if (base64Decode(epkB64.data(), epkB64.size(), epk, sizeof(epk)) != static_cast<int32_t>(sizeof(epk)) ||
      base64Decode(ivB64.data(), ivB64.size(), iv, sizeof(iv)) != static_cast<int32_t>(sizeof(iv)) ||
      base64Decode(ctB64.data(), ctB64.size(), ct, sizeof(ct)) != static_cast<int32_t>(sizeof(ct)) ||
      base64Decode(tagB64.data(), tagB64.size(), tag, sizeof(tag)) != static_cast<int32_t>(sizeof(tag))) {
    return false;
  }
  loadOnce();
  uint8_t priv[32];
  if (!identity().valid() || !lcpservice::fromHex(identity().privHex, priv, sizeof(priv))) return false;
  freeink::content::WolfsslCrypto crypto;
  const bool ok = freeink::content::lcpUnwrapContentKey(crypto, priv, epk, iv, ct, tag, out);
  memset(priv, 0, sizeof(priv));
  return ok;
}

}  // namespace lcpdevice

namespace {
constexpr char PASSPHRASE_FILE[] = "/.inkmod/lcp_passphrases.json";
constexpr size_t MAX_HASH_CHARS = 64;  // a hex SHA-256; anything longer is a malformed file
}  // namespace

LcpPassphraseStore& LcpPassphraseStore::getInstance() {
  static LcpPassphraseStore instance;
  return instance;
}

bool LcpPassphraseStore::loadFromFile() {
  loaded = true;
  entries.clear();
  const String json = Storage.readFile(PASSPHRASE_FILE);
  if (json.isEmpty()) return true;
  JsonDocument doc;
  if (deserializeJson(doc, json.c_str()) != DeserializationError::Ok) return false;
  for (JsonObjectConst obj : doc["passphrases"].as<JsonArrayConst>()) {
    if (entries.size() >= MAX_PROVIDERS) break;
    Entry entry;
    entry.provider = obj["provider"] | "";
    bool ok = false;
    entry.userKeyHex = obfuscation::deobfuscateFromBase64(obj["key_obf"] | "", &ok);
    if (entry.provider.empty() || !ok || entry.userKeyHex.empty() || entry.userKeyHex.size() > MAX_HASH_CHARS) continue;
    entries.push_back(std::move(entry));
  }
  return true;
}

void LcpPassphraseStore::ensureLoaded() const {
  if (!loaded) const_cast<LcpPassphraseStore*>(this)->loadFromFile();
}

bool LcpPassphraseStore::saveToFile() const {
  JsonDocument doc;
  JsonArray arr = doc["passphrases"].to<JsonArray>();
  for (const auto& entry : entries) {
    JsonObject obj = arr.add<JsonObject>();
    obj["provider"] = entry.provider;
    obj["key_obf"] = obfuscation::obfuscateToBase64(entry.userKeyHex);
  }
  String out;
  serializeJson(doc, out);
  Storage.mkdir("/.inkmod");
  return Storage.writeFile(PASSPHRASE_FILE, out);
}

std::string LcpPassphraseStore::get(const std::string& provider) const {
  ensureLoaded();
  for (const auto& entry : entries) {
    if (entry.provider == provider) return entry.userKeyHex;
  }
  return {};
}

bool LcpPassphraseStore::put(const std::string& provider, const std::string& userKeyHex) {
  ensureLoaded();
  const auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.provider == provider; });
  if (userKeyHex.empty()) {
    if (it == entries.end()) return true;
    entries.erase(it);
    return saveToFile();
  }
  if (it != entries.end()) {
    if (it->userKeyHex == userKeyHex) return true;
    it->userKeyHex = userKeyHex;
    return saveToFile();
  }
  if (entries.size() >= MAX_PROVIDERS) entries.erase(entries.begin());  // evict the oldest
  entries.push_back(Entry{provider, userKeyHex});
  return saveToFile();
}
