#pragma once

#include <HalStorage.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

// True when the file at path exists and holds exactly these bytes, so callers can
// skip rewriting identical content: a small read costs far less than an SD write.
// (CrossInk 2.0.0 idea.)
inline bool fileContentEquals(const char* moduleName, const char* path, const uint8_t* expected,
                              const size_t expectedSize) {
  if (!Storage.exists(path)) return false;  // first-time saves are expected, no error log
  FsFile file;
  if (!Storage.openFileForRead(moduleName, path, file)) return false;
  if (file.fileSize() != expectedSize) {
    file.close();
    return false;
  }
  uint8_t chunk[64];  // small chunk keeps the C3 stack budget
  size_t offset = 0;
  while (offset < expectedSize) {
    const size_t requested = std::min(sizeof(chunk), expectedSize - offset);
    const int read = file.read(chunk, requested);
    if (read != static_cast<int>(requested) || memcmp(chunk, expected + offset, requested) != 0) {
      file.close();
      return false;
    }
    offset += requested;
  }
  file.close();
  return true;
}
