// Web portal extras: Weather, Notes (bookmarks + clippings) and Diagnostics pages.
// Kept apart from InkMODWebServer.cpp, which is already very large.
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cctype>
#include <ctime>
#include <string>
#include <vector>

#include "BookmarkStore.h"
#include "ClippingStore.h"
#include "InkMODSettings.h"
#include "InkMODWebServer.h"
#include "KOReaderProfileStore.h"
#include "dictionary/DictionaryStore.h"
#include "html/DiagnosticsPageHtml.generated.h"
#include "html/DictionaryPageHtml.generated.h"
#include "html/NotesPageHtml.generated.h"
#include "html/StatsPageHtml.generated.h"
#include "html/WeatherPageHtml.generated.h"
#include "activities/reader/GlobalReadingStats.h"
#include "activities/settings/FontDownloadActivity.h"
#include "weather/WeatherService.h"

namespace {

void sendPage(WebServer* server, const char* data, size_t len) {
  server->sendHeader("Content-Encoding", "gzip");
  server->sendHeader("Cache-Control", "no-cache");
  server->send_P(200, "text/html", data, len);
}

void sendJson(WebServer* server, int code, const JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  server->sendHeader("Cache-Control", "no-store");
  server->send(code, "application/json", out);
}

// The book type becomes part of a file name; accept only what the stores themselves produce.
bool validBookType(const std::string& type) {
  if (type.empty() || type.size() > 8) return false;
  for (const char c : type) {
    if (!std::islower(static_cast<unsigned char>(c)) && !std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

bool validBookPath(const std::string& path) { return !path.empty() && path[0] == '/' && path.find("..") == std::string::npos; }

struct BookRef {
  std::string path;
  std::string type;
  std::string title;
  std::string author;
};

bool readBookRef(const JsonDocument& doc, BookRef& ref) {
  ref.path = doc["path"] | "";
  ref.type = doc["type"] | "";
  ref.title = doc["title"] | "";
  ref.author = doc["author"] | "";
  return validBookPath(ref.path) && validBookType(ref.type);
}

}  // namespace

// ---- Pages ----------------------------------------------------------------

void InkMODWebServer::handleWeatherPage() const { sendPage(server.get(), WeatherPageHtml, sizeof(WeatherPageHtml)); }
void InkMODWebServer::handleNotesPage() const { sendPage(server.get(), NotesPageHtml, sizeof(NotesPageHtml)); }
void InkMODWebServer::handleDictionaryPage() const {
  sendPage(server.get(), DictionaryPageHtml, sizeof(DictionaryPageHtml));
}
void InkMODWebServer::handleDiagnosticsPage() const {
  sendPage(server.get(), DiagnosticsPageHtml, sizeof(DiagnosticsPageHtml));
}

// ---- Weather --------------------------------------------------------------
//
// The device cannot reach Open-Meteo over TLS cheaply while serving the portal, but the browser can:
// the page fetches the forecast itself and hands the raw response back, so the reader shows fresh
// data the next time it is opened offline.

void InkMODWebServer::handleGetWeather() const {
  const auto& cfg = WEATHER.settings();

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->sendHeader("Cache-Control", "no-store");
  server->send(200, "application/json", "");

  JsonDocument doc;
  doc["hasLocation"] = cfg.hasLocation;
  doc["latitude"] = cfg.latitude;
  doc["longitude"] = cfg.longitude;
  doc["name"] = cfg.locationName;
  doc["temp"] = static_cast<int>(cfg.tempUnit);
  doc["wind"] = static_cast<int>(cfg.windUnit);
  doc["precip"] = static_cast<int>(cfg.precipUnit);
  doc["days"] = cfg.forecastDays;
  doc["onHome"] = SETTINGS.weatherOnHome != 0;
  doc["language"] = WeatherService::geocodingLanguage();
  if (cfg.hasLocation) doc["forecastUrl"] = weather::buildForecastUrl(cfg);

  std::string raw;
  time_t fetchedAt = 0;
  const bool hasCache = WEATHER.readRawCache(raw, fetchedAt);
  doc["fetchedAt"] = hasCache ? static_cast<long long>(fetchedAt) : 0LL;

  String head;
  serializeJson(doc, head);
  // Splice the already-valid raw forecast in as a "cache" member without re-parsing it.
  head.remove(head.length() - 1);  // drop closing '}'
  server->sendContent(head);
  server->sendContent(",\"cache\":");
  if (hasCache) {
    server->sendContent(raw.c_str());
  } else {
    server->sendContent("null");
  }
  server->sendContent("}");
  server->sendContent("");
}

void InkMODWebServer::handlePostWeatherSettings() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server->arg("plain"))) {
    server->send(400, "text/plain", "Invalid JSON");
    return;
  }

