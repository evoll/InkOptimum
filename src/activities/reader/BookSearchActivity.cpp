#include "BookSearchActivity.h"

#include <Epub/BookTextSearch.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Print.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int ROW_HEIGHT = 68;
constexpr int LIST_START_Y = 60;

class SearchSink final : public Print {
 public:
  explicit SearchSink(book_search::Matcher& m) : matcher(m) {}
  size_t write(const uint8_t c) override {
    const char ch = static_cast<char>(c);
    matcher.feed(&ch, 1);
    return 1;
  }
  size_t write(const uint8_t* buffer, const size_t size) override {
    matcher.feed(reinterpret_cast<const char*>(buffer), size);
    return size;
  }

 private:
  book_search::Matcher& matcher;
};
}  // namespace

int BookSearchActivity::getPageItems() const {
  const int hintGutterHeight = renderer.getOrientation() == GfxRenderer::Orientation::PortraitInverted ? 50 : 0;
  const int available = renderer.getScreenHeight() - (LIST_START_Y + hintGutterHeight) - ROW_HEIGHT;
  return std::max(1, available / ROW_HEIGHT);
}

void BookSearchActivity::onEnter() {
  Activity::onEnter();
  spineCount = epub ? epub->getSpineItemsCount() : 0;
  nextSpine = 0;
  phase = spineCount > 0 ? Phase::Scanning : Phase::Results;
  requestUpdate();
}

void BookSearchActivity::onExit() { Activity::onExit(); }

void BookSearchActivity::scanOneChapter() {
  const int spine = nextSpine++;
  book_search::Matcher matcher(query, kMaxHits - rows.size());
  if (!matcher.valid()) return;
  const auto entry = epub->getSpineItem(spine);
  if (entry.href.empty()) return;
  SearchSink sink(matcher);
  if (!epub->readItemContentsToStream(entry.href, sink, 1024)) return;
  matcher.finish();

  const size_t total = std::max<size_t>(1, matcher.bytesConsumed());
  std::string chapter;
  const int tocIndex = epub->getTocIndexForSpineIndex(spine);
  if (tocIndex >= 0) chapter = epub->getTocItem(tocIndex).title;

  std::vector<Row> found;
  for (const auto& h : matcher.hits()) {
    Row r;
    r.spineIndex = static_cast<uint16_t>(spine);
    r.progress = std::min(0.999f, static_cast<float>(h.byteOffset) / static_cast<float>(total));
    r.paragraphIndex = h.paragraphIndex;
    r.snippet = h.snippet;
    r.chapter = chapter;
    found.push_back(std::move(r));
  }
  if (found.empty()) return;
  RenderLock lock(*this);
  for (auto& r : found) rows.push_back(std::move(r));
  if (rows.size() >= kMaxHits) capped = true;
}

void BookSearchActivity::enterResults() {
  RenderLock lock(*this);
  phase = Phase::Results;
  selectedIndex = 0;
  for (size_t i = 0; i < rows.size(); i++) {
    if (rows[i].spineIndex >= currentSpineIndex) {
      selectedIndex = static_cast<int>(i);
      break;
    }
  }
}

void BookSearchActivity::openSelected() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(rows.size())) return;
  const Row& r = rows[selectedIndex];
  setResult(BookmarkResult{r.spineIndex, r.progress, r.paragraphIndex});
  finish();
}

