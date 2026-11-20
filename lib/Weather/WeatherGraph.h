#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ctime>
#include <vector>

#include "WeatherData.h"

// Pure helpers for the 24 h graph on the weather screen (natively tested, no drawing here).
namespace weather {

struct AxisRange {
  int min = 0;
  int max = 1;
};

// Whole-degree axis a little wider than the data, never narrower than `minSpan` degrees so a flat
// day does not turn into a staircase.
inline AxisRange niceAxis(const float lo, const float hi, const int minSpan = 4) {
  int a = static_cast<int>(std::floor(lo)) - 1;
  int b = static_cast<int>(std::ceil(hi)) + 1;
  while (b - a < minSpan) {
    --a;
    if (b - a < minSpan) ++b;
  }
  return {a, b};
}

// Pixel row for a value; `top` is the row of axis.max, `bottom` the row of axis.min.
inline int scaleY(const float value, const AxisRange& axis, const int top, const int bottom) {
  const float span = static_cast<float>(std::max(1, axis.max - axis.min));
  const float t = std::clamp((value - static_cast<float>(axis.min)) / span, 0.0f, 1.0f);
  return bottom - static_cast<int>(std::lround(t * static_cast<float>(bottom - top)));
}

// Index of the first hourly entry that has not fully passed yet (the hour containing `now` counts).
inline size_t firstCurrentHour(const std::vector<Hourly>& hourly, const time_t now) {
  size_t i = 0;
  while (i < hourly.size() && hourly[i].time + 3600 <= now) ++i;
  return i;
}

}  // namespace weather
