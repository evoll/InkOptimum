#include <gtest/gtest.h>

#include "WeatherCache.h"
#include "WeatherParser.h"
#include "WeatherSettings.h"

using namespace weather;

namespace {
const char* kForecast = R"({
"latitude":55.75,"longitude":37.625,"generationtime_ms":0.2,"utc_offset_seconds":10800,
"timezone":"Europe/Moscow","timezone_abbreviation":"GMT+3","elevation":151.0,
"current_units":{"time":"unixtime","interval":"seconds","temperature_2m":"°C","is_day":""},
"current":{"time":1760600000,"interval":900,"temperature_2m":4.6,"relative_humidity_2m":81,
 "apparent_temperature":1.2,"weather_code":61,"wind_speed_10m":12.5,"wind_direction_10m":225,
 "is_day":0,"precipitation":0.3,"uv_index":0.0},
"hourly_units":{"time":"unixtime","temperature_2m":"°C"},
"hourly":{"time":[1760601600,1760605200,1760608800],
 "temperature_2m":[4.1,3.8,-0.5],
 "precipitation":[0.0,0.2,1.5],
 "precipitation_probability":[10,null,80],
 "weather_code":[3,61,71],
 "is_day":[0,0,1]},
"daily_units":{"time":"unixtime"},
"daily":{"time":[1760562000,1760648400],
 "temperature_2m_max":[7.2,5.1],"temperature_2m_min":[2.0,-1.3],
 "weather_code":[61,3],"precipitation_sum":[3.4,0.0],
 "sunrise":[1760587200,1760673900],"sunset":[1760625600,1760711700],
 "uv_index_max":[1.5,2.0]}
})";
}  // namespace

TEST(WeatherParser, ParsesCurrent) {
  Forecast f;
  ASSERT_TRUE(parseForecast(kForecast, f));
  EXPECT_EQ(f.timezone, "Europe/Moscow");
  EXPECT_EQ(f.utcOffsetSeconds, 10800);
  EXPECT_FLOAT_EQ(f.current.temperature, 4.6f);
  EXPECT_FLOAT_EQ(f.current.apparentTemperature, 1.2f);
  EXPECT_EQ(f.current.humidity, 81);
  EXPECT_EQ(f.current.weatherCode, 61);
  EXPECT_FLOAT_EQ(f.current.windSpeed, 12.5f);
  EXPECT_EQ(f.current.windDirection, 225);
  EXPECT_FALSE(f.current.isDay);
  EXPECT_FLOAT_EQ(f.current.precipitation, 0.3f);
}

TEST(WeatherParser, ParsesHourlyAndKeepsNullAligned) {
  Forecast f;
  ASSERT_TRUE(parseForecast(kForecast, f));
  ASSERT_EQ(f.hourly.size(), 3u);
  EXPECT_EQ(f.hourly[0].time, 1760601600);
  EXPECT_FLOAT_EQ(f.hourly[2].temperature, -0.5f);
  EXPECT_EQ(f.hourly[0].precipitationProbability, 10);
  EXPECT_EQ(f.hourly[1].precipitationProbability, 0);  // null
  EXPECT_EQ(f.hourly[2].precipitationProbability, 80);
  EXPECT_EQ(f.hourly[2].weatherCode, 71);
  EXPECT_FALSE(f.hourly[1].isDay);
  EXPECT_TRUE(f.hourly[2].isDay);
}

TEST(WeatherParser, ParsesDaily) {
  Forecast f;
  ASSERT_TRUE(parseForecast(kForecast, f));
  ASSERT_EQ(f.daily.size(), 2u);
  EXPECT_FLOAT_EQ(f.daily[0].tempMax, 7.2f);
  EXPECT_FLOAT_EQ(f.daily[1].tempMin, -1.3f);
  EXPECT_EQ(f.daily[0].sunrise, 1760587200);
  EXPECT_EQ(f.daily[1].sunset, 1760711700);
  EXPECT_FLOAT_EQ(f.daily[1].uvIndexMax, 2.0f);
}

