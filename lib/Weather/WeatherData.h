#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

// Plain data model for the Open-Meteo forecast. No Arduino/ESP dependencies, so the parser,
// URL builder and settings codec can be unit-tested natively.
namespace weather {

struct Current {
  float temperature = 0;
  float apparentTemperature = 0;
  int humidity = 0;
  int weatherCode = 0;
  float windSpeed = 0;
  int windDirection = 0;
  float precipitation = 0;
  float uvIndex = 0;
  bool isDay = true;
};

struct Daily {
  time_t date = 0;
  float tempMax = 0;
  float tempMin = 0;
  int weatherCode = 0;
  float precipSum = 0;
  float uvIndexMax = 0;
  time_t sunrise = 0;
  time_t sunset = 0;
};

struct Hourly {
  time_t time = 0;
  float temperature = 0;
  float precipitation = 0;
  int precipitationProbability = 0;  // percent, 0 when the API had no value
  int weatherCode = 0;
  bool isDay = true;
};

struct Forecast {
  Current current;
  std::vector<Daily> daily;
  std::vector<Hourly> hourly;
  std::string timezone;
  int utcOffsetSeconds = 0;
  // Optional API error ("error": true, "reason": "...").
  bool apiError = false;
  std::string errorReason;
};

struct CityResult {
  std::string name;
  std::string country;
  std::string admin1;  // state / region
  float latitude = 0;
  float longitude = 0;
};

}  // namespace weather
