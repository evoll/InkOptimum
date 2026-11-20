#pragma once
#include <Epub.h>

#include <memory>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class EpubReaderChapterSelectionActivity final : public Activity {
  std::shared_ptr<Epub> epub;
  std::string epubPath;
  ButtonNavigator buttonNavigator;
  int currentSpineIndex = 0;
  int currentTocIndex = -1;
  int selectorIndex = 0;

  // Number of items that fit on a page, derived from logical screen height.
  // This adapts automatically when switching between portrait and landscape.
  int getPageItems() const;

  // Total number of currently visible rows (collapsed branches are hidden).
  int getTotalItems() const;

  // Nested contents: rows are indices into the epub TOC; parents with children can be collapsed.
  std::vector<uint8_t> levels;    // TOC level per entry
  std::vector<uint8_t> expanded;  // 1 = branch open (only meaningful for parents)
  std::vector<int> visible;       // visible row -> TOC index
  bool hasNested = false;
  bool isParent(int tocIndex) const {
    return tocIndex + 1 < static_cast<int>(levels.size()) && levels[tocIndex + 1] > levels[tocIndex];
  }
  void rebuildVisible(int keepTocIndex);
  int tocIndexAt(int row) const { return hasNested ? visible[row] : row; }
  void openRow(int row);

 public:
  explicit EpubReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                              const std::shared_ptr<Epub>& epub, const std::string& epubPath,
                                              const int currentSpineIndex, const int currentTocIndex = -1)
      : Activity("EpubReaderChapterSelection", renderer, mappedInput),
        epub(epub),
        epubPath(epubPath),
        currentSpineIndex(currentSpineIndex),
        currentTocIndex(currentTocIndex) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool allowPowerAsConfirmInReaderMode() const override { return true; }
};
