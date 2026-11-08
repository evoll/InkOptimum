#pragma once

#include <cstddef>
#include <string>

// Pure helpers for user-given bookmark names ("избранные страницы").
// Header-only so the rules can be unit-tested natively.
namespace bookmark_name {

// Normalises raw keyboard input into a storable name:
//  - control characters and any whitespace run collapse to a single space,
//  - leading/trailing spaces are removed,
//  - the result is cut to at most `capacity - 1` bytes without splitting a UTF-8 sequence.
// Returns an empty string when nothing printable remains (meaning "no name").
inline std::string normalize(const std::string& raw, const size_t capacity) {
  if (capacity < 2) return {};

  std::string out;
  out.reserve(raw.size());
  bool pendingSpace = false;
  for (const char ch : raw) {
    const auto c = static_cast<unsigned char>(ch);
    if (c <= 0x20 || c == 0x7F) {
      pendingSpace = !out.empty();
      continue;
    }
    if (pendingSpace) {
      out.push_back(' ');
      pendingSpace = false;
    }
    out.push_back(ch);
  }

  const size_t limit = capacity - 1;
  if (out.size() > limit) {
    size_t cut = limit;
    // Step back over continuation bytes (10xxxxxx) so the last sequence stays whole.
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
    out.resize(cut);
    while (!out.empty() && out.back() == ' ') out.pop_back();
  }
  return out;
}

}  // namespace bookmark_name
