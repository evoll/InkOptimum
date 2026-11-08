#include "LibraryActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>

#include "InkMODSettings.h"
#include "LibraryIndex.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TouchListNavigation.h"

namespace {
constexpr unsigned long LONG_PRESS_MS = 800;

const char* tabLabel(library::Tab t) {
  switch (t) {
    case library::Tab::Books: return tr(STR_LIB_TAB_BOOKS);
    case library::Tab::Recent: return tr(STR_MENU_RECENT_BOOKS);
    case library::Tab::New: return tr(STR_LIB_TAB_NEW);
    case library::Tab::Authors: return tr(STR_LIB_TAB_AUTHORS);
  }
  return "";
}

const char* viewLabel(library::View v) {
  switch (v) {
    case library::View::Names: return tr(STR_LIB_VIEW_NAMES);
    case library::View::Details: return tr(STR_LIB_VIEW_DETAILS);
    case library::View::Covers: return tr(STR_LIB_VIEW_COVERS);
  }
  return "";
}

std::string detailLine(const library::Entry& e) {
  std::string s = e.author.empty() ? std::string(tr(STR_LIB_NO_AUTHOR)) : e.author;
  s += " · " + library::formatLabel(e.path);
  if (e.size > 0) s += " · " + library::humanSize(e.size);
  return s;
}
}  // namespace

void LibraryActivity::onEnter() {
  Activity::onEnter();
  const uint8_t v = std::min<uint8_t>(SETTINGS.libraryView, library::VIEW_COUNT - 1);
  view = static_cast<library::View>(v);

  const bool hadCache = !LIBRARY_INDEX.books().empty();
  if (!hadCache) {
    // First scan can take a few seconds on a big card: say so.
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_LIB_SCANNING));
    renderer.displayBuffer();
  }
  LIBRARY_INDEX.ensureLoaded(false);
  rebuild();
  requestUpdate();
}

void LibraryActivity::onExit() {
  Activity::onExit();
  list.clear();
  authors.clear();
}

void LibraryActivity::rebuild() {
  const auto& all = LIBRARY_INDEX.books();
  list.clear();
  authors.clear();
  switch (tab) {
    case library::Tab::Books:
      list = all;
      break;
    case library::Tab::Recent: {
      for (const auto& rb : RECENT_BOOKS.getBooks()) {
        library::Entry e;
        e.path = rb.path;
        e.title = rb.title;
        e.author = rb.author;
        for (const auto& a : all) {
          if (a.path == rb.path) {
            e.size = a.size;
            break;
          }
        }
        if (e.title.empty()) library::parseFileName(rb.path, e.title, e.author);
        list.push_back(std::move(e));
      }
      break;
    }
    case library::Tab::New: {
      std::vector<std::string> recentPaths;
      for (const auto& rb : RECENT_BOOKS.getBooks()) recentPaths.push_back(rb.path);
      list = library::newBooks(all, recentPaths);
      break;
    }
    case library::Tab::Authors:
      if (authorOpen) {
        list = library::booksOfAuthor(all, openAuthor);
      } else {
        authors = library::groupByAuthor(all);
      }
      break;
  }
  const int n = itemCount();
  if (selector >= n) selector = std::max(0, n - 1);
}

// The cached index can be stale (file deleted from a PC): drop the entry instead of failing in the reader.
void LibraryActivity::openBook(const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    LIBRARY_INDEX.removePath(path);
    rebuild();
    requestUpdate(true);
    return;
  }
  onSelectBook(path);
}

void LibraryActivity::switchTab(int dir) {
  tab = library::nextTab(tab, dir);
  authorOpen = false;
  selector = 0;
  rebuild();
  requestUpdate();
}

void LibraryActivity::cycleView() {
  view = library::nextView(view);
  SETTINGS.libraryView = static_cast<uint8_t>(view);
  SETTINGS.saveToFile();
  selector = 0;
  requestUpdate(true);
}

