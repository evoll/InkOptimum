#include "EpubReaderBookmarkListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "MappedInputManager.h"
#include "InkMODHumor.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/OptionSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TouchListNavigation.h"

static constexpr int ROW_HEIGHT = 68;
static constexpr int LIST_START_Y = 60;
static constexpr unsigned long BOOKMARK_DELETE_HOLD_MS = 1000;

int EpubReaderBookmarkListActivity::getPageItems() const {
  const auto orientation = renderer.getOrientation();
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int startY = LIST_START_Y + hintGutterHeight;
  const int available = renderer.getScreenHeight() - startY - ROW_HEIGHT;
  return std::max(1, available / ROW_HEIGHT);
}

void EpubReaderBookmarkListActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void EpubReaderBookmarkListActivity::onExit() { Activity::onExit(); }

void EpubReaderBookmarkListActivity::deleteSelectedBookmark() {
  if (bookmarks.empty() || selectedIndex < 0 || selectedIndex >= static_cast<int>(bookmarks.size())) return;

  if (!BOOKMARKS.removeBookmarkAt(static_cast<size_t>(selectedIndex))) return;

  bookmarks = BOOKMARKS.getBookmarks();
  if (bookmarks.empty()) {
    selectedIndex = 0;
  } else if (selectedIndex >= static_cast<int>(bookmarks.size())) {
    selectedIndex = static_cast<int>(bookmarks.size()) - 1;
  }
  requestUpdate();
}

int EpubReaderBookmarkListActivity::findBookmarkIndex(const Bookmark& target) const {
  const auto it = std::find_if(bookmarks.begin(), bookmarks.end(), [&target](const Bookmark& bm) {
    return bm.spineIndex == target.spineIndex && bm.progress == target.progress;
  });
  return it == bookmarks.end() ? -1 : static_cast<int>(std::distance(bookmarks.begin(), it));
}

void EpubReaderBookmarkListActivity::confirmDeleteBookmark(const Bookmark& selectedBookmark,
                                                           const bool ignoreInitialConfirmRelease) {
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE),
                                             tr(STR_DELETE_BOOKMARK_CONFIRM), ignoreInitialConfirmRelease),
      [this, selectedBookmark](const ActivityResult& result) {
        longPressConfirmHandled = false;
        const int index = result.isCancelled ? -1 : findBookmarkIndex(selectedBookmark);
        if (index >= 0) {
          selectedIndex = index;
          deleteSelectedBookmark();
        } else {
          requestUpdate();
        }
      });
}

void EpubReaderBookmarkListActivity::renameBookmark(const Bookmark& selectedBookmark) {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_RENAME), selectedBookmark.name,
                                              BOOKMARK_NAME_MAX - 1, InputType::Text),
      [this, selectedBookmark](const ActivityResult& result) {
        longPressConfirmHandled = false;
        const auto* kb = result.isCancelled ? nullptr : std::get_if<KeyboardResult>(&result.data);
        const int index = kb ? findBookmarkIndex(selectedBookmark) : -1;
        if (index >= 0 && BOOKMARKS.renameBookmarkAt(static_cast<size_t>(index), kb->text)) {
          bookmarks = BOOKMARKS.getBookmarks();
        }
        requestUpdate();
      });
}

