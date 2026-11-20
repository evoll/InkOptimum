#include "ZipFile.h"

#include "ZipCentralDirWindow.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <InflateReader.h>
#include <Logging.h>
#include <ReaderWork.h>

#include <algorithm>

struct ZipInflateCtx {
  InflateReader reader;  // Must be first — callback casts uzlib_uncomp* to ZipInflateCtx*
  HalFile* file = nullptr;
  size_t fileRemaining = 0;
  uint8_t* readBuf = nullptr;
  size_t readBufSize = 0;
};

namespace {
constexpr uint16_t ZIP_METHOD_STORED = 0;
constexpr uint16_t ZIP_METHOD_DEFLATED = 8;
constexpr size_t ZIP_MIN_INPUT_CHUNK = 4096;
constexpr uint32_t ZIP_CENTRAL_HEADER_SIGNATURE = 0x02014b50;

uint16_t readLe16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t readLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// RAII zip: opens the zip if not already open, closes on destruction only if
// it performed the open.  Removes the wasOpen/close boilerplate from every method.
class ScopedOpenClose final {
 public:
  [[nodiscard]] explicit ScopedOpenClose(ZipFile& zf) : zf(zf), needsClose(!zf.isOpen()) {
    if (needsClose) ok = zf.open();
  }
  ~ScopedOpenClose() {
    if (needsClose && ok) zf.close();
  }
  ScopedOpenClose(const ScopedOpenClose&) = delete;
  ScopedOpenClose& operator=(const ScopedOpenClose&) = delete;
  ScopedOpenClose(ScopedOpenClose&&) = delete;
  ScopedOpenClose& operator=(ScopedOpenClose&&) = delete;
  explicit operator bool() const { return ok || !needsClose; }

 private:
  ZipFile& zf;
  bool needsClose = false;
  bool ok = true;  // true when zip was already open (no open() call needed)
};

int zipReadCallback(uzlib_uncomp* uncomp) {
  auto* ctx = reinterpret_cast<ZipInflateCtx*>(uncomp);
  if (ctx->fileRemaining == 0) return -1;

  const size_t toRead = ctx->fileRemaining < ctx->readBufSize ? ctx->fileRemaining : ctx->readBufSize;
  const size_t bytesRead = ctx->file->read(ctx->readBuf, toRead);
  ctx->fileRemaining -= bytesRead;

  if (bytesRead == 0) return -1;

  uncomp->source = ctx->readBuf + 1;
  uncomp->source_limit = ctx->readBuf + bytesRead;
  return ctx->readBuf[0];
}
}  // namespace

bool ZipFile::loadAllFileStatSlims() {
  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  if (!loadZipDetails()) return false;

  fileStatSlimCache.clear();
  fileStatSlimCache.reserve(zipDetails.totalEntries);

  // The directory is read in blocks and each entry parsed from RAM (see ZipCentralDirWindow.h).
  zip_cd::Buffer buffer;
  zip_cd::Window<HalFile> window(file, buffer.data(), buffer.size());
  window.begin(zipDetails.centralDirOffset);
  zip_cd::Entry entry;
  while (window.next(entry)) {
    if (!entry.name) continue;  // longer than 255 bytes: never looked up
    FileStatSlim fileStat = {};
    fileStat.method = entry.method;
    fileStat.compressedSize = entry.compressedSize;
    fileStat.uncompressedSize = entry.uncompressedSize;
    fileStat.localHeaderOffset = entry.localHeaderOffset;
    fileStatSlimCache.emplace(std::string(entry.name, entry.nameLen), fileStat);
  }

  lastCentralDirPos = zipDetails.centralDirOffset;
  lastCentralDirPosValid = true;

  return true;
}

