#pragma once

#include <BlePageTurner.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Bluetooth page turner: switch it on, scan, pair a remote, forget one, and map page/chapter buttons.
// The radio and the remote belong to lib/BlePageTurner; this screen only asks it.
class BleSettingsActivity final : public Activity {
 public:
  explicit BleSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BleSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View { Menu, Scan, Paired, Mapping };
  struct Row {
    std::string label;
    std::string value;
    std::string subtitle;
    int16_t code = 0;
    bool enabled = true;
  };

  ButtonNavigator buttonNavigator;
  View view = View::Menu;
  int selected = 0;
  std::vector<Row> rows;
  std::string status;
  uint32_t shownSignature = 0;
  unsigned long lastPollMs = 0;
  bleturner::Action learnAction = bleturner::Action::None;
  char learnAddr[18]{};
  uint32_t learnCode = 0;     // the pressed button, kept until its release
  uint32_t learnPressMs = 0;  // when it went down (the remote's clock), to tell a hold from a tap
  unsigned long learnSinceMs = 0;
  const char* learnNote = nullptr;

  const char* headerTitle() const;
  void showView(View target);
  uint32_t signature() const;
  void refresh();
  void activateIndex(int index);
  void startLearning(int index);
  void stepLearning();
  void keepLearned(bool hold);
  void clearMapping(int index);
};
