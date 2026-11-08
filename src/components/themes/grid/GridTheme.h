#pragma once

#include "components/themes/lyra/LyraTheme.h"

// Grid: Lyra's look with a bordered "continue reading" card (cover, title, author, reading time, percent and
// progress bar) and the main menu laid out as a two-column grid of rounded icon+label buttons.
// Everything else (lists, popups, keyboard, header, reader status bar) is inherited from Lyra.
namespace GridMetrics {
constexpr ThemeMetrics makeValues() {
  ThemeMetrics v = LyraMetrics::values;
  // Cover geometry stays Lyra's: LyraTheme::drawRecentBookCover reads LyraMetrics directly.
  v.homeMenuTopOffset = 14;
  v.menuRowHeight = 64;
  v.menuSpacing = 10;
  return v;
}
constexpr ThemeMetrics values = makeValues();

constexpr int kColumns = 2;
constexpr int kColumnGap = 10;
}  // namespace GridMetrics

class GridTheme : public LyraTheme {
 public:
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           const std::function<bool()>& storeCoverBuffer, const BookReadingStats* stats = nullptr,
                           float progressPercent = -1.0f) const override;

  // Geometry shared with HomeActivity's touch hit-test: rectangle of grid cell `index` for a menu area `rect`.
  static Rect cellRect(const Rect& rect, int buttonCount, int index);
  // Cell under (x, y), or -1.
  static int hitTest(const Rect& rect, int buttonCount, int x, int y);
};