TEST(WeatherParser, RejectsApiError) {
  Forecast f;
  EXPECT_FALSE(parseForecast(R"({"error":true,"reason":"Latitude must be in range of -90 to 90"})", f));
  EXPECT_TRUE(f.apiError);
  EXPECT_EQ(f.errorReason, "Latitude must be in range of -90 to 90");
}

TEST(WeatherParser, RejectsGarbageAndEmpty) {
  Forecast f;
  EXPECT_FALSE(parseForecast("", f));
  EXPECT_FALSE(parseForecast("not json", f));
  EXPECT_FALSE(parseForecast(R"({"latitude":1})", f));        // no data sections
  EXPECT_FALSE(parseForecast(R"({"current":{"temperature_2m":1)", f));  // truncated
}

TEST(WeatherParser, CapsHourlyCount) {
  std::string json = R"({"hourly":{"time":[)";
  for (int i = 0; i < 200; ++i) json += (i ? "," : "") + std::to_string(1760000000 + i * 3600);
  json += "]}}";
  Forecast f;
  ASSERT_TRUE(parseForecast(json, f));
  EXPECT_EQ(f.hourly.size(), MAX_HOURLY);
}

TEST(WeatherParser, ParsesCities) {
  const char* json = R"({"results":[
    {"id":524901,"name":"Moscow","latitude":55.75222,"longitude":37.61556,"elevation":144.0,
     "feature_code":"PPLC","country_code":"RU","timezone":"Europe/Moscow","country":"Russia","admin1":"Moscow"},
    {"id":5601538,"name":"Moscow","latitude":46.73239,"longitude":-117.00017,"country":"United States",
     "admin1":"Idaho","admin2":"Latah"}]
  ,"generationtime_ms":0.5})";
  std::vector<CityResult> r;
  ASSERT_TRUE(parseCities(json, r));
  ASSERT_EQ(r.size(), 2u);
  EXPECT_EQ(r[0].name, "Moscow");
  EXPECT_EQ(r[0].country, "Russia");
  EXPECT_NEAR(r[0].latitude, 55.75222f, 1e-3);
  EXPECT_EQ(r[1].admin1, "Idaho");
  EXPECT_NEAR(r[1].longitude, -117.00017f, 1e-3);
}

TEST(WeatherParser, NoCitiesIsSuccess) {
  std::vector<CityResult> r;
  EXPECT_TRUE(parseCities(R"({"generationtime_ms":0.4})", r));
  EXPECT_TRUE(r.empty());
}

TEST(WeatherParser, CyrillicCityNames) {
  std::vector<CityResult> r;
  ASSERT_TRUE(parseCities(R"({"results":[{"name":"Москва","latitude":55.7,"longitude":37.6,"country":"Россия"}]})", r));
  ASSERT_EQ(r.size(), 1u);
  EXPECT_EQ(r[0].name, "Москва");
  EXPECT_EQ(r[0].country, "Россия");
}

TEST(WeatherSettings, RoundTrip) {
  Settings s;
  s.hasLocation = true;
  s.latitude = 55.7522f;
  s.longitude = 37.6156f;
  s.locationName = "Москва, Россия";
  s.tempUnit = TempUnit::Fahrenheit;
  s.windUnit = WindUnit::Knots;
  s.precipUnit = PrecipUnit::Inch;
  s.setForecastDays(5);

  const Settings t = parseSettings(serializeSettings(s));
  EXPECT_TRUE(t.hasLocation);
  EXPECT_NEAR(t.latitude, 55.7522f, 1e-3);
  EXPECT_NEAR(t.longitude, 37.6156f, 1e-3);
  EXPECT_EQ(t.locationName, "Москва, Россия");
  EXPECT_EQ(t.tempUnit, TempUnit::Fahrenheit);
  EXPECT_EQ(t.windUnit, WindUnit::Knots);
  EXPECT_EQ(t.precipUnit, PrecipUnit::Inch);
  EXPECT_EQ(t.forecastDays, 5);
}

