#include "WeatherSettings.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace weather {
namespace {

std::string trimEol(std::string s) {
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
  return s;
}

int toInt(const std::string& v, int fallback, int lo, int hi) {
  char* end = nullptr;
  const long n = strtol(v.c_str(), &end, 10);
  if (end == v.c_str() || *end != '\0' || n < lo || n > hi) return fallback;
  return static_cast<int>(n);
}

bool toFloat(const std::string& v, float& out) {
  char* end = nullptr;
  const double d = strtod(v.c_str(), &end);
  if (end == v.c_str() || *end != '\0' || !std::isfinite(d)) return false;
  out = static_cast<float>(d);
  return true;
}

// Strip control characters from a stored/edited name and cap its length without splitting UTF-8.
std::string cleanName(const std::string& raw) {
  std::string out;
  for (const char ch : raw) {
    const auto c = static_cast<unsigned char>(ch);
    if (c < 0x20 || c == 0x7F) continue;
    out.push_back(ch);
  }
  if (out.size() > MAX_LOCATION_NAME) {
    size_t cut = MAX_LOCATION_NAME;
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
    out.resize(cut);
  }
  return out;
}

std::string fmtCoord(float v) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(v));
  return buf;
}

}  // namespace

std::string serializeSettings(const Settings& s) {
  std::string out;
  out += "v=1\n";
  out += std::string("located=") + (s.hasLocation ? "1" : "0") + "\n";
  out += "lat=" + fmtCoord(s.latitude) + "\n";
  out += "lon=" + fmtCoord(s.longitude) + "\n";
  out += "temp=" + std::to_string(static_cast<int>(s.tempUnit)) + "\n";
  out += "wind=" + std::to_string(static_cast<int>(s.windUnit)) + "\n";
  out += "precip=" + std::to_string(static_cast<int>(s.precipUnit)) + "\n";
  out += "days=" + std::to_string(static_cast<int>(s.forecastDays)) + "\n";
  out += "name=" + cleanName(s.locationName) + "\n";
  return out;
}

Settings parseSettings(const std::string& text) {
  Settings s;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string::npos) eol = text.size();
    const std::string line = trimEol(text.substr(pos, eol - pos));
    pos = eol + 1;

    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = line.substr(0, eq);
    const std::string val = line.substr(eq + 1);

    if (key == "located") s.hasLocation = val == "1";
    else if (key == "lat") toFloat(val, s.latitude);
    else if (key == "lon") toFloat(val, s.longitude);
    else if (key == "temp") s.tempUnit = static_cast<TempUnit>(toInt(val, 0, 0, 1));
    else if (key == "wind") s.windUnit = static_cast<WindUnit>(toInt(val, 0, 0, 3));
    else if (key == "precip") s.precipUnit = static_cast<PrecipUnit>(toInt(val, 0, 0, 1));
    else if (key == "days") s.setForecastDays(toInt(val, 3, MIN_FORECAST_DAYS, MAX_FORECAST_DAYS));
    else if (key == "name") s.locationName = cleanName(val);
  }
  // Coordinates outside the valid range mean a corrupted file: drop the location.
  if (std::fabs(s.latitude) > 90.0f || std::fabs(s.longitude) > 180.0f) {
    s.hasLocation = false;
    s.latitude = s.longitude = 0;
  }
  return s;
}

const char* tempUnitParam(TempUnit u) { return u == TempUnit::Fahrenheit ? "fahrenheit" : "celsius"; }

const char* windUnitParam(WindUnit u) {
  switch (u) {
    case WindUnit::Ms: return "ms";
    case WindUnit::Mph: return "mph";
    case WindUnit::Knots: return "kn";
    case WindUnit::Kmh:
    default: return "kmh";
  }
}

const char* precipUnitParam(PrecipUnit u) { return u == PrecipUnit::Inch ? "inch" : "mm"; }

const char* tempUnitSymbol(TempUnit u) { return u == TempUnit::Fahrenheit ? "\xC2\xB0" "F" : "\xC2\xB0" "C"; }

const char* windUnitSymbol(WindUnit u) {
  switch (u) {
    case WindUnit::Ms: return "m/s";
    case WindUnit::Mph: return "mph";
    case WindUnit::Knots: return "kn";
    case WindUnit::Kmh:
    default: return "km/h";
  }
}

const char* precipUnitSymbol(PrecipUnit u) { return u == PrecipUnit::Inch ? "in" : "mm"; }

std::string requestSignature(const Settings& s) {
  char buf[160];
  snprintf(buf, sizeof(buf), "lat=%.4f|lon=%.4f|t=%s|w=%s|p=%s|d=%u", static_cast<double>(s.latitude),
           static_cast<double>(s.longitude), tempUnitParam(s.tempUnit), windUnitParam(s.windUnit),
           precipUnitParam(s.precipUnit), static_cast<unsigned>(s.forecastDays));
  return buf;
}

std::string urlEncode(const std::string& value) {
  std::string out;
  out.reserve(value.size() * 3);
  for (const char ch : value) {
    const auto c = static_cast<unsigned char>(ch);
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out.push_back(ch);
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      out.append(buf);
    }
  }
  return out;
}

std::string buildForecastUrl(const Settings& s) {
  std::string url = "https://api.open-meteo.com/v1/forecast?latitude=" + fmtCoord(s.latitude) +
                    "&longitude=" + fmtCoord(s.longitude);
  url +=
      "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,"
      "wind_direction_10m,is_day,precipitation,uv_index";
  url += "&hourly=temperature_2m,precipitation,precipitation_probability,weather_code,is_day";
  url +=
      "&daily=temperature_2m_max,temperature_2m_min,weather_code,precipitation_sum,sunrise,sunset,"
      "uv_index_max";
  url += "&timezone=auto&timeformat=unixtime";
  url += std::string("&temperature_unit=") + tempUnitParam(s.tempUnit);
  url += std::string("&wind_speed_unit=") + windUnitParam(s.windUnit);
  url += std::string("&precipitation_unit=") + precipUnitParam(s.precipUnit);
  url += "&forecast_days=" + std::to_string(static_cast<int>(s.forecastDays));
  url += "&forecast_hours=48";
  return url;
}

std::string buildGeocodingUrl(const std::string& query, const std::string& language) {
  std::string url = "https://geocoding-api.open-meteo.com/v1/search?name=" + urlEncode(query);
  url += "&count=" + std::to_string(8);
  url += "&language=" + urlEncode(language.empty() ? "en" : language);
  url += "&format=json";
  return url;
}

const char* windDirectionLabel(int degrees) {
  static const char* const labels[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  int d = degrees % 360;
  if (d < 0) d += 360;
  return labels[((d + 22) / 45) % 8];
}

}  // namespace weather
