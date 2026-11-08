#include "LibraryIndex.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

#include "InkMODSettings.h"
#include "RecentBooksStore.h"

LibraryIndex LibraryIndex::instance;

namespace {
constexpr char INDEX_FILE[] = "/.inkmod/library.idx";
constexpr int MAX_DEPTH = 5;
constexpr size_t MAX_BOOKS = 2000;  // ~100 bytes per entry: bounded heap on the ESP32-C3

bool skipName(const char* name, bool isDir) {
  if (name[0] == '.') return true;  // hidden, macOS metadata, our own /.inkmod
  if (isDir && (!strcmp(name, "System Volume Information") || !strcmp(name, "LOST.DIR"))) return true;
  return false;
}
}  // namespace

uint32_t LibraryIndex::rootSignature() const {
  uint32_t sig = 2166136261u;
  auto root = Storage.open("/");
  if (!root || !root.isDirectory()) return sig;
  root.rewindDirectory();
  char name[256];
  for (auto f = root.openNextFile(); f; f = root.openNextFile()) {
    f.getName(name, sizeof(name));
    if (!skipName(name, f.isDirectory())) {
      sig = library::mixSignature(sig, name, f.isDirectory() ? 0 : static_cast<uint32_t>(f.size()));
    }
    f.close();
  }
  root.close();
  return sig;
}

void LibraryIndex::scanDir(const std::string& dir, int depth) {
  auto d = Storage.open(dir.c_str());
  if (!d || !d.isDirectory()) {
    if (d) d.close();
    return;
  }
  d.rewindDirectory();
  char name[256];
  std::vector<std::string> subdirs;
  for (auto f = d.openNextFile(); f && entries.size() < MAX_BOOKS; f = d.openNextFile()) {
    f.getName(name, sizeof(name));
    const bool isDir = f.isDirectory();
    if (!skipName(name, isDir)) {
      if (isDir) {
        subdirs.emplace_back(name);
      } else if (library::isBookFileName(name)) {
        library::Entry e;
        e.path = (dir == "/" ? "/" : dir + "/") + name;
        e.size = static_cast<uint32_t>(f.size());
        library::parseFileName(name, e.title, e.author);
        entries.push_back(std::move(e));
      }
    }
    f.close();
  }
  d.close();
  if (depth >= MAX_DEPTH) return;
  for (const auto& s : subdirs) {
    if (entries.size() >= MAX_BOOKS) break;
    scanDir((dir == "/" ? "/" : dir + "/") + s, depth + 1);
  }
}

void LibraryIndex::overlayRecents() {
  for (const auto& rb : RECENT_BOOKS.getBooks()) {
    for (auto& e : entries) {
      if (e.path == rb.path) {
        if (!rb.title.empty()) e.title = rb.title;
        if (!rb.author.empty()) e.author = rb.author;
        break;
      }
    }
  }
}

// The index is streamed in both directions: a few thousand books must never exist as one big String on the heap.
void LibraryIndex::save(uint32_t signature) const {
  Storage.ensureDirectoryExists("/.inkmod");
  FsFile f;
  if (!Storage.openFileForWrite("LIB", INDEX_FILE, f)) return;
  const std::string header = library::indexHeader(signature);
  f.write(reinterpret_cast<const uint8_t*>(header.data()), header.size());
  for (const auto& e : entries) {
    const std::string line = library::indexLine(e);
    f.write(reinterpret_cast<const uint8_t*>(line.data()), line.size());
  }
  f.close();
}

bool LibraryIndex::loadCache(uint32_t signature) {
  FsFile f;
  if (!Storage.exists(INDEX_FILE) || !Storage.openFileForRead("LIB", INDEX_FILE, f)) return false;
  std::vector<library::Entry> parsed;
  std::string line;
  bool headerSeen = false;
  bool ok = true;
  uint8_t buf[256];
  int n;
  while (ok && (n = f.read(buf, sizeof(buf))) > 0) {
    for (int i = 0; i < n && ok; ++i) {
      if (buf[i] != '\n') {
        if (line.size() < 1024) line += static_cast<char>(buf[i]);
        continue;
      }
      if (!headerSeen) {
        uint32_t sig = 0;
        ok = library::parseIndexHeader(line, sig) && sig == signature;
        headerSeen = true;
      } else {
        library::Entry e;
        if (library::parseIndexLine(line, e)) parsed.push_back(std::move(e));
      }
      line.clear();
    }
  }
  f.close();
  if (!ok || !headerSeen) return false;
  entries = std::move(parsed);
  return true;
}

bool LibraryIndex::ensureLoaded(bool forceRescan) {
  const uint32_t sig = rootSignature();
  if (!forceRescan && loaded) return !entries.empty();
  entries.clear();
  if (forceRescan || !loadCache(sig)) {
    scanDir("/", 0);
    library::sortByTitle(entries);
    save(sig);
    LOG_DBG("LIB", "Scanned %u books", static_cast<unsigned>(entries.size()));
  }
  overlayRecents();
  loaded = true;
  return !entries.empty();
}

void LibraryIndex::removePath(const std::string& path) {
  for (auto it = entries.begin(); it != entries.end(); ++it) {
    if (it->path == path) {
      entries.erase(it);
      save(rootSignature());
      return;
    }
  }
}
