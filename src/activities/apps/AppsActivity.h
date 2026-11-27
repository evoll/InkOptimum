#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/PluginLocations.h"

// Home > Apps: one list of everything that is installed — the built-in apps (Weather, Reading stats, Bookmarks,
// OPDS, File transfer) followed by the SD-card plugins (when Settings > System > Plugins is on).
// Built-in apps are not started here: the activity finishes with MenuResult{action = AppsAction} and Home
// opens the app, so Home keeps owning its own state (stats, covers, navigation stack).
// A plugin opens its catalog or README, exactly like the Plugins screen.
class AppsActivity final : public Activity {
 public:
  enum AppsAction : int { ACTION_WEATHER = 0, ACTION_READING_STATS, ACTION_BOOKMARKS, ACTION_OPDS, ACTION_FILE_TRANSFER, ACTION_LCP_LOANS, ACTION_CLOCK, ACTION_READING_CALENDAR, ACTION_SHELVES, ACTION_RESET_LOCKSCREEN, ACTION_LINKED_DEVICES };

  explicit AppsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Apps", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct Item {
    bool builtIn = true;
    int action = -1;  // AppsAction for built-ins
    std::string title;
    std::string description;
    // plugins only
    PluginLocations::Entry entry;
    bool hasReadme = false;
    bool hasCatalog = false;
    // XtApps (/XTApps): a Lua mini app folder, or an encrypted .xtapp package that cannot run
    bool isXtApp = false;
    std::string xtDir;
    std::string xtError;  // non-empty = locked, shown instead of running
  };

  void loadItems();
  void openSelected();

  ButtonNavigator buttonNavigator_;
  std::vector<Item> items_;
  int selected_ = 0;
};
