#pragma once
#include <cstdint>

// Layout switches the reader app sets before sections are built. They are part of the section cache key
// (packed into the header's image-rendering byte), so changing one rebuilds cached chapters.
namespace epub_layout {

// How images inside headings (chapter ornaments) are spaced and aligned.
enum OrnamentMode : uint8_t {
  ORNAMENT_STANDARD = 0,        // previous behaviour
  ORNAMENT_COMPACT = 1,         // no repeated heading drop margin, half-line gap under the ornament
  ORNAMENT_COMPACT_ALIGNED = 2  // compact + honour an explicit text-align left/right of the container
};
constexpr uint8_t ORNAMENT_MODE_COUNT = 3;

inline uint8_t ornamentMode = ORNAMENT_COMPACT;

inline uint8_t sanitizeOrnamentMode(const uint8_t v) { return v < ORNAMENT_MODE_COUNT ? v : ORNAMENT_COMPACT; }

// Image-rendering byte as stored in the section header: bits 0-1 image rendering, bits 2-3 ornament mode.
inline uint8_t packImageRendering(const uint8_t imageRendering) {
  return static_cast<uint8_t>((imageRendering & 0x03) | (sanitizeOrnamentMode(ornamentMode) << 2));
}

// Horizontal position of a block image. `alignment` is the CssTextAlign value of the container
// (Left = 1, Center = 2, Right = 3), used only in ORNAMENT_COMPACT_ALIGNED and when explicitly set.
inline int imageX(const int viewportWidth, const int imageWidth, const int leftInset, const int rightInset,
                  const uint8_t alignment, const bool alignDefined) {
  if (ornamentMode == ORNAMENT_COMPACT_ALIGNED && alignDefined) {
    if (alignment == 1) return leftInset;
    if (alignment == 3) return viewportWidth - imageWidth - rightInset;
  }
  return (viewportWidth - imageWidth) / 2;
}

}  // namespace epub_layout