  auto& cfg = WEATHER.settings();
  if (doc["latitude"].is<float>() && doc["longitude"].is<float>()) {
    const float lat = doc["latitude"].as<float>();
    const float lon = doc["longitude"].as<float>();
    if (lat < -90.f || lat > 90.f || lon < -180.f || lon > 180.f) {
      server->send(400, "text/plain", "Coordinates out of range");
      return;
    }
    std::string name = doc["name"] | "";
    if (name.size() > weather::MAX_LOCATION_NAME) name.resize(weather::MAX_LOCATION_NAME);
    cfg.latitude = lat;
    cfg.longitude = lon;
    cfg.locationName = name;
    cfg.hasLocation = true;
  }
  if (doc["temp"].is<int>()) {
    const int v = doc["temp"].as<int>();
    if (v >= 0 && v <= 1) cfg.tempUnit = static_cast<weather::TempUnit>(v);
  }
  if (doc["wind"].is<int>()) {
    const int v = doc["wind"].as<int>();
    if (v >= 0 && v <= 3) cfg.windUnit = static_cast<weather::WindUnit>(v);
  }
  if (doc["precip"].is<int>()) {
    const int v = doc["precip"].as<int>();
    if (v >= 0 && v <= 1) cfg.precipUnit = static_cast<weather::PrecipUnit>(v);
  }
  if (doc["days"].is<int>()) cfg.setForecastDays(doc["days"].as<int>());
  if (doc["onHome"].is<bool>()) {
    SETTINGS.weatherOnHome = doc["onHome"].as<bool>() ? 1 : 0;
    SETTINGS.saveToFile();
  }

  if (!WEATHER.saveSettings()) {
    server->send(500, "text/plain", "Failed to save");
    return;
  }
  server->send(200, "text/plain", "OK");
}

void InkMODWebServer::handlePostWeatherCache() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing body");
    return;
  }
  // The device clock may be unset; accept the browser's time as a fallback.
  time_t stamp = time(nullptr);
  if (stamp < 1700000000 && server->hasArg("t")) stamp = static_cast<time_t>(server->arg("t").toInt());

  const std::string body = server->arg("plain").c_str();
  if (!WEATHER.storeCache(body, stamp)) {
    server->send(422, "text/plain", "Rejected: not a valid forecast for the configured location");
    return;
  }
  server->send(200, "text/plain", "OK");
}

// ---- Notes ----------------------------------------------------------------

void InkMODWebServer::handleGetNotes() const {
  std::vector<BookmarkedBookEntry> bookmarked;
  std::vector<ClippedBookEntry> clipped;
  BookmarkStore::getAllBookmarkedBooks(bookmarked);
  ClippingStore::getAllClippedBooks(clipped);

  JsonDocument doc;
  JsonArray books = doc["books"].to<JsonArray>();
  for (const auto& b : bookmarked) {
    JsonObject o = books.add<JsonObject>();
    o["title"] = b.bookTitle;
    o["author"] = b.bookAuthor;
    o["path"] = b.bookPath;
    o["type"] = b.bookType;
    o["bookmarks"] = b.count;
    o["clippings"] = 0;
  }
  for (const auto& c : clipped) {
    bool merged = false;
    for (JsonObject o : books) {
      if (c.path == (o["path"] | "") && c.bookType == (o["type"] | "")) {
        o["clippings"] = c.count;
        merged = true;
        break;
      }
    }
    if (merged) continue;
    JsonObject o = books.add<JsonObject>();
    o["title"] = c.title;
    o["author"] = c.author;
    o["path"] = c.path;
    o["type"] = c.bookType;
    o["bookmarks"] = 0;
    o["clippings"] = c.count;
  }
  sendJson(server.get(), 200, doc);
}

