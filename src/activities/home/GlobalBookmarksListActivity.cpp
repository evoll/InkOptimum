#include "GlobalBookmarksListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "BookmarkStore.h"
#include "InkMODState.h"
#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/OptionSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TouchListNavigation.h"

void GlobalBookmarksListActivity::reload() {
  rows.clear();

  std::vector<BookmarkedBookEntry> books;
  BookmarkStore::getAllBookmarkedBooks(books);

  for (const auto& book : books) {
    if (!BOOKMARKS.loadForBook(book.bookPath, book.bookTitle, book.bookAuthor, book.bookType)) continue;
    for (const auto& bm : BOOKMARKS.getBookmarks()) {
      Row row;
      row.bookPath = book.bookPath;
      row.bookTitle = book.bookTitle;
      row.bookAuthor = book.bookAuthor;
      row.bookType = book.bookType;
      row.spineIndex = bm.spineIndex;
      row.progress = bm.progress;
      row.paragraphIndex = bm.paragraphIndex;
      row.timestamp = bm.timestamp;
      std::memcpy(row.chapterTitle, bm.chapterTitle, sizeof(row.chapterTitle));
      row.chapterTitle[sizeof(row.chapterTitle) - 1] = '\0';
      static_assert(sizeof(Row::name) == BOOKMARK_NAME_MAX, "Row::name must match Bookmark::name");
      std::memcpy(row.name, bm.name, sizeof(row.name));
      row.name[sizeof(row.name) - 1] = '\0';
      rows.push_back(std::move(row));
    }
  }
  BOOKMARKS.unload();

  std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.timestamp > b.timestamp; });
  if (rows.size() > kMaxRows) rows.resize(kMaxRows);

  if (rows.empty()) {
    selectedIndex = 0;
  } else if (selectedIndex >= static_cast<int>(rows.size())) {
    selectedIndex = static_cast<int>(rows.size()) - 1;
  }
}

void GlobalBookmarksListActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  reload();
  requestUpdate();
}

void GlobalBookmarksListActivity::onExit() {
  Activity::onExit();
  rows.clear();
  rows.shrink_to_fit();
}

std::string GlobalBookmarksListActivity::rowTitle(int index) const {
  const Row& row = rows[index];
  return row.bookTitle.empty() ? row.bookPath : row.bookTitle;
}

std::string GlobalBookmarksListActivity::rowSubtitle(int index) const {
  const Row& row = rows[index];
  char buf[128];
  const int percent = static_cast<int>(row.progress * 100.0f + 0.5f);
  if (row.name[0] != '\0') {
    // Named ("favourite") bookmarks lead with their name.
    snprintf(buf, sizeof(buf), "%s  %d%%", row.name, percent);
  } else if (row.chapterTitle[0] != '\0') {
    snprintf(buf, sizeof(buf), "%s  %d%%", row.chapterTitle, percent);
  } else {
    snprintf(buf, sizeof(buf), "%d%%", percent);
  }
  return buf;
}

void GlobalBookmarksListActivity::openSelected() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(rows.size())) return;
  const Row& row = rows[selectedIndex];
  APP_STATE.pendingBookmarkSpine = row.spineIndex;
  APP_STATE.pendingBookmarkProgress = row.progress;
  APP_STATE.pendingBookmarkParagraphIndex = row.paragraphIndex;
  APP_STATE.saveToFile();
  onSelectBook(row.bookPath);
}

void GlobalBookmarksListActivity::showActionMenu(const bool ignoreInitialConfirmRelease) {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(rows.size())) return;
  // Button long-press: the key is still held, so its release must not pick a menu row.
  if (ignoreInitialConfirmRelease) mappedInput.suppressNextConfirmRelease();
  startActivityForResult(
      std::make_unique<OptionSelectionActivity>(renderer, mappedInput, "BookmarkActionSelect", StrId::STR_BOOKMARKS,
                                                std::vector<std::string>{tr(STR_RENAME), tr(STR_DELETE)}, 0,
                                                /*readerMode=*/false, /*showCurrentMarker=*/false),
      [this](const ActivityResult& result) {
        const auto* choice = result.isCancelled ? nullptr : std::get_if<OptionSelectionResult>(&result.data);
        if (choice == nullptr) {
          requestUpdate();
        } else if (choice->index == 0) {
          renameSelected();
        } else {
          deleteSelected();
        }
      });
}

