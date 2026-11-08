#include "KOReaderDocumentId.h"

#include <HalStorage.h>
#include <Logging.h>
#include <MD5Builder.h>

namespace {
const char* getFilenamePtr(const std::string& path) {
  const size_t pos = path.rfind('/');
  return path.c_str() + (pos == std::string::npos ? 0 : pos + 1);
}
}  // namespace

std::string KOReaderDocumentId::calculateFromFilename(const std::string& filePath) {
  // The basename is already the null-terminated suffix of filePath, so hash it
  // in place instead of allocating/copying a temporary std::string.
  const char* filename = getFilenamePtr(filePath);
  if (!filename || *filename == '\0') {
    return "";
  }

  MD5Builder md5;
  md5.begin();
  md5.add(filename);
  md5.calculate();

  std::string result = md5.toString().c_str();
  LOG_DBG("KODoc", "Filename hash: %s (from '%s')", result.c_str(), filename);
  return result;
}

size_t KOReaderDocumentId::getOffset(int i) {
  // Offset = 1024 << (2*i)
  // For i = -1: KOReader uses a value of 0
  // For i >= 0: 1024 << (2*i)
  if (i < 0) {
    return 0;
  }
  return CHUNK_SIZE << (2 * i);
}

std::string KOReaderDocumentId::calculate(const std::string& filePath) {
  HalFile file;
  if (!Storage.openFileForRead("KODoc", filePath, file)) {
    LOG_DBG("KODoc", "Failed to open file: %s", filePath.c_str());
    return "";
  }

  const size_t fileSize = file.fileSize();
  LOG_DBG("KODoc", "Calculating hash for file: %s (size: %zu)", filePath.c_str(), fileSize);

  // Initialize MD5 builder
  MD5Builder md5;
  md5.begin();

  // Buffer for reading chunks
  uint8_t buffer[CHUNK_SIZE];
  size_t totalBytesRead = 0;

  // Read from each offset (i = -1 to 10)
  for (int i = -1; i < OFFSET_COUNT - 1; i++) {
    const size_t offset = getOffset(i);

    // Skip if offset is beyond file size
    if (offset >= fileSize) {
      continue;
    }

    // A skipped or partial sample hashes a different file: the id would match no other device,
    // and saveCachedHash() below would keep it until the book file changes. So any failure
    // fails the whole id. Ported from crosspoint-reader PR #3425 (Daviex).
    if (!file.seekSet(offset)) {
      LOG_ERR("KODoc", "Failed to seek to hash sample at offset %zu", offset);
      file.close();
      return "";
    }

    // Read up to CHUNK_SIZE bytes. read() returns -1 on error: kept signed until checked, as a
    // size_t it became SIZE_MAX and md5.add() hashed far past the end of `buffer`.
    const size_t bytesToRead = std::min(CHUNK_SIZE, fileSize - offset);
    const int bytesRead = file.read(buffer, bytesToRead);
    if (bytesRead < 0 || static_cast<size_t>(bytesRead) != bytesToRead) {
      LOG_ERR("KODoc", "Incomplete hash sample at %zu: read %d of %zu bytes", offset, bytesRead, bytesToRead);
      file.close();
      return "";
    }
    md5.add(buffer, bytesToRead);
    totalBytesRead += bytesToRead;
  }

  // Calculate final hash
  md5.calculate();
  std::string result = md5.toString().c_str();

  LOG_DBG("KODoc", "Hash calculated: %s (from %zu bytes)", result.c_str(), totalBytesRead);

  return result;
}
