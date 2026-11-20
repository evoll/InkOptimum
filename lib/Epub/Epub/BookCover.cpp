#include "BookCover.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdint>

namespace {
// FAT is case-insensitive, so only lower-case names are listed.
constexpr const char* CUSTOM_SUFFIXES[] = {".cover.jpg", ".cover.jpeg", ".cover.png"};

bool removeIfPresent(const std::string& path) { return !Storage.exists(path.c_str()) || Storage.remove(path.c_str()); }

bool copyVerified(const std::string& from, const std::string& to) {
  FsFile src;
  if (!Storage.openFileForRead("COVER", from, src)) return false;
  FsFile dst;
  if (!Storage.openFileForWrite("COVER", to, dst)) {
    src.close();
    return false;
  }
  uint8_t buffer[1024];
  uint64_t copied = 0;
  bool ok = true;
  for (;;) {
    const int n = src.read(buffer, sizeof(buffer));
    if (n < 0) {
      ok = false;
      break;
    }
    if (n == 0) break;
    if (dst.write(buffer, static_cast<size_t>(n)) != static_cast<size_t>(n)) {
      ok = false;
      break;
    }
    copied += static_cast<uint64_t>(n);
  }
  const uint64_t expected = src.size();
  src.close();
  dst.close();
  return ok && copied > 0 && copied == expected;
}
}  // namespace

namespace BookCover {

std::string basePath(const std::string& bookPath) {
  const size_t separator = bookPath.find_last_of('/');
  auto stripLast = [&](std::string p) {
    const size_t dot = p.rfind('.');
    if (dot == std::string::npos || (separator != std::string::npos && dot < separator)) return p;
    return p.substr(0, dot);
  };
  std::string stem = stripLast(bookPath);
  // "book.fb2.zip" / "book.fb3.zip": drop the inner extension too.
  if (FsHelpers::checkFileExtension(bookPath, ".zip") &&
      (FsHelpers::checkFileExtension(stem, ".fb2") || FsHelpers::checkFileExtension(stem, ".fb3"))) {
    stem = stripLast(stem);
  }
  return stem;
}

std::string findCustom(const std::string& bookPath) {
  const std::string base = basePath(bookPath);
  for (const char* suffix : CUSTOM_SUFFIXES) {
    const std::string candidate = base + suffix;
    if (Storage.exists(candidate.c_str())) return candidate;
  }
  return {};
}

bool isSupportedImage(const std::string& imagePath) {
  return FsHelpers::hasJpgExtension(imagePath) || FsHelpers::hasPngExtension(imagePath);
}

bool install(const std::string& bookPath, const std::string& imagePath) {
  if (!isSupportedImage(imagePath)) {
    LOG_ERR("COVER", "Unsupported custom cover format: %s", imagePath.c_str());
    return false;
  }
  const std::string destination = basePath(bookPath) + (FsHelpers::hasPngExtension(imagePath) ? ".cover.png" : ".cover.jpg");
  if (imagePath == destination) return true;

  const std::string partial = destination + ".part";
  const std::string backup = destination + ".bak";
  removeIfPresent(partial);
  removeIfPresent(backup);
  if (!copyVerified(imagePath, partial)) {
    LOG_ERR("COVER", "Custom cover copy failed: %s", imagePath.c_str());
    removeIfPresent(partial);
    return false;
  }

  const bool hadDestination = Storage.exists(destination.c_str());
  if (hadDestination && !Storage.rename(destination.c_str(), backup.c_str())) {
    removeIfPresent(partial);
    return false;
  }
  if (!Storage.rename(partial.c_str(), destination.c_str())) {
    if (hadDestination) Storage.rename(backup.c_str(), destination.c_str());
    removeIfPresent(partial);
    return false;
  }
  removeIfPresent(backup);

  // Only one managed cover at a time.
  const std::string base = basePath(bookPath);
  for (const char* suffix : CUSTOM_SUFFIXES) {
    const std::string other = base + suffix;
    if (other != destination) removeIfPresent(other);
  }
  LOG_INF("COVER", "Installed custom cover %s", destination.c_str());
  return true;
}

bool reset(const std::string& bookPath) {
  bool ok = true;
  const std::string base = basePath(bookPath);
  for (const char* suffix : CUSTOM_SUFFIXES) {
    const std::string path = base + suffix;
    if (!removeIfPresent(path)) ok = false;
    removeIfPresent(path + ".part");
    removeIfPresent(path + ".bak");
  }
  return ok;
}

}  // namespace BookCover
