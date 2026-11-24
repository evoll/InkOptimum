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
  if (yearView_) {
    year_ = std::clamp(year_ + delta, 2000, 2099);
    requestUpdate();
    return;
  }
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
    if (yearView_) {
      yearView_ = false;
      requestUpdate();
    } else if (today_.isValid() && (year_ != today_.year || month_ != today_.month)) {
      year_ = today_.year;
      month_ = today_.month;
      requestUpdate();
    } else if (year_ != 0) {
      yearView_ = true;
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

// A year as two stacked half-year blocks of week columns (Monday first): a day is an outlined square when nothing was
// read, and a square filled more the longer it was read (under 20, 60, 120 min, more). Days that the history only
// marks as read, without minutes, count as the lightest fill.
void ReadingCalendarActivity::renderYear(const int top) {
  const int width = renderer.getScreenWidth();
  char heading[24];
  snprintf(heading, sizeof(heading), "< %d >", year_);
  const int headingY = top + 4 + renderer.getLineHeight(UI_12_FONT_ID) + 14;
  renderer.drawText(UI_12_FONT_ID, (width - renderer.getTextWidth(UI_12_FONT_ID, heading)) / 2, headingY, heading);

  const int margin = 12;
  const int cell = (width - 2 * margin) / 27;
  const int gap = cell >= 12 ? 2 : 1;
  const int gridLeft = (width - cell * 27) / 2;
  const int blockH = 7 * cell + renderer.getLineHeight(UI_10_FONT_ID) + 6;
  const int blockTop[2] = {headingY + renderer.getLineHeight(UI_12_FONT_ID) + 16,
                           headingY + renderer.getLineHeight(UI_12_FONT_ID) + 16 + blockH + 14};

  ReadingStatsDate first;
  first.year = static_cast<uint16_t>(year_);
  first.month = 1;
  first.day = 1;
  const int jan1Dow = readingStatsDayOfWeekIndex(first);
  ReadingStatsDate july = first;
  july.month = 7;
  const int julyColBase = (readingStatsDayIndex(july) - readingStatsDayIndex(first) + jan1Dow) / 7;

  // Month numbers above the column where each month starts.
  for (int m = 1; m <= 12; m++) {
    ReadingStatsDate d = first;
    d.month = static_cast<uint8_t>(m);
    const int doy = readingStatsDayIndex(d) - readingStatsDayIndex(first);
    const int col = (doy + jan1Dow) / 7;
    const int half = m <= 6 ? 0 : 1;
    const int c = half == 0 ? col : col - julyColBase;
    char num[4];
    snprintf(num, sizeof(num), "%d", m);
    renderer.drawText(UI_10_FONT_ID, gridLeft + c * cell, blockTop[half], num);
  }

  int readDays = 0;
  uint32_t totalSeconds = 0;
  const int days = (year_ % 4 == 0 && (year_ % 100 != 0 || year_ % 400 == 0)) ? 366 : 365;
  ReadingStatsDate date = first;
  const int labelH = renderer.getLineHeight(UI_10_FONT_ID) + 6;
  for (int i = 0; i < days; i++, addDaysToReadingStatsDate(date, 1)) {
    const int col = (i + jan1Dow) / 7;
    const int row = (i + jan1Dow) % 7;
    const int half = date.month <= 6 ? 0 : 1;
    const int c = half == 0 ? col : col - julyColBase;
    const int x = gridLeft + c * cell;
    const int y = blockTop[half] + labelH + row * cell;
    const int dayIndex = readingStatsDayIndex(date);
    const bool read = readingHistoryHasDay(stats_.readingHistoryAnchorDay, stats_.readingHistoryBits, dayIndex);
    const uint32_t secs = read ? daily_.secondsFor(dayIndex) : 0;
    const bool future = today_.isValid() && compareReadingStatsDate(date, today_) > 0;
    const int inner = cell - gap;
    if (!read) {
      if (!future) renderer.drawRect(x, y, inner, inner, 1, true);
    } else {
      readDays++;
      totalSeconds += secs;
      const unsigned mins = secs / 60;
      const int level = mins >= 120 ? 4 : mins >= 60 ? 3 : mins >= 20 ? 2 : 1;
      renderer.drawRect(x, y, inner, inner, 1, true);
      const int fill = level == 4 ? inner : (inner * level) / 4 + 1;
      const int off = (inner - fill) / 2;
      renderer.fillRect(x + off, y + off, fill, fill, true);
    }
    if (today_.isValid() && compareReadingStatsDate(date, today_) == 0) {
      renderer.drawRect(x - 1, y - 1, inner + 2, inner + 2, 1, true);
    }
  }

  int infoY = blockTop[1] + blockH + 8;
  char line[72];
  snprintf(line, sizeof(line), "%s: %d / %d", tr(STR_CAL_READ_DAYS), readDays, days);
  renderer.drawText(UI_10_FONT_ID, gridLeft, infoY, line);
  infoY += renderer.getLineHeight(UI_10_FONT_ID) + 2;
  if (totalSeconds >= 60) {
    const unsigned totalMin = totalSeconds / 60;
    snprintf(line, sizeof(line), "%s: %u %s %02u %s", tr(STR_STATS_TIME_LBL), totalMin / 60, tr(STR_UNIT_HOUR_SHORT),
             totalMin % 60, tr(STR_UNIT_MIN_SHORT));
    renderer.drawText(UI_10_FONT_ID, gridLeft, infoY, line);
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
  } else if (yearView_) {
    renderYear(top);
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

  const bool onCurrent = today_.isValid() && year_ == today_.year && month_ == today_.month;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), yearView_ ? tr(STR_CAL_MONTH_VIEW) : (onCurrent ? tr(STR_CAL_YEAR_VIEW) : tr(STR_TODAY)),
                                            tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
