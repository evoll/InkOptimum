#pragma once

#include <cstdint>

#include "Utf8.h"

// Line-break rules for Chinese, Japanese and Korean text (kinsoku shori, simplified from JIS X 4051 and
// the Chinese GB/T 15834 practice): a closing bracket or a stop never starts a line, an opening
// bracket never ends one, and a two-character dash or ellipsis is not torn apart. Every ideograph
// otherwise allows a break on either side, which Utf8.h's utf8IsCjkBreakable() already expresses.
namespace cjk_break {

// Characters that must not begin a line.
inline bool noLineStart(const uint32_t cp) {
  switch (cp) {
    case 0x3001: case 0x3002:                                  // ideographic comma, full stop
    case 0xFF0C: case 0xFF0E: case 0xFF1A: case 0xFF1B:       // fullwidth , . : ;
    case 0xFF01: case 0xFF1F:                                  // fullwidth ! ?
    case 0xFF09: case 0xFF3D: case 0xFF5D:                     // fullwidth ) ] }
    case 0x3009: case 0x300B: case 0x300D: case 0x300F:        // closing angle / double angle / corner brackets
    case 0x3011: case 0x3015: case 0x3017: case 0x3019: case 0x301F:  // closing lenticular / tortoise / white
    case 0x2019: case 0x201D:                                  // closing single / double quotation mark
    case 0x30FB: case 0xFF65:                                  // katakana middle dot
    case 0x30FC:                                               // prolonged sound mark
    case 0x3005: case 0x303B: case 0x309D: case 0x309E: case 0x30FD: case 0x30FE:  // iteration marks
    case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049: case 0x3063:  // small hiragana
    case 0x3083: case 0x3085: case 0x3087: case 0x308E:
    case 0x30A1: case 0x30A3: case 0x30A5: case 0x30A7: case 0x30A9: case 0x30C3:  // small katakana
    case 0x30E3: case 0x30E5: case 0x30E7: case 0x30EE: case 0x30F5: case 0x30F6:
    case 0xFF05:                                               // fullwidth percent
    case 0x2025: case 0x2026:                                  // two-dot and horizontal ellipsis
      return true;
    default:
      return false;
  }
}

// Characters that must not end a line.
inline bool noLineEnd(const uint32_t cp) {
  switch (cp) {
    case 0x2018: case 0x201C:                                  // opening single / double quotation mark
    case 0xFF08: case 0xFF3B: case 0xFF5B:                     // fullwidth ( [ {
    case 0x3008: case 0x300A: case 0x300C: case 0x300E:        // opening angle / double angle / corner brackets
    case 0x3010: case 0x3014: case 0x3016: case 0x3018: case 0x301D:  // opening lenticular / tortoise / white
      return true;
    default:
      return false;
  }
}

// ASCII closers and stops that follow CJK text belong to it as well; the same for an ASCII opener
// that precedes it.
inline bool asciiCloser(const uint32_t cp) {
  return cp == ')' || cp == ']' || cp == '}' || cp == ',' || cp == '.' || cp == '!' || cp == '?' || cp == ':' ||
         cp == ';';
}
inline bool asciiOpener(const uint32_t cp) { return cp == '(' || cp == '[' || cp == '{'; }

inline bool isPairedDashOrEllipsis(const uint32_t a, const uint32_t b) {
  return a == b && (a == 0x2014 || a == 0x2015 || a == 0x2025 || a == 0x2026);
}

// True when a line may break between `prev` and `next`. Pairs of characters that involve no CJK
// character are none of this module's business and are allowed (the caller's other rules decide).
inline bool breakAllowed(const uint32_t prev, const uint32_t next) {
  if (isPairedDashOrEllipsis(prev, next)) return false;
  const bool prevCjk = utf8IsCjkBreakable(prev);
  const bool nextCjk = utf8IsCjkBreakable(next);
  if (!prevCjk && !nextCjk) return true;
  if (noLineStart(next)) return false;
  if (noLineEnd(prev)) return false;
  if (prevCjk && asciiCloser(next)) return false;
  if (nextCjk && asciiOpener(prev)) return false;
  return true;
}

}  // namespace cjk_break
