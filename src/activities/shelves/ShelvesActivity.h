#pragma once

#include <Shelves.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Apps > Shelves: user-made lists of books. Level 1 lists the shelves (plus "New shelf"), Confirm opens one; level 2
// lists its books, Confirm opens the book. Holding Confirm opens a menu (shelf: rename/delete; book: remove from the
// shelf). Back goes up one level. Books that no longer exist on the card are dropped when a shelf is opened.
class ShelvesActivity final : public Activity {
 public:
  explicit ShelvesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Shelves", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  size_t rowCount() const;
  void activate();
  void showMenu();
  void createShelf();
  void renameShelf(size_t index);
  void deleteShelf(size_t index);
  void removeBook(size_t shelf, size_t bookIndex);
  void pruneMissing(size_t shelf);
  void save();

  ButtonNavigator nav_;
  shelves::ShelfStore store_;
  int openShelf_ = -1;  // -1: the list of shelves
  size_t selected_ = 0;
  bool longPressFired_ = false;
};
