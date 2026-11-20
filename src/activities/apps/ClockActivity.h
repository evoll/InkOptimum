#pragma once

#include "activities/Activity.h"

// Apps > Clock: a full-screen clock with three faces (big digital, digital with date, analog).
// Confirm (or a tap) switches the face; the screen redraws when the minute changes, with a full
// refresh every ten minutes to keep ghosting down. Faces are drawn with rectangles and lines, so
// no extra fonts are needed.
class ClockActivity final : public Activity {
 public:
  explicit ClockActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Clock", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum Face : int { FACE_DIGITAL = 0, FACE_DIGITAL_DATE, FACE_ANALOG, FACE_COUNT };

  bool readTime(int& hour24, int& minute) const;
  void drawDigits(int hour24, int minute, int left, int top, int width, int height) const;
  void drawSegmentDigit(int digit, int x, int y, int w, int h, int t) const;
  void drawAnalog(int hour24, int minute, int cx, int cy, int radius) const;

  int face_ = FACE_DIGITAL;
  int shownMinuteOfDay_ = -1;
  int minutesSinceFull_ = 0;
  unsigned long lastCheckMs_ = 0;
};
