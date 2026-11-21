#include "ReadingCalendarActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
const char* monthName(const int month) {
  switch (month) {
    case 1: return tr(STR_CAL_MONTH_1);
    case 2: return tr(STR_CAL_MONTH_2);
    case 3: return tr(STR_CAL_MONTH_3);
    case 4: return tr(STR_CAL_MONTH_4);
    case 5: return tr(STR_CAL_MONTH_5);
    case 6: return tr(STR_CAL_MONTH_6);
    case 7: return tr(STR_CAL_MONTH_7);
    case 8: return tr(STR_CAL_MONTH_8);
    case 9: return tr(STR_CAL_MONTH_9);
    case 10: return tr(STR_CAL_MONTH_10);
    case 11: return tr(STR_CAL_MONTH_11);
    default: return tr(STR_CAL_MONTH_12);
  }
}

const char* weekdayShort(const int index) {  // Monday = 0
  switch (index) {
    case 0: return tr(STR_CAL_DOW_1);
    case 1: return tr(STR_CAL_DOW_2);
    case 2: return tr(STR_CAL_DOW_3);
    case 3: return tr(STR_CAL_DOW_4);
    case 4: return tr(STR_CAL_DOW_5);
    case 5: return tr(STR_CAL_DOW_6);
    default: return tr(STR_CAL_DOW_7);
  }
}
}  // namespace

void ReadingCalendarActivity::onEnter() {
  Activity::onEnter();
  daily_ = DailyReadingStore::load();
  stats_ = GlobalReadingStats::hasSyncedStats() ? GlobalReadingStats::loadAggregated() : GlobalReadingStats::load();
  ReadingStatsDateTime now;
  if (getCurrentLocalReadingStatsDateTime(now) && now.date.isValid()) {
    today_ = now.date;
  } else if (stats_.readingHistoryAnchorDay != 0 &&
             readingStatsDateFromDayIndex(stats_.readingHistoryAnchorDay, today_)) {
    // No clock: fall back to the most recent day that has reading recorded.
  } else {
    today_.clear();
  }
  year_ = today_.isValid() ? today_.year : 0;
  month_ = today_.isValid() ? today_.month : 0;
  requestUpdate();
}

void ReadingCalendarActivity::shiftMonth(const int delta) {
  if (year_ == 0) return;
  month_ += delta;
  while (month_ < 1) {
    month_ += 12;
    --year_;
  }
  while (month_ > 12) {
    month_ -= 12;
    ++year_;
  }
  year_ = std::clamp(year_, 2000, 2099);
  requestUpdate();
}

void ReadingCalendarActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult cancelled;
    cancelled.isCancelled = true;
    setResult(std::move(cancelled));
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (today_.isValid() && (year_ != today_.year || month_ != today_.month)) {
      year_ = today_.year;
      month_ = today_.month;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    shiftMonth(-1);
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
      mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    shiftMonth(1);
    return;
  }
  int tx = 0;
  int ty = 0;
  if (mappedInput.hasTouch() && mappedInput.wasScreenTapped(tx, ty)) {
    shiftMonth(tx < renderer.getScreenWidth() / 2 ? -1 : 1);
  }
}

void ReadingCalendarActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int top = metrics.topPadding;

  const char* title = tr(STR_READING_CALENDAR);
  renderer.drawText(UI_12_FONT_ID, (width - renderer.getTextWidth(UI_12_FONT_ID, title, EpdFontFamily::BOLD)) / 2,
                    top + 4, title, true, EpdFontFamily::BOLD);

  if (year_ == 0) {
    const char* msg = tr(STR_CLOCK_NOT_SET);
    renderer.drawText(UI_12_FONT_ID, (width - renderer.getTextWidth(UI_12_FONT_ID, msg)) / 2, height / 2, msg);
  } else {
    char heading[48];
    snprintf(heading, sizeof(heading), "< %s %d >", monthName(month_), year_);
    const int headingY = top + 4 + renderer.getLineHeight(UI_12_FONT_ID) + 14;
    renderer.drawText(UI_12_FONT_ID, (width - renderer.getTextWidth(UI_12_FONT_ID, heading)) / 2, headingY, heading);

    const int margin = 12;
    const int cell = (width - 2 * margin) / 7;
    const int gridLeft = (width - cell * 7) / 2;
    const int dowY = headingY + renderer.getLineHeight(UI_12_FONT_ID) + 14;
    for (int c = 0; c < 7; c++) {
      const char* label = weekdayShort(c);
      renderer.drawText(UI_10_FONT_ID, gridLeft + c * cell + (cell - renderer.getTextWidth(UI_10_FONT_ID, label)) / 2,
                        dowY, label);
    }

    const int gridTop = dowY + renderer.getLineHeight(UI_10_FONT_ID) + 8;
    const int rowH = std::min(cell, 78);
    ReadingStatsDate first;
    first.year = static_cast<uint16_t>(year_);
    first.month = static_cast<uint8_t>(month_);
    first.day = 1;
    const int offset = readingStatsDayOfWeekIndex(first);
    const int days = daysInMonth(first.year, first.month);
    int readDays = 0;
    uint32_t monthSeconds = 0;
    const int pad = 4;
    for (int d = 1; d <= days; d++) {
      ReadingStatsDate date = first;
      date.day = static_cast<uint8_t>(d);
      const int slot = offset + d - 1;
      const int col = slot % 7;
      const int row = slot / 7;
      const int x = gridLeft + col * cell;
      const int y = gridTop + row * rowH;
      const bool read = readingHistoryHasDay(stats_.readingHistoryAnchorDay, stats_.readingHistoryBits,
                                             readingStatsDayIndex(date));
      const bool isToday = today_.isValid() && compareReadingStatsDate(date, today_) == 0;
      char num[4];
      snprintf(num, sizeof(num), "%d", d);
      const int tw = renderer.getTextWidth(UI_10_FONT_ID, num);
      const int th = renderer.getLineHeight(UI_10_FONT_ID);
      const int bx = x + pad;
      const int by = y + pad;
      const int bw = cell - 2 * pad;
      const int bh = rowH - 2 * pad;
      const uint32_t daySeconds = read ? daily_.secondsFor(readingStatsDayIndex(date)) : 0;
      if (read) {
        readDays++;
        monthSeconds += daySeconds;
        renderer.fillRect(bx, by, bw, bh, true);
        if (daySeconds >= 60 && bh >= 2 * th + 6) {
          char mins[8];
          snprintf(mins, sizeof(mins), "%u", static_cast<unsigned>(std::min<uint32_t>(daySeconds / 60, 999)));
          const int mw = renderer.getTextWidth(UI_10_FONT_ID, mins);
          const int blockH = 2 * th + 2;
          const int topY = by + (bh - blockH) / 2;
          renderer.drawText(UI_10_FONT_ID, bx + (bw - tw) / 2, topY, num, false);
          renderer.drawText(UI_10_FONT_ID, bx + (bw - mw) / 2, topY + th + 2, mins, false);
        } else {
          renderer.drawText(UI_10_FONT_ID, bx + (bw - tw) / 2, by + (bh - th) / 2, num, false);
        }
      } else {
        renderer.drawText(UI_10_FONT_ID, bx + (bw - tw) / 2, by + (bh - th) / 2, num, true);
      }
      if (isToday) renderer.drawRect(bx - 1, by - 1, bw + 2, bh + 2, 2, true);
    }

    const int rows = (offset + days + 6) / 7;
    int infoY = gridTop + rows * rowH + 10;
    char line[64];
    snprintf(line, sizeof(line), "%s: %d / %d", tr(STR_CAL_READ_DAYS), readDays, days);
    renderer.drawText(UI_10_FONT_ID, gridLeft, infoY, line);
    infoY += renderer.getLineHeight(UI_10_FONT_ID) + 2;
    if (monthSeconds >= 60) {
      const unsigned totalMin = monthSeconds / 60;
      snprintf(line, sizeof(line), "%s: %u %s %02u %s", tr(STR_STATS_TIME_LBL), totalMin / 60, tr(STR_UNIT_HOUR_SHORT),
               totalMin % 60, tr(STR_UNIT_MIN_SHORT));
      renderer.drawText(UI_10_FONT_ID, gridLeft, infoY, line);
      infoY += renderer.getLineHeight(UI_10_FONT_ID) + 2;
    }
    if (today_.isValid()) {
      const uint16_t cur = stats_.currentReadingStreak(&today_);
      snprintf(line, sizeof(line), "%s: %u   %s: %u", tr(STR_STATS_READING_STREAK_LBL), static_cast<unsigned>(cur),
               tr(STR_STATS_LONGEST_STREAK_LBL), static_cast<unsigned>(stats_.displayLongestReadingStreak()));
      renderer.drawText(UI_10_FONT_ID, gridLeft, infoY, line);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TODAY), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
