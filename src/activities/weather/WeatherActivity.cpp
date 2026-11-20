#include "WeatherActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "InkMODSettings.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "WeatherIcons.h"
#include "WeatherSettingsActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "weather/WeatherText.h"
#include "WeatherGraph.h"

namespace {
constexpr int MARGIN = 16;
constexpr int CONTROL_HEIGHT = 44;
constexpr int CONTROL_GAP = 8;

struct Controls {
  Rect refresh;
  Rect settings;
  Rect page;
};

Controls touchControls(const GfxRenderer& renderer) {
  const int w = (renderer.getScreenWidth() - MARGIN * 2 - CONTROL_GAP * 2) / 3;
  const int y = renderer.getScreenHeight() - CONTROL_HEIGHT - 12;
  return {Rect{MARGIN, y, w, CONTROL_HEIGHT}, Rect{MARGIN + w + CONTROL_GAP, y, w, CONTROL_HEIGHT},
          Rect{MARGIN + (w + CONTROL_GAP) * 2, y, w, CONTROL_HEIGHT}};
}

void drawControl(const GfxRenderer& renderer, const Rect& r, const char* label) {
  renderer.fillRect(r.x, r.y, r.width, r.height, false);
  renderer.drawRect(r.x, r.y, r.width, r.height, 2, true);
  const std::string text = renderer.truncatedText(UI_10_FONT_ID, label, r.width - 10);
  const int tw = renderer.getTextWidth(UI_10_FONT_ID, text.c_str());
  renderer.drawText(UI_10_FONT_ID, r.x + (r.width - tw) / 2, r.y + (r.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                    text.c_str());
}

void drawIcon(const GfxRenderer& renderer, const weather::IconType type, const int x, const int y) {
  const uint8_t* bmp = weather::iconBitmap(type);
  for (int py = 0; py < WEATHER_ICON_SIZE; ++py) {
    for (int px = 0; px < WEATHER_ICON_SIZE; ++px) {
      if (weather::iconPixelSet(bmp, px, py)) renderer.drawPixel(x + px, y + py);
    }
  }
}

std::string fmtTemp(const float value) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%d\xC2\xB0", static_cast<int>(std::lround(value)));
  return buf;
}

std::string fmtClock(const time_t t, const int offset) {
  char buf[12];
  weather_text::formatClock(t, offset, SETTINGS.clockFormat == 1, buf, sizeof(buf));
  return buf;
}

// Label of the page the "page" control switches to (Now -> Graph -> Days -> Now).
const char* nextPageLabel(const uint8_t page) {
  switch (page) {
    case 0: return tr(STR_WEATHER_GRAPH);
    case 1: return tr(STR_WEATHER_DAILY);
    default: return tr(STR_WEATHER_NEXT_HOURS);
  }
}
}  // namespace

void WeatherActivity::onEnter() {
  Activity::onEnter();
  data = WEATHER.loadCached();
  page = Page::Now;
  lastFetchFailed = false;
  requestUpdate();
  // Nothing saved yet: go online right away (a configured city is required).
  if (WEATHER.settings().hasLocation && data.status == WeatherService::Status::NoCache) {
    beginRefresh();
  }
}

void WeatherActivity::onExit() {
  Activity::onExit();
  if (shouldTearDownWifiOnExit && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    WiFi.mode(WIFI_OFF);
  }
}

void WeatherActivity::openSettings() {
  startActivityForResult(std::make_unique<WeatherSettingsActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) {
                           data = WEATHER.loadCached();
                           lastFetchFailed = false;
                           requestUpdate();
                           if (WEATHER.settings().hasLocation && data.status == WeatherService::Status::NoCache) {
                             beginRefresh();
                           }
                         });
}

void WeatherActivity::togglePage() {
  page = page == Page::Now ? Page::Graph : page == Page::Graph ? Page::Days : Page::Now;
  requestUpdate();
}

void WeatherActivity::beginRefresh() {
  if (!WEATHER.settings().hasLocation) return;
  if (WiFi.status() == WL_CONNECTED) {
    fetchPending = true;
    requestUpdate();
    return;
  }
  shouldTearDownWifiOnExit = true;
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (!result.isCancelled && WiFi.status() == WL_CONNECTED) {
                             fetchPending = true;
                           }
                           requestUpdate();
                         });
}

