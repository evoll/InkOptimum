#include "ShelfFile.h"

#include <HalStorage.h>
#include <Logging.h>

#include <string>

namespace ShelfFile {

void load(shelves::ShelfStore& store) {
  if (!Storage.exists(PATH)) {
    store.load("");
    return;
  }
  const String text = Storage.readFile(PATH);
  store.load(std::string(text.c_str(), text.length()));
}

bool save(const shelves::ShelfStore& store) {
  Storage.mkdir("/.crosspoint");
  const bool ok = Storage.writeFile(PATH, store.serialize().c_str());
  if (!ok) LOG_ERR("SHELF", "Cannot write %s", PATH);
  return ok;
}

void onPathRenamed(const char* from, const char* to) {
  if (!Storage.exists(PATH)) return;
  shelves::ShelfStore s;
  load(s);
  s.renamePath(from, to);
  save(s);
}

void onPathDeleted(const char* path) {
  if (!Storage.exists(PATH)) return;
  shelves::ShelfStore s;
  load(s);
  s.forgetPath(path);
  save(s);
}

}  // namespace ShelfFile
