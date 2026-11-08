#pragma once
// SD-card book index for the Library screen. Scans folders without opening books;
// titles/authors come from the file name, upgraded by the recent-books store.
// The index is cached in /.inkmod/library.idx so the screen opens instantly.
#include <Library.h>

#include <string>
#include <vector>

class LibraryIndex {
 public:
  static LibraryIndex& getInstance() { return instance; }

  // Loads the cache (if valid for the current root listing) or rescans. Returns false when empty.
  bool ensureLoaded(bool forceRescan = false);
  const std::vector<library::Entry>& books() const { return entries; }
  void removePath(const std::string& path);

 private:
  static LibraryIndex instance;
  std::vector<library::Entry> entries;
  bool loaded = false;

  uint32_t rootSignature() const;
  void scanDir(const std::string& dir, int depth);
  void overlayRecents();
  void save(uint32_t signature) const;
  bool loadCache(uint32_t signature);
};

#define LIBRARY_INDEX LibraryIndex::getInstance()