void WeatherActivity::runFetch() {
  fetchPending = false;
  fetching = true;
  requestUpdateAndWait();
  // TLS needs the heap the SD font cache would otherwise hold.
  sdFontSystem.releaseLoadedFont(renderer);

  auto fresh = WEATHER.fetch();
  fetching = false;
  if (fresh.status == WeatherService::Status::Ok) {
    data = std::move(fresh);
    lastFetchFailed = false;
  } else {
    LOG_ERR("WEA", "Refresh failed (status %d); keeping previous data", static_cast<int>(fresh.status));
    lastFetchFailed = true;
  }
  requestUpdate();
}

void WeatherActivity::loop() {
  if (fetchPending) {
    runFetch();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.hasTouch()) {
    const auto c = touchControls(renderer);
    if (mappedInput.wasTapInRect(c.refresh.x, c.refresh.y, c.refresh.width, c.refresh.height)) {
      beginRefresh();
      return;
    }
    if (mappedInput.wasTapInRect(c.settings.x, c.settings.y, c.settings.width, c.settings.height)) {
      openSettings();
      return;
    }
    if (mappedInput.wasTapInRect(c.page.x, c.page.y, c.page.width, c.page.height)) {
      togglePage();
      return;
    }
    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Right) {
      togglePage();
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (WEATHER.settings().hasLocation) {
      beginRefresh();
    } else {
      openSettings();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    openSettings();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    togglePage();
    return;
  }
}

void WeatherActivity::drawNow(const int top, const int bottom, const int width) const {
  const auto& fc = data.forecast;
  const auto& cfg = WEATHER.settings();
  const auto& cur = fc.current;
  const int offset = fc.utcOffsetSeconds;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);

  int y = top;
  drawIcon(renderer, weather::iconForCode(cur.weatherCode, cur.isDay), MARGIN, y);

  const int textX = MARGIN + WEATHER_ICON_SIZE + 16;
  const std::string temp = fmtTemp(cur.temperature) + (cfg.tempUnit == weather::TempUnit::Celsius ? "C" : "F");
  renderer.drawText(UI_12_FONT_ID, textX, y, temp.c_str(), true, EpdFontFamily::BOLD);
  const int tempH = renderer.getLineHeight(UI_12_FONT_ID);
  const std::string desc =
      renderer.truncatedText(UI_10_FONT_ID, I18N.get(weather_text::descriptionId(cur.weatherCode)), width - textX - MARGIN);
  renderer.drawText(UI_10_FONT_ID, textX, y + tempH, desc.c_str());
  char feels[48];
  snprintf(feels, sizeof(feels), "%s %s", tr(STR_WEATHER_FEELS_LIKE), fmtTemp(cur.apparentTemperature).c_str());
  renderer.drawText(SMALL_FONT_ID, textX, y + tempH + lineH, feels);
  y += WEATHER_ICON_SIZE + 14;

  // Details, two per row to keep the hourly list on the same page.
  char left[48];
  char right[48];
  snprintf(left, sizeof(left), "%s %d%%", tr(STR_WEATHER_HUMIDITY), cur.humidity);
  snprintf(right, sizeof(right), "%s %.0f %s %s", tr(STR_WEATHER_WIND), static_cast<double>(cur.windSpeed),
           weather::windUnitSymbol(cfg.windUnit), weather::windDirectionLabel(cur.windDirection));
  renderer.drawText(UI_10_FONT_ID, MARGIN, y, left);
  renderer.drawText(UI_10_FONT_ID, width / 2, y, right);
  y += lineH + 4;

  snprintf(left, sizeof(left), "%s %.1f %s", tr(STR_WEATHER_PRECIP), static_cast<double>(cur.precipitation),
           weather::precipUnitSymbol(cfg.precipUnit));
  snprintf(right, sizeof(right), "%s %.0f", tr(STR_WEATHER_UV), static_cast<double>(cur.uvIndex));
  renderer.drawText(UI_10_FONT_ID, MARGIN, y, left);
  renderer.drawText(UI_10_FONT_ID, width / 2, y, right);
  y += lineH + 4;

  if (!fc.daily.empty() && fc.daily[0].sunrise != 0) {
    snprintf(left, sizeof(left), "%s %s", tr(STR_WEATHER_SUNRISE), fmtClock(fc.daily[0].sunrise, offset).c_str());
    snprintf(right, sizeof(right), "%s %s", tr(STR_WEATHER_SUNSET), fmtClock(fc.daily[0].sunset, offset).c_str());
    renderer.drawText(UI_10_FONT_ID, MARGIN, y, left);
    renderer.drawText(UI_10_FONT_ID, width / 2, y, right);
    y += lineH + 4;
  }

  // Hourly list: every second hour from the next full hour on, as many rows as fit.
  y += 6;
  renderer.drawLine(MARGIN, y, width - MARGIN, y);
  y += 6;
  renderer.drawText(UI_10_FONT_ID, MARGIN, y, tr(STR_WEATHER_NEXT_HOURS), true, EpdFontFamily::BOLD);
  y += lineH + 4;

  const time_t now = time(nullptr);
  const int rowH = std::max(smallH, lineH) + 6;
  const int colTemp = MARGIN + 90;
  const int colDesc = colTemp + 70;
  size_t i = 0;
  while (i < fc.hourly.size() && fc.hourly[i].time + 3600 <= now) ++i;  // skip hours already past
  for (; i < fc.hourly.size() && y + rowH <= bottom; i += 2) {
    const auto& h = fc.hourly[i];
    renderer.drawText(UI_10_FONT_ID, MARGIN, y, fmtClock(h.time, offset).c_str());
    renderer.drawText(UI_10_FONT_ID, colTemp, y, fmtTemp(h.temperature).c_str(), true, EpdFontFamily::BOLD);
    char tail[40];
    if (h.precipitationProbability > 0) {
      snprintf(tail, sizeof(tail), "%d%%", h.precipitationProbability);
    } else {
      tail[0] = '\0';
    }
    const int tailW = renderer.getTextWidth(UI_10_FONT_ID, tail);
    const std::string hdesc = renderer.truncatedText(UI_10_FONT_ID, I18N.get(weather_text::descriptionId(h.weatherCode)),
                                                     width - colDesc - MARGIN - tailW - 8);
    renderer.drawText(UI_10_FONT_ID, colDesc, y, hdesc.c_str());
    if (tail[0] != '\0') renderer.drawText(UI_10_FONT_ID, width - MARGIN - tailW, y, tail);
    y += rowH;
  }
}