TEST(WeatherSettings, DefaultsAndBadValues) {
  const Settings s = parseSettings("junk\nlat=abc\ndays=99\nwind=7\nunknown=1\n");
  EXPECT_FALSE(s.hasLocation);
  EXPECT_EQ(s.forecastDays, 3);
  EXPECT_EQ(s.windUnit, WindUnit::Kmh);
}

TEST(WeatherSettings, CorruptCoordinatesDropLocation) {
  const Settings s = parseSettings("located=1\nlat=123.0\nlon=10\n");
  EXPECT_FALSE(s.hasLocation);
}

TEST(WeatherSettings, NameCleaned) {
  Settings s;
  s.hasLocation = true;
  s.locationName = std::string("A\nB\x01") + std::string(100, 'x');
  const Settings t = parseSettings(serializeSettings(s));
  EXPECT_EQ(t.locationName.size(), MAX_LOCATION_NAME);
  EXPECT_EQ(t.locationName.rfind("AB", 0), 0u);
}

TEST(WeatherSettings, ForecastUrl) {
  Settings s;
  s.hasLocation = true;
  s.latitude = 55.7522f;
  s.longitude = 37.6156f;
  s.tempUnit = TempUnit::Fahrenheit;
  s.windUnit = WindUnit::Ms;
  s.setForecastDays(4);
  const std::string url = buildForecastUrl(s);
  EXPECT_NE(url.find("latitude=55.7522"), std::string::npos);
  EXPECT_NE(url.find("longitude=37.6156"), std::string::npos);
  EXPECT_NE(url.find("temperature_unit=fahrenheit"), std::string::npos);
  EXPECT_NE(url.find("wind_speed_unit=ms"), std::string::npos);
  EXPECT_NE(url.find("forecast_days=4"), std::string::npos);
  EXPECT_NE(url.find("timeformat=unixtime"), std::string::npos);
  EXPECT_EQ(url.rfind("https://api.open-meteo.com/", 0), 0u);
}

TEST(WeatherSettings, GeocodingUrlEncodes) {
  const std::string url = buildGeocodingUrl("Санкт Петербург", "ru");
  EXPECT_NE(url.find("name=%D0%A1"), std::string::npos);
  EXPECT_NE(url.find("%20"), std::string::npos);
  EXPECT_NE(url.find("language=ru"), std::string::npos);
  EXPECT_EQ(url.find(' '), std::string::npos);
}

TEST(WeatherSettings, SignatureChangesWithRequest) {
  Settings a, b;
  a.hasLocation = b.hasLocation = true;
  a.latitude = b.latitude = 10;
  a.longitude = b.longitude = 20;
  EXPECT_EQ(requestSignature(a), requestSignature(b));
  b.tempUnit = TempUnit::Fahrenheit;
  EXPECT_NE(requestSignature(a), requestSignature(b));
}

TEST(WeatherSettings, WindDirectionLabels) {
  EXPECT_STREQ(windDirectionLabel(0), "N");
  EXPECT_STREQ(windDirectionLabel(359), "N");
  EXPECT_STREQ(windDirectionLabel(90), "E");
  EXPECT_STREQ(windDirectionLabel(225), "SW");
  EXPECT_STREQ(windDirectionLabel(-45), "NW");
}

TEST(WeatherCache, RoundTripAndFreshness) {
  const std::string text = wrapCache("sig|a", 1760000000, "{\"x\":1}\n");
  std::string sig, json;
  time_t at = 0;
  ASSERT_TRUE(unwrapCache(text, sig, at, json));
  EXPECT_EQ(sig, "sig|a");
  EXPECT_EQ(at, 1760000000);
  EXPECT_EQ(json, "{\"x\":1}\n");
  EXPECT_TRUE(isFresh(at, at + 10));
  EXPECT_FALSE(isFresh(at, at + CACHE_TTL_SECONDS));
  EXPECT_FALSE(isFresh(at, at - 5));  // clock went backwards
}

