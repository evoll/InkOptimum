#pragma once

// What "Import from another firmware" may copy from /.crosspoint (the data directory of CrossPoint, CrossInk,
// Paperio-Reader and witchhunt-reader, which share its file names) into /.inkmod. Pure rules, no storage access,
// so they are unit-tested on the host. Nothing here ever overwrites: a file we already have wins.

#include <cstddef>
#include <cstring>

namespace legacy_import {

inline constexpr const char* SOURCE_ROOT = "/.crosspoint";
inline constexpr const char* TARGET_ROOT = "/.inkmod";

// A file in the data root. `alt` is another file of ours that holds the same data (an older storage format); when
// either exists the import skips the entry.
struct RootFile {
  const char* name;
  const char* alt;
};
inline constexpr RootFile ROOT_FILES[] = {
    {"wifi.json", "wifi.bin"},           // saved Wi-Fi networks (passwords stay obfuscated, same scheme)
    {"koreader.json", "koreader.bin"},   // KOReader sync account
    {"opds.json", nullptr},              // OPDS servers
    {"lcp_device.json", nullptr},        // LCP device id
    {"lcp_passphrases.json", nullptr},   // LCP passphrases
    {"recent.json", "recent.bin"},       // recent books
    {"global_stats.bin", nullptr},       // all-time reading statistics
};

// Directories whose files are copied one by one (flat, never overwriting).
inline constexpr const char* ROOT_DIRS[] = {"bookmarks", "clippings"};

// Per-book files in <prefix>_<hash>/ (the hash of the book path is computed the same way on every board).
inline constexpr const char* BOOK_FILES[] = {"progress.bin", "stats.bin"};

inline bool startsWith(const char* text, const char* prefix) { return std::strncmp(text, prefix, std::strlen(prefix)) == 0; }

// epub_123456, txt_123456 or xtc_123456: a book cache directory (digits only after the prefix).
inline bool isBookCacheDirName(const char* name) {
  if (!name) return false;
  const char* digits = nullptr;
  if (startsWith(name, "epub_")) {
    digits = name + 5;
  } else if (startsWith(name, "txt_") || startsWith(name, "xtc_")) {
    digits = name + 4;
  } else {
    return false;
  }
  if (*digits == '\0') return false;
  for (const char* c = digits; *c; ++c) {
    if (*c < '0' || *c > '9') return false;
  }
  return true;
}

// A plain file name from a directory listing: no path parts, not hidden-dot entries, not a temp/backup copy.
inline bool isPlainFileName(const char* name) {
  if (!name || name[0] == '\0' || name[0] == '.') return false;
  if (std::strchr(name, '/') || std::strchr(name, '\\')) return false;
  const size_t len = std::strlen(name);
  if (len > 4 && std::strcmp(name + len - 4, ".tmp") == 0) return false;
  if (len > 4 && std::strcmp(name + len - 4, ".bak") == 0) return false;
  return true;
}

// Sanity bounds on what a copied file may weigh; a larger file is not what the readers write.
inline size_t maxSizeForBookFile(const char* name) { return std::strcmp(name, "progress.bin") == 0 ? 16 : 8 * 1024; }
inline size_t maxSizeForRootFile() { return 256 * 1024; }
inline size_t maxSizeForDirFile() { return 64 * 1024; }  // a bookmark list or a clippings text

}  // namespace legacy_import
