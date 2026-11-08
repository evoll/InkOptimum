// MobiFormat.h - recognising Mobipocket / Kindle books and small shared helpers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include <PathUtil.h>

namespace mobi {

// ".mobi", ".azw", ".azw3" or ".prc" (ASCII case-insensitive).
inline bool hasMobiExtension(std::string_view path) {
  return fictionbook::endsWithIgnoreCase(path, ".mobi") || fictionbook::endsWithIgnoreCase(path, ".azw") ||
         fictionbook::endsWithIgnoreCase(path, ".azw3") || fictionbook::endsWithIgnoreCase(path, ".prc");
}

// A Palm database whose type/creator is BOOKMOBI (Mobipocket, KF8) or TEXtREAd
// (PalmDOC). Needs at least the first 68 bytes of the file.
inline bool looksLikeMobi(const uint8_t* head, size_t len) {
  if (len < 68) return false;
  return memcmp(head + 60, "BOOKMOBI", 8) == 0 || memcmp(head + 60, "TEXtREAd", 8) == 0;
}

inline uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
inline uint32_t be32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

// Kindle's base-32 numbers (digits 0-9 then A-V), as used in kindle:embed and
// kindle:pos references. Consumes [begin, end) and stops at the first other char.
inline uint32_t parseBase32(const char* begin, const char* end, const char** stop = nullptr) {
  uint32_t value = 0;
  const char* p = begin;
  for (; p < end; ++p) {
    const char c = *p;
    uint32_t v;
    if (c >= '0' && c <= '9') {
      v = static_cast<uint32_t>(c - '0');
    } else if (c >= 'A' && c <= 'V') {
      v = static_cast<uint32_t>(c - 'A') + 10;
    } else if (c >= 'a' && c <= 'v') {
      v = static_cast<uint32_t>(c - 'a') + 10;
    } else {
      break;
    }
    value = (value << 5) | v;
  }
  if (stop) *stop = p;
  return value;
}

}  // namespace mobi
