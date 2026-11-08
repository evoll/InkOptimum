#pragma once

#include <ScreenInsets.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Settings > System > Screen Calibration (idea from CrossInk): moves the edges of the viewable area per device, so a
// bezel that hides a few pixels (or a panel with a wider visible border) no longer cuts text. The frame shown is the
// area every screen and the reader lay out inside. Confirm picks the next edge (Top, Right, Bottom, Left, Reset),
// Up/Down move it by one pixel. Stored in NVS (not in the SD settings), saved when leaving the screen.
class ScreenCalibrationActivity final : public Activity {
 public:
  explicit ScreenCalibrationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ScreenCalibration", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  ScreenInsets insets;
  ScreenInsets saved;
  int selected = 0;  // 0..3 oriented Top, Right, Bottom, Left; 4 = reset to the factory values

  unsigned portraitEdge(int orientedEdge) const;
  void adjust(int delta);
};
