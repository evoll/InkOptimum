#include "LegacyDataImport.h"

#include <HalStorage.h>
#include <LegacyImportRules.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "WifiCredentialStore.h"

namespace LegacyDataImport {
namespace {

using namespace legacy_import;

// Copies src to dst unless dst exists or src is larger than maxSize. Returns 1 copied, 0 skipped, -1 failed.
int copyIfMissing(const std::string& src, const std::string& dst, const size_t maxSize) {
  if (Storage.exists(dst.c_str())) return 0;
  HalFile in;
  if (!Storage.openFileForRead("IMPORT", src, in)) return 0;
  const size_t size = in.fileSize();
  if (size == 0 || size > maxSize) {
    in.close();
    return 0;
  }
  HalFile out;
  if (!Storage.openFileForWrite("IMPORT", dst, out)) {
    in.close();
    return -1;
  }
  uint8_t buffer[256];
  size_t remaining = size;
  bool ok = true;
  while (remaining > 0) {
    const size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    if (in.read(buffer, chunk) != static_cast<int>(chunk) || out.write(buffer, chunk) != chunk) {
      ok = false;
      break;
    }
    remaining -= chunk;
  }
  in.close();
  out.close();
  if (!ok) {
    Storage.remove(dst.c_str());  // never leave a half-copied file for a loader to trip over
    return -1;
  }
  return 1;
}

// Names of the plain entries of a directory (files or directories as asked), read first so no directory handle
// stays open while files are copied.
std::vector<std::string> listEntries(const std::string& path, const bool wantDirectories) {
  std::vector<std::string> names;
  auto dir = Storage.open(path.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return names;
  }
  char name[128];
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    entry.getName(name, sizeof(name));
    const bool isDir = entry.isDirectory();
    entry.close();
    if (isDir == wantDirectories && name[0] != '\0') names.emplace_back(name);
  }
  dir.close();
  return names;
}

void tally(Result& result, const int outcome) {
  if (outcome > 0) result.copied++;
  if (outcome < 0) result.failed++;
}

}  // namespace

Result run() {
  Result result;
  const std::string source = SOURCE_ROOT;
  const std::string target = TARGET_ROOT;
  auto root = Storage.open(source.c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return result;
  }
  root.close();
  result.sourceFound = true;
  Storage.mkdir(target.c_str());

  // Root files, skipped when we already hold the data in either of our formats.
  for (const auto& file : ROOT_FILES) {
    const std::string dst = target + "/" + file.name;
    if (file.alt && Storage.exists((target + "/" + file.alt).c_str())) continue;
    tally(result, copyIfMissing(source + "/" + file.name, dst, maxSizeForRootFile()));
  }

  // Flat directories (bookmarks, clippings).
  for (const char* dirName : ROOT_DIRS) {
    const std::string srcDir = source + "/" + dirName;
    const auto names = listEntries(srcDir, false);
    if (names.empty()) continue;
    const std::string dstDir = target + "/" + dirName;
    Storage.mkdir(dstDir.c_str());
    for (const auto& name : names) {
      if (!isPlainFileName(name.c_str())) continue;
      tally(result, copyIfMissing(srcDir + "/" + name, dstDir + "/" + name, maxSizeForDirFile()));
    }
  }

  // Per-book reading position and statistics.
  for (const auto& dirName : listEntries(source, true)) {
    if (!isBookCacheDirName(dirName.c_str())) continue;
    int copiedForBook = 0;
    for (const char* fileName : BOOK_FILES) {
      const std::string src = source + "/" + dirName + "/" + fileName;
      if (!Storage.exists(src.c_str())) continue;
      const std::string dstDir = target + "/" + dirName;
      Storage.mkdir(dstDir.c_str());
      const int outcome = copyIfMissing(src, dstDir + "/" + fileName, maxSizeForBookFile(fileName));
      tally(result, outcome);
      if (outcome > 0) copiedForBook++;
    }
    if (copiedForBook > 0) result.books++;
  }

  // The stores read their files once at boot and write them back on change: reload what was just imported, or
  // the first book opened would overwrite the imported files with the (empty) in-memory copies.
  if (result.copied > 0) {
    RECENT_BOOKS.loadFromFile();
    WIFI_STORE.loadFromFile();
  }
  LOG_DBG("IMPORT", "Imported %d files for %d books, %d failed", result.copied, result.books, result.failed);
  return result;
}

}  // namespace LegacyDataImport
