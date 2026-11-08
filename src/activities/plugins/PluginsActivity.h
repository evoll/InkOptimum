#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/PluginLocations.h"

// Home > Plugins: lists the SD-card plugin folders (title and one-line description from manifest.json, with
// device.json overriding). A plugin with a README opens it as paged text; one without does nothing yet.
class PluginsActivity final : public Activity {
 public:
  explicit PluginsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Plugins", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct Item {
    PluginLocations::Entry entry;
    std::string title;
    std::string description;
    bool hasReadme = false;
    bool hasCatalog = false;  // device.json declares browse.url
  };

  void loadItems();
  void openSelected();

  ButtonNavigator buttonNavigator_;
  std::vector<Item> items_;
  int selected_ = 0;
  std::string notice_;  // one-line status under the list
};
