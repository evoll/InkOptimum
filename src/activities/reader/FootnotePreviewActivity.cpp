#include "FootnotePreviewActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include <algorithm>

#include "components/UITheme.h"
#include "fontIds.h"

void FootnotePreviewActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void FootnotePreviewActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      mappedInput.wasReleased(MappedInputManager::Button::Power)) {
    setResult(MenuResult{0});
    finish();
  }
}

void FootnotePreviewActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto safeArea =
      UITheme::getInstance().getScreenSafeArea(renderer, /*hasFrontButtonHints=*/true, /*hasSideButtonHints=*/false);

  const std::string title = marker.empty() ? std::string(tr(STR_FOOTNOTES)) : std::string(tr(STR_FOOTNOTES)) + " " + marker;
  GUI.drawHeader(renderer, Rect{safeArea.x, metrics.topPadding, safeArea.width, metrics.headerHeight}, title.c_str());

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bottom = safeArea.y + safeArea.height - metrics.verticalSpacing;
  const int width = safeArea.width - 2 * metrics.contentSidePadding;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int maxLines = std::max(1, (bottom - top) / std::max(1, lineHeight));

  const auto lines = renderer.wrappedText(UI_10_FONT_ID, text.c_str(), width, maxLines);
  int y = top;
  for (const auto& line : lines) {
    renderer.drawText(UI_10_FONT_ID, safeArea.x + metrics.contentSidePadding, y, line.c_str());
    y += lineHeight;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_NOTE_READ_FULL), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