void GlobalBookmarksListActivity::renameSelected() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(rows.size())) return;
  const Row row = rows[selectedIndex];

  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_RENAME), row.name, BOOKMARK_NAME_MAX - 1,
                                              InputType::Text),
      [this, row](const ActivityResult& result) {
        const auto* kb = result.isCancelled ? nullptr : std::get_if<KeyboardResult>(&result.data);
        if (kb != nullptr && BOOKMARKS.loadForBook(row.bookPath, row.bookTitle, row.bookAuthor, row.bookType)) {
          const auto& list = BOOKMARKS.getBookmarks();
          for (size_t i = 0; i < list.size(); ++i) {
            if (list[i].spineIndex == row.spineIndex && list[i].progress == row.progress &&
                list[i].timestamp == row.timestamp) {
              BOOKMARKS.renameBookmarkAt(i, kb->text);
              break;
            }
          }
          BOOKMARKS.unload();
        }
        reload();
        requestUpdate();
      });
}

void GlobalBookmarksListActivity::deleteSelected() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(rows.size())) return;
  const Row row = rows[selectedIndex];

  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE), tr(STR_DELETE_BOOKMARK_CONFIRM),
                                             /*ignoreInitialConfirmRelease=*/false),
      [this, row](const ActivityResult& result) {
        if (!result.isCancelled && BOOKMARKS.loadForBook(row.bookPath, row.bookTitle, row.bookAuthor, row.bookType)) {
          const auto& list = BOOKMARKS.getBookmarks();
          for (size_t i = 0; i < list.size(); ++i) {
            // Identify the bookmark by its saved position; timestamps alone can collide.
            if (list[i].spineIndex == row.spineIndex && list[i].progress == row.progress &&
                list[i].timestamp == row.timestamp) {
              BOOKMARKS.removeBookmarkAt(i);
              break;
            }
          }
          BOOKMARKS.unload();
        }
        reload();
        requestUpdate();
      });
}

void GlobalBookmarksListActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (rows.empty()) return;

  if (mappedInput.hasTouch()) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const auto safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = safeArea.y + safeArea.height - contentTop - metrics.verticalSpacing;
    const Rect listRect{safeArea.x, contentTop, safeArea.width, contentHeight};
    const int rowHeight = metrics.listWithSubtitleRowHeight;
    const int total = static_cast<int>(rows.size());

    // Swipe a row to the left: rename / delete menu for that bookmark.
    const int swiped = TouchListNavigation::swipeDeleteRow(mappedInput, selectedIndex, total, listRect, rowHeight);
    if (swiped >= 0) {
      selectedIndex = swiped;
      showActionMenu(false);
      return;
    }
    const auto touch = TouchListNavigation::handle(mappedInput, selectedIndex, total, listRect, rowHeight);
    if (touch.handled) {
      if (touch.activate) {
        openSelected();
      } else {
        requestUpdate();
      }
      return;
    }
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() >= 1000) {
    // Long press: rename / delete menu. The release is swallowed so it does not also open the book.
    showActionMenu(true);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelected();
    return;
  }

  const int listSize = static_cast<int>(rows.size());
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

void GlobalBookmarksListActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto safeArea =
      UITheme::getInstance().getScreenSafeArea(renderer, /*hasFrontButtonHints=*/true, /*hasSideButtonHints=*/false);

  GUI.drawHeader(renderer, Rect{safeArea.x, metrics.topPadding, safeArea.width, metrics.headerHeight},
                 tr(STR_ALL_BOOKMARKS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = safeArea.y + safeArea.height - contentTop - metrics.verticalSpacing;

  if (rows.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_BOOKMARKS));
  } else {
    GUI.drawList(
        renderer, Rect{safeArea.x, contentTop, safeArea.width, contentHeight}, static_cast<int>(rows.size()), selectedIndex,
        [this](int index) { return rowTitle(index); }, [this](int index) { return rowSubtitle(index); }, nullptr);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
