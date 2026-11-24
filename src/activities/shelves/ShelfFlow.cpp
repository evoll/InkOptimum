#include "ShelfFlow.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <memory>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "activities/home/BookActions.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/OptionSelectionActivity.h"
#include "util/ShelfFile.h"

namespace shelf_flow {
namespace {

void storeAndToast(Activity& host, GfxRenderer& r, shelves::ShelfStore& store, const size_t index, const std::string& path) {
  if (store.contains(index, path)) {
    BookActions::drawToast(r, tr(STR_SHELF_ALREADY));
  } else if (store.addBook(index, path) && ShelfFile::save(store)) {
    BookActions::drawToast(r, tr(STR_SHELF_ADDED));
  } else {
    BookActions::drawToast(r, tr(STR_SHELF_FULL));
  }
  delay(900);
  host.requestUpdate(true);
}

}  // namespace

void addBook(Activity& host, GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& bookPath) {
  auto store = std::make_shared<shelves::ShelfStore>();
  ShelfFile::load(*store);
  std::vector<std::string> options;
  for (const auto& s : store->shelves()) options.push_back(s.name);
  options.push_back(tr(STR_NEW_SHELF));
  host.startActivityForResult(
      std::make_unique<OptionSelectionActivity>(renderer, mappedInput, "ShelfPick",
                                                StrId::STR_ADD_TO_SHELF, options, 0, false, false),
      [&host, &renderer, &mappedInput, store, bookPath](const ActivityResult& result) {
        if (result.isCancelled) return;
        const size_t index = std::get<OptionSelectionResult>(result.data).index;
        if (index < store->shelves().size()) {
          storeAndToast(host, renderer, *store, index, bookPath);
          return;
        }
        host.startActivityForResult(
            std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SHELF_NAME), "",
                                                    shelves::ShelfStore::kMaxNameBytes),
            [&host, &renderer, &mappedInput, store, bookPath](const ActivityResult& kb) {
              if (kb.isCancelled) return;
              const int created = store->create(std::get<KeyboardResult>(kb.data).text);
              if (created < 0) {
                BookActions::drawToast(renderer, tr(STR_SHELF_BAD_NAME));
                delay(900);
                host.requestUpdate(true);
                return;
              }
              storeAndToast(host, renderer, *store, static_cast<size_t>(created), bookPath);
            });
      });
}

}  // namespace shelf_flow
