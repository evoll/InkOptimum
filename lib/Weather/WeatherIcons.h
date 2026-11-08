#pragma once

#include <cstdint>

#include "WeatherIconsLarge.h"

// Weather icons: Weather Icons by Erik Flowers (SIL OFL 1.1), rendered to 64x64 1-bit bitmaps.
// Bit value 0 = black (drawn), 1 = background.
namespace weather {

enum class IconType : uint8_t {
  ClearDay,
  ClearNight,
  PartlyCloudyDay,
  PartlyCloudyNight,
  Overcast,
  Fog,
  Drizzle,
  Rain,
  Snow,
  Thunderstorm,
};

// Maps a WMO weather code (as used by Open-Meteo) plus day/night to an icon.
inline IconType iconForCode(int wmoCode, bool isDay) {
  switch (wmoCode) {
    case 0: return isDay ? IconType::ClearDay : IconType::ClearNight;
    case 1:
    case 2: return isDay ? IconType::PartlyCloudyDay : IconType::PartlyCloudyNight;
    case 3: return IconType::Overcast;
    case 45:
    case 48: return IconType::Fog;
    case 51:
    case 53:
    case 55:
    case 56:
    case 57: return IconType::Drizzle;
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
    case 80:
    case 81:
    case 82: return IconType::Rain;
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86: return IconType::Snow;
    case 95:
    case 96:
    case 99: return IconType::Thunderstorm;
    default: return IconType::Overcast;
  }
}

inline const uint8_t* iconBitmap(IconType type) {
  switch (type) {
    case IconType::ClearDay: return WI_LARGE_CLEAR_DAY;
    case IconType::ClearNight: return WI_LARGE_CLEAR_NIGHT;
    case IconType::PartlyCloudyDay: return WI_LARGE_PARTLY_CLOUDY_DAY;
    case IconType::PartlyCloudyNight: return WI_LARGE_PARTLY_CLOUDY_NIGHT;
    case IconType::Overcast: return WI_LARGE_OVERCAST;
    case IconType::Fog: return WI_LARGE_FOG;
    case IconType::Drizzle: return WI_LARGE_DRIZZLE;
    case IconType::Rain: return WI_LARGE_RAIN;
    case IconType::Snow: return WI_LARGE_SNOW;
    case IconType::Thunderstorm: return WI_LARGE_THUNDERSTORM;
  }
  return WI_LARGE_OVERCAST;
}

// True when the pixel at (x, y) is black (drawn).
inline bool iconPixelSet(const uint8_t* bitmap, int x, int y) {
  const int stride = (WEATHER_ICON_SIZE + 7) / 8;
  return (bitmap[y * stride + (x >> 3)] & (0x80 >> (x & 7))) == 0;
}

}  // namespace weather