void LibraryActivity::loop() {
  if (longPressFired) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) longPressFired = false;
    return;
  }
  // Hold Confirm: cycle names -> details -> covers.
  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() >= LONG_PRESS_MS) {
    longPressFired = true;
    cycleView();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (tab == library::Tab::Authors && authorOpen) {
      authorOpen = false;
      selector = 0;
      rebuild();
      requestUpdate();
    } else {
      onGoHome();
    }
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing * 2 +
                         metrics.listRowHeight / 2;
  const Rect listRect{safeArea.x, contentTop, safeArea.width,
                      safeArea.y + safeArea.height - contentTop - metrics.verticalSpacing};

  if (mappedInput.hasTouch()) {
    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Left) {
      switchTab(1);
      return;
    }
    if (swipe == MappedInputManager::SwipeDir::Right) {
      switchTab(-1);
      return;
    }
    int tx = 0, ty = 0;
    if (mappedInput.wasScreenTapped(tx, ty)) {
      if (ty >= metrics.topPadding + metrics.headerHeight &&
          ty < metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight) {
        // Tabs are laid out left to right; pick by horizontal quarter.
        const int idx = std::clamp((tx - safeArea.x) * library::TAB_COUNT / std::max(1, safeArea.width), 0,
                                   library::TAB_COUNT - 1);
        if (idx != static_cast<int>(tab)) {
          tab = static_cast<library::Tab>(idx);
          authorOpen = false;
          selector = 0;
          rebuild();
          requestUpdate();
        }
        return;
      }
      if (ty >= metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight && ty < contentTop) {
        cycleView();
        return;
      }
    }
  }

  const int n = itemCount();
  const bool cover = view == library::View::Covers && !showsAuthors();

  if (onTabBar) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      switchTab(1);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      switchTab(-1);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down) && n > 0) {
      onTabBar = false;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      cycleView();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && n > 0) {
    if (showsAuthors()) {
      openAuthor = authors[selector].author;
      authorOpen = true;
      selector = 0;
      rebuild();
      requestUpdate();
    } else if (selector < static_cast<int>(list.size())) {
      openBook(list[selector].path);
    }
    return;
  }

  if (!cover && n > 0) {
    int idx = selector;
    const int rowH = std::max(metrics.listRowHeight, 56);
    auto touch = TouchListNavigation::handle(mappedInput, idx, n, listRect, rowH);
    if (touch.handled) {
      selector = idx;
      requestUpdate();
      if (touch.activate) {
        if (showsAuthors()) {
          openAuthor = authors[selector].author;
          authorOpen = true;
          selector = 0;
          rebuild();
        } else {
          openBook(list[selector].path);
          return;
        }
      }
      return;
    }
  }

  buttonNavigator.onNextRelease([this, n] {
    if (n > 0) selector = ButtonNavigator::nextIndex(selector, n);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, n] {
    if (selector == 0) {
      onTabBar = true;
    } else {
      selector = ButtonNavigator::previousIndex(selector, n);
    }
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, n] {
    if (n > 0) selector = ButtonNavigator::nextIndex(selector, n);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, n] {
    if (selector > 0) selector = ButtonNavigator::previousIndex(selector, n);
    requestUpdate();
  });
}

std::string LibraryActivity::coverThumbFor(const std::string& path, int w, int h) const {
  for (const auto& rb : RECENT_BOOKS.getBooks()) {
    if (rb.path == path && !rb.coverBmpPath.empty()) {
      const std::string thumb = UITheme::getCoverThumbPath(rb.coverBmpPath, w, h);
      if (!thumb.empty() && Storage.exists(thumb.c_str())) return thumb;
      return "";
    }
  }
  return "";
}

