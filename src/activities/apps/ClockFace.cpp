#include "ClockFace.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cmath>

#include "fontIds.h"

namespace ClockFace {
namespace {
constexpr float kPi = 3.14159265f;

// Seven segments a..g as bits 0..6: a top, b upper right, c lower right, d bottom, e lower left, f upper left, g middle.
constexpr uint8_t kSegments[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

void drawSegmentDigit(GfxRenderer& r, const int digit, const int x, const int y, const int w, const int h,
                                     const int t) {
  if (digit < 0 || digit > 9) return;
  const uint8_t seg = kSegments[digit];
  const int halfH = h / 2;
  const int inner = w - 2 * t;
  if (seg & 0x01) r.fillRect(x + t, y, inner, t, true);                          // a
  if (seg & 0x02) r.fillRect(x + w - t, y + t / 2, t, halfH - t / 2, true);      // b
  if (seg & 0x04) r.fillRect(x + w - t, y + halfH, t, halfH - t / 2, true);      // c
  if (seg & 0x08) r.fillRect(x + t, y + h - t, inner, t, true);                  // d
  if (seg & 0x10) r.fillRect(x, y + halfH, t, halfH - t / 2, true);              // e
  if (seg & 0x20) r.fillRect(x, y + t / 2, t, halfH - t / 2, true);              // f
  if (seg & 0x40) r.fillRect(x + t, y + halfH - t / 2, inner, t, true);          // g
}

void drawDigits(GfxRenderer& r, const bool use12h, const int hour24, const int minute, const int left, const int top, const int width,
                               const int height) {
  int hour = hour24;
  if (use12h) {
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
      r.fillRect(cx - t / 2, y + h / 3 - t / 2, t, t, true);
      r.fillRect(cx - t / 2, y + 2 * h / 3 - t / 2, t, t, true);
      x += 2 * g + c;
    }
    if (!(i == 0 && use12h && digits[0] == 0)) drawSegmentDigit(r, digits[i], x, y, d, h, t);
    x += d + g;
  }
}

void drawAnalog(GfxRenderer& r, const int hour24, const int minute, const int cx, const int cy,
                               const int radius) {
  // Rim as a polyline.
  constexpr int kSteps = 96;
  for (int i = 0; i < kSteps; i++) {
    const float a0 = 2.f * kPi * i / kSteps;
    const float a1 = 2.f * kPi * (i + 1) / kSteps;
    r.drawLine(cx + static_cast<int>(radius * std::sin(a0)), cy - static_cast<int>(radius * std::cos(a0)),
                      cx + static_cast<int>(radius * std::sin(a1)), cy - static_cast<int>(radius * std::cos(a1)), 3,
                      true);
  }
  // Hour ticks.
  for (int i = 0; i < 12; i++) {
    const float a = 2.f * kPi * i / 12.f;
    const bool major = i % 3 == 0;
    const int inner = radius - (major ? radius / 7 : radius / 12);
    const int outer = radius - 6;
    r.drawLine(cx + static_cast<int>(inner * std::sin(a)), cy - static_cast<int>(inner * std::cos(a)),
                      cx + static_cast<int>(outer * std::sin(a)), cy - static_cast<int>(outer * std::cos(a)),
                      major ? 5 : 2, true);
  }
  // Hands: hour moves with the minutes.
  const float hourAngle = 2.f * kPi * (((hour24 % 12) * 60 + minute) / 720.f);
  const float minuteAngle = 2.f * kPi * (minute / 60.f);
  const int hourLen = radius * 5 / 10;
  const int minuteLen = radius * 8 / 10;
  r.drawLine(cx, cy, cx + static_cast<int>(hourLen * std::sin(hourAngle)),
                    cy - static_cast<int>(hourLen * std::cos(hourAngle)), 8, true);
  r.drawLine(cx, cy, cx + static_cast<int>(minuteLen * std::sin(minuteAngle)),
                    cy - static_cast<int>(minuteLen * std::cos(minuteAngle)), 4, true);
  r.fillRect(cx - 6, cy - 6, 12, 12, true);
}

}  // namespace

void draw(GfxRenderer& r, const int face, const bool use12h, const int hour, const int minute, const char* date,
          const int areaTop, const int areaHeight) {
  const int width = r.getScreenWidth();
  switch (face) {
    case 1: {
      const int digitsHeight = areaHeight * 2 / 3;
      drawDigits(r, use12h, hour, minute, 0, areaTop, width, digitsHeight);
      if (date) {
        const int dateY = areaTop + digitsHeight + (areaHeight - digitsHeight) / 2 - r.getLineHeight(UI_12_FONT_ID) / 2;
        r.drawText(UI_12_FONT_ID, (width - r.getTextWidth(UI_12_FONT_ID, date)) / 2, dateY, date, true,
                   EpdFontFamily::BOLD);
      }
      break;
    }
    case 2: {
      const int dateRoom = date ? r.getLineHeight(UI_10_FONT_ID) + 16 : 0;
      const int radius = std::min(width / 2 - 16, (areaHeight - dateRoom) / 2 - 8);
      const int cy = areaTop + (areaHeight - dateRoom) / 2;
      drawAnalog(r, hour, minute, width / 2, cy, radius);
      if (date) {
        r.drawText(UI_10_FONT_ID, (width - r.getTextWidth(UI_10_FONT_ID, date)) / 2,
                   areaTop + areaHeight - r.getLineHeight(UI_10_FONT_ID) - 4, date);
      }
      break;
    }
    default:
      drawDigits(r, use12h, hour, minute, 0, areaTop, width, areaHeight);
      break;
  }
}

}  // namespace ClockFace
