#pragma once
// Test stub: layout tests run left-to-right only, so the bidi line pass never runs.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace BidiLine {
inline constexpr uint8_t kMaxOpenControls = 8;
struct OpenControls {
  uint32_t cps[kMaxOpenControls] = {};
  uint8_t count = 0;
  uint8_t overflow = 0;
  void advance(std::string_view) {}
};
inline bool dropControls(std::string&) { return false; }
struct Token {
  std::string_view text;
  bool spaceBefore;
};
enum class Gap : uint8_t { None, Glued, Space };
struct Piece {
  uint16_t token, begin, end, displayBegin, displayLength;
  Gap gapBefore;
};
struct Line {
  std::vector<Piece> pieces;
  std::string display;
  bool reordered = false;
  bool fallback = false;
};
struct Scratch {};
inline bool layoutLine(const Token*, size_t, const OpenControls&, uint8_t, Scratch&, Line&) { return false; }
inline Scratch* heapScratch() { return nullptr; }
inline void releaseBuildMemory() {}
}  // namespace BidiLine
