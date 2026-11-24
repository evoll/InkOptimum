#include "ShelvesActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <memory>

#include "MappedInputManager.h"
#include "activities/home/BookActions.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/OptionSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ShelfFile.h"

namespace {
constexpr unsigned long LONG_PRESS_MS = 1000;

std::string titleOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos && dot > 0) name.resize(dot);
  return name;
}
}  // namespace

void ShelvesActivity::onEnter() {
  Activity::onEnter();
  ShelfFile::load(store_);
  openShelf_ = -1;
  selected_ = 0;
  requestUpdate();
}

void ShelvesActivity::onExit() {
  Activity::onExit();
  store_ = shelves::ShelfStore();
}

void ShelvesActivity::save() { ShelfFile::save(store_); }

size_t ShelvesActivity::rowCount() const {
  if (openShelf_ < 0) return store_.shelves().size() + 1;  // + "New shelf"
  return store_.shelves()[static_cast<size_t>(openShelf_)].books.size();
}

void ShelvesActivity::pruneMissing(const size_t shelf) {
  const auto books = store_.shelves()[shelf].books;  // copy: removeBook edits the list
  bool changed = false;
  for (const auto& b : books) {
    if (!Storage.exists(b.c_str())) {
      store_.removeBook(shelf, b);
      changed = true;
    }
  }
  if (changed) save();
}

void ShelvesActivity::activate() {
  if (openShelf_ < 0) {
    if (selected_ >= store_.shelves().size()) {
      createShelf();
      return;
    }
    openShelf_ = static_cast<int>(selected_);
    pruneMissing(selected_);
    selected_ = 0;
    requestUpdate();
    return;
  }
  const auto& books = store_.shelves()[static_cast<size_t>(openShelf_)].books;
  if (selected_ < books.size()) onSelectBook(books[selected_]);
}

void ShelvesActivity::createShelf() {
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SHELF_NAME), "",
                                                                 shelves::ShelfStore::kMaxNameBytes),
                         [this](const ActivityResult& r) {
                           if (r.isCancelled) return;
                           const int idx = store_.create(std::get<KeyboardResult>(r.data).text);
                           if (idx < 0) {
                             BookActions::drawToast(renderer, tr(STR_SHELF_BAD_NAME));
                             delay(900);
                             return;
                           }
                           save();
                           selected_ = static_cast<size_t>(idx);
                         });
}

void ShelvesActivity::renameShelf(const size_t index) {
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SHELF_NAME),
                                                                 store_.shelves()[index].name,
                                                                 shelves::ShelfStore::kMaxNameBytes),
                         [this, index](const ActivityResult& r) {
                           if (r.isCancelled) return;
                           if (store_.rename(index, std::get<KeyboardResult>(r.data).text)) {
                             save();
                           } else {
                             BookActions::drawToast(renderer, tr(STR_SHELF_BAD_NAME));
                             delay(900);
                           }
                         });
}

void ShelvesActivity::deleteShelf(const size_t index) {
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE),
                                                                store_.shelves()[index].name),
                         [this, index](const ActivityResult& r) {
                           if (r.isCancelled) return;
                           store_.removeShelf(index);
                           save();
                           selected_ = std::min(selected_, store_.shelves().size());
                         });
}

void ShelvesActivity::removeBook(const size_t shelf, const size_t bookIndex) {
  const auto& books = store_.shelves()[shelf].books;
  if (bookIndex >= books.size()) return;
  store_.removeBook(shelf, std::string(books[bookIndex]));
  save();
  const size_t n = store_.shelves()[shelf].books.size();
  selected_ = n == 0 ? 0 : std::min(selected_, n - 1);
}

