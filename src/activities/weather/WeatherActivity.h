#pragma once

#include "activities/Activity.h"
#include "weather/WeatherService.h"

// Weather screen: page 1 = current conditions + next hours, page 2 = daily forecast.
// Shows cached data right away; Confirm (or a tap on "Refresh") fetches fresh data over Wi-Fi.
class WeatherActivity final : public Activity {
 public:
  explicit WeatherActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Weather", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }
  void render(RenderLock&&) override;

 private:
  enum class Page : uint8_t { Now, Days };

  WeatherService::Result data;
  Page page = Page::Now;
  bool fetchPending = false;
  bool fetching = false;
  bool lastFetchFailed = false;
  bool shouldTearDownWifiOnExit = false;

  void beginRefresh();
  void runFetch();
  void openSettings();
  void togglePage();

  bool hasData() const { return data.fetchedAt != 0; }
  void drawNow(int top, int bottom, int width) const;
  void drawDays(int top, int bottom, int width) const;
  void drawTouchControls() const;
};
