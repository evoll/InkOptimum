#pragma once

#include <cstdint>

// Glyph keys: how a shaped glyph travels through text that is otherwise UTF-8.
//
// A bitmap font is addressed by Unicode code point, and so is everything that
// measures or draws text. An OpenType shaper's output is glyph IDs, which no
// code point stands for (a conjunct, a reordered vowel sign), and a mark's
// position from GPOS. The shaped word is therefore written as a string of
// KEYS, one per glyph, each a 31-bit "code point" above Unicode's range:
//
//   bit 30       always set, which is what makes it a key (real code points
//                stay under 0x110000)
//   bit 29       the glyph advances by nothing (a GDEF mark, as in HarfBuzz)
//   bits 23-28   y offset, signed, in 1/32 em
//   bits 16-22   x offset, signed, in 1/32 em
//   bits 0-15    glyph ID
//
// The string is "extended UTF-8": a key is the six-byte form of its value
// (lead byte 0xFD), which utf8NextCodepoint() reads and utf8AppendCodepoint()
// writes. Everything that walks text by code point then carries glyphs without
// knowing it, and only the font that holds the glyph IDs gives a key meaning
// (TtfEpdFont::faultGlyph). Every glyph advances by its font's own advance
// unless bit 29 says it is a mark.
//
// Offsets in 1/32 em are good to 1/64 em (a quarter pixel at 16 px); x reaches
// +-2 em (a mark attached across a wide conjunct), y +-1 em.
namespace glyphkey {

constexpr uint32_t FLAG = 0x40000000u;
constexpr uint32_t NO_ADVANCE = 0x20000000u;
constexpr int OFFSET_UNITS_PER_EM = 32;

constexpr bool isKey(const uint32_t cp) { return cp >= FLAG && cp < 0x80000000u; }
constexpr uint32_t gid(const uint32_t key) { return key & 0xFFFFu; }

constexpr bool noAdvance(const uint32_t key) { return (key & NO_ADVANCE) != 0; }
constexpr int xOffsetUnits(const uint32_t key) { return static_cast<int>((key >> 16) & 0x7Fu) - (((key >> 16) & 0x40u) ? 128 : 0); }
constexpr int yOffsetUnits(const uint32_t key) { return static_cast<int>((key >> 23) & 0x3Fu) - (((key >> 23) & 0x20u) ? 64 : 0); }

constexpr int clampX(const int v) { return v < -64 ? -64 : v > 63 ? 63 : v; }
constexpr int clampY(const int v) { return v < -32 ? -32 : v > 31 ? 31 : v; }

// `x` and `y` in 1/32 em, clamped to their fields.
constexpr uint32_t make(const uint32_t glyph, const int x, const int y, const bool mark = false) {
  return FLAG | (mark ? NO_ADVANCE : 0u) | (static_cast<uint32_t>(clampY(y) & 0x3F) << 23) |
         (static_cast<uint32_t>(clampX(x) & 0x7F) << 16) | (glyph & 0xFFFFu);
}

}  // namespace glyphkey
