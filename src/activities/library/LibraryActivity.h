#pragma once
// Library: one screen with tabs Books / Recent / New / Authors and three views
// (file names, details, covers). Index comes from LibraryIndex (cached on SD).
#include <Library.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

class LibraryActivity final : public Activity {
 public:
  explicit LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Library", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  library::Tab tab = library::Tab::Books;
  library::View view = library::View::Details;
  bool onTabBar = false;
  int selector = 0;
  bool longPressFired = false;

  std::vector<library::Entry> list;            // books of the active tab / author
  std::vector<library::AuthorGroup> authors;   // Authors tab, top level
  bool authorOpen = false;
  std::string openAuthor;

  void rebuild();
  void openBook(const std::string& path);
  void switchTab(int dir);
  void cycleView();
  bool showsAuthors() const { return tab == library::Tab::Authors && !authorOpen; }
  int itemCount() const { return showsAuthors() ? static_cast<int>(authors.size()) : static_cast<int>(list.size()); }
  int gridColumns() const { return 3; }
  void renderCovers(const Rect& area);
  std::string coverThumbFor(const std::string& path, int w, int h) const;
};
