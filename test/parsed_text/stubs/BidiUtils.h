#pragma once
// Test stub: layout tests run left-to-right only.
#include <cstdint>
#include <string>
#include <vector>

namespace BidiUtils {
enum class BidiBaseDir : signed char { AUTO = -1, LTR = 0, RTL = 1, VISUAL = 2 };
inline constexpr int RTL_PARAGRAPH_PROBE_DEPTH = 5;
inline bool startsWithRtl(const char*, int = RTL_PARAGRAPH_PROBE_DEPTH) { return false; }
inline int detectParagraphLevel(const char*, int fallbackLevel = 0, int = 64) { return fallbackLevel; }
inline bool applyBidiVisual(const char*, std::string&, int = -1) { return false; }
inline bool computeVisualWordOrder(const std::vector<std::string>&, bool, std::vector<uint16_t>&) { return false; }
}  // namespace BidiUtils