bool ZipFile::loadFileStatSlim(const char* filename, FileStatSlim* fileStat) {
  if (!fileStatSlimCache.empty()) {
    const auto it = fileStatSlimCache.find(filename);
    if (it != fileStatSlimCache.end()) {
      *fileStat = it->second;
      return true;
    }
    return false;
  }

  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  if (!loadZipDetails()) return false;

  const uint32_t startPos = lastCentralDirPosValid ? lastCentralDirPos : zipDetails.centralDirOffset;
  const size_t wantedLen = strlen(filename);
  bool wrapped = false;
  bool found = false;

  // Lookups usually ask for entries in directory order (spine order), so the walk resumes where the last
  // one ended and wraps once to the start; the window makes each stretch of it a few block reads.
  zip_cd::Buffer buffer;
  zip_cd::Window<HalFile> window(file, buffer.data(), buffer.size());
  window.begin(startPos);
  zip_cd::Entry entry;

  while (true) {
    if (!window.next(entry)) {
      if (!wrapped && lastCentralDirPosValid && startPos != zipDetails.centralDirOffset) {
        window.begin(zipDetails.centralDirOffset);
        wrapped = true;
        continue;
      }
      break;
    }

    if (wrapped && entry.start >= startPos) {
      break;
    }

    if (entry.name && entry.nameLen == wantedLen && memcmp(entry.name, filename, wantedLen) == 0) {
      fileStat->method = entry.method;
      fileStat->compressedSize = entry.compressedSize;
      fileStat->uncompressedSize = entry.uncompressedSize;
      fileStat->localHeaderOffset = entry.localHeaderOffset;
      lastCentralDirPos = entry.nextStart;
      lastCentralDirPosValid = true;
      found = true;
      break;
    }
  }

  return found;
}

long ZipFile::getDataOffset(const FileStatSlim& fileStat) {
  const ScopedOpenClose zip{*this};
  if (!zip) return -1;

  constexpr auto localHeaderSize = 30;

  uint8_t pLocalHeader[localHeaderSize];
  // ZIP offsets stored in the central directory are relative to the start of
  // the ZIP archive itself. Some converter-generated EPUBs prepend a couple
  // of harmless bytes (for example CRLF) before the first PK header, so add
  // the detected archive prefix here. Normal EPUBs keep archiveOffset == 0.
  const uint64_t fileOffset = static_cast<uint64_t>(zipDetails.archiveOffset) + fileStat.localHeaderOffset;

  file.seek(fileOffset);
  const size_t read = file.read(pLocalHeader, localHeaderSize);

  if (read != localHeaderSize) {
    LOG_ERR("ZIP", "Something went wrong reading the local header");
    return -1;
  }

  if (readLe32(pLocalHeader) != 0x04034b50) {
    LOG_ERR("ZIP", "Not a valid zip file header");
    return -1;
  }

  const uint16_t filenameLength = readLe16(pLocalHeader + 26);
  const uint16_t extraOffset = readLe16(pLocalHeader + 28);
  return fileOffset + localHeaderSize + filenameLength + extraOffset;
}

