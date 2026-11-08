#pragma once

#include "CssStyle.h"

// font-size declared too small to read: the `font-size:1px` span a watermarking service hides in
// the prose. The line is a tenth of the body: 1.6 px of the CSS 16 px medium, 1.2 pt of 12 pt.
// Watermarks sit at 1px (0.0625) or 1pt (0.083). Nothing meant to be read comes near it: the
// smallest keyword (xx-small) is 0.6, the sup/sub default 0.5, a footnote marker 0.6-0.8. A tenth
// itself is left to the renderer's own floor. A zero size is not "small": it is the zero-size
// container idiom whose children re-declare a size, so it is never reported.
namespace css_font_size {

inline constexpr float kUnreadableMultiplier = 0.1f;

// Size relative to the body text, or a negative number when it cannot be told.
inline float multiplierOf(const CssLength& length) {
  switch (length.unit) {
    case CssUnit::Pixels:
      return length.value / 16.0f;
    case CssUnit::Points:
      return length.value / 12.0f;
    case CssUnit::Em:
    case CssUnit::Rem:
      return length.value;
    case CssUnit::Percent:
      return length.value / 100.0f;
  }
  return -1.0f;
}

inline bool isUnreadablySmall(const CssLength& length) {
  if (!(length.value > 0.0f)) return false;
  const float multiplier = multiplierOf(length);
  return multiplier >= 0.0f && multiplier < kUnreadableMultiplier;
}

}  // namespace css_font_size