TEST(WeatherCache, RejectsMalformed) {
  std::string sig, json;
  time_t at = 0;
  EXPECT_FALSE(unwrapCache("", sig, at, json));
  EXPECT_FALSE(unwrapCache("sig\n", sig, at, json));
  EXPECT_FALSE(unwrapCache("sig\nabc\n{}", sig, at, json));
  EXPECT_FALSE(unwrapCache("sig\n123\n", sig, at, json));
}

#include "WeatherIcons.h"

TEST(WeatherIcons, CodeMapping) {
  EXPECT_EQ(iconForCode(0, true), IconType::ClearDay);
  EXPECT_EQ(iconForCode(0, false), IconType::ClearNight);
  EXPECT_EQ(iconForCode(2, false), IconType::PartlyCloudyNight);
  EXPECT_EQ(iconForCode(63, true), IconType::Rain);
  EXPECT_EQ(iconForCode(81, true), IconType::Rain);
  EXPECT_EQ(iconForCode(75, true), IconType::Snow);
  EXPECT_EQ(iconForCode(96, true), IconType::Thunderstorm);
  EXPECT_EQ(iconForCode(1234, true), IconType::Overcast);
}

TEST(WeatherIcons, EveryIconHasInk) {
  for (int t = 0; t <= static_cast<int>(IconType::Thunderstorm); ++t) {
    const uint8_t* bmp = iconBitmap(static_cast<IconType>(t));
    int ink = 0;
    for (int y = 0; y < WEATHER_ICON_SIZE; ++y)
      for (int x = 0; x < WEATHER_ICON_SIZE; ++x) ink += iconPixelSet(bmp, x, y) ? 1 : 0;
    EXPECT_GT(ink, 100) << "icon " << t;
    EXPECT_LT(ink, WEATHER_ICON_SIZE * WEATHER_ICON_SIZE / 2) << "icon " << t;
  }
}

#include "WeatherGraph.h"

TEST(WeatherGraph, AxisPadsAndKeepsMinimumSpan) {
  auto a = weather::niceAxis(3.2f, 11.7f);
  EXPECT_EQ(a.min, 2);
  EXPECT_EQ(a.max, 13);
  auto flat = weather::niceAxis(5.0f, 5.0f);
  EXPECT_GE(flat.max - flat.min, 4);
  EXPECT_LE(flat.min, 4);
  EXPECT_GE(flat.max, 6);
  auto neg = weather::niceAxis(-7.5f, -2.1f);
  EXPECT_EQ(neg.min, -9);
  EXPECT_EQ(neg.max, -1);
}

TEST(WeatherGraph, ScaleYMapsAxisToRows) {
  const weather::AxisRange ax{0, 10};
  EXPECT_EQ(weather::scaleY(10.0f, ax, 100, 200), 100);
  EXPECT_EQ(weather::scaleY(0.0f, ax, 100, 200), 200);
  EXPECT_EQ(weather::scaleY(5.0f, ax, 100, 200), 150);
  EXPECT_EQ(weather::scaleY(99.0f, ax, 100, 200), 100);  // clamped
  EXPECT_EQ(weather::scaleY(-9.0f, ax, 100, 200), 200);
}

TEST(WeatherGraph, FirstCurrentHourSkipsPast) {
  std::vector<weather::Hourly> h(5);
  for (int i = 0; i < 5; ++i) h[i].time = 1000000 + i * 3600;
  EXPECT_EQ(weather::firstCurrentHour(h, 1000000 + 100), 0u);
  EXPECT_EQ(weather::firstCurrentHour(h, 1000000 + 3600 + 5), 1u);  // inside hour 1: hour 1 still current
  EXPECT_EQ(weather::firstCurrentHour(h, 1000000 + 3600 * 10), 5u);
}
