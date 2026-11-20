#include "EpubReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TouchListNavigation.h"

int EpubReaderChapterSelectionActivity::getTotalItems() const {
  return hasNested ? static_cast<int>(visible.size()) : epub->getTocItemsCount();
}

void EpubReaderChapterSelectionActivity::rebuildVisible(const int keepTocIndex) {
  visible.clear();
  const int n = static_cast<int>(levels.size());
  int skipAbove = -1;  // while >= 0, hide entries deeper than this level
  for (int i = 0; i < n; i++) {
    if (skipAbove >= 0) {
      if (levels[i] > skipAbove) continue;
      skipAbove = -1;
    }
    visible.push_back(i);
    if (isParent(i) && !expanded[i]) skipAbove = levels[i];
  }
  selectorIndex = 0;
  for (int row = 0; row < static_cast<int>(visible.size()); row++) {
    if (visible[row] <= keepTocIndex) selectorIndex = row;
  }
}

void EpubReaderChapterSelectionActivity::openRow(const int row) {
  const auto tocItem = epub->getTocItem(tocIndexAt(row));
  if (tocItem.spineIndex == -1) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
  } else {
    setResult(ChapterResult{tocItem.spineIndex, tocItem.anchor});
  }
  finish();
}

void EpubReaderChapterSelectionActivity::onEnter() {
  Activity::onEnter();

  if (!epub) {
    return;
  }

  int currentToc = currentTocIndex >= 0 ? currentTocIndex : epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (currentToc < 0) {
    currentToc = 0;
  }
  selectorIndex = currentToc;

  const int count = epub->getTocItemsCount();
  levels.assign(count, 1);
  hasNested = false;
  for (int i = 0; i < count; i++) {
    levels[i] = epub->getTocItem(i).level;
    if (i > 0 && levels[i] > levels[i - 1]) hasNested = true;
  }
  if (hasNested) {
    // Short contents stay fully open; long ones start collapsed except the path to the current chapter.
    expanded.assign(count, count <= 15 ? 1 : 0);
    int level = levels[currentToc] > 0 ? levels[currentToc] : 1;
    for (int i = currentToc - 1; i >= 0 && level > 0; i--) {
      if (levels[i] < level) {
        expanded[i] = 1;
        level = levels[i];
      }
    }
    if (isParent(currentToc)) expanded[currentToc] = 1;
    rebuildVisible(currentToc);
  }

  // Trigger first update
  requestUpdate();
}

void EpubReaderChapterSelectionActivity::onExit() { Activity::onExit(); }

void EpubReaderChapterSelectionActivity::loop() {
  const int totalItems = getTotalItems();
  constexpr int kChapterJump = 5;

  if (totalItems > 0) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;
    if (hasNested && mappedInput.hasTouch()) {
      // Touch folding: swipe left on a branch row collapses it, swipe right expands it.
      int sx = 0;
      int sy = 0;
      const auto dir = mappedInput.wasSwipeFrom(sx, sy);
      if ((dir == MappedInputManager::SwipeDir::Left || dir == MappedInputManager::SwipeDir::Right) &&
          sx >= screen.x && sx < screen.x + screen.width && sy >= contentTop && sy < contentTop + contentHeight) {
        const int pageItems = std::max(1, contentHeight / metrics.listRowHeight);
        const int pageStart = (std::max(0, selectorIndex) / pageItems) * pageItems;
        const int row = pageStart + (sy - contentTop) / metrics.listRowHeight;
        if (row >= 0 && row < totalItems) {
          const int tocIdx = tocIndexAt(row);
          const uint8_t wantOpen = dir == MappedInputManager::SwipeDir::Right ? 1 : 0;
          if (isParent(tocIdx) && expanded[tocIdx] != wantOpen) {
            expanded[tocIdx] = wantOpen;
            rebuildVisible(tocIdx);
            requestUpdate();
          }
          return;
        }
      }
    }
    auto touch = TouchListNavigation::handle(mappedInput, selectorIndex, totalItems,
                                              Rect{screen.x, contentTop, screen.width, contentHeight},
                                              metrics.listRowHeight);
    if (touch.handled) {
      requestUpdate();
      if (touch.activate) {
        openRow(selectorIndex);
      }
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const int tocIdx = tocIndexAt(selectorIndex);
    if (hasNested && isParent(tocIdx) && mappedInput.getHeldTime() >= 500) {
      // Long press on a branch: fold / unfold it instead of opening the chapter.
      expanded[tocIdx] = !expanded[tocIdx];
      rebuildVisible(tocIdx);
      requestUpdate();
      return;
    }
    openRow(selectorIndex);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
  }

  // Chapter selector: a short press always moves exactly one chapter so the
  // user can land on any TOC entry. Holding any navigation button accelerates
  // movement in fixed groups of five. ButtonNavigator suppresses the release
  // callback after continuous navigation has fired, so a long press does not
  // add an extra +/-1 step when the button is released.
  auto nextOne = [this, totalItems] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, totalItems);
    requestUpdate();
  };
  auto previousOne = [this, totalItems] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, totalItems);
    requestUpdate();
  };
  auto jumpForward = [this, totalItems] {
    if (totalItems <= 0) return;
    selectorIndex = (selectorIndex + kChapterJump) % totalItems;
    requestUpdate();
  };
  auto jumpBackward = [this, totalItems] {
    if (totalItems <= 0) return;
    selectorIndex = (selectorIndex - (kChapterJump % totalItems) + totalItems) % totalItems;
    requestUpdate();
  };

  buttonNavigator.onNextRelease(nextOne);
  buttonNavigator.onPreviousRelease(previousOne);
  buttonNavigator.onNextContinuous(jumpForward);
  buttonNavigator.onPreviousContinuous(jumpBackward);
}

void EpubReaderChapterSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_SELECT_CHAPTER), nullptr, true);

  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;

  const int totalItems = getTotalItems();
  GUI.drawList(renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, totalItems, selectorIndex,
               [this](int index) {
                 const int tocIdx = tocIndexAt(index);
                 auto item = epub->getTocItem(tocIdx);
                 std::string indent((item.level - 1) * 2, ' ');
                 if (hasNested && isParent(tocIdx)) {
                   indent += expanded[tocIdx] ? "- " : "+ ";
                 }
                 return indent + item.title;
               });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);

  renderer.displayBuffer();
}
