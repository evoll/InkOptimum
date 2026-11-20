#pragma once

#include <Epub.h>

#include <memory>
#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

// Full-text search in the open book. Scans the chapters one after another (one chapter per loop
// tick, so the progress screen refreshes and Back stops the scan), then lists the matches with the
// chapter name and a text snippet. Confirm returns a BookmarkResult the reader jumps to exactly like
// a bookmark (chapter + position in it + paragraph number).
class BookSearchActivity final : public Activity {
 public:
  explicit BookSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Epub> epub,
                              std::string query, int currentSpineIndex)
      : Activity("BookSearch", renderer, mappedInput),
        epub(std::move(epub)),
        query(std::move(query)),
        currentSpineIndex(currentSpineIndex) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool allowPowerAsConfirmInReaderMode() const override { return true; }

 private:
  struct Row {
    uint16_t spineIndex = 0;
    float progress = 0.0f;
    uint16_t paragraphIndex = 0;
    std::string snippet;
    std::string chapter;
  };
  enum class Phase { Scanning, Results };

  static constexpr size_t kMaxHits = 200;

  std::shared_ptr<Epub> epub;
  std::string query;
  int currentSpineIndex;
  Phase phase = Phase::Scanning;
  int nextSpine = 0;
  int spineCount = 0;
  bool cancelPending = false;
  bool capped = false;
  std::vector<Row> rows;
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

  void scanOneChapter();
  void enterResults();
  void openSelected();
  int getPageItems() const;
};
