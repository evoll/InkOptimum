#include "Fb3Source.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>

#include <BufferedFileWriter.h>
#include <FsFileReader.h>
#include <PathUtil.h>

#include <algorithm>
#include <new>

namespace fb3 {

namespace {

constexpr char kBodyFile[] = "/.source.fb3body";
constexpr char kDescriptionFile[] = "/.source.fb3desc";
constexpr char kCoverFile[] = "/.fb3_cover";
constexpr char kStagedPackageFile[] = "/.package.fb3";

bool readSmallFile(const std::string& path, std::string& out, size_t limit) {
  out.clear();
  HalFile file;
  if (!Storage.openFileForRead("FB3", path, file)) return false;
  char buf[512];
  while (out.size() < limit) {
    const size_t n = file.read(buf, sizeof(buf));
    if (n == 0) break;
    out.append(buf, std::min(n, limit - out.size()));
  }
  file.close();
  return true;
}

bool writeSmallFile(const std::string& path, const std::string& data) {
  HalFile file;
  if (!Storage.openFileForWrite("FB3", path, file)) return false;
  const bool ok = data.empty() || file.write(reinterpret_cast<const uint8_t*>(data.data()), data.size()) == data.size();
  file.close();
  return ok;
}

}  // namespace

bool hasFb3Extension(const std::string& path) { return fictionbook::endsWithIgnoreCase(path, ".fb3"); }

bool isBodySource(const std::string& sourcePath) { return fictionbook::endsWithIgnoreCase(sourcePath, ".fb3body"); }

bool prepareSource(const std::string& bookPath, const std::string& cachePath,
                   const std::function<void(int percent)>& onProgress, PreparedSource& out) {
  out = PreparedSource();
  const std::string stagedPath = cachePath + kStagedPackageFile;
  const std::string bodyPath = cachePath + kBodyFile;
  const std::string descPath = cachePath + kDescriptionFile;
  const std::string coverPath = cachePath + kCoverFile;
  Storage.remove(stagedPath.c_str());  // stale staging from an earlier attempt

  HalFile packageFile;
  if (!Storage.openFileForRead("FB3", bookPath, packageFile)) return false;
  bool outerOpen = true;
  FsFileReader outerReader(packageFile);

  HalFile stagedFile;
  bool stagedOpen = false;
  std::unique_ptr<FsFileReader> stagedReader;
  IByteReader* package = &outerReader;

  const auto closeAll = [&]() {
    if (stagedOpen) {
      stagedFile.close();
      stagedOpen = false;
    }
    if (outerOpen) {
      packageFile.close();
      outerOpen = false;
    }
  };
  const auto fail = [&](const char* why) {
    (void)why;  // only used when logging is compiled in
    closeAll();
    Storage.remove(stagedPath.c_str());
    Storage.remove(bodyPath.c_str());
    LOG_ERR("FB3", "%s: %s", why, bookPath.c_str());
    return false;
  };

  // "book.fb3.zip" built like "book.fb2.zip" wraps a plain .fb3. A member of a
  // ZIP cannot be seeked inside, so the inner package is staged once on SD and
  // then read with random access (it also serves the images later).
  if (!isFb3Package(outerReader)) {
    ZipEntryInfo inner;
    if (!findNestedFb3Entry(outerReader, inner)) return fail("Not an FB3 package and no .fb3 inside archive");
    HalFile staged;
    if (!Storage.openFileForWrite("FB3", stagedPath, staged)) return fail("Cannot stage inner FB3");
    bool stageOk = true;
    bool extracted = false;
    {
      BufferedFileWriter writer(staged);
      extracted = extractZipEntry(outerReader, inner, [&](const uint8_t* d, size_t n) {
        if (stageOk && writer.write(d, n) != n) stageOk = false;
      });
      stageOk = writer.finish() && stageOk;
    }
    staged.close();
    packageFile.close();
    outerOpen = false;
    if (!extracted || !stageOk) return fail("Failed to extract inner FB3");
    if (!Storage.openFileForRead("FB3", stagedPath, stagedFile)) return fail("Cannot reopen staged FB3");
    stagedOpen = true;
    stagedReader.reset(new (std::nothrow) FsFileReader(stagedFile));
    if (!stagedReader) return fail("Out of memory opening staged FB3");
    package = stagedReader.get();
    if (!isFb3Package(*package)) return fail("Inner file is not an FB3 package");
    out.wrapped = true;
  }
  if (onProgress) onProgress(10);

  PackageInfo info;
  if (!readPackageInfo(*package, info)) return fail("Broken FB3 package structure");

  // The body XML is inflated once into the cache; Fb3Parser then indexes and
  // renders it by byte offset.
  const unsigned long started = millis();
  HalFile bodyOut;
  if (!Storage.openFileForWrite("FB3", bodyPath, bodyOut)) return fail("Cannot create FB3 body cache");
  bool bodyOk = true;
  bool bodyExtracted = false;
  {
    BufferedFileWriter writer(bodyOut);
    bodyExtracted = extractEntryByOrdinal(*package, info.bodyOrdinal, [&](const uint8_t* d, size_t n) {
      if (bodyOk && writer.write(d, n) != n) bodyOk = false;
    });
    bodyOk = writer.finish() && bodyOk;
  }
  bodyOut.close();
  if (!bodyExtracted || !bodyOk) return fail("FB3 body extraction failed");

  // Sidecars for per-chapter parsers.
  if (!writeSmallFile(descPath, info.descriptionXml)) return fail("FB3 description write failed");
  Storage.remove(coverPath.c_str());
  if (!info.coverBinaryId.empty() && !writeSmallFile(coverPath, info.coverBinaryId)) {
    return fail("FB3 cover marker write failed");
  }

  closeAll();
  out.bodyPath = bodyPath;
  out.imageCount = static_cast<uint32_t>(info.images.size());
  out.hasCover = !info.coverBinaryId.empty();
  LOG_INF("FB3", "prepared: body+description %lums images=%u cover=%d wrapped=%d", millis() - started,
          static_cast<unsigned>(out.imageCount), out.hasCover ? 1 : 0, out.wrapped ? 1 : 0);
  return true;
}

std::string packagePath(const std::string& cachePath, const std::string& bookPath) {
  const std::string staged = cachePath + kStagedPackageFile;
  if (Storage.exists(staged.c_str())) return staged;
  if (!bookPath.empty() && Storage.exists(bookPath.c_str())) return bookPath;
  return std::string();
}

bool loadPackageInfo(const std::string& cachePath, const std::string& bookPath, PackageInfo& out) {
  const std::string path = packagePath(cachePath, bookPath);
  if (path.empty()) return false;
  HalFile file;
  if (!Storage.openFileForRead("FB3", path, file)) return false;
  FsFileReader reader(file);
  const bool ok = readPackageInfo(reader, out);
  file.close();
  return ok;
}

std::unique_ptr<Fb3Parser> makeRenderParser(const std::string& cachePath) {
  std::unique_ptr<Fb3Parser> parser(new Fb3Parser());
  std::string description, cover;
  readSmallFile(cachePath + kDescriptionFile, description, kMaxDescriptionBytes);
  readSmallFile(cachePath + kCoverFile, cover, 512);
  parser->setRenderContext(std::move(description), std::move(cover));
  return parser;
}

}  // namespace fb3
