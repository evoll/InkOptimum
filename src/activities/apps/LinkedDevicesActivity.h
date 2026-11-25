#pragma once

#include <CompanionAuth.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Apps > Linked devices: the companion apps paired with this reader (docs/COMPANION_APP.md). Confirm on a device
// asks to unpair it; its token stops working at once.
class LinkedDevicesActivity final : public Activity {
 public:
  explicit LinkedDevicesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LinkedDevices", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void load();
  void revokeSelected();

  ButtonNavigator nav_;
  companion::TokenStore store_;
  size_t selected_ = 0;
};
