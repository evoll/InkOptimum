#include "GridTheme.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kCornerRadius = 10;
constexpr int kIconSize = 32;
constexpr int kInnerPadding = 14;
}  // namespace

Rect GridTheme::cellRect(const Rect& rect, int buttonCount, int index) {
  const auto& m = GridMetrics::values;
  const int rows = std::max(1, (buttonCount + GridMetrics::kColumns - 1) / GridMetrics::kColumns);
  const int usableWidth = rect.width - 2 * m.contentSidePadding;
  const int cellWidth = (usableWidth - GridMetrics::kColumnGap * (GridMetrics::kColumns - 1)) / GridMetrics::kColumns;
  // Rows share the available height but never grow beyond a comfortable touch size.
  const int fitted = (rect.height - m.menuSpacing * (rows - 1)) / rows;
  const int cellHeight = std::clamp(fitted, 40, m.menuRowHeight + 16);
  const int col = index % GridMetrics::kColumns;
  const int row = index / GridMetrics::kColumns;
  return Rect{rect.x + m.contentSidePadding + col * (cellWidth + GridMetrics::kColumnGap),
              rect.y + row * (cellHeight + m.menuSpacing), cellWidth, cellHeight};
}

int GridTheme::hitTest(const Rect& rect, int buttonCount, int x, int y) {
  for (int i = 0; i < buttonCount; ++i) {
    const Rect c = cellRect(rect, buttonCount, i);
    // The gaps count as part of the cell on the right/bottom: friendlier on e-ink touch panels.
    if (x >= c.x && x < c.x + c.width + GridMetrics::kColumnGap && y >= c.y &&
        y < c.y + c.height + GridMetrics::values.menuSpacing) {
      return i;
    }
  }
  return -1;
}

void GridTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                               const std::function<std::string(int index)>& buttonLabel,
                               const std::function<UIIcon(int index)>& rowIcon) const {
  for (int i = 0; i < buttonCount; ++i) {
    const Rect cell = cellRect(rect, buttonCount, i);
    const bool selected = (i == selectedIndex);

    if (selected) {
      renderer.fillRoundedRect(cell.x, cell.y, cell.width, cell.height, kCornerRadius, Color::LightGray);
    }
    renderer.drawRoundedRect(cell.x, cell.y, cell.width, cell.height, selected ? 2 : 1, kCornerRadius, true);

    int textX = cell.x + kInnerPadding;
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int iconY = cell.y + (cell.height - kIconSize) / 2;

    if (rowIcon != nullptr) {
      const UIIcon icon = rowIcon(i);
      if (icon == UIIcon::BookmarkIcon) {
        constexpr int ribbonWidth = 16;
        constexpr int ribbonHeight = 22;
        constexpr int notch = 6;
        const int ix = textX + (kIconSize - ribbonWidth) / 2;
        const int iy = cell.y + (cell.height - ribbonHeight) / 2;
        const int polyX[5] = {ix, ix + ribbonWidth, ix + ribbonWidth, ix + ribbonWidth / 2, ix};
        const int polyY[5] = {iy, iy, iy + ribbonHeight, iy + ribbonHeight - notch, iy + ribbonHeight};
        renderer.fillPolygon(polyX, polyY, 5, true);
        textX += kIconSize + 10;
      } else if (const uint8_t* bitmap = iconForName(icon, kIconSize)) {
        renderer.drawIcon(bitmap, textX, iconY, kIconSize, kIconSize);
        textX += kIconSize + 10;
      }
    }

    const std::string label =
        renderer.truncatedText(UI_10_FONT_ID, buttonLabel(i).c_str(), cell.x + cell.width - kInnerPadding - textX);
    renderer.drawText(UI_10_FONT_ID, textX, cell.y + (cell.height - lineHeight) / 2, label.c_str(), true);
  }
}

void GridTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                    int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                    bool& bufferRestored, const std::function<bool()>& storeCoverBuffer,
                                    const BookReadingStats* stats, float progressPercent) const {
  // Cover, title, author, "Total time", "Avg. session", percent and progress bar are Lyra's card;
  // Grid frames it with a border like the reference layout. The border is drawn on every frame (it is not part
  // of the cached cover rectangle's content) so it also survives a restored cover buffer.
  LyraTheme::drawRecentBookCover(renderer, rect, recentBooks, selectorIndex, coverRendered, coverBufferStored,
                                 bufferRestored, storeCoverBuffer, stats, progressPercent);
  const int x = GridMetrics::values.contentSidePadding;
  const int w = rect.width - 2 * GridMetrics::values.contentSidePadding;
  const bool selected = !recentBooks.empty() && selectorIndex == 0;
  renderer.drawRoundedRect(x, rect.y, w, rect.height, selected ? 2 : 1, kCornerRadius, true);
}
