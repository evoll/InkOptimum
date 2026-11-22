#pragma once

#include "activities/Activity.h"

// Apps > Clock: a full-screen clock with three faces (big digital, digital with date, analog).
// Confirm (or a tap) switches the face; the screen redraws when the minute changes, with a full
// refresh every ten minutes to keep ghosting down. Faces are drawn with rectangles and lines, so
// no extra fonts are needed. On boards with a hardware RTC a power-saving mode puts the device to sleep after 15 s
// without input and redraws the face once a minute (ClockStandby).
class ClockActivity final : public Activity {
 public:
  explicit ClockActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Clock", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  bool readTime(int& hour24, int& minute) const;

  int face_ = 0;
  unsigned long lastInputMs_ = 0;
  bool ecoAvailable_ = false;  // hardware RTC with a valid time: power saving can be offered
  int shownMinuteOfDay_ = -1;
  int minutesSinceFull_ = 0;
  unsigned long lastCheckMs_ = 0;
};
