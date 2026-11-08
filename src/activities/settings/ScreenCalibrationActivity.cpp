#include "ScreenCalibrationActivity.h"

#include <GfxRenderer.h>
#include <HalScreenCalibration.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void ScreenCalibrationActivity::onEnter() {
  Activity::onEnter();
  insets = renderer.getViewableInsets();
  saved = insets;
  selected = 0;
  requestUpdate();
}

void ScreenCalibrationActivity::onExit() {
  if (insets != saved) HalScreenCalibration::save(insets);
  Activity::onExit();
}

// The renderer rotates the portrait edges with the screen; the user edits what he sees.
unsigned ScreenCalibrationActivity::portraitEdge(const int orientedEdge) const {
  return static_cast<unsigned>((orientedEdge + 4 - static_cast<int>(renderer.getOrientation())) % 4);
}

void ScreenCalibrationActivity::adjust(const int delta) {
  if (selected == 4) {
    insets = ScreenInsets{};
  } else {
    insets.adjust(portraitEdge(selected), delta);
  }
  renderer.setViewableInsets(insets);  // live: every later screen, and this frame, use it at once
}

void ScreenCalibrationActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    selected = (selected + 1) % 5;
    requestUpdate();
    return;
  }
  buttonNavigator.onNextRelease([this] {
    adjust(+1);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    adjust(-1);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this] {
    adjust(+1);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this] {
    adjust(-1);
    requestUpdate();
  });
}

void ScreenCalibrationActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  int top = 0, right = 0, bottom = 0, left = 0;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);

  // The frame of the viewable area; the edge being edited is drawn four pixels thick.
  const int fx = left, fy = top, fw = std::max(0, w - left - right), fh = std::max(0, h - top - bottom);
  renderer.drawRect(fx, fy, fw, fh, true);
  constexpr int kThick = 4;
  switch (selected) {
    case 0: renderer.fillRect(fx, fy, fw, kThick, true); break;
    case 1: renderer.fillRect(fx + fw - kThick, fy, kThick, fh, true); break;
    case 2: renderer.fillRect(fx, fy + fh - kThick, fw, kThick, true); break;
    case 3: renderer.fillRect(fx, fy, kThick, fh, true); break;
    default: break;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{left + 8, top + metrics.topPadding, std::max(0, w - left - right - 16), metrics.headerHeight},
                 tr(STR_SCREEN_CALIBRATION));

  const char* const names[5] = {tr(STR_TOP), tr(STR_DIR_RIGHT), tr(STR_BOTTOM), tr(STR_DIR_LEFT),
                                tr(STR_CALIBRATION_RESET)};
  int y = h / 2 - 70;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_CALIBRATION_HINT));
  y += lineHeight * 2;
  for (int i = 0; i < 5; ++i) {
    char row[64];
    if (i < 4) {
      std::snprintf(row, sizeof(row), "%s %s: %d px", i == selected ? ">" : " ", names[i],
                    insets.edges[portraitEdge(i)]);
    } else {
      std::snprintf(row, sizeof(row), "%s %s", i == selected ? ">" : " ", names[i]);
    }
    renderer.drawCenteredText(UI_12_FONT_ID, y, row, true, i == selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    y += lineHeight + 14;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_NEXT_FIELD), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
