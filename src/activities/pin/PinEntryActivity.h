#pragma once

#include <functional>
#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/PinGate.h"

// Digit-by-digit PIN entry for a button-only reader: Up/Down move over 0-9, "<" (delete) and "OK"; Confirm acts on
// the highlighted cell. `check` judges the entered PIN (accepted / wrong / locked); with no `check` any valid PIN is
// accepted (used when choosing a new one). When accepted, `onDone(pin)` runs; if `finishOnDone` the activity then
// finishes with a KeyboardResult holding the PIN, otherwise `onDone` is expected to replace this activity.
class PinEntryActivity final : public Activity {
 public:
  using Check = std::function<PinGate::Result(const std::string&)>;
  using Done = std::function<void(const std::string&)>;

  PinEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title, Check check, Done onDone,
                   bool cancellable, bool finishOnDone)
      : Activity("PinEntry", renderer, mappedInput),
        title_(std::move(title)),
        check_(std::move(check)),
        onDone_(std::move(onDone)),
        cancellable_(cancellable),
        finishOnDone_(finishOnDone) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int kCells = 12;  // 0-9, delete, OK
  void activate();
  void submit();

  ButtonNavigator nav_;
  std::string title_;
  Check check_;
  Done onDone_;
  bool cancellable_;
  bool finishOnDone_;
  std::string entered_;
  std::string message_;
  int selected_ = 0;
  uint32_t lastShownWait_ = 0;
};
