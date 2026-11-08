#include "WeatherSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>

#include "InkMODSettings.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/OptionSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TouchListNavigation.h"
#include "weather/WeatherService.h"

namespace {
constexpr size_t MAX_QUERY_LENGTH = 40;
}

void WeatherSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void WeatherSettingsActivity::onExit() {
  Activity::onExit();
  if (shouldTearDownWifiOnExit && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    WiFi.mode(WIFI_OFF);
  }
}

const char* WeatherSettingsActivity::rowTitle(const int row) const {
  switch (row) {
    case Location: return tr(STR_WEATHER_LOCATION);
    case TempUnit: return tr(STR_WEATHER_TEMP_UNIT);
    case WindUnit: return tr(STR_WEATHER_WIND_UNIT);
    case PrecipUnit: return tr(STR_WEATHER_PRECIP_UNIT);
    case ForecastDays: return tr(STR_WEATHER_FORECAST_DAYS);
    default: return tr(STR_WEATHER_ON_HOME);
  }
}

std::string WeatherSettingsActivity::rowValue(const int row) const {
  const auto& cfg = WEATHER.settings();
  switch (row) {
    case Location: return cfg.hasLocation ? (cfg.locationName.empty() ? std::string("?") : cfg.locationName) : "-";
    case TempUnit: return weather::tempUnitSymbol(cfg.tempUnit);
    case WindUnit: return weather::windUnitSymbol(cfg.windUnit);
    case PrecipUnit: return weather::precipUnitSymbol(cfg.precipUnit);
    case ForecastDays: return std::to_string(static_cast<int>(cfg.forecastDays));
    default: return SETTINGS.weatherOnHome ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  }
}

void WeatherSettingsActivity::activate(const int row) {
  auto& cfg = WEATHER.settings();
  switch (row) {
    case Location:
      startCitySearch();
      return;
    case TempUnit:
      cfg.tempUnit = cfg.tempUnit == weather::TempUnit::Celsius ? weather::TempUnit::Fahrenheit
                                                                : weather::TempUnit::Celsius;
      break;
    case WindUnit:
      cfg.windUnit = static_cast<weather::WindUnit>((static_cast<int>(cfg.windUnit) + 1) % 4);
      break;
    case PrecipUnit:
      cfg.precipUnit = cfg.precipUnit == weather::PrecipUnit::Mm ? weather::PrecipUnit::Inch : weather::PrecipUnit::Mm;
      break;
    case ForecastDays:
      cfg.setForecastDays(cfg.forecastDays >= weather::MAX_FORECAST_DAYS ? weather::MIN_FORECAST_DAYS
                                                                          : cfg.forecastDays + 1);
      break;
    default:
      SETTINGS.weatherOnHome = SETTINGS.weatherOnHome ? 0 : 1;
      SETTINGS.saveToFile();
      requestUpdate();
      return;
  }
  WEATHER.saveSettings();
  requestUpdate();
}

void WeatherSettingsActivity::startCitySearch() {
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_WEATHER_SEARCH_CITY), "",
                                                                 MAX_QUERY_LENGTH, InputType::Text, 2),
                         [this](const ActivityResult& result) {
                           const auto* kb = result.isCancelled ? nullptr : std::get_if<KeyboardResult>(&result.data);
                           if (kb == nullptr || kb->text.empty()) {
                             requestUpdate();
                             return;
                           }
                           pendingQuery = kb->text;
                           if (WiFi.status() == WL_CONNECTED) {
                             searchWithWifi();
                             return;
                           }
                           shouldTearDownWifiOnExit = true;
                           startActivityForResult(
                               std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                               [this](const ActivityResult& wifi) {
                                 if (wifi.isCancelled) {
                                   requestUpdate();
                                   return;
                                 }
                                 searchWithWifi();
                               });
                         });
}

void WeatherSettingsActivity::searchWithWifi() {
  if (WiFi.status() != WL_CONNECTED) {
    requestUpdate();
    return;
  }
  GUI.drawPopup(renderer, tr(STR_WEATHER_SEARCHING));
  results = WEATHER.searchCity(pendingQuery);
  if (results.empty()) {
    GUI.drawPopup(renderer, tr(STR_WEATHER_NO_RESULTS));
    delay(1500);
    requestUpdate();
    return;
  }
  showResults();
}

void WeatherSettingsActivity::showResults() {
  std::vector<std::string> options;
  std::vector<std::string> subtitles;
  options.reserve(results.size());
  for (const auto& city : results) {
    options.push_back(city.name);
    std::string place = city.admin1;
    if (!city.country.empty()) place += (place.empty() ? "" : ", ") + city.country;
    subtitles.push_back(place);
  }
  startActivityForResult(
      std::make_unique<OptionSelectionActivity>(renderer, mappedInput, "WeatherCityResults",
                                                StrId::STR_WEATHER_SEARCH_RESULTS, std::move(options), 0,
                                                /*readerMode=*/false, /*showCurrentMarker=*/false,
                                                std::move(subtitles)),
      [this](const ActivityResult& result) {
        const auto* choice = result.isCancelled ? nullptr : std::get_if<OptionSelectionResult>(&result.data);
        if (choice != nullptr && choice->index < results.size()) {
          const auto& city = results[choice->index];
          auto& cfg = WEATHER.settings();
          cfg.hasLocation = true;
          cfg.latitude = city.latitude;
          cfg.longitude = city.longitude;
          cfg.locationName = city.admin1.empty() || city.admin1 == city.name ? city.name : city.name + ", " + city.admin1;
          WEATHER.saveSettings();
        }
        results.clear();
        requestUpdate();
      });
}

void WeatherSettingsActivity::loop() {
  if (mappedInput.hasTouch()) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const auto safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = safeArea.y + safeArea.height - contentTop - metrics.verticalSpacing;
    const auto touch = TouchListNavigation::handle(mappedInput, selectedIndex, RowCount,
                                                   Rect{safeArea.x, contentTop, safeArea.width, contentHeight},
                                                   metrics.listRowHeight);
    if (touch.handled) {
      if (touch.activate) {
        activate(selectedIndex);
      } else {
        requestUpdate();
      }
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activate(selectedIndex);
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, RowCount);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, RowCount);
    requestUpdate();
  });
}

void WeatherSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{safeArea.x, metrics.topPadding, safeArea.width, metrics.headerHeight},
                 tr(STR_WEATHER_SETTINGS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = safeArea.y + safeArea.height - contentTop - metrics.verticalSpacing;
  GUI.drawList(
      renderer, Rect{safeArea.x, contentTop, safeArea.width, contentHeight}, RowCount, selectedIndex,
      [this](int index) { return std::string(rowTitle(index)); }, nullptr, nullptr,
      [this](int index) { return rowValue(index); }, true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