bool ZipFile::loadZipDetails() {
  if (zipDetails.isSet) {
    return true;
  }

  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  const size_t fileSize = file.size();
  constexpr size_t EOCD_MIN_SIZE = 22;
  constexpr size_t EOCD_MAX_COMMENT = 0xFFFF;
  constexpr size_t EOCD_MAX_SEARCH = EOCD_MIN_SIZE + EOCD_MAX_COMMENT;
  constexpr size_t EOCD_SCAN_CHUNK = 1024;
  constexpr uint32_t EOCD_SIGNATURE = 0x06054b50;

  if (fileSize < EOCD_MIN_SIZE) {
    LOG_ERR("ZIP", "File too small to be a valid zip");
    return false;
  }

  const size_t searchStart = fileSize > EOCD_MAX_SEARCH ? fileSize - EOCD_MAX_SEARCH : 0;
  size_t windowEnd = fileSize;
  uint8_t buffer[EOCD_SCAN_CHUNK];
  uint8_t record[EOCD_MIN_SIZE];

  while (windowEnd > searchStart) {
    const size_t windowStart = windowEnd - searchStart > EOCD_SCAN_CHUNK ? windowEnd - EOCD_SCAN_CHUNK : searchStart;
    const size_t readLen = windowEnd - windowStart;
    if (!file.seek(windowStart) || file.read(buffer, readLen) != static_cast<int>(readLen)) {
      LOG_ERR("ZIP", "Failed to read EOCD scan window");
      return false;
    }

    if (readLen >= 4) {
      for (int i = static_cast<int>(readLen) - 4; i >= 0; --i) {
        if (readLe32(buffer + i) != EOCD_SIGNATURE) continue;

        const size_t absoluteOffset = windowStart + static_cast<size_t>(i);
        if (absoluteOffset + EOCD_MIN_SIZE > fileSize) continue;

        if (static_cast<size_t>(i) + EOCD_MIN_SIZE <= readLen) {
          std::copy_n(buffer + i, EOCD_MIN_SIZE, record);
        } else {
          if (!file.seek(absoluteOffset) || file.read(record, sizeof(record)) != static_cast<int>(sizeof(record))) {
            continue;
          }
        }

        const uint16_t commentLen = readLe16(record + 20);
        if (absoluteOffset + EOCD_MIN_SIZE + commentLen != fileSize) continue;

        const uint32_t centralDirSize = readLe32(record + 12);
        const uint32_t centralDirOffset = readLe32(record + 16);
        if (centralDirOffset > absoluteOffset || centralDirSize > absoluteOffset) {
          continue;
        }

        // Most EPUBs start at byte 0, so EOCD offsets are already absolute.
        // A few real-world converters prepend bytes before the ZIP stream
        // (e.g. "\r\nPK...") while leaving all ZIP offsets relative to the
        // first PK header. In that case the declared centralDirOffset points
        // just before the real central directory and lookups fail even though
        // the archive is otherwise valid.
        //
        // Keep the existing path untouched when the declared offset is valid.
        // Only if its signature is missing do we try the mathematically exact
        // central-directory start (EOCD position - centralDirSize). This makes
        // the compatibility path a strict fallback for malformed/prefixed ZIPs.
        uint32_t archiveOffset = 0;
        uint32_t actualCentralDirOffset = centralDirOffset;
        uint8_t sig[4] = {};
        bool declaredOffsetValid = false;
        if (file.seek(centralDirOffset) && file.read(sig, sizeof(sig)) == static_cast<int>(sizeof(sig))) {
          declaredOffsetValid = readLe32(sig) == ZIP_CENTRAL_HEADER_SIGNATURE;
        }

        if (!declaredOffsetValid) {
          if (absoluteOffset < centralDirSize) continue;
          const uint32_t candidateOffset = static_cast<uint32_t>(absoluteOffset - centralDirSize);
          if (candidateOffset < centralDirOffset) continue;
          if (!file.seek(candidateOffset) || file.read(sig, sizeof(sig)) != static_cast<int>(sizeof(sig)) ||
              readLe32(sig) != ZIP_CENTRAL_HEADER_SIGNATURE) {
            continue;
          }
          archiveOffset = candidateOffset - centralDirOffset;
          actualCentralDirOffset = candidateOffset;
          LOG_INF("ZIP", "Detected %lu-byte prefix before ZIP stream", static_cast<unsigned long>(archiveOffset));
        } else if (static_cast<uint64_t>(centralDirOffset) + centralDirSize > absoluteOffset) {
          continue;
        }

        zipDetails.totalEntries = readLe16(record + 10);
        zipDetails.centralDirOffset = actualCentralDirOffset;
        zipDetails.archiveOffset = archiveOffset;
        zipDetails.isSet = true;
        return true;
      }
    }

    if (windowStart == searchStart) break;
    windowEnd = windowStart + 3;
  }

  LOG_ERR("ZIP", "EOCD signature not found in zip file");
  return false;
}

bool ZipFile::open() {
  if (!Storage.openFileForRead("ZIP", filePath, file)) {
    return false;
  }
  return true;
}

bool ZipFile::close() {
  if (file) {
    file.close();
  }
  lastCentralDirPos = 0;
  lastCentralDirPosValid = false;
  return true;
}

bool ZipFile::getInflatedFileSize(const char* filename, size_t* size) {
  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) {
    return false;
  }

  *size = static_cast<size_t>(fileStat.uncompressedSize);
  return true;
}