void InkMODWebServer::handleGetBookNotes() const {
  BookRef ref;
  ref.path = server->arg("path").c_str();
  ref.type = server->arg("type").c_str();
  ref.title = server->arg("title").c_str();
  ref.author = server->arg("author").c_str();
  if (!validBookPath(ref.path) || !validBookType(ref.type)) {
    server->send(400, "text/plain", "Bad book reference");
    return;
  }

  // A book can hold hundreds of clippings (~0.5 KB each), so stream item by item
  // instead of building one large document in RAM.
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->sendHeader("Cache-Control", "no-store");
  server->send(200, "application/json", "");

  auto sendItem = [this](JsonDocument& doc, bool first) {
    String out;
    serializeJson(doc, out);
    if (!first) server->sendContent(",");
    server->sendContent(out);
    doc.clear();
  };

  JsonDocument doc;
  server->sendContent("{\"bookmarks\":[");
  BOOKMARKS.loadForBook(ref.path, ref.title, ref.author, ref.type);
  size_t index = 0;
  for (const auto& b : BOOKMARKS.getBookmarks()) {
    doc["i"] = index;
    doc["chapter"] = b.chapterTitle;
    doc["spine"] = b.spineIndex;
    doc["progress"] = static_cast<int>(b.progress * 1000.0f + 0.5f) / 10.0f;
    doc["time"] = b.timestamp;
    doc["snippet"] = b.snippet;
    doc["name"] = b.name;
    sendItem(doc, index == 0);
    ++index;
  }
  BOOKMARKS.unload();

  server->sendContent("],\"clippings\":[");
  ClippingStore clippings;
  clippings.loadForBook(ref.path, ref.title, ref.author, ref.type);
  index = 0;
  for (const auto& c : clippings.getClippings()) {
    doc["i"] = index;
    doc["chapter"] = c.chapterTitle;
    doc["page"] = c.pageNumber;
    doc["endPage"] = c.endPageNumber;
    doc["pages"] = c.pageCount;
    doc["time"] = c.timestamp;
    doc["text"] = c.text;
    sendItem(doc, index == 0);
    ++index;
  }
  server->sendContent("]}");
  server->sendContent("");
}

// Shared body for the three note mutations: parses the JSON, resolves the book and the index.
static bool parseNoteRequest(WebServer* server, JsonDocument& doc, BookRef& ref, size_t& index) {
  if (!server->hasArg("plain") || deserializeJson(doc, server->arg("plain")) || !readBookRef(doc, ref) ||
      !doc["index"].is<int>() || doc["index"].as<int>() < 0) {
    server->send(400, "text/plain", "Bad request");
    return false;
  }
  index = static_cast<size_t>(doc["index"].as<int>());
  return true;
}

void InkMODWebServer::handleRenameBookmark() {
  JsonDocument doc;
  BookRef ref;
  size_t index = 0;
  if (!parseNoteRequest(server.get(), doc, ref, index)) return;

  BOOKMARKS.loadForBook(ref.path, ref.title, ref.author, ref.type);
  const bool ok = BOOKMARKS.renameBookmarkAt(index, std::string(doc["name"] | ""));
  BOOKMARKS.unload();
  server->send(ok ? 200 : 404, "text/plain", ok ? "OK" : "No such bookmark");
}

