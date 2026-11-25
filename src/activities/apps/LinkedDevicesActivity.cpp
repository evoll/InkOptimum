#include "LinkedDevicesActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <memory>

#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/CompanionApi.h"

namespace {
constexpr const char* TOKENS_PATH = "/.crosspoint/companion_tokens.txt";
}

void LinkedDevicesActivity::load() {
  store_ = companion::TokenStore();
  if (!Storage.exists(TOKENS_PATH)) return;
  const String text = Storage.readFile(TOKENS_PATH);
  if (text.length()) store_.load(std::string(text.c_str(), text.length()));
}

void LinkedDevicesActivity::onEnter() {
  Activity::onEnter();
  load();
  selected_ = 0;
  requestUpdate();
}

void LinkedDevicesActivity::onExit() {
  Activity::onExit();
  store_ = companion::TokenStore();
}

void LinkedDevicesActivity::revokeSelected() {
  if (selected_ >= store_.tokens().size()) return;
  const std::string id = store_.tokens()[selected_].id;
  const std::string name = store_.tokens()[selected_].name;
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_UNPAIR_DEVICE), name),
                         [this, id](const ActivityResult& r) {
                           if (r.isCancelled) return;
                           if (store_.revoke(id)) {
                             Storage.mkdir("/.crosspoint");
                             Storage.writeFile(TOKENS_PATH, store_.serialize().c_str());
                             companionapi::invalidateTokens();
                           }
                           const size_t n = store_.tokens().size();
                           selected_ = n == 0 ? 0 : (selected_ >= n ? n - 1 : selected_);
                         });
}

void LinkedDevicesActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult cancelled;
    cancelled.isCancelled = true;
    setResult(std::move(cancelled));
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    revokeSelected();
    return;
  }
  const int n = static_cast<int>(store_.tokens().size());
  if (n == 0) return;
  nav_.onNextRelease([this, n] {
    selected_ = ButtonNavigator::nextIndex(static_cast<int>(selected_), n);
    requestUpdate();
  });
  nav_.onPreviousRelease([this, n] {
    selected_ = ButtonNavigator::previousIndex(static_cast<int>(selected_), n);
    requestUpdate();
  });
}

void LinkedDevicesActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{safeArea.x, metrics.topPadding, safeArea.width, metrics.headerHeight},
                 tr(STR_LINKED_DEVICES));
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = safeArea.y + safeArea.height - contentTop - metrics.verticalSpacing;
  const size_t rows = store_.tokens().size();
  if (rows == 0) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_LINKED_DEVICES));
  } else {
    GUI.drawList(
        renderer, Rect{safeArea.x, contentTop, safeArea.width, contentHeight}, rows, selected_,
        [this](int i) { return store_.tokens()[static_cast<size_t>(i)].name; }, nullptr, nullptr,
        [this](int i) { return store_.tokens()[static_cast<size_t>(i)].id; });
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), rows ? tr(STR_UNPAIR_DEVICE) : "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
