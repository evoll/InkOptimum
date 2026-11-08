#pragma once

#include <ctime>
#include <string>
#include <vector>

#include "WeatherData.h"
#include "WeatherSettings.h"

// Firmware glue for the Weather library: persistence of the user's settings, the Open-Meteo
// HTTP calls and the on-SD response cache. All parsing/formatting rules live in lib/Weather.
class WeatherService {
 public:
  static WeatherService& getInstance() { return instance; }

  // Settings (stored in /.inkmod/weather.cfg).
  void loadSettings();
  bool saveSettings() const;
  weather::Settings& settings() { return cfg; }
  const weather::Settings& settings() const { return cfg; }

  enum class Status : uint8_t {
    Ok,          // fresh data
    Stale,       // cached data older than the TTL (shown, caller may refresh)
    NoLocation,  // no city configured
    NoCache,     // nothing cached; a network fetch is required
    NetworkError,
    ParseError,
  };

  struct Result {
    Status status = Status::NoCache;
    weather::Forecast forecast;
    time_t fetchedAt = 0;
    bool fromCache = false;
  };

  // Cache only; never touches the network.
  Result loadCached() const;
  // Always hits the network (caller must have Wi-Fi up). On failure the previous cache is kept.
  Result fetch();

  // Accepts a raw Open-Meteo response fetched by a browser (web UI) and caches it for the current
  // settings. Returns false when the body is not a valid forecast.
  bool storeCache(const std::string& json, time_t fetchedAt) const;
  // Raw cache payload (JSON) for the current settings plus its timestamp; false when absent/mismatched.
  bool readRawCache(std::string& json, time_t& fetchedAt) const;

  std::vector<weather::CityResult> searchCity(const std::string& query) const;

  // BCP-47-ish language tag for the geocoder, from the UI language.
  static const char* geocodingLanguage();

 private:
  static WeatherService instance;
  weather::Settings cfg;
};

#define WEATHER WeatherService::getInstance()