// Long-press / touch long-press menu: rename (favourite-page name) or delete.
void EpubReaderBookmarkListActivity::showBookmarkActionMenu(bool ignoreInitialConfirmRelease) {
  if (bookmarks.empty() || selectedIndex < 0 || selectedIndex >= static_cast<int>(bookmarks.size())) return;

  const Bookmark selectedBookmark = bookmarks[selectedIndex];
  // Button long-press: the key is still held, so its release must not pick a menu row.
  if (ignoreInitialConfirmRelease) mappedInput.suppressNextConfirmRelease();
  startActivityForResult(
      std::make_unique<OptionSelectionActivity>(renderer, mappedInput, "BookmarkActionSelect", StrId::STR_BOOKMARKS,
                                                std::vector<std::string>{tr(STR_RENAME), tr(STR_DELETE)}, 0,
                                                /*readerMode=*/true, /*showCurrentMarker=*/false),
      [this, selectedBookmark](const ActivityResult& result) {
        longPressConfirmHandled = false;
        const auto* choice = result.isCancelled ? nullptr : std::get_if<OptionSelectionResult>(&result.data);
        if (choice == nullptr) {
          requestUpdate();
          return;
        }
        if (choice->index == 0) {
          renameBookmark(selectedBookmark);
        } else {
          confirmDeleteBookmark(selectedBookmark, false);
        }
      });
}

void EpubReaderBookmarkListActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (!bookmarks.empty() && !longPressConfirmHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= BOOKMARK_DELETE_HOLD_MS) {
    longPressConfirmHandled = true;
    showBookmarkActionMenu(true);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (longPressConfirmHandled) {
      longPressConfirmHandled = false;
      return;
    }
    if (!bookmarks.empty() && selectedIndex >= 0 && selectedIndex < static_cast<int>(bookmarks.size())) {
      setResult(BookmarkResult{bookmarks[selectedIndex].spineIndex, bookmarks[selectedIndex].progress,
                               bookmarks[selectedIndex].paragraphIndex});
      finish();
    }
    return;
  }

  const int total = static_cast<int>(bookmarks.size());
  if (total == 0) return;

  // X4 Pro/touch: tap opens a bookmark, long-press asks before deleting, vertical swipes page the list.
  if (mappedInput.hasTouch()) {
    const int pageItems = getPageItems();
    int tx = 0, ty = 0;
    if (mappedInput.wasScreenLongPress(tx, ty)) {
      const auto orientation = renderer.getOrientation();
      const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
      const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
      const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
      const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
      const int contentX = isLandscapeCw ? hintGutterWidth : 0;
      const int contentWidth = renderer.getScreenWidth() - hintGutterWidth;
      const int contentY = isPortraitInverted ? 50 : 0;
      const int listTop = LIST_START_Y + contentY;
      if (tx >= contentX && tx < contentX + contentWidth && ty >= listTop) {
        const int row = (ty - listTop) / ROW_HEIGHT;
        const int pageStart = (selectedIndex / pageItems) * pageItems;
        const int index = pageStart + row;
        if (row >= 0 && row < pageItems && index >= 0 && index < total) {
          selectedIndex = index;
          showBookmarkActionMenu(false);
          return;
        }
      }
    }

    {
      // Swipe a row to the left: ask to delete that bookmark.
      const auto orientation = renderer.getOrientation();
      const int gutterW = (orientation == GfxRenderer::Orientation::LandscapeClockwise ||
                           orientation == GfxRenderer::Orientation::LandscapeCounterClockwise)
                              ? 30
                              : 0;
      const int rectX = orientation == GfxRenderer::Orientation::LandscapeClockwise ? gutterW : 0;
      const int rectY = LIST_START_Y + (orientation == GfxRenderer::Orientation::PortraitInverted ? 50 : 0);
      const int row = TouchListNavigation::swipeDeleteRow(
          mappedInput, selectedIndex, total,
          Rect{rectX, rectY, renderer.getScreenWidth() - gutterW, pageItems * ROW_HEIGHT}, ROW_HEIGHT);
      if (row >= 0) {
        selectedIndex = row;
        confirmDeleteBookmark(bookmarks[row], false);
        return;
      }
    }

    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Up) {
      selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, total, pageItems);
      requestUpdate();
      return;
    }
    if (swipe == MappedInputManager::SwipeDir::Down) {
      selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, total, pageItems);
      requestUpdate();
      return;
    }
    tx = 0;
    ty = 0;
    if (mappedInput.wasScreenTapped(tx, ty)) {
      const auto orientation = renderer.getOrientation();
      const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
      const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
      const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
      const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
      const int contentX = isLandscapeCw ? hintGutterWidth : 0;
      const int contentWidth = renderer.getScreenWidth() - hintGutterWidth;
      const int contentY = isPortraitInverted ? 50 : 0;
      const int listTop = LIST_START_Y + contentY;
      if (tx >= contentX && tx < contentX + contentWidth && ty >= listTop) {
        const int row = (ty - listTop) / ROW_HEIGHT;
        if (row >= 0 && row < pageItems) {
          const int pageStart = (selectedIndex / pageItems) * pageItems;
          const int index = pageStart + row;
          if (index >= 0 && index < total) {
            selectedIndex = index;
            setResult(BookmarkResult{bookmarks[selectedIndex].spineIndex, bookmarks[selectedIndex].progress,
                                     bookmarks[selectedIndex].paragraphIndex});
            finish();
            return;
          }
        }
      }
    }
  }

  buttonNavigator.onNextRelease([this, total] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, total);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, total] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, total);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, total] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, total);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, total] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, total);
    requestUpdate();
  });
}

void EpubReaderBookmarkListActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int contentY = hintGutterHeight;

  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, tr(STR_BOOKMARKS), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, tr(STR_BOOKMARKS), true, EpdFontFamily::BOLD);

  if (bookmarks.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, LIST_START_Y + contentY + 20, tr(STR_NO_BOOKMARKS));
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int jokeTop = LIST_START_Y + contentY + 58;
    const int jokeBottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - 8;
    InkMODHumor::drawBounded(renderer, InkMODHumor::Moment::NoBookmarks, contentX, jokeTop, contentWidth,
                             std::max(0, jokeBottom - jokeTop), SMALL_FONT_ID, true);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
    renderer.displayBuffer();
    return;
  }

  const int pageItems = getPageItems();
  const int total = static_cast<int>(bookmarks.size());
  const int pageStartIndex = (selectedIndex / pageItems) * pageItems;
  const int marginLeft = contentX + 20;

  for (int i = 0; i < pageItems; i++) {
    const int itemIndex = pageStartIndex + i;
    if (itemIndex >= total) break;

    const int rowY = LIST_START_Y + contentY + i * ROW_HEIGHT;
    const bool isSelected = (itemIndex == selectedIndex);

    if (isSelected) {
      renderer.fillRect(contentX, rowY, contentWidth - 1, ROW_HEIGHT, true);
    }

    const Bookmark& bm = bookmarks[itemIndex];
    const char* chapter = (bm.chapterTitle[0] != '\0') ? bm.chapterTitle : tr(STR_UNKNOWN_CHAPTER);
    // A user-given name ("favourite page") takes the headline slot; otherwise show the text snippet.
    const char* headline = bm.name[0] != '\0' ? bm.name : bm.snippet;
    const bool hasSnippet = headline[0] != '\0';
    if (hasSnippet) {
      const std::string snippetTrunc = renderer.truncatedText(UI_10_FONT_ID, headline, contentWidth - 40,
                                                              bm.name[0] != '\0' ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      renderer.drawText(UI_10_FONT_ID, marginLeft, rowY + 5, snippetTrunc.c_str(), !isSelected);
    }

    const std::string chapterTrunc = renderer.truncatedText(SMALL_FONT_ID, chapter, contentWidth - 40);
    renderer.drawText(SMALL_FONT_ID, marginLeft, rowY + (hasSnippet ? 27 : 10), chapterTrunc.c_str(), !isSelected);

    char pageBuf[24];
    snprintf(pageBuf, sizeof(pageBuf), "%d%%", static_cast<int>(std::lround(bm.progress * 100.0)));
    renderer.drawText(SMALL_FONT_ID, marginLeft, rowY + (hasSnippet ? 47 : 36), pageBuf, !isSelected);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);

  renderer.displayBuffer();
}
