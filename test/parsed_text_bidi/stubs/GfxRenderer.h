#pragma once
#include <functional>
// Test stub of GfxRenderer: a fixed-pitch "font" so line breaking is deterministic.
// ASCII advances 10 px, a space 6 px, CJK 20 px; SUP/SUB halve the advance. Drawing is recorded.
#include <BidiUtils.h>  // the real one: this test runs the bidi line pass
namespace BidiUtils {
enum class BidiBaseDir : signed char { AUTO = -1, LTR = 0, RTL = 1, VISUAL = 2 };
}
#include <EpdFontFamily.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Utf8.h"

class GfxRenderer {
 public:
  struct DrawCall {
    int x, y;
    std::string text;
    EpdFontFamily::Style style;
  };
  mutable std::vector<DrawCall> drawn;

  bool isFontCacheScanning() const { return false; }
  bool isSdCardFont(int) const { return false; }
  // Test hook: when set, shapes an Indic run into the glyph keys it returns.
  mutable std::function<bool(const std::vector<uint32_t>&, uint8_t, std::string*)> shaper;
  bool shapeIndicRun(int, EpdFontFamily::Style, const uint32_t* cps, unsigned count, uint8_t script, const char*,
                     std::string* out) const {
    return shaper && shaper(std::vector<uint32_t>(cps, cps + count), script, out);
  }
  void ensureSdCardFontReady(int, const char*, uint8_t = 0x0F) const {}
  void ensureSdCardFontReady(int, const std::vector<std::string>&, bool, uint8_t = 0x0F) const {}
  void drawLine(int, int, int, int, bool = true) const {}
  void drawLine(int, int, int, int, int, bool) const {}
  void fillRect(int, int, int, int, bool = true) const {}
  int getSpaceWidth(int, EpdFontFamily::Style = EpdFontFamily::REGULAR) const { return 6; }
  int getSpaceAdvance(int, uint32_t, uint32_t, EpdFontFamily::Style) const { return 6; }
  int getKerning(int, uint32_t, uint32_t, EpdFontFamily::Style) const { return 0; }
  int getFontAscenderSize(int) const { return 16; }
  void setLetterSpacing(const int px) const { letterSpacingPx_ = px; }
  int getLetterSpacing() const { return letterSpacingPx_; }
  mutable int letterSpacingPx_ = 0;
  int getTextAdvanceX(int, const char* text, EpdFontFamily::Style style) const {
    int width = 0;
    const auto* p = reinterpret_cast<const unsigned char*>(text);
    while (*p) {
      const uint32_t cp = utf8NextCodepoint(&p);
      if (cp == 0) break;
      width += (cp == ' ' ? 6 : (cp >= 0x2E80 ? 20 : 10)) + letterSpacingPx_;
    }
    if ((style & (EpdFontFamily::SUP | EpdFontFamily::SUB)) != 0) width = (width + 1) / 2;
    return width;
  }
  int getTextWidth(int fontId, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                   BidiUtils::BidiBaseDir = BidiUtils::BidiBaseDir::AUTO) const {
    return getTextAdvanceX(fontId, text, style);
  }
  void drawText(int, int x, int y, const char* text, bool = true,
                EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                BidiUtils::BidiBaseDir = BidiUtils::BidiBaseDir::AUTO) const {
    drawn.push_back({x, y, text, style});
  }
  // Heading fonts are never loaded in these tests.
  const std::map<int, EpdFontFamily>& getFontMap() const {
    static const std::map<int, EpdFontFamily> none;
    return none;
  }
};
