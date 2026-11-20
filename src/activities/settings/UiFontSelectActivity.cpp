#include "UiFontSelectActivity.h"

#include <I18n.h>

#include <algorithm>
#include <cstring>

#include "I18nKeys.h"
#include "InkMODSettings.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"

void UiFontSelectActivity::onEnter() {
  Activity::onEnter();
  families_.clear();
  if (registry_) {
    for (const auto& family : registry_->getFamilies()) {
      if (!family.vector) families_.push_back(family.name);
    }
  }
  totalItems = 1 + static_cast<int>(families_.size());
  selectedIndex = 0;
  for (size_t i = 0; i < families_.size(); ++i) {
    if (families_[i] == SETTINGS.uiFontFamily) selectedIndex = 1 + static_cast<int>(i);
  }
  requestUpdate();
}

void UiFontSelectActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }
  buttonNavigator.onNextRelease([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, totalItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, totalItems);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, totalItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, totalItems);
    requestUpdate();
  });
}

void UiFontSelectActivity::handleSelection() {
  if (selectedIndex == 0) {
    SETTINGS.uiFontFamily[0] = '\0';
  } else {
    strncpy(SETTINGS.uiFontFamily, families_[selectedIndex - 1].c_str(), sizeof(SETTINGS.uiFontFamily) - 1);
    SETTINGS.uiFontFamily[sizeof(SETTINGS.uiFontFamily) - 1] = '\0';
  }
  {
    RenderLock lock(*this);
    sdFontSystem.applyUiFont(renderer);
  }
  SETTINGS.saveToFile();
  finish();
}

void UiFontSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{safeArea.x, metrics.topPadding, safeArea.width, metrics.headerHeight}, tr(STR_UI_FONT));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = safeArea.y + safeArea.height - contentTop - metrics.verticalSpacing;
  GUI.drawList(
      renderer, Rect{safeArea.x, contentTop, safeArea.width, contentHeight}, totalItems, selectedIndex,
      [this](int index) { return index == 0 ? std::string(tr(STR_UI_FONT_AUTO)) : families_[index - 1]; }, nullptr,
      nullptr,
      [this](int index) {
        const bool current = index == 0 ? SETTINGS.uiFontFamily[0] == '\0' : families_[index - 1] == SETTINGS.uiFontFamily;
        return current ? tr(STR_SELECTED) : "";
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
