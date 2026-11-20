#pragma once

#include <GfxRenderer.h>
#include <SdCardFontRegistry.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/UITheme.h"
#include "util/ButtonNavigator.h"

class MappedInputManager;

/**
 * Picks the SD font family that draws interface glyphs the built-in fonts lack (CJK).
 * "Auto" keeps borrowing the reading font. Only bitmap .cpfont families are offered.
 */
class UiFontSelectActivity final : public Activity {
 public:
  UiFontSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const SdCardFontRegistry* registry)
      : Activity("UiFontSelect", renderer, mappedInput), registry_(registry) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void handleSelection();

  const SdCardFontRegistry* registry_;
  ButtonNavigator buttonNavigator;
  std::vector<std::string> families_;  // index 0 in the list is "Auto", then these
  int selectedIndex = 0;
  int totalItems = 1;
};
