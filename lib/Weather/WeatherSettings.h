#pragma once

#include <cstdint>
#include <string>

// Weather configuration plus its on-disk text codec and the Open-Meteo URL builders.
// Pure C++ (no Arduino), covered by native tests.
namespace weather {

enum class TempUnit : uint8_t { Celsius = 0, Fahrenheit = 1 };
enum class WindUnit : uint8_t { Kmh = 0, Ms = 1, Mph = 2, Knots = 3 };
enum class PrecipUnit : uint8_t { Mm = 0, Inch = 1 };

inline constexpr uint8_t MIN_FORECAST_DAYS = 1;
inline constexpr uint8_t MAX_FORECAST_DAYS = 5;
inline constexpr size_t MAX_LOCATION_NAME = 63;

struct Settings {
  bool hasLocation = false;
  float latitude = 0;
  float longitude = 0;
  std::string locationName;
  TempUnit tempUnit = TempUnit::Celsius;
  WindUnit windUnit = WindUnit::Kmh;
  PrecipUnit precipUnit = PrecipUnit::Mm;
  uint8_t forecastDays = 3;

  void setForecastDays(int days) {
    if (days < MIN_FORECAST_DAYS) days = MIN_FORECAST_DAYS;
    if (days > MAX_FORECAST_DAYS) days = MAX_FORECAST_DAYS;
    forecastDays = static_cast<uint8_t>(days);
  }
};

// Line-oriented "key=value" codec. Unknown keys are ignored, bad values fall back to defaults,
// so the file stays readable across versions.
std::string serializeSettings(const Settings& s);
Settings parseSettings(const std::string& text);

const char* tempUnitParam(TempUnit u);
const char* windUnitParam(WindUnit u);
const char* precipUnitParam(PrecipUnit u);
const char* tempUnitSymbol(TempUnit u);
const char* windUnitSymbol(WindUnit u);
const char* precipUnitSymbol(PrecipUnit u);

// Identifies the request a cached response belongs to (location + units + days).
std::string requestSignature(const Settings& s);

std::string buildForecastUrl(const Settings& s);
// `language` is an ISO code understood by Open-Meteo ("en", "ru", "uk"); empty means "en".
std::string buildGeocodingUrl(const std::string& query, const std::string& language);

std::string urlEncode(const std::string& value);

// Compass label (N, NE, ...) for a wind direction in degrees.
const char* windDirectionLabel(int degrees);

}  // namespace weather
