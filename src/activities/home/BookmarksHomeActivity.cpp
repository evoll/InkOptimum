#include "BookmarksHomeActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "../reader/EpubReaderBookmarkListActivity.h"
#include "GlobalBookmarksListActivity.h"
#include "BookmarkStore.h"
#include "InkMODState.h"
#include "FileBrowserActionActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TouchListNavigation.h"

namespace {
constexpr unsigned long BOOKMARK_DELETE_HOLD_MS = 1000;
}

// Row 0 is the synthetic "All bookmarks" entry; rows 1..N map to books[0..N-1].
int BookmarksHomeActivity::rowCount() const { return books.empty() ? 0 : static_cast<int>(books.size()) + 1; }

void BookmarksHomeActivity::reloadBookmarks() {
  books.clear();
  BookmarkStore::getAllBookmarkedBooks(books);
  if (books.empty()) {
    selectedIndex = 0;
  } else if (selectedIndex >= rowCount()) {
    selectedIndex = rowCount() - 1;
  }
}

void BookmarksHomeActivity::onEnter() {
  Activity::onEnter();

  reloadBookmarks();
  selectedIndex = 0;
  requestUpdate();
}

void BookmarksHomeActivity::onExit() {
  Activity::onExit();
  books.clear();
}

void BookmarksHomeActivity::loop() {
  bool touchActivate = false;
  if (mappedInput.hasTouch() && !books.empty()) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const auto safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = safeArea.y + safeArea.height - contentTop - metrics.verticalSpacing;
    auto touch = TouchListNavigation::handle(mappedInput, selectedIndex, rowCount(),
                                             Rect{safeArea.x, contentTop, safeArea.width, contentHeight}, metrics.listRowHeight);
    if (touch.handled && !touch.activate) { requestUpdate(); return; }
    touchActivate = touch.activate;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  if (!books.empty() && !longPressOpenHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= BOOKMARK_DELETE_HOLD_MS) {
    longPressOpenHandled = true;
    if (selectedIndex > 0) showBookmarkBookActionMenu(selectedIndex - 1, true);
    return;
  }

  if (touchActivate || mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (longPressOpenHandled) {
      longPressOpenHandled = false;
      return;
    }
    if (selectedIndex == 0 && !books.empty()) {
      startActivityForResult(std::make_unique<GlobalBookmarksListActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               reloadBookmarks();
                               requestUpdate();
                             });
    } else if (selectedIndex > 0 && selectedIndex < rowCount()) {
      openBookmarkList(selectedIndex - 1);
    }
    return;
  }

  const int listSize = rowCount();

  buttonNavigator.onNextRelease([this, listSize] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, listSize);
    requestUpdate();
  });
}

void BookmarksHomeActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto safeArea = UITheme::getInstance().getScreenSafeArea(renderer, /*hasFrontButtonHints=*/true, /*hasSideButtonHints=*/false);

  GUI.drawHeader(renderer, Rect{safeArea.x, metrics.topPadding, safeArea.width, metrics.headerHeight}, tr(STR_BOOKMARKS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = safeArea.y + safeArea.height - contentTop - metrics.verticalSpacing;

  if (books.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_BOOKMARKS));
  } else {
    GUI.drawList(
        renderer, Rect{safeArea.x, contentTop, safeArea.width, contentHeight}, rowCount(), selectedIndex,
        [this](int index) { return index == 0 ? std::string(tr(STR_ALL_BOOKMARKS)) : books[index - 1].bookTitle; },
        [this](int index) { return index == 0 ? std::string() : books[index - 1].bookAuthor; }, nullptr);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void BookmarksHomeActivity::showBookmarkBookActionMenu(int bookIndex, bool ignoreInitialConfirmRelease) {
  if (bookIndex < 0 || bookIndex >= static_cast<int>(books.size())) return;

  const BookmarkedBookEntry entry = books[bookIndex];
  std::vector<FileBrowserActionActivity::MenuItem> items;
  items.reserve(1);
  items.push_back({FileBrowserAction::Delete, StrId::STR_DELETE});

  startActivityForResult(std::make_unique<FileBrowserActionActivity>(renderer, mappedInput, entry.bookTitle,
                                                                     std::move(items), ignoreInitialConfirmRelease),
                         [this, entry](const ActivityResult& result) {
                           longPressOpenHandled = false;
                           const auto* actionResult = std::get_if<FileBrowserActionResult>(&result.data);
                           if (!result.isCancelled && actionResult &&
                               static_cast<FileBrowserAction>(actionResult->action) == FileBrowserAction::Delete) {
                             BOOKMARKS.loadForBook(entry.bookPath, entry.bookTitle, entry.bookAuthor, entry.bookType);
                             BOOKMARKS.clearAll();
                           }
                           reloadBookmarks();
                           requestUpdate();
                         });
}

void BookmarksHomeActivity::openBookmarkList(int bookIndex) {
  const BookmarkedBookEntry entry = books[bookIndex];
  BOOKMARKS.loadForBook(entry.bookPath, entry.bookTitle, entry.bookAuthor, entry.bookType);

  startActivityForResult(
      std::make_unique<EpubReaderBookmarkListActivity>(renderer, mappedInput, BOOKMARKS.getBookmarks()),
      [this, entry](const ActivityResult& result) {
        if (!result.isCancelled) {
          const auto* bm = std::get_if<BookmarkResult>(&result.data);
          if (bm) {
            APP_STATE.pendingBookmarkSpine = bm->spineIndex;
            APP_STATE.pendingBookmarkProgress = bm->progress;
            APP_STATE.pendingBookmarkParagraphIndex = bm->paragraphIndex;
            APP_STATE.saveToFile();
            onSelectBook(entry.bookPath);
          } else {
            LOG_ERR("BKA", "openBookmarkList: result.data holds unexpected variant type, not BookmarkResult");
            requestUpdate();
          }
        } else {
          reloadBookmarks();
          requestUpdate();
        }
      });
}
