#include "WeatherParser.h"

#include <cstdlib>
#include <cstring>

#include "StreamingJsonParser.h"

namespace weather {
namespace {

constexpr size_t MAX_DEPTH = 6;
constexpr size_t NAME_LEN = 28;

struct Frame {
  bool isArray = false;
  char name[NAME_LEN] = {};  // key under which this container appeared ("" for the root)
};

enum class Mode : uint8_t { Forecast, Cities };

struct Ctx {
  Mode mode = Mode::Forecast;
  Forecast* forecast = nullptr;
  std::vector<CityResult>* cities = nullptr;

  Frame stack[MAX_DEPTH];
  size_t depth = 0;
  char lastKey[NAME_LEN] = {};
  size_t arrayIndex = 0;  // position inside the array currently being read
  bool sawSection = false;
  bool sawRoot = false;
  bool overflow = false;
  CityResult pendingCity;
};

Ctx* C(void* p) { return static_cast<Ctx*>(p); }

void copyName(char* dst, const char* src, size_t len) {
  const size_t n = len < NAME_LEN - 1 ? len : NAME_LEN - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

bool is(const char* a, const char* b) { return strcmp(a, b) == 0; }

const Frame* top(const Ctx* c) { return c->depth > 0 ? &c->stack[c->depth - 1] : nullptr; }
const Frame* parent(const Ctx* c) { return c->depth > 1 ? &c->stack[c->depth - 2] : nullptr; }

// ---- forecast -------------------------------------------------------------------------

void setCurrent(Current& cur, const char* key, double v) {
  if (is(key, "temperature_2m")) cur.temperature = static_cast<float>(v);
  else if (is(key, "apparent_temperature")) cur.apparentTemperature = static_cast<float>(v);
  else if (is(key, "relative_humidity_2m")) cur.humidity = static_cast<int>(v);
  else if (is(key, "weather_code")) cur.weatherCode = static_cast<int>(v);
  else if (is(key, "wind_speed_10m")) cur.windSpeed = static_cast<float>(v);
  else if (is(key, "wind_direction_10m")) cur.windDirection = static_cast<int>(v);
  else if (is(key, "precipitation")) cur.precipitation = static_cast<float>(v);
  else if (is(key, "uv_index")) cur.uvIndex = static_cast<float>(v);
  else if (is(key, "is_day")) cur.isDay = v != 0;
}

void setHourly(Hourly& h, const char* key, double v) {
  if (is(key, "time")) h.time = static_cast<time_t>(v);
  else if (is(key, "temperature_2m")) h.temperature = static_cast<float>(v);
  else if (is(key, "precipitation")) h.precipitation = static_cast<float>(v);
  else if (is(key, "precipitation_probability")) h.precipitationProbability = static_cast<int>(v);
  else if (is(key, "weather_code")) h.weatherCode = static_cast<int>(v);
  else if (is(key, "is_day")) h.isDay = v != 0;
}

void setDaily(Daily& d, const char* key, double v) {
  if (is(key, "time")) d.date = static_cast<time_t>(v);
  else if (is(key, "temperature_2m_max")) d.tempMax = static_cast<float>(v);
  else if (is(key, "temperature_2m_min")) d.tempMin = static_cast<float>(v);
  else if (is(key, "weather_code")) d.weatherCode = static_cast<int>(v);
  else if (is(key, "precipitation_sum")) d.precipSum = static_cast<float>(v);
  else if (is(key, "sunrise")) d.sunrise = static_cast<time_t>(v);
  else if (is(key, "sunset")) d.sunset = static_cast<time_t>(v);
  else if (is(key, "uv_index_max")) d.uvIndexMax = static_cast<float>(v);
}

// A numeric value (or `null`, passed with hasValue=false so array columns stay aligned).
void forecastValue(Ctx* c, double v, bool hasValue) {
  Forecast& f = *c->forecast;
  const Frame* t = top(c);
  if (t == nullptr) return;

  if (!t->isArray) {
    if (is(t->name, "current")) {
      if (hasValue) setCurrent(f.current, c->lastKey, v);
    } else if (t->name[0] == '\0' && is(c->lastKey, "utc_offset_seconds") && hasValue) {
      f.utcOffsetSeconds = static_cast<int>(v);
    }
    return;
  }

  const Frame* p = parent(c);
  if (p == nullptr) return;
  const size_t i = c->arrayIndex++;
  if (is(p->name, "hourly")) {
    if (i >= MAX_HOURLY) return;
    if (f.hourly.size() <= i) f.hourly.resize(i + 1);
    if (hasValue) setHourly(f.hourly[i], t->name, v);
  } else if (is(p->name, "daily")) {
    if (i >= MAX_DAILY) return;
    if (f.daily.size() <= i) f.daily.resize(i + 1);
    if (hasValue) setDaily(f.daily[i], t->name, v);
  }
}

// ---- callbacks ------------------------------------------------------------------------

void onKey(void* ctx, const char* key, size_t len) { copyName(C(ctx)->lastKey, key, len); }

void push(Ctx* c, bool isArray) {
  if (c->depth >= MAX_DEPTH) {
    c->overflow = true;
    return;
  }
  Frame& fr = c->stack[c->depth++];
  fr.isArray = isArray;
  copyName(fr.name, c->lastKey, strlen(c->lastKey));
  c->lastKey[0] = '\0';
  c->arrayIndex = 0;
}

void pop(Ctx* c) {
  if (c->depth > 0) c->depth--;
}

void onObjectStart(void* ctx) {
  Ctx* c = C(ctx);
  c->sawRoot = true;
  push(c, false);
  if (c->mode == Mode::Cities) {
    const Frame* p = parent(c);
    if (p != nullptr && p->isArray && is(p->name, "results")) c->pendingCity = CityResult{};
  } else {
    const Frame* t = top(c);
    if (t != nullptr && (is(t->name, "current") || is(t->name, "hourly") || is(t->name, "daily"))) {
      c->sawSection = true;
    }
  }
}

void onObjectEnd(void* ctx) {
  Ctx* c = C(ctx);
  if (c->mode == Mode::Cities) {
    const Frame* p = parent(c);
    if (p != nullptr && p->isArray && is(p->name, "results") && c->cities->size() < MAX_CITIES &&
        !c->pendingCity.name.empty()) {
      c->cities->push_back(c->pendingCity);
    }
  }
  pop(c);
}

void onArrayStart(void* ctx) { push(C(ctx), true); }
void onArrayEnd(void* ctx) { pop(C(ctx)); }

void onNumber(void* ctx, const char* value, size_t) {
  Ctx* c = C(ctx);
  const double v = strtod(value, nullptr);
  if (c->mode == Mode::Forecast) {
    forecastValue(c, v, true);
  } else {
    const Frame* t = top(c);
    const Frame* p = parent(c);
    if (t != nullptr && !t->isArray && p != nullptr && is(p->name, "results")) {
      if (is(c->lastKey, "latitude")) c->pendingCity.latitude = static_cast<float>(v);
      else if (is(c->lastKey, "longitude")) c->pendingCity.longitude = static_cast<float>(v);
    }
  }
}

void onNull(void* ctx) {
  Ctx* c = C(ctx);
  if (c->mode == Mode::Forecast) forecastValue(c, 0, false);
}

void onBool(void* ctx, bool value) {
  Ctx* c = C(ctx);
  if (c->mode != Mode::Forecast) return;
  const Frame* t = top(c);
  if (t != nullptr && t->name[0] == '\0' && !t->isArray && is(c->lastKey, "error")) {
    c->forecast->apiError = value;
    return;
  }
  forecastValue(c, value ? 1 : 0, true);
}

void onString(void* ctx, const char* value, size_t len) {
  Ctx* c = C(ctx);
  const Frame* t = top(c);
  if (t == nullptr) return;
  if (c->mode == Mode::Forecast) {
    if (t->name[0] != '\0' || t->isArray) return;
    if (is(c->lastKey, "timezone")) c->forecast->timezone.assign(value, len);
    else if (is(c->lastKey, "reason")) c->forecast->errorReason.assign(value, len);
  } else {
    const Frame* p = parent(c);
    if (t->isArray || p == nullptr || !is(p->name, "results")) return;
    if (is(c->lastKey, "name")) c->pendingCity.name.assign(value, len);
    else if (is(c->lastKey, "country")) c->pendingCity.country.assign(value, len);
    else if (is(c->lastKey, "admin1")) c->pendingCity.admin1.assign(value, len);
  }
}

bool run(Ctx& c, const std::string& json) {
  JsonCallbacks cb{&c, onKey, onString, onNumber, onBool, onNull, onObjectStart, onObjectEnd, onArrayStart, onArrayEnd};
  StreamingJsonParser parser(cb);
  parser.feed(json.data(), json.size());
  return !parser.hasError() && !c.overflow && c.sawRoot && c.depth == 0;
}

}  // namespace

bool parseForecast(const std::string& json, Forecast& out) {
  out = Forecast{};
  Ctx c;
  c.mode = Mode::Forecast;
  c.forecast = &out;
  if (!run(c, json)) return false;
  if (out.apiError) return false;
  return c.sawSection;
}

bool parseCities(const std::string& json, std::vector<CityResult>& out) {
  out.clear();
  Ctx c;
  c.mode = Mode::Cities;
  c.cities = &out;
  return run(c, json);
}

}  // namespace weather
