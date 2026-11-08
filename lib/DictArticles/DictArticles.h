#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// One word can lead to several articles: a StarDict synonym ("よんで") may name more than one headword
// ("よむ", "よぶ"), and a dictionary may hold two entries under one spelling. Showing only the first hides
// the others, so a lookup collects up to kMaxParts distinct articles and shows each under its own headword.
namespace dict_articles {

constexpr size_t kMaxParts = 4;
constexpr size_t kMaxHeadword = 96;

// Where one article is and what it is called.
struct Part {
  char headword[kMaxHeadword] = {};
  uint32_t bodyOffset = 0;  // file position of the article text
  uint32_t bodyLength = 0;
};

// True when `parts` already holds this article (the same text on disk) or this headword spelled the same.
inline bool contains(const Part* parts, const size_t count, const uint32_t bodyOffset, const char* headword) {
  for (size_t i = 0; i < count; ++i) {
    if (parts[i].bodyOffset == bodyOffset || std::strcmp(parts[i].headword, headword) == 0) return true;
  }
  return false;
}

// Builds the text shown for `count` parts into out[0..cap). `read(i, dst, maxBytes)` copies up to maxBytes of
// part i's article into dst and returns the bytes copied, 0 on failure. One part is the article alone, as
// before. Several parts are each preceded by a line with their headword and separated by a blank line; the
// space is shared evenly among those still to come, so one long article cannot push the others out.
// Returns the length written (out is always NUL-terminated), 0 when nothing could be read.
template <class ReadFn>
size_t assemble(const Part* parts, const size_t count, char* out, const size_t cap, ReadFn&& read) {
  if (!out || cap < 2) return 0;
  out[0] = '\0';
  if (count == 0) return 0;
  if (count == 1) {
    const size_t n = read(0, out, cap - 1);
    out[n] = '\0';
    return n;
  }

  size_t used = 0;
  size_t written = 0;  // parts that made it in
  for (size_t i = 0; i < count; ++i) {
    const size_t remaining = cap - 1 - used;
    const size_t share = remaining / (count - i);
    const size_t labelLen = std::strlen(parts[i].headword);
    // A label line, a body and the separator must all fit; a share too small for them is skipped.
    if (share < labelLen + 4) continue;
    const size_t separator = (i + 1 < count) ? 2 : 0;
    const size_t bodyRoom = share - labelLen - 1 - separator;
    char* at = out + used;
    std::memcpy(at, parts[i].headword, labelLen);
    at[labelLen] = '\n';
    const size_t n = read(i, at + labelLen + 1, bodyRoom);
    if (n == 0) continue;  // unreadable: leave no bare label behind
    size_t len = labelLen + 1 + n;
    if (separator) {
      at[len++] = '\n';
      at[len++] = '\n';
    }
    used += len;
    ++written;
  }
  while (used > 0 && out[used - 1] == '\n') --used;  // no separator after the last part that made it in
  out[used] = '\0';
  return written == 0 ? 0 : used;
}

}  // namespace dict_articles