void ShelvesActivity::showMenu() {
  if (openShelf_ < 0) {
    if (selected_ >= store_.shelves().size()) return;
    const size_t index = selected_;
    std::vector<std::string> options = {tr(STR_RENAME), tr(STR_DELETE)};
    startActivityForResult(std::make_unique<OptionSelectionActivity>(renderer, mappedInput, "ShelfMenu",
                                                                     StrId::STR_SHELVES, options, 0, false, false),
                           [this, index](const ActivityResult& r) {
                             if (r.isCancelled) return;
                             if (std::get<OptionSelectionResult>(r.data).index == 0) {
                               renameShelf(index);
                             } else {
                               deleteShelf(index);
                             }
                           });
    return;
  }
  const size_t shelf = static_cast<size_t>(openShelf_);
  const size_t book = selected_;
  if (book >= store_.shelves()[shelf].books.size()) return;
  std::vector<std::string> options = {tr(STR_REMOVE_FROM_SHELF)};
  startActivityForResult(std::make_unique<OptionSelectionActivity>(renderer, mappedInput, "ShelfBookMenu",
                                                                   StrId::STR_SHELVES, options, 0, false, false),
                         [this, shelf, book](const ActivityResult& r) {
                           if (!r.isCancelled) removeBook(shelf, book);
                         });
}

void ShelvesActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (openShelf_ >= 0) {
      selected_ = static_cast<size_t>(openShelf_);
      openShelf_ = -1;
      requestUpdate();
    } else {
      ActivityResult cancelled;
      cancelled.isCancelled = true;
      setResult(std::move(cancelled));
      finish();
    }
    return;
  }

  if (longPressFired_) {  // swallow the release that ends a long press
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) longPressFired_ = false;
    return;
  }
  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() >= LONG_PRESS_MS) {
    longPressFired_ = true;
    showMenu();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activate();
    return;
  }

  const int n = static_cast<int>(rowCount());
  nav_.onNextRelease([this, n] {
    selected_ = ButtonNavigator::nextIndex(static_cast<int>(selected_), n);
    requestUpdate();
  });
  nav_.onPreviousRelease([this, n] {
    selected_ = ButtonNavigator::previousIndex(static_cast<int>(selected_), n);
    requestUpdate();
  });
  nav_.onNextContinuous([this, n] {
    selected_ = ButtonNavigator::nextIndex(static_cast<int>(selected_), n);
    requestUpdate();
  });
  nav_.onPreviousContinuous([this, n] {
    selected_ = ButtonNavigator::previousIndex(static_cast<int>(selected_), n);
    requestUpdate();
  });
}

void ShelvesActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const std::string heading = openShelf_ < 0 ? std::string(tr(STR_SHELVES))
                                             : store_.shelves()[static_cast<size_t>(openShelf_)].name;
  GUI.drawHeader(renderer, Rect{safeArea.x, metrics.topPadding, safeArea.width, metrics.headerHeight},
                 heading.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = safeArea.y + safeArea.height - contentTop - metrics.verticalSpacing;
  const size_t rows = rowCount();
  if (openShelf_ >= 0 && rows == 0) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_SHELF_EMPTY));
  } else if (openShelf_ < 0) {
    GUI.drawList(
        renderer, Rect{safeArea.x, contentTop, safeArea.width, contentHeight}, rows, selected_,
        [this](int i) -> std::string {
          return static_cast<size_t>(i) < store_.shelves().size() ? store_.shelves()[static_cast<size_t>(i)].name
                                                                    : std::string(tr(STR_NEW_SHELF));
        },
        [this](int i) -> std::string {
          return static_cast<size_t>(i) < store_.shelves().size()
                     ? std::to_string(store_.shelves()[static_cast<size_t>(i)].books.size())
                     : std::string();
        });
  } else {
    const auto& books = store_.shelves()[static_cast<size_t>(openShelf_)].books;
    GUI.drawList(
        renderer, Rect{safeArea.x, contentTop, safeArea.width, contentHeight}, rows, selected_,
        [&books](int i) { return titleOf(books[static_cast<size_t>(i)]); }, nullptr,
        [&books](int i) { return UITheme::getFileIcon(books[static_cast<size_t>(i)]); });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
