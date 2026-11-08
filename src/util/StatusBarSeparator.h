#pragma once
// Separator choice for the centered reader status bar (ported from CrossInk).
// Pure header so it can be unit-tested natively.
#include <cstdint>
#include <string>
#include <vector>

namespace StatusBarSeparator {

enum Value : uint8_t { Bullet = 0, Dash, Pipe, MiddleDot, Count };

inline const char* glyph(const uint8_t value) {
  static const char* const GLYPHS[Count] = {"\xE2\x80\xA2", "\xE2\x80\x93", "|", "\xC2\xB7"};
  return GLYPHS[value < Count ? value : Bullet];
}

inline uint8_t sanitize(const uint8_t value) { return value < Count ? value : Bullet; }

// Joins non-empty parts as "a <sep> b <sep> c" (single spaces around the glyph).
inline std::string join(const std::vector<std::string>& parts, const uint8_t separator) {
  std::string out;
  for (const auto& part : parts) {
    if (part.empty()) continue;
    if (!out.empty()) {
      out += ' ';
      out += glyph(separator);
      out += ' ';
    }
    out += part;
  }
  return out;
}

}  // namespace StatusBarSeparator