int ZipFile::fillUncompressedSizes(std::deque<SizeTarget>& targets, std::deque<uint32_t>& sizes) {
  if (targets.empty()) {
    return 0;
  }

  const ScopedOpenClose zip{*this};
  if (!zip) return 0;

  if (!loadZipDetails()) return 0;

  int matched = 0;
  const int targetCount = static_cast<int>(targets.size());

  zip_cd::Buffer buffer;
  zip_cd::Window<HalFile> window(file, buffer.data(), buffer.size());
  window.begin(zipDetails.centralDirOffset);
  zip_cd::Entry entry;

  while (window.next(entry)) {
    if (!entry.name) continue;

    const uint64_t hash = fnvHash64(entry.name, entry.nameLen);
    const SizeTarget key = {hash, entry.nameLen, 0};

    auto it = std::lower_bound(targets.begin(), targets.end(), key, [](const SizeTarget& a, const SizeTarget& b) {
      return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
    });

    while (it != targets.end() && it->hash == hash && it->len == entry.nameLen) {
      if (it->index < sizes.size()) {
        sizes[it->index] = entry.uncompressedSize;
        matched++;
      }
      ++it;
    }

    if (matched >= targetCount) {
      break;
    }
  }

  return matched;
}

uint8_t* ZipFile::readFileToMemory(const char* filename, size_t* size, const bool trailingNullByte) {
  const ScopedOpenClose zip{*this};
  if (!zip) return nullptr;

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) return nullptr;

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) return nullptr;

  file.seek(fileOffset);

  const auto deflatedDataSize = fileStat.compressedSize;
  const auto inflatedDataSize = fileStat.uncompressedSize;
  // A corrupt entry (or a ZIP64 one, whose 32-bit size field holds the 0xFFFFFFFF marker) claims
  // 4 GB. With the trailing NUL that size wraps to a 0-byte allocation, which the read below would
  // then fill with the entry: a heap overflow from a crafted EPUB.
  if (trailingNullByte && inflatedDataSize == UINT32_MAX) {
    LOG_ERR("ZIP", "Entry claims %u bytes, refusing %s", static_cast<unsigned>(inflatedDataSize), filename);
    return nullptr;
  }
  const size_t dataSize = trailingNullByte ? size_t{inflatedDataSize} + 1 : inflatedDataSize;
  const auto data = static_cast<uint8_t*>(malloc(dataSize));
  if (data == nullptr) {
    LOG_ERR("ZIP", "Failed to allocate memory for output buffer (%zu bytes)", dataSize);
    return nullptr;
  }

  if (fileStat.method == ZIP_METHOD_STORED) {
    const size_t dataRead = file.read(data, inflatedDataSize);

    if (dataRead != inflatedDataSize) {
      LOG_ERR("ZIP", "Failed to read data");
      free(data);
      return nullptr;
    }
  } else if (fileStat.method == ZIP_METHOD_DEFLATED) {
    const auto deflatedData = static_cast<uint8_t*>(malloc(deflatedDataSize));
    if (deflatedData == nullptr) {
      LOG_ERR("ZIP", "Failed to allocate memory for decompression buffer");
      free(data);
      return nullptr;
    }

    const size_t dataRead = file.read(deflatedData, deflatedDataSize);

    if (dataRead != deflatedDataSize) {
      LOG_ERR("ZIP", "Failed to read data, expected %d got %d", deflatedDataSize, dataRead);
      free(deflatedData);
      free(data);
      return nullptr;
    }

    bool success = false;
    {
      InflateReader r;
      r.init(false);
      r.setSource(deflatedData, deflatedDataSize);
      success = r.read(data, inflatedDataSize);
    }
    free(deflatedData);

    if (!success) {
      LOG_ERR("ZIP", "Failed to inflate file");
      free(data);
      return nullptr;
    }
  } else {
    LOG_ERR("ZIP", "Unsupported compression method");
    free(data);
    return nullptr;
  }

  if (trailingNullByte) data[inflatedDataSize] = '\0';
  if (size) *size = inflatedDataSize;
  return data;
}