void InkMODWebServer::handleDeleteBookmark() {
  JsonDocument doc;
  BookRef ref;
  size_t index = 0;
  if (!parseNoteRequest(server.get(), doc, ref, index)) return;

  BOOKMARKS.loadForBook(ref.path, ref.title, ref.author, ref.type);
  const bool ok = BOOKMARKS.removeBookmarkAt(index);
  BOOKMARKS.unload();
  server->send(ok ? 200 : 404, "text/plain", ok ? "OK" : "No such bookmark");
}

void InkMODWebServer::handleDeleteClipping() {
  JsonDocument doc;
  BookRef ref;
  size_t index = 0;
  if (!parseNoteRequest(server.get(), doc, ref, index)) return;

  ClippingStore clippings;
  clippings.loadForBook(ref.path, ref.title, ref.author, ref.type);
  const bool ok = clippings.removeAt(index);
  server->send(ok ? 200 : 404, "text/plain", ok ? "OK" : "No such clipping");
}

// ---- Font catalog ---------------------------------------------------------
// The browser downloads catalogue fonts itself and uploads them through /api/fonts/upload, so the
// reader does not need internet access (works in AP mode too). The device only tells it where the
// catalogue lives and which point sizes this build accepts.

void InkMODWebServer::handleFontCatalog() const {
  JsonDocument doc;
  doc["manifestUrl"] = FONT_MANIFEST_URL;
  doc["manifestVersion"] = FONTS_MANIFEST_VERSION;
  doc["sizeRange"] = SETTINGS.sdFontSizeRange;
  sendJson(server.get(), 200, doc);
}

// ---- Dictionaries ---------------------------------------------------------

static size_t fileSizeOrZero(const std::string& path) {
  FsFile f = Storage.open(path.c_str());
  if (!f) return 0;
  const size_t size = f.size();
  f.close();
  return size;
}

void InkMODWebServer::handleListDictionaries() const {
  Dictionary::Store store;
  store.scan();
  JsonDocument doc;
  JsonArray list = doc["dictionaries"].to<JsonArray>();
  for (uint8_t i = 0; i < store.count(); ++i) {
    const auto* entry = store.entry(i);
    if (!entry) continue;
    JsonObject o = list.add<JsonObject>();
    o["index"] = i;
    o["name"] = entry->name;
    o["path"] = entry->path;
    o["entries"] = entry->entryCount;
    o["bytes"] = fileSizeOrZero(std::string(entry->path) + "/dictionary.idx") +
                 fileSizeOrZero(std::string(entry->path) + "/dictionary.dat");
  }
  sendJson(server.get(), 200, doc);
}

void InkMODWebServer::handleLookupDictionary() const {
  Dictionary::Store store;
  store.scan();
  const int index = server->arg("index").toInt();
  const String word = server->arg("word");
  if (index < 0 || index >= store.count() || word.isEmpty() || word.length() > 96) {
    server->send(400, "text/plain", "Bad request");
    return;
  }

  constexpr size_t ARTICLE_MAX = 8192;
  std::string article(ARTICLE_MAX + 1, '\0');
  char matched[96] = {};
  const bool found = store.lookup(static_cast<uint8_t>(index), word.c_str(), matched, sizeof(matched), &article[0],
                                  article.size());
  JsonDocument doc;
  doc["found"] = found;
  if (found) {
    doc["headword"] = matched;
    doc["article"] = article.c_str();
  }
  sendJson(server.get(), 200, doc);
}

void InkMODWebServer::handleDeleteDictionary() {
  JsonDocument doc;
  if (!server->hasArg("plain") || deserializeJson(doc, server->arg("plain")) || !doc["path"].is<const char*>()) {
    server->send(400, "text/plain", "Bad request");
    return;
  }
  // Only a folder the scanner itself reports may be removed; never an arbitrary client-supplied path.
  const std::string requested = doc["path"] | "";
  Dictionary::Store store;
  store.scan();
  for (uint8_t i = 0; i < store.count(); ++i) {
    const auto* entry = store.entry(i);
    if (entry && requested == entry->path) {
      const bool ok = Storage.removeDir(entry->path);
      server->send(ok ? 200 : 500, "text/plain", ok ? "OK" : "Delete failed");
      return;
    }
  }
  server->send(404, "text/plain", "Unknown dictionary");
}

