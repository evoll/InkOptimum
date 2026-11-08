#pragma once
#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

// Flat "All bookmarks" list across every book, newest first. Opening a row jumps
// straight to the bookmarked position in its book (no per-book intermediate list).
class GlobalBookmarksListActivity final : public Activity {
 public:
  explicit GlobalBookmarksListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("GlobalBookmarksList", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Lightweight row: keeps RAM small when many books carry bookmarks.
  struct Row {
    std::string bookPath;
    std::string bookTitle;
    std::string bookAuthor;
    std::string bookType;
    uint16_t spineIndex = 0;
    float progress = 0.0f;
    uint16_t paragraphIndex = 0xFFFF;
    uint32_t timestamp = 0;
    char chapterTitle[48] = {};
    char name[40] = {};  // BOOKMARK_NAME_MAX
  };

  static constexpr size_t kMaxRows = 200;

  std::vector<Row> rows;
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

  void reload();
  void openSelected();
  void showActionMenu(bool ignoreInitialConfirmRelease);
  void renameSelected();
  void deleteSelected();
  std::string rowTitle(int index) const;
  std::string rowSubtitle(int index) const;
};
