#include "SleepSync.h"

#include <ArduinoJson.h>
#include <CrossPointExtendedSync.h>
#include <HalStorage.h>
#include <KOReaderCredentialStore.h>
#include <KOReaderSyncClient.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_mac.h>

#include <algorithm>
#include <cstdio>

#include "InkMODSettings.h"
#include "WifiCredentialStore.h"

namespace SleepSync {
namespace {
constexpr const char* TAG = "SLPSYNC";
constexpr const char* DIR = "/.crosspoint";
constexpr const char* PATH = "/.crosspoint/sleep_sync.json";
// Server progress this far ahead (fraction of the book) is another device's newer reading: never overwritten.
constexpr float SERVER_AHEAD_MARGIN = 0.0005f;

bool loadSnapshot(Snapshot& s, bool& pending) {
  const String json = Storage.readFile(PATH);
  if (json.isEmpty()) return false;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  pending = doc["pending"] | false;
  s.document = doc["doc"] | "";
  s.xpath = doc["xpath"] | "";
  s.percentage = doc["pct"] | 0.0f;
  s.spine = doc["spine"] | 0;
  s.page = doc["page"] | 0;
  s.totalPages = doc["total"] | 1;
  s.paragraph = doc["para"] | 0;
  s.filename = doc["file"] | "";
  s.title = doc["title"] | "";
  s.authors = doc["authors"] | "";
  return s.document.size() == 32;
}

bool writeSnapshot(const Snapshot& s, bool pending) {
  JsonDocument doc;
  doc["pending"] = pending;
  doc["doc"] = s.document;
  doc["xpath"] = s.xpath;
  doc["pct"] = s.percentage;
  doc["spine"] = s.spine;
  doc["page"] = s.page;
  doc["total"] = s.totalPages;
  doc["para"] = s.paragraph;
  doc["file"] = s.filename;
  doc["title"] = s.title;
  doc["authors"] = s.authors;
  std::string out;
  serializeJson(doc, out);
  Storage.mkdir(DIR);
  const std::string tmp = std::string(PATH) + ".tmp";
  if (!Storage.writeFile(tmp.c_str(), String(out.c_str()))) return false;
  Storage.remove(PATH);
  return Storage.rename(tmp.c_str(), PATH);
}

const WifiCredential* sleepNetwork() {
  WIFI_STORE.loadFromFile();
  const std::string last = WIFI_STORE.getLastConnectedSsid();
  return last.empty() ? nullptr : WIFI_STORE.findCredential(last);
}

bool online() { return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0); }

std::string ownDeviceId() {
  uint8_t mac[6] = {};
  if (esp_efuse_mac_get_default(mac) != ESP_OK) return "inkmod-device";
  char buf[32];
  snprintf(buf, sizeof(buf), "inkmod-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return buf;
}
}  // namespace

bool enabled() { return KOREADER_STORE.getSyncOnSleep() && KOREADER_STORE.hasCredentials(); }

bool saveSnapshot(const Snapshot& snapshot) { return writeSnapshot(snapshot, true); }

bool wanted(const uint16_t batteryPercent) {
  if (!enabled() || batteryPercent < 20) return false;
  if (!Storage.exists(PATH)) return false;
  Snapshot s;
  bool pending = false;
  if (!loadSnapshot(s, pending) || !pending) return false;
  return online() || sleepNetwork() != nullptr;
}

void run() {
  if (!enabled()) return;
  Snapshot snap;
  bool pending = false;
  if (!loadSnapshot(snap, pending) || !pending) return;

  bool joined = false;
  if (!online()) {
    const WifiCredential* cred = sleepNetwork();
    if (!cred) return;
    const std::string ssid = cred->ssid;
    const std::string password = cred->password;
    LOG_INF(TAG, "joining %s (heap %u, largest %u)", ssid.c_str(), static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()));
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    const uint32_t started = millis();
    while (millis() - started < 10000) {  // sleep must never be held up longer than this
      if (online()) break;
      delay(100);
    }
    joined = true;
  }

  if (online()) {
    // Another device that read further since: its position stays (this book is then only marked handled).
    bool send = true;
    KOReaderProgress remote;
    if (KOReaderSyncClient::getProgress(snap.document, remote) == KOReaderSyncClient::OK &&
        remote.deviceId != ownDeviceId() && remote.percentage > snap.percentage + SERVER_AHEAD_MARGIN) {
      LOG_INF(TAG, "server is ahead (%.3f > %.3f, device %s): not overwriting", remote.percentage,
              snap.percentage, remote.device.c_str());
      send = false;
    }
    bool done = !send;
    if (send) {
      KOReaderProgress up;
      up.document = snap.document;
      up.progress = snap.xpath;
      up.percentage = std::clamp(snap.percentage, 0.0f, 1.0f);
      up.device = SETTINGS.getEffectiveDeviceName();
      up.timestamp = 0;
      if (KOREADER_STORE.usesCrossPointSyncServer()) {
        KOReaderRichPosition rich;
        rich.pctQ = static_cast<uint32_t>(up.percentage * 1000000.0f + 0.5f);
        rich.spineIndex = snap.spine;
        rich.pageNumber = snap.page;
        rich.totalPages = std::max<uint16_t>(1, snap.totalPages);
        if (snap.paragraph > 0) rich.paragraphIndex = snap.paragraph;
        rich.xpath = snap.xpath;
        up.position = std::move(rich);
      }
      if (KOREADER_STORE.getSendMetadata()) {
        KOReaderMetadata metadata;
        metadata.filename = snap.filename;
        metadata.title = snap.title;
        metadata.authors = snap.authors;
        up.metadata = std::move(metadata);
      }
      done = KOReaderSyncClient::updateProgress(up) == KOReaderSyncClient::OK;
      LOG_INF(TAG, "progress %.1f%% %s", up.percentage * 100.0f, done ? "sent" : "failed");
    }
    CrossPointExtendedSync::syncWithoutBook(snap.document);
    if (done) writeSnapshot(snap, false);
  } else {
    LOG_INF(TAG, "join timed out, the position waits for the next sleep");
  }

  if (joined) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
}

}  // namespace SleepSync