void WeatherActivity::drawGraph(const int top, const int bottom, const int width) const {
  const auto& fc = data.forecast;
  const auto& cfg = WEATHER.settings();
  const auto& cur = fc.current;
  const int offset = fc.utcOffsetSeconds;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);

  // Header: icon, big current temperature, description.
  int y = top;
  drawIcon(renderer, weather::iconForCode(cur.weatherCode, cur.isDay), MARGIN, y);
  const int textX = MARGIN + WEATHER_ICON_SIZE + 16;
  const std::string temp = fmtTemp(cur.temperature) + (cfg.tempUnit == weather::TempUnit::Celsius ? "C" : "F");
  renderer.drawText(UI_12_FONT_ID, textX, y, temp.c_str(), true, EpdFontFamily::BOLD);
  const int tempH = renderer.getLineHeight(UI_12_FONT_ID);
  const std::string desc =
      renderer.truncatedText(UI_10_FONT_ID, I18N.get(weather_text::descriptionId(cur.weatherCode)), width - textX - MARGIN);
  renderer.drawText(UI_10_FONT_ID, textX, y + tempH, desc.c_str());
  y += WEATHER_ICON_SIZE + 14;

  const size_t first = weather::firstCurrentHour(fc.hourly, time(nullptr));
  const size_t count = std::min<size_t>(24, fc.hourly.size() > first ? fc.hourly.size() - first : 0);
  if (count < 2) {
    renderer.drawCenteredText(UI_10_FONT_ID, (y + bottom) / 2, tr(STR_WEATHER_FETCH_FAILED));
    return;
  }

  float lo = fc.hourly[first].temperature;
  float hi = lo;
  size_t loIdx = 0;
  size_t hiIdx = 0;
  float precipTotal = 0;
  for (size_t i = 0; i < count; ++i) {
    const auto& h = fc.hourly[first + i];
    if (h.temperature < lo) { lo = h.temperature; loIdx = i; }
    if (h.temperature > hi) { hi = h.temperature; hiIdx = i; }
    precipTotal += h.precipitation;
  }
  const auto axis = weather::niceAxis(lo, hi);

  // Layout: chart, precipitation-probability strip, hour labels, summary line.
  const int labelW = renderer.getTextWidth(SMALL_FONT_ID, "-88\xC2\xB0") + 6;
  const int chartL = MARGIN + labelW;
  const int chartR = width - MARGIN;
  const int summaryH = lineH + 4;
  const int hoursY = bottom - summaryH - smallH;
  const int barsH = 34;
  const int barsBottom = hoursY - 4;
  const int chartTop = y + smallH / 2 + 4;
  const int chartBottom = barsBottom - barsH - 10;
  if (chartBottom - chartTop < 40) return;  // not enough room (tiny/odd layout): skip rather than overlap

  // Axis labels and light grid at the top, middle and bottom of the range.
  const int mid = (axis.min + axis.max) / 2;
  const int ticks[3] = {axis.max, mid, axis.min};
  for (const int t : ticks) {
    const int ty = weather::scaleY(static_cast<float>(t), axis, chartTop, chartBottom);
    char lbl[12];
    snprintf(lbl, sizeof(lbl), "%d\xC2\xB0", t);
    renderer.drawText(SMALL_FONT_ID, chartL - 4 - renderer.getTextWidth(SMALL_FONT_ID, lbl), ty - smallH / 2, lbl);
    for (int gx = chartL; gx < chartR; gx += 8) renderer.drawPixel(gx, ty);
  }
  renderer.drawLine(chartL, chartTop, chartL, chartBottom);

  const float step = static_cast<float>(chartR - chartL) / static_cast<float>(count - 1);
  auto px = [&](const size_t i) { return chartL + static_cast<int>(std::lround(static_cast<float>(i) * step)); };

  // Temperature curve (2 px thick).
  for (size_t i = 0; i + 1 < count; ++i) {
    const int x0 = px(i);
    const int x1 = px(i + 1);
    const int y0 = weather::scaleY(fc.hourly[first + i].temperature, axis, chartTop, chartBottom);
    const int y1 = weather::scaleY(fc.hourly[first + i + 1].temperature, axis, chartTop, chartBottom);
    renderer.drawLine(x0, y0, x1, y1);
    renderer.drawLine(x0, y0 + 1, x1, y1 + 1);
  }
  // Mark "now" and the extremes.
  auto dot = [&](const size_t i) {
    const int cx = px(i);
    const int cy = weather::scaleY(fc.hourly[first + i].temperature, axis, chartTop, chartBottom);
    renderer.fillRect(cx - 3, cy - 3, 7, 7, true);
  };
  dot(0);
  if (hiIdx != 0) dot(hiIdx);
  if (loIdx != 0 && loIdx != hiIdx) dot(loIdx);

  // Precipitation probability bars.
  renderer.drawLine(chartL, barsBottom, chartR, barsBottom);
  const int barW = std::max(2, static_cast<int>(step) - 3);
  for (size_t i = 0; i < count; ++i) {
    const int prob = std::clamp(fc.hourly[first + i].precipitationProbability, 0, 100);
    const int bh = prob * barsH / 100;
    if (bh > 0) renderer.fillRect(px(i) - barW / 2, barsBottom - bh, barW, bh, true);
  }
  renderer.drawText(SMALL_FONT_ID, MARGIN, barsBottom - smallH, "%");

  // Hour labels every 6 hours.
  for (size_t i = 0; i < count; i += 6) {
    const std::string hl = fmtClock(fc.hourly[first + i].time, offset);
    int lx = px(i) - renderer.getTextWidth(SMALL_FONT_ID, hl.c_str()) / 2;
    lx = std::clamp(lx, MARGIN, width - MARGIN - renderer.getTextWidth(SMALL_FONT_ID, hl.c_str()));
    renderer.drawText(SMALL_FONT_ID, lx, hoursY, hl.c_str());
  }

  // Summary: range and expected precipitation.
  char sum[96];
  snprintf(sum, sizeof(sum), "%s / %s    %s %.1f %s", fmtTemp(hi).c_str(), fmtTemp(lo).c_str(), tr(STR_WEATHER_PRECIP),
           static_cast<double>(precipTotal), weather::precipUnitSymbol(cfg.precipUnit));
  renderer.drawText(UI_10_FONT_ID, MARGIN, bottom - lineH, sum);
}

