#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Paged plain-text view of a plugin's README.md (the first 16 KB).
class PluginReadmeActivity final : public Activity {
 public:
  PluginReadmeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title, std::string path)
      : Activity("PluginReadme", renderer, mappedInput), title_(std::move(title)), path_(std::move(path)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void wrapText();
  int linesPerPage() const;

  ButtonNavigator buttonNavigator_;
  std::string title_;
  std::string path_;
  std::string text_;
  std::vector<std::string> lines_;
  int wrappedWidth_ = 0;
  int page_ = 0;
};
