#include "WeatherService.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include "WeatherCache.h"
#include "WeatherParser.h"
#include "network/HttpDownloader.h"

WeatherService WeatherService::instance;

namespace {
constexpr char DIR[] = "/.inkmod";
constexpr char SETTINGS_FILE[] = "/.inkmod/weather.cfg";
constexpr char CACHE_FILE[] = "/.inkmod/weather_cache.txt";
// A forecast response is ~10 KB; anything larger is not ours.
constexpr size_t MAX_RESPONSE_BYTES = 48 * 1024;
}  // namespace

void WeatherService::loadSettings() {
  if (!Storage.exists(SETTINGS_FILE)) return;
  const String text = Storage.readFile(SETTINGS_FILE);
  if (text.isEmpty()) return;
  cfg = weather::parseSettings(std::string(text.c_str(), text.length()));
}

bool WeatherService::saveSettings() const {
  Storage.mkdir(DIR);
  return Storage.writeFile(SETTINGS_FILE, String(weather::serializeSettings(cfg).c_str()));
}

WeatherService::Result WeatherService::loadCached() const {
  Result r;
  if (!cfg.hasLocation) {
    r.status = Status::NoLocation;
    return r;
  }
  if (!Storage.exists(CACHE_FILE)) return r;

  const String text = Storage.readFile(CACHE_FILE);
  std::string signature;
  std::string json;
  time_t fetchedAt = 0;
  if (text.isEmpty() || !weather::unwrapCache(std::string(text.c_str(), text.length()), signature, fetchedAt, json)) {
    return r;
  }
  // A cache made for another city/unit set must not be shown as if it were current.
  if (signature != weather::requestSignature(cfg)) {
    LOG_DBG("WEA", "Ignoring cache for a different request");
    return r;
  }
  if (!weather::parseForecast(json, r.forecast)) {
    r.status = Status::ParseError;
    return r;
  }
  r.fetchedAt = fetchedAt;
  r.fromCache = true;
  r.status = weather::isFresh(fetchedAt, time(nullptr)) ? Status::Ok : Status::Stale;
  return r;
}

WeatherService::Result WeatherService::fetch() {
  Result r;
  if (!cfg.hasLocation) {
    r.status = Status::NoLocation;
    return r;
  }

  std::string body;
  if (!HttpDownloader::fetchUrl(weather::buildForecastUrl(cfg), body) || body.empty() ||
      body.size() > MAX_RESPONSE_BYTES) {
    LOG_ERR("WEA", "Forecast request failed (%u bytes)", static_cast<unsigned>(body.size()));
    r.status = Status::NetworkError;
    return r;
  }
  if (!weather::parseForecast(body, r.forecast)) {
    LOG_ERR("WEA", "Forecast response rejected");
    r.status = Status::ParseError;
    return r;
  }

  r.fetchedAt = time(nullptr);
  r.status = Status::Ok;
  Storage.mkdir(DIR);
  Storage.writeFile(CACHE_FILE, String(weather::wrapCache(weather::requestSignature(cfg), r.fetchedAt, body).c_str()));
  return r;
}

bool WeatherService::storeCache(const std::string& json, const time_t fetchedAt) const {
  if (!cfg.hasLocation || json.empty() || json.size() > MAX_RESPONSE_BYTES || fetchedAt <= 0) return false;
  weather::Forecast probe;
  if (!weather::parseForecast(json, probe)) return false;
  Storage.mkdir(DIR);
  return Storage.writeFile(CACHE_FILE,
                           String(weather::wrapCache(weather::requestSignature(cfg), fetchedAt, json).c_str()));
}

bool WeatherService::readRawCache(std::string& json, time_t& fetchedAt) const {
  if (!cfg.hasLocation || !Storage.exists(CACHE_FILE)) return false;
  const String text = Storage.readFile(CACHE_FILE);
  std::string signature;
  if (text.isEmpty() || !weather::unwrapCache(std::string(text.c_str(), text.length()), signature, fetchedAt, json)) {
    return false;
  }
  return signature == weather::requestSignature(cfg);
}

std::vector<weather::CityResult> WeatherService::searchCity(const std::string& query) const {
  std::vector<weather::CityResult> cities;
  std::string body;
  if (query.empty() || !HttpDownloader::fetchUrl(weather::buildGeocodingUrl(query, geocodingLanguage()), body) ||
      body.size() > MAX_RESPONSE_BYTES) {
    return cities;
  }
  weather::parseCities(body, cities);
  return cities;
}

const char* WeatherService::geocodingLanguage() {
  switch (I18N.getLanguage()) {
    case Language::RU: return "ru";
    default: return "en";
  }
}
