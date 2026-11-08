#pragma once

#include <I18n.h>

#include <cstdio>
#include <ctime>

// Localised text helpers for the weather screens.
namespace weather_text {

inline StrId descriptionId(const int code) {
  switch (code) {
    case 0: return StrId::STR_WEATHER_DESC_CLEAR_SKY;
    case 1: return StrId::STR_WEATHER_DESC_MAINLY_CLEAR;
    case 2: return StrId::STR_WEATHER_DESC_PARTLY_CLOUDY;
    case 3: return StrId::STR_WEATHER_DESC_OVERCAST;
    case 45: return StrId::STR_WEATHER_DESC_FOG;
    case 48: return StrId::STR_WEATHER_DESC_RIME_FOG;
    case 51: return StrId::STR_WEATHER_DESC_LIGHT_DRIZZLE;
    case 53: return StrId::STR_WEATHER_DESC_DRIZZLE;
    case 55: return StrId::STR_WEATHER_DESC_DENSE_DRIZZLE;
    case 56: return StrId::STR_WEATHER_DESC_FREEZING_DRIZZLE;
    case 57: return StrId::STR_WEATHER_DESC_DENSE_FREEZING_DRIZZLE;
    case 61: return StrId::STR_WEATHER_DESC_SLIGHT_RAIN;
    case 63: return StrId::STR_WEATHER_DESC_MODERATE_RAIN;
    case 65: return StrId::STR_WEATHER_DESC_HEAVY_RAIN;
    case 66: return StrId::STR_WEATHER_DESC_FREEZING_RAIN;
    case 67: return StrId::STR_WEATHER_DESC_HEAVY_FREEZING_RAIN;
    case 71: return StrId::STR_WEATHER_DESC_SLIGHT_SNOW;
    case 73: return StrId::STR_WEATHER_DESC_MODERATE_SNOW;
    case 75: return StrId::STR_WEATHER_DESC_HEAVY_SNOW;
    case 77: return StrId::STR_WEATHER_DESC_SNOW_GRAINS;
    case 80: return StrId::STR_WEATHER_DESC_SLIGHT_SHOWERS;
    case 81: return StrId::STR_WEATHER_DESC_MODERATE_SHOWERS;
    case 82: return StrId::STR_WEATHER_DESC_VIOLENT_SHOWERS;
    case 85: return StrId::STR_WEATHER_DESC_SNOW_SHOWERS;
    case 86: return StrId::STR_WEATHER_DESC_HEAVY_SNOW_SHOWERS;
    case 95: return StrId::STR_WEATHER_DESC_THUNDERSTORM;
    case 96: return StrId::STR_WEATHER_DESC_THUNDERSTORM_HAIL;
    case 99: return StrId::STR_WEATHER_DESC_THUNDERSTORM_HEAVY_HAIL;
    default: return StrId::STR_WEATHER_DESC_UNKNOWN;
  }
}

inline const char* weekdayName(const int tmWday) {  // 0 = Sunday
  switch (((tmWday % 7) + 7) % 7) {
    case 0: return tr(STR_WEATHER_DAY_SUN);
    case 1: return tr(STR_WEATHER_DAY_MON);
    case 2: return tr(STR_WEATHER_DAY_TUE);
    case 3: return tr(STR_WEATHER_DAY_WED);
    case 4: return tr(STR_WEATHER_DAY_THU);
    case 5: return tr(STR_WEATHER_DAY_FRI);
    default: return tr(STR_WEATHER_DAY_SAT);
  }
}

inline const char* monthName(const int tmMon) {  // 0 = January
  switch (((tmMon % 12) + 12) % 12) {
    case 0: return tr(STR_WEATHER_MONTH_JAN);
    case 1: return tr(STR_WEATHER_MONTH_FEB);
    case 2: return tr(STR_WEATHER_MONTH_MAR);
    case 3: return tr(STR_WEATHER_MONTH_APR);
    case 4: return tr(STR_WEATHER_MONTH_MAY);
    case 5: return tr(STR_WEATHER_MONTH_JUN);
    case 6: return tr(STR_WEATHER_MONTH_JUL);
    case 7: return tr(STR_WEATHER_MONTH_AUG);
    case 8: return tr(STR_WEATHER_MONTH_SEP);
    case 9: return tr(STR_WEATHER_MONTH_OCT);
    case 10: return tr(STR_WEATHER_MONTH_NOV);
    default: return tr(STR_WEATHER_MONTH_DEC);
  }
}

// Breaks a UTC timestamp into the location's local calendar fields using the API's UTC offset.
inline bool localTime(const time_t utc, const int utcOffsetSeconds, tm& out) {
  const time_t shifted = utc + utcOffsetSeconds;
  return gmtime_r(&shifted, &out) != nullptr;
}

inline void formatClock(const time_t utc, const int utcOffsetSeconds, const bool twelveHour, char* buf,
                        const size_t size) {
  tm t{};
  if (!localTime(utc, utcOffsetSeconds, t)) {
    snprintf(buf, size, "--:--");
    return;
  }
  if (twelveHour) {
    const int h = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
    snprintf(buf, size, "%d:%02d%s", h, t.tm_min, t.tm_hour < 12 ? "a" : "p");
  } else {
    snprintf(buf, size, "%02d:%02d", t.tm_hour, t.tm_min);
  }
}

}  // namespace weather_text
