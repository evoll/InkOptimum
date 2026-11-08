#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// XTC progress is the one-based page number divided by the page count. Pick the nearest page for a target
// percent and keep 100% on the last bitmap.
inline uint32_t xtcPageForPercent(const float percent, const uint32_t pageCount) {
  if (pageCount == 0) return 0;
  const float clamped = std::clamp(percent, 0.0f, 100.0f);
  const auto oneBased = static_cast<uint32_t>(std::lround(clamped / 100.0f * static_cast<float>(pageCount)));
  return std::clamp<uint32_t>(oneBased, 1, pageCount) - 1;
}