void LibraryActivity::renderCovers(const Rect& area) {
  const int cols = gridColumns();
  const int gap = 8;
  const int textH = renderer.getLineHeight(SMALL_FONT_ID) + 4;
  const int cellW = (area.width - gap * (cols + 1)) / cols;
  int coverH = cellW * 3 / 2;
  // Always fit at least two rows.
  coverH = std::min(coverH, (area.height - gap * 3) / 2 - textH);
  const int coverW = std::min(cellW, coverH * 2 / 3);
  const int cellH = coverH + textH + gap;
  const int rows = std::max(1, (area.height - gap) / cellH);
  const int perPage = rows * cols;
  const int pageStart = (selector / perPage) * perPage;
  
  for (int i = 0; i < perPage; ++i) {
    const int idx = pageStart + i;
    if (idx >= static_cast<int>(list.size())) break;
    const int cx = area.x + gap + (i % cols) * (cellW + gap) + (cellW - coverW) / 2;
    const int cy = area.y + gap + (i / cols) * cellH;
    const auto& e = list[idx];

    bool drawn = false;
    const std::string thumb = coverThumbFor(e.path, coverW, coverH);
    if (!thumb.empty()) {
      FsFile file;
      if (Storage.openFileForRead("LIB", thumb, file)) {
        Bitmap bmp(file);
        if (bmp.parseHeaders() == BmpReaderError::Ok) {
          renderer.drawBitmap(bmp, cx, cy, coverW, coverH);
          drawn = true;
        }
        file.close();
      }
    }
    renderer.drawRect(cx, cy, coverW, coverH);
    if (!drawn) {
      // Placeholder: format label centred in the empty frame.
      const std::string fmt = library::formatLabel(e.path);
      const int tw = renderer.getTextWidth(UI_10_FONT_ID, fmt.c_str());
      renderer.drawText(UI_10_FONT_ID, cx + (coverW - tw) / 2, cy + coverH / 2 - renderer.getLineHeight(UI_10_FONT_ID) / 2,
                        fmt.c_str());
    }
    if (idx == selector && !onTabBar) {
      renderer.drawRect(cx - 3, cy - 3, coverW + 6, coverH + 6, 3, true);
    }
    const std::string cap = renderer.truncatedText(SMALL_FONT_ID, e.title.c_str(), cellW);
    renderer.drawText(SMALL_FONT_ID, cx + (coverW - renderer.getTextWidth(SMALL_FONT_ID, cap.c_str())) / 2,
                      cy + coverH + 2, cap.c_str());
  }
}

void LibraryActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  GUI.drawHeader(renderer, Rect{safeArea.x, metrics.topPadding, safeArea.width, metrics.headerHeight},
                 tr(STR_LIBRARY));

  std::vector<TabInfo> tabs;
  for (int i = 0; i < library::TAB_COUNT; ++i) {
    const auto t = static_cast<library::Tab>(i);
    tabs.push_back({tabLabel(t), t == tab});
  }
  GUI.drawTabBar(renderer,
                 Rect{safeArea.x, metrics.topPadding + metrics.headerHeight, safeArea.width, metrics.tabBarHeight}, tabs,
                 onTabBar);

  const int subTop = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  const int subH = metrics.listRowHeight / 2;
  const std::string count = std::to_string(itemCount()) + " " + (showsAuthors() ? tr(STR_LIB_TAB_AUTHORS) : tr(STR_LIB_BOOKS_COUNT));
  const std::string subTitle = authorOpen ? (openAuthor.empty() ? std::string(tr(STR_LIB_NO_AUTHOR)) : openAuthor)
                                          : std::string(viewLabel(view));
  GUI.drawSubHeader(renderer, Rect{safeArea.x, subTop, safeArea.width, subH}, subTitle.c_str(), count.c_str());

  const int contentTop = subTop + subH + metrics.verticalSpacing;
  const Rect area{safeArea.x, contentTop, safeArea.width, safeArea.y + safeArea.height - contentTop - metrics.verticalSpacing};
  const int sel = onTabBar ? -1 : selector;

  if (itemCount() == 0) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, area.y + 20, tr(STR_LIB_EMPTY));
  } else if (showsAuthors()) {
    GUI.drawList(
        renderer, area, authors.size(), sel,
        [this](int i) { return authors[i].author.empty() ? std::string(tr(STR_LIB_NO_AUTHOR)) : authors[i].author; },
        nullptr, nullptr, [this](int i) { return std::to_string(authors[i].count); });
  } else if (view == library::View::Covers) {
    renderCovers(area);
  } else if (view == library::View::Details) {
    GUI.drawList(
        renderer, area, list.size(), sel, [this](int i) { return list[i].title; },
        [this](int i) { return detailLine(list[i]); }, [this](int i) { return UITheme::getFileIcon(list[i].path); });
  } else {
    GUI.drawList(
        renderer, area, list.size(), sel,
        [this](int i) { return library::baseName(list[i].path); }, nullptr,
        [this](int i) { return UITheme::getFileIcon(list[i].path); });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), onTabBar ? tr(STR_LIB_VIEW) : tr(STR_OPEN), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
