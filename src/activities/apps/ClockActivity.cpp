#include "ClockActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "InkMODSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long kCheckIntervalMs = 5000;
constexpr int kFullRefreshEveryMinutes = 10;
constexpr float kPi = 3.14159265f;

// Seven segments a..g as bits 0..6: a top, b upper right, c lower right, d bottom, e lower left, f upper left, g middle.
constexpr uint8_t kSegments[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

// Face remembered while the device stays on.
int g_lastFace = 0;
}  // namespace

void ClockActivity::onEnter() {
  Activity::onEnter();
  face_ = g_lastFace;
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
    face_ = (face_ + 1) % FACE_COUNT;
    g_lastFace = face_;
    renderer.promoteNextRefresh(HalDisplay::HALF_REFRESH);
    requestUpdate();
    return;
  }

  const unsigned long now = millis();
  if (now - lastCheckMs_ >= kCheckIntervalMs) {
    lastCheckMs_ = now;
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

void ClockActivity::drawSegmentDigit(const int digit, const int x, const int y, const int w, const int h,
                                     const int t) const {
  if (digit < 0 || digit > 9) return;
  const uint8_t seg = kSegments[digit];
  const int halfH = h / 2;
  const int inner = w - 2 * t;
  if (seg & 0x01) renderer.fillRect(x + t, y, inner, t, true);                          // a
  if (seg & 0x02) renderer.fillRect(x + w - t, y + t / 2, t, halfH - t / 2, true);      // b
  if (seg & 0x04) renderer.fillRect(x + w - t, y + halfH, t, halfH - t / 2, true);      // c
  if (seg & 0x08) renderer.fillRect(x + t, y + h - t, inner, t, true);                  // d
  if (seg & 0x10) renderer.fillRect(x, y + halfH, t, halfH - t / 2, true);              // e
  if (seg & 0x20) renderer.fillRect(x, y + t / 2, t, halfH - t / 2, true);              // f
  if (seg & 0x40) renderer.fillRect(x + t, y + halfH - t / 2, inner, t, true);          // g
}

void ClockActivity::drawDigits(const int hour24, const int minute, const int left, const int top, const int width,
                               const int height) const {
  int hour = hour24;
  if (SETTINGS.clockFormat == 1) {
    hour = hour24 % 12;
    if (hour == 0) hour = 12;
  }
  // Four digits plus a colon: digit width d, gap g, colon width c.
  const int d = std::min(width * 17 / 100, height * 11 / 20);  // 4d + 5g + c = 5.33 d <= 0.91 width
  const int g = d / 5;
  const int c = d / 3;
  const int t = std::max(4, d / 5);
  const int total = 4 * d + 3 * g + 2 * g + c;
  int x = left + (width - total) / 2;
  const int y = top + (height - d * 2) / 2;
  const int h = d * 2;

  const int digits[4] = {hour / 10, hour % 10, minute / 10, minute % 10};
  // A leading zero is dropped in 12-hour mode ("9:05", not "09:05"); keep its space so the colon stays put.
  for (int i = 0; i < 4; i++) {
    if (i == 2) {
      const int cx = x + g + c / 2;
      renderer.fillRect(cx - t / 2, y + h / 3 - t / 2, t, t, true);
      renderer.fillRect(cx - t / 2, y + 2 * h / 3 - t / 2, t, t, true);
      x += 2 * g + c;
    }
    if (!(i == 0 && SETTINGS.clockFormat == 1 && digits[0] == 0)) drawSegmentDigit(digits[i], x, y, d, h, t);
    x += d + g;
  }
}

void ClockActivity::drawAnalog(const int hour24, const int minute, const int cx, const int cy,
                               const int radius) const {
  // Rim as a polyline.
  constexpr int kSteps = 96;
  for (int i = 0; i < kSteps; i++) {
    const float a0 = 2.f * kPi * i / kSteps;
    const float a1 = 2.f * kPi * (i + 1) / kSteps;
    renderer.drawLine(cx + static_cast<int>(radius * std::sin(a0)), cy - static_cast<int>(radius * std::cos(a0)),
                      cx + static_cast<int>(radius * std::sin(a1)), cy - static_cast<int>(radius * std::cos(a1)), 3,
                      true);
  }
  // Hour ticks.
  for (int i = 0; i < 12; i++) {
    const float a = 2.f * kPi * i / 12.f;
    const bool major = i % 3 == 0;
    const int inner = radius - (major ? radius / 7 : radius / 12);
    const int outer = radius - 6;
    renderer.drawLine(cx + static_cast<int>(inner * std::sin(a)), cy - static_cast<int>(inner * std::cos(a)),
                      cx + static_cast<int>(outer * std::sin(a)), cy - static_cast<int>(outer * std::cos(a)),
                      major ? 5 : 2, true);
  }
  // Hands: hour moves with the minutes.
  const float hourAngle = 2.f * kPi * (((hour24 % 12) * 60 + minute) / 720.f);
  const float minuteAngle = 2.f * kPi * (minute / 60.f);
  const int hourLen = radius * 5 / 10;
  const int minuteLen = radius * 8 / 10;
  renderer.drawLine(cx, cy, cx + static_cast<int>(hourLen * std::sin(hourAngle)),
                    cy - static_cast<int>(hourLen * std::cos(hourAngle)), 8, true);
  renderer.drawLine(cx, cy, cx + static_cast<int>(minuteLen * std::sin(minuteAngle)),
                    cy - static_cast<int>(minuteLen * std::cos(minuteAngle)), 4, true);
  renderer.fillRect(cx - 6, cy - 6, 12, 12, true);
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

    switch (face_) {
      case FACE_DIGITAL:
        drawDigits(hour, minute, 0, areaTop, width, areaHeight);
        break;
      case FACE_DIGITAL_DATE: {
        const int digitsHeight = areaHeight * 2 / 3;
        drawDigits(hour, minute, 0, areaTop, width, digitsHeight);
        if (hasDate) {
          const int dateY = areaTop + digitsHeight + (areaHeight - digitsHeight) / 2 -
                            renderer.getLineHeight(UI_12_FONT_ID) / 2;
          renderer.drawText(UI_12_FONT_ID, (width - renderer.getTextWidth(UI_12_FONT_ID, date)) / 2, dateY, date, true,
                            EpdFontFamily::BOLD);
        }
        break;
      }
      case FACE_ANALOG: {
        const int dateRoom = hasDate ? renderer.getLineHeight(UI_10_FONT_ID) + 16 : 0;
        const int radius = std::min(width / 2 - 16, (areaHeight - dateRoom) / 2 - 8);
        const int cy = areaTop + (areaHeight - dateRoom) / 2;
        drawAnalog(hour, minute, width / 2, cy, radius);
        if (hasDate) {
          renderer.drawText(UI_10_FONT_ID, (width - renderer.getTextWidth(UI_10_FONT_ID, date)) / 2,
                            areaTop + areaHeight - renderer.getLineHeight(UI_10_FONT_ID) - 4, date);
        }
        break;
      }
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CLOCK_FACE), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
