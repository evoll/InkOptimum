#pragma once

#include <string>
#include <vector>

#include "WeatherData.h"

namespace weather {

// Parses an Open-Meteo /v1/forecast response requested with timeformat=unixtime.
// Returns false for malformed JSON or an API error object (see Forecast::apiError / errorReason).
// A response without any "current" or "hourly" or "daily" section is rejected.
bool parseForecast(const std::string& json, Forecast& out);

// Parses an Open-Meteo geocoding /v1/search response. Returns false on malformed JSON.
// An empty result list is still success.
bool parseCities(const std::string& json, std::vector<CityResult>& out);

// Maximum number of hourly / daily entries kept (guards RAM on ESP32-C3).
inline constexpr size_t MAX_HOURLY = 72;
inline constexpr size_t MAX_DAILY = 7;
inline constexpr size_t MAX_CITIES = 8;

}  // namespace weather