void BookSearchActivity::loop() {
  if (phase == Phase::Scanning) {
    // One chapter blocks for a moment, so a short tap can fall between two polls: remember the press
    // and act when the key is up again.
    if (mappedInput.isPressed(MappedInputManager::Button::Back)) cancelPending = true;
    if (cancelPending && !mappedInput.isPressed(MappedInputManager::Button::Back)) {
      cancelPending = false;
      if (rows.empty()) {
        ActivityResult cancelled;
        cancelled.isCancelled = true;
        setResult(std::move(cancelled));
        finish();
      } else {
        enterResults();
        requestUpdate();
      }
      return;
    }
    if (cancelPending) return;
    if (nextSpine >= spineCount || rows.size() >= kMaxHits) {
      enterResults();
      requestUpdate();
      return;
    }
    scanOneChapter();
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult cancelled;
    cancelled.isCancelled = true;
    setResult(std::move(cancelled));
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelected();
    return;
  }

  const int total = static_cast<int>(rows.size());
  if (total == 0) return;

  if (mappedInput.hasTouch()) {
    const int pageItems = getPageItems();
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
    int tx = 0, ty = 0;
    if (mappedInput.wasScreenTapped(tx, ty)) {
      const auto orientation = renderer.getOrientation();
      const bool landscape = orientation == GfxRenderer::Orientation::LandscapeClockwise ||
                             orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
      const int gutterW = landscape ? 30 : 0;
      const int contentX = orientation == GfxRenderer::Orientation::LandscapeClockwise ? gutterW : 0;
      const int contentY = orientation == GfxRenderer::Orientation::PortraitInverted ? 50 : 0;
      const int listTop = LIST_START_Y + contentY;
      if (tx >= contentX && tx < contentX + renderer.getScreenWidth() - gutterW && ty >= listTop) {
        const int row = (ty - listTop) / ROW_HEIGHT;
        const int index = (selectedIndex / pageItems) * pageItems + row;
        if (row >= 0 && row < pageItems && index < total) {
          selectedIndex = index;
          openSelected();
          return;
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

void BookSearchActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto orientation = renderer.getOrientation();
  const bool landscape = orientation == GfxRenderer::Orientation::LandscapeClockwise ||
                         orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const int hintGutterWidth = landscape ? 30 : 0;
  const int contentX = orientation == GfxRenderer::Orientation::LandscapeClockwise ? hintGutterWidth : 0;
  const int contentWidth = renderer.getScreenWidth() - hintGutterWidth;
  const int contentY = orientation == GfxRenderer::Orientation::PortraitInverted ? 50 : 0;

  const char* title = tr(STR_SEARCH_BOOK);
  const int titleX = contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, title, EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, title, true, EpdFontFamily::BOLD);

  if (phase == Phase::Scanning) {
    const int midY = renderer.getScreenHeight() / 2 - 40;
    renderer.drawCenteredText(UI_10_FONT_ID, midY, tr(STR_SEARCHING));
    const std::string q = renderer.truncatedText(UI_10_FONT_ID, ("\"" + query + "\"").c_str(), contentWidth - 40);
    renderer.drawCenteredText(UI_10_FONT_ID, midY + 32, q.c_str());
    const int barW = contentWidth - 80;
    GUI.drawProgressBar(
        renderer, Rect{contentX + 40, midY + 76, barW, 16}, static_cast<size_t>(std::min(nextSpine, spineCount)),
        static_cast<size_t>(std::max(1, spineCount)));
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(rows.size()));
    renderer.drawCenteredText(UI_10_FONT_ID, midY + 110, buf);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
    renderer.displayBuffer();
    return;
  }

  if (rows.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, LIST_START_Y + contentY + 20, tr(STR_SEARCH_NO_RESULTS));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
    renderer.displayBuffer();
    return;
  }

  const int pageItems = getPageItems();
  const int total = static_cast<int>(rows.size());
  const int pageStart = (selectedIndex / pageItems) * pageItems;
  const int marginLeft = contentX + 20;

  for (int i = 0; i < pageItems; i++) {
    const int idx = pageStart + i;
    if (idx >= total) break;
    const int rowY = LIST_START_Y + contentY + i * ROW_HEIGHT;
    const bool selected = idx == selectedIndex;
    if (selected) renderer.fillRect(contentX, rowY, contentWidth - 1, ROW_HEIGHT, true);

    const Row& r = rows[idx];
    const std::string snip = renderer.truncatedText(UI_10_FONT_ID, r.snippet.c_str(), contentWidth - 40);
    renderer.drawText(UI_10_FONT_ID, marginLeft, rowY + 5, snip.c_str(), !selected);
    const char* chapter = r.chapter.empty() ? tr(STR_UNKNOWN_CHAPTER) : r.chapter.c_str();
    const std::string ch = renderer.truncatedText(SMALL_FONT_ID, chapter, contentWidth - 40);
    renderer.drawText(SMALL_FONT_ID, marginLeft, rowY + 27, ch.c_str(), !selected);
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%d / %d", idx + 1, total);
    renderer.drawText(SMALL_FONT_ID, marginLeft, rowY + 47, buf, !selected);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  renderer.displayBuffer();
}