bool ZipFile::readFileToStream(const char* filename, Print& out, const size_t chunkSize,
                               const reader::ReaderCancellationToken* cancellationToken) {
  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) return false;

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) return false;

  file.seek(fileOffset);
  const auto deflatedDataSize = fileStat.compressedSize;
  const auto inflatedDataSize = fileStat.uncompressedSize;

  if (fileStat.method == ZIP_METHOD_STORED) {
    const auto buffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!buffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for buffer");
      return false;
    }

    size_t remaining = inflatedDataSize;
    while (remaining > 0) {
      if (cancellationToken && cancellationToken->isCancellationRequested()) {
        LOG_INF("ZIP", "Stream extraction cancelled");
        free(buffer);
        return false;
      }
      const int readResult = file.read(buffer, remaining < chunkSize ? remaining : chunkSize);
      if (readResult <= 0) {  // read() returns -1 on an SD error
        LOG_ERR("ZIP", "Could not read more bytes");
        free(buffer);
        return false;
      }
      const auto dataRead = static_cast<size_t>(readResult);

      if (out.write(buffer, dataRead) != dataRead) {
        LOG_ERR("ZIP", "Failed to write all output bytes to stream");
        free(buffer);
        return false;
      }
      remaining -= dataRead;
    }

    free(buffer);
    return true;
  }

  if (fileStat.method == ZIP_METHOD_DEFLATED) {
    ZipInflateCtx ctx;
    ctx.file = &file;
    ctx.fileRemaining = deflatedDataSize;

    if (!ctx.reader.init(true)) {
      LOG_ERR("ZIP", "Failed to init inflate reader (free=%u, maxAlloc=%u, chunk=%zu)", ESP.getFreeHeap(),
              ESP.getMaxAllocHeap(), chunkSize);
      return false;
    }

    const size_t inputChunkSize = std::max(chunkSize, ZIP_MIN_INPUT_CHUNK);
    auto* fileReadBuffer = static_cast<uint8_t*>(malloc(inputChunkSize));
    if (!fileReadBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for zip file read buffer (free=%u, maxAlloc=%u, chunk=%zu)",
              ESP.getFreeHeap(), ESP.getMaxAllocHeap(), inputChunkSize);
      return false;
    }

    auto* outputBuffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!outputBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for output buffer (free=%u, maxAlloc=%u, chunk=%zu)", ESP.getFreeHeap(),
              ESP.getMaxAllocHeap(), chunkSize);
      free(fileReadBuffer);
      return false;
    }

    ctx.readBuf = fileReadBuffer;
    ctx.readBufSize = inputChunkSize;
    ctx.reader.setReadCallback(zipReadCallback);

    bool success = false;
    size_t totalProduced = 0;

    while (true) {
      if (cancellationToken && cancellationToken->isCancellationRequested()) {
        LOG_INF("ZIP", "Stream inflation cancelled after %zu bytes", totalProduced);
        break;
      }
      size_t produced;
      const InflateStatus status = ctx.reader.readAtMost(outputBuffer, chunkSize, &produced);

      totalProduced += produced;
      if (totalProduced > static_cast<size_t>(inflatedDataSize)) {
        LOG_ERR("ZIP", "Decompressed size exceeds expected (%zu > %zu)", totalProduced,
                static_cast<size_t>(inflatedDataSize));
        break;
      }

      if (produced > 0) {
        if (out.write(outputBuffer, produced) != produced) {
          LOG_ERR("ZIP", "Failed to write all output bytes to stream");
          break;
        }
      }

      if (status == InflateStatus::Done) {
        if (totalProduced != static_cast<size_t>(inflatedDataSize)) {
          LOG_ERR("ZIP", "Decompressed size mismatch (expected %zu, got %zu)", static_cast<size_t>(inflatedDataSize),
                  totalProduced);
          break;
        }
        LOG_DBG("ZIP", "Decompressed %d bytes into %d bytes", deflatedDataSize, inflatedDataSize);
        success = true;
        break;
      }

      if (status == InflateStatus::Error) {
        LOG_ERR("ZIP", "Decompression failed");
        break;
      }
    }

    free(outputBuffer);
    free(fileReadBuffer);
    return success;
  }

  LOG_ERR("ZIP", "Unsupported compression method");
  return false;
}
