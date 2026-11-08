#pragma once

#include <vector>

#include "WeatherData.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Weather settings: city (searched via Open-Meteo geocoding), units, forecast length and whether the
// Weather entry appears on the home screen. Confirm changes the selected row.
class WeatherSettingsActivity final : public Activity {
 public:
  explicit WeatherSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("WeatherSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum Row : int { Location, TempUnit, WindUnit, PrecipUnit, ForecastDays, OnHome, RowCount };

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  bool shouldTearDownWifiOnExit = false;
  std::string pendingQuery;
  std::vector<weather::CityResult> results;

  void activate(int row);
  void startCitySearch();
  void searchWithWifi();
  void showResults();
  std::string rowValue(int row) const;
  const char* rowTitle(int row) const;
};
