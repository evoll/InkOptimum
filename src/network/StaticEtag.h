#pragma once
// ETag helpers for the flash-baked web pages. The content never changes within one firmware image, so a hash of
// the bytes is a stable validator; a matching If-None-Match gets a 304 instead of the whole page.
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

namespace static_etag {

inline std::string make(const uint8_t* data, const size_t len) {
  uint32_t h = 2166136261u;  // FNV-1a, 32 bit
  for (size_t i = 0; i < len; ++i) {
    h ^= data[i];
    h *= 16777619u;
  }
  char buf[24];
  snprintf(buf, sizeof(buf), "\"%08x-%x\"", static_cast<unsigned>(h), static_cast<unsigned>(len));
  return buf;
}

// True when `ifNoneMatch` (a header value: "*", one tag, or a comma list; weak "W/" prefixes allowed)
// matches `etag`.
inline bool matches(const std::string& ifNoneMatch, const std::string& etag) {
  if (ifNoneMatch.empty() || etag.empty()) return false;
  size_t i = 0;
  while (i < ifNoneMatch.size()) {
    while (i < ifNoneMatch.size() && (ifNoneMatch[i] == ' ' || ifNoneMatch[i] == ',')) ++i;
    size_t j = i;
    while (j < ifNoneMatch.size() && ifNoneMatch[j] != ',') ++j;
    size_t end = j;
    while (end > i && ifNoneMatch[end - 1] == ' ') --end;
    std::string tag = ifNoneMatch.substr(i, end - i);
    if (tag == "*") return true;
    if (tag.compare(0, 2, "W/") == 0) tag.erase(0, 2);
    if (tag == etag) return true;
    i = j;
  }
  return false;
}

}  // namespace static_etag