void WeatherActivity::drawDays(const int top, const int bottom, const int width) const {
  const auto& fc = data.forecast;
  const auto& cfg = WEATHER.settings();
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int rowH = lineH * 2 + 14;
  int y = top;
  const size_t maxDays = std::min<size_t>(fc.daily.size(), cfg.forecastDays);
  for (size_t i = 0; i < maxDays && y + rowH <= bottom; ++i) {
    const auto& d = fc.daily[i];
    tm t{};
    char date[24] = "";
    if (weather_text::localTime(d.date, fc.utcOffsetSeconds, t)) {
      snprintf(date, sizeof(date), "%s %d %s", weather_text::weekdayName(t.tm_wday), t.tm_mday,
               weather_text::monthName(t.tm_mon));
    }
    renderer.drawText(UI_10_FONT_ID, MARGIN, y, date, true, EpdFontFamily::BOLD);
    const std::string range = fmtTemp(d.tempMax) + " / " + fmtTemp(d.tempMin);
    const int rw = renderer.getTextWidth(UI_10_FONT_ID, range.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, width - MARGIN - rw, y, range.c_str(), true, EpdFontFamily::BOLD);

    char sub[96];
    snprintf(sub, sizeof(sub), "%s  %.1f %s", I18N.get(weather_text::descriptionId(d.weatherCode)),
             static_cast<double>(d.precipSum), weather::precipUnitSymbol(cfg.precipUnit));
    const std::string subText = renderer.truncatedText(SMALL_FONT_ID, sub, width - MARGIN * 2);
    renderer.drawText(SMALL_FONT_ID, MARGIN, y + lineH + 2, subText.c_str());
    y += rowH;
    if (i + 1 < maxDays) renderer.drawLine(MARGIN, y - 6, width - MARGIN, y - 6);
  }
}

void WeatherActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const auto& cfg = WEATHER.settings();

  const char* title = cfg.hasLocation && !cfg.locationName.empty() ? cfg.locationName.c_str() : tr(STR_WEATHER);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, title);

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 4;
  const int bottom = height - (mappedInput.hasTouch() ? CONTROL_HEIGHT + 24 : metrics.buttonHintsHeight + 8);

  if (!cfg.hasLocation) {
    renderer.drawCenteredText(UI_10_FONT_ID, height / 2 - 12, tr(STR_WEATHER_NO_LOCATION));
  } else if (fetching) {
    renderer.drawCenteredText(UI_10_FONT_ID, height / 2, tr(STR_WEATHER_LOADING));
  } else if (!hasData()) {
    renderer.drawCenteredText(UI_10_FONT_ID, height / 2 - 12, tr(STR_WEATHER_FETCH_FAILED));
  } else {
    int contentBottom = bottom;
    // Footer line: age of the data / offline marker / last failure.
    char status[64] = "";
    if (lastFetchFailed) {
      snprintf(status, sizeof(status), "%s", tr(STR_WEATHER_FETCH_FAILED));
    } else if (data.status == WeatherService::Status::Stale) {
      snprintf(status, sizeof(status), "%s", tr(STR_WEATHER_CACHED));
    }
    char stamp[64];
    snprintf(stamp, sizeof(stamp), "%s %s", tr(STR_WEATHER_UPDATED),
             fmtClock(data.fetchedAt, data.forecast.utcOffsetSeconds).c_str());
    const int smallH = renderer.getLineHeight(SMALL_FONT_ID);
    contentBottom -= smallH + 4;
    renderer.drawText(SMALL_FONT_ID, MARGIN, contentBottom + 2, stamp);
    if (status[0] != '\0') {
      const int sw = renderer.getTextWidth(SMALL_FONT_ID, status);
      renderer.drawText(SMALL_FONT_ID, width - MARGIN - sw, contentBottom + 2, status);
    }

    if (page == Page::Now) {
      drawNow(top, contentBottom, width);
    } else if (page == Page::Graph) {
      drawGraph(top, contentBottom, width);
    } else {
      drawDays(top, contentBottom, width);
    }
  }

  if (mappedInput.hasTouch()) {
    drawTouchControls();
  } else {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_WEATHER_REFRESH), tr(STR_WEATHER_SETTINGS_SHORT),
                                              nextPageLabel(static_cast<uint8_t>(page)));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}

void WeatherActivity::drawTouchControls() const {
  const auto c = touchControls(renderer);
  drawControl(renderer, c.refresh, tr(STR_WEATHER_REFRESH));
  drawControl(renderer, c.settings, tr(STR_WEATHER_SETTINGS_SHORT));
  drawControl(renderer, c.page, nextPageLabel(static_cast<uint8_t>(page)));
}
