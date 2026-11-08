#include "KOReaderProfileStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <KOReaderCredentialStore.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <OpdsProfile.h>

KOReaderProfileStore KOReaderProfileStore::instance;

namespace {
constexpr char FILE_PATH[] = "/.inkmod/koreader_profiles.json";
}

void KOReaderProfileStore::ensureLoaded() {
  if (!loaded) load();
}

bool KOReaderProfileStore::load() {
  loaded = true;
  list.clear();
  active.clear();
  if (!Storage.exists(FILE_PATH)) return true;
  const String raw = Storage.readFile(FILE_PATH);
  JsonDocument doc;
  if (deserializeJson(doc, raw.c_str())) {
    LOG_ERR("KRP", "Profile file unreadable");
    return false;
  }
  active = doc["active"] | std::string("");
  for (JsonObject o : doc["profiles"].as<JsonArray>()) {
    if (list.size() >= sync_profile::MAX_PROFILES) break;
    sync_profile::Profile p;
    p.name = sync_profile::sanitizeName(o["name"] | std::string(""));
    if (p.name.empty()) continue;
    p.serverUrl = o["serverUrl"] | std::string("");
    p.username = o["username"] | std::string("");
    obfuscation::DecodeStatus st = obfuscation::DecodeStatus::INVALID;
    p.password = obfuscation::deobfuscateFromBase64(o["password_obf"] | "", &st);
    p.header = o["header"] | std::string("");
    list.push_back(std::move(p));
  }
  if (sync_profile::indexOf(list, active) < 0) active.clear();
  return true;
}

bool KOReaderProfileStore::save() const {
  JsonDocument doc;
  doc["active"] = active;
  JsonArray arr = doc["profiles"].to<JsonArray>();
  for (const auto& p : list) {
    JsonObject o = arr.add<JsonObject>();
    o["name"] = p.name;
    o["serverUrl"] = p.serverUrl;
    o["username"] = p.username;
    o["password_obf"] = obfuscation::obfuscateToBase64(p.password);
    o["header"] = p.header;
  }
  Storage.ensureDirectoryExists("/.inkmod");
  FsFile file;
  if (!Storage.openFileForWrite("KRP", FILE_PATH, file)) return false;
  const size_t expected = measureJson(doc);
  const size_t written = serializeJson(doc, file);
  return file.close() && written == expected;
}

bool KOReaderProfileStore::upsert(sync_profile::Profile p, bool keepPasswordIfEmpty, std::string& error) {
  ensureLoaded();
  p.name = sync_profile::sanitizeName(p.name);
  if (p.name.empty()) {
    error = "name";
    return false;
  }
  std::string url;
  if (!sync_profile::normalizeServerUrl(p.serverUrl, url)) {
    error = "url";
    return false;
  }
  p.serverUrl = url;
  opds_profile::Header h;
  if (!opds_profile::parseHeaderLine(p.header, h)) {
    error = "header";
    return false;
  }
  p.header = opds_profile::formatHeaderLine(h);
  if (keepPasswordIfEmpty && p.password.empty()) {
    const int i = sync_profile::indexOf(list, p.name);
    if (i >= 0) p.password = list[i].password;
  }
  if (!sync_profile::upsert(list, p)) {
    error = "full";
    return false;
  }
  // The active profile is mirrored into the live store as soon as it is edited.
  const bool wasActive = p.name == active;
  const bool ok = save();
  if (wasActive) activate(p.name);
  return ok;
}

bool KOReaderProfileStore::remove(const std::string& name) {
  ensureLoaded();
  if (!sync_profile::remove(list, name)) return false;
  if (active == name) active.clear();
  return save();
}

bool KOReaderProfileStore::activate(const std::string& name) {
  ensureLoaded();
  const int i = sync_profile::indexOf(list, name);
  if (i < 0) return false;
  const auto& p = list[i];
  KOREADER_STORE.setCredentials(p.username, p.password);
  KOREADER_STORE.setServerUrl(p.serverUrl);
  KOREADER_STORE.setCustomHeader(p.header);
  KOREADER_STORE.saveToFile();
  active = name;
  return save();
}

std::string KOReaderProfileStore::activateNext() {
  ensureLoaded();
  const int n = sync_profile::nextIndex(list, active);
  if (n < 0) return "";
  const std::string name = list[n].name;
  return activate(name) ? name : "";
}