// ---- KOReader sync profiles ----

void InkMODWebServer::handleGetKoProfiles() const {
  KOREADER_PROFILES.load();
  JsonDocument doc;
  JsonArray arr = doc["profiles"].to<JsonArray>();
  for (const auto& p : KOREADER_PROFILES.profiles()) {
    JsonObject o = arr.add<JsonObject>();
    o["name"] = p.name;
    o["serverUrl"] = p.serverUrl;
    o["username"] = p.username;
    o["hasPassword"] = !p.password.empty();
    o["header"] = p.header;
  }
  doc["active"] = KOREADER_PROFILES.activeName();
  sendJson(server.get(), 200, doc);
}

void InkMODWebServer::handlePostKoProfile() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server->arg("plain"))) {
    server->send(400, "text/plain", "Invalid JSON");
    return;
  }
  sync_profile::Profile p;
  p.name = doc["name"] | "";
  p.serverUrl = doc["serverUrl"] | "";
  p.username = doc["username"] | "";
  p.password = doc["password"] | "";
  p.header = doc["header"] | "";
  std::string error;
  if (!KOREADER_PROFILES.upsert(std::move(p), /*keepPasswordIfEmpty=*/true, error)) {
    server->send(400, "text/plain", error.c_str());
    return;
  }
  server->send(200, "text/plain", "OK");
}

void InkMODWebServer::handleKoProfileAction(const bool remove) {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server->arg("plain"))) {
    server->send(400, "text/plain", "Invalid JSON");
    return;
  }
  const std::string name = doc["name"] | "";
  const bool ok = remove ? KOREADER_PROFILES.remove(name) : KOREADER_PROFILES.activate(name);
  server->send(ok ? 200 : 404, "text/plain", ok ? "OK" : "Not found");
}

// ---- Reading statistics ----

void InkMODWebServer::handleStatsPage() const { sendPage(server.get(), StatsPageHtml, sizeof(StatsPageHtml)); }

void InkMODWebServer::handleGetStats() const {
  const GlobalReadingStats st = GlobalReadingStats::loadAggregated();
  JsonDocument doc;
  doc["totalSeconds"] = st.totalReadingSeconds;
  doc["sessions"] = st.totalSessions;
  doc["pages"] = st.totalPagesTurned;
  doc["completed"] = st.completedBooks;
  JsonArray dow = doc["dayOfWeek"].to<JsonArray>();
  for (const uint32_t v : st.dayOfWeekSeconds) dow.add(v);
  JsonArray tod = doc["timeOfDay"].to<JsonArray>();
  for (const uint32_t v : st.timeOfDaySeconds) tod.add(v);

  ReadingStatsDateTime now;
  const bool haveNow = getCurrentLocalReadingStatsDateTime(now);
  doc["currentStreak"] = st.currentReadingStreak(haveNow ? &now.date : nullptr);
  doc["longestStreak"] = st.displayLongestReadingStreak();

  // history[i] == '1' means the day (anchor - i) had reading.
  ReadingStatsDate anchor;
  if (!readingStatsDateFromDayIndex(st.readingHistoryAnchorDay, anchor)) {
    anchor = haveNow ? now.date : ReadingStatsDate{2026, 1, 1};
  }
  JsonObject a = doc["anchor"].to<JsonObject>();
  a["y"] = anchor.year;
  a["m"] = anchor.month;
  a["d"] = anchor.day;
  std::string history(READING_HISTORY_DAYS, '0');
  for (size_t i = 0; i < READING_HISTORY_DAYS; ++i) {
    if (st.readingHistoryBits[i / 8] & (1u << (i % 8))) history[i] = '1';
  }
  doc["history"] = history;
  sendJson(server.get(), 200, doc);
}
