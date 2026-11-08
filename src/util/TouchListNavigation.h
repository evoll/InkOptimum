#pragma once

#include <algorithm>

#include "MappedInputManager.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

namespace TouchListNavigation {
struct Result {
  bool handled = false;
  bool activate = false;
};

inline Result handle(MappedInputManager& input, int& selectedIndex, int totalItems, const Rect& rect, int rowHeight) {
  if (!input.hasTouch() || totalItems <= 0 || rowHeight <= 0 || rect.height <= 0) return {};

  const int pageItems = std::max(1, rect.height / rowHeight);
  const auto swipe = input.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, totalItems, pageItems);
    return {true, false};
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, totalItems, pageItems);
    return {true, false};
  }

  int x = 0;
  int y = 0;
  if (!input.wasScreenTapped(x, y)) return {};
  if (x < rect.x || x >= rect.x + rect.width || y < rect.y || y >= rect.y + rect.height) return {};

  const int pageStart = (std::max(0, selectedIndex) / pageItems) * pageItems;
  const int row = (y - rect.y) / rowHeight;
  const int index = pageStart + row;
  if (index < 0 || index >= totalItems) return {};

  selectedIndex = index;
  return {true, true};
}

// Swipe-to-delete gesture: a leftward swipe that *starts* on a list row selects that row.
// Returns the absolute item index of the row, or -1 when there is no such gesture. The caller must
// ask for confirmation before deleting anything. Vertical swipes still page the list.
inline int swipeDeleteRow(MappedInputManager& input, int selectedIndex, int totalItems, const Rect& rect,
                          int rowHeight) {
  if (!input.hasTouch() || totalItems <= 0 || rowHeight <= 0 || rect.height <= 0) return -1;

  int sx = 0;
  int sy = 0;
  if (input.wasSwipeFrom(sx, sy) != MappedInputManager::SwipeDir::Left) return -1;
  if (sx < rect.x || sx >= rect.x + rect.width || sy < rect.y || sy >= rect.y + rect.height) return -1;

  const int pageItems = std::max(1, rect.height / rowHeight);
  const int pageStart = (std::max(0, selectedIndex) / pageItems) * pageItems;
  const int index = pageStart + (sy - rect.y) / rowHeight;
  return (index >= 0 && index < totalItems) ? index : -1;
}
}  // namespace TouchListNavigation
