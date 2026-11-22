#include "ClockActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ClockFace.h"
#include "InkMODSettings.h"
#include "util/ClockStandby.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long kCheckIntervalMs = 5000;
constexpr int kFullRefreshEveryMinutes = 10;
constexpr unsigned long kStandbyAfterIdleMs = 15000;  // power saving: no input for this long, then the minute-wake loop
}  // namespace

void ClockActivity::onEnter() {
  Activity::onEnter();
  face_ = std::clamp<int>(SETTINGS.clockFace, 0, ClockFace::kFaceCount - 1);
  lastInputMs_ = millis();
  ecoAvailable_ = ClockStandby::available();
  shownMinuteOfDay_ = -1;
  minutesSinceFull_ = 0;
  lastCheckMs_ = millis();
  requestUpdate();
}

bool ClockActivity::readTime(int& hour24, int& minute) const {
  char buf[9];
  if (!halClock.isAvailable() || !halClock.formatTime(buf, sizeof(buf), SETTINGS.clockUtcOffsetQ, false)) return false;
  return sscanf(buf, "%d:%d", &hour24, &minute) == 2;
}

void ClockActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult cancelled;
    cancelled.isCancelled = true;
    setResult(std::move(cancelled));
    finish();
    return;
  }

  int tx = 0;
  int ty = 0;
  const bool tapped = mappedInput.hasTouch() && mappedInput.wasScreenTapped(tx, ty);
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || tapped) {
    face_ = (face_ + 1) % ClockFace::kFaceCount;
    SETTINGS.clockFace = static_cast<uint8_t>(face_);
    SETTINGS.saveToFile();
    lastInputMs_ = millis();
    renderer.promoteNextRefresh(HalDisplay::HALF_REFRESH);
    requestUpdate();
    return;
  }

  // Power saving (hardware-RTC boards only): the third button toggles it; after 15 s without input the clock paints a
  // frame without button hints and the device sleeps, waking once a minute to redraw (see ClockStandby).
  if (ecoAvailable_ && mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    SETTINGS.clockPowerSaving = SETTINGS.clockPowerSaving ? 0 : 1;
    SETTINGS.saveToFile();
    lastInputMs_ = millis();
    requestUpdate();
    return;
  }
  if (mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased()) lastInputMs_ = millis();

  const unsigned long now = millis();
  if (SETTINGS.clockPowerSaving && ecoAvailable_ && now - lastInputMs_ >= kStandbyAfterIdleMs &&
      shownMinuteOfDay_ >= 0) {
    ClockStandby::enter();  // does not return
    return;
  }
  if (now - lastCheckMs_ >= kCheckIntervalMs) {
    lastCheckMs_ = now;
    ecoAvailable_ = ClockStandby::available();
    int h = 0;
    int m = 0;
    const int minuteOfDay = readTime(h, m) ? h * 60 + m : -2;
    if (minuteOfDay != shownMinuteOfDay_) {
      if (++minutesSinceFull_ >= kFullRefreshEveryMinutes) {
        minutesSinceFull_ = 0;
        renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
      }
      requestUpdate();
    }
  }
}

void ClockActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int areaTop = metrics.topPadding;
  const int areaHeight = height - metrics.topPadding - metrics.buttonHintsHeight - 8;

  int hour = 0;
  int minute = 0;
  if (!readTime(hour, minute)) {
    shownMinuteOfDay_ = -2;
    const char* msg = tr(STR_CLOCK_NOT_SET);
    renderer.drawText(UI_12_FONT_ID, (width - renderer.getTextWidth(UI_12_FONT_ID, msg)) / 2,
                      areaTop + areaHeight / 2 - renderer.getLineHeight(UI_12_FONT_ID) / 2, msg);
  } else {
    shownMinuteOfDay_ = hour * 60 + minute;
    char date[13] = {};
    const bool hasDate = halClock.formatDate(date, sizeof(date), SETTINGS.clockUtcOffsetQ);

    ClockFace::draw(renderer, face_, SETTINGS.clockFormat == 1, hour, minute, hasDate ? date : nullptr, areaTop,
                    areaHeight);
  }

  const char* ecoLabel = !ecoAvailable_ ? "" : SETTINGS.clockPowerSaving ? tr(STR_CLOCK_ECO_ON) : tr(STR_CLOCK_ECO_OFF);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CLOCK_FACE), ecoLabel, "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
