#pragma once
// Pure validation rules for fonts uploaded through the web page (.cpfont bitmap fonts and TrueType/OpenType
// vector fonts). Header-only so it can be unit-tested natively.
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <strings.h>

namespace font_upload {

enum class Kind : uint8_t { None, Cpfont, Vector };

inline bool safePathChar(const char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ' ' || c == '.' || c == '(' ||
         c == ')';
}

// Classifies a bare filename ("Bookerly-Bold.ttf"). Returns None for anything unsafe or with another extension.
inline Kind classify(const char* name) {
  if (name == nullptr || name[0] == '\0') return Kind::None;
  if (std::strstr(name, "..") != nullptr || std::strchr(name, '/') != nullptr || std::strchr(name, '\\') != nullptr) {
    return Kind::None;
  }
  const size_t n = std::strlen(name);
  if (n > 120) return Kind::None;
  size_t extLen = 0;
  Kind kind = Kind::None;
  if (n > 7 && strcasecmp(name + n - 7, ".cpfont") == 0) {
    extLen = 7;
    kind = Kind::Cpfont;
  } else if (n > 4 && (strcasecmp(name + n - 4, ".ttf") == 0 || strcasecmp(name + n - 4, ".otf") == 0 ||
                       strcasecmp(name + n - 4, ".ttc") == 0)) {
    extLen = 4;
    kind = Kind::Vector;
  }
  if (kind == Kind::None) return Kind::None;
  for (size_t i = 0; i < n - extLen; ++i) {
    if (!safePathChar(name[i])) return Kind::None;
  }
  return kind;
}

// Magic of the first upload chunk, at least 8 bytes of it.
inline bool magicMatches(const Kind kind, const uint8_t* buf, const size_t len) {
  if (buf == nullptr) return false;
  if (kind == Kind::Cpfont) return len >= 8 && std::memcmp(buf, "CPFONT\0\0", 8) == 0;
  if (kind == Kind::Vector) {
    if (len < 4) return false;
    return std::memcmp(buf, "\x00\x01\x00\x00", 4) == 0 ||  // TrueType
           std::memcmp(buf, "OTTO", 4) == 0 ||              // CFF OpenType
           std::memcmp(buf, "true", 4) == 0 ||              // Apple TrueType
           std::memcmp(buf, "ttcf", 4) == 0;                // collection
  }
  return false;
}

// Vector fonts only work on PSRAM boards; the server refuses them elsewhere with a clear error.
}  // namespace font_upload
