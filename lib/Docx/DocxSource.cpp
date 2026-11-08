#include "DocxSource.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>

#include <BufferedFileWriter.h>
#include <FsFileReader.h>
#include <PathUtil.h>

#include <algorithm>

#include "DocxPackage.h"

namespace docx {

namespace {

constexpr char kBodyFile[] = "/.source.docxbody";
constexpr char kInfoFile[] = "/.docx_info";
constexpr char kPartTmpFile[] = "/.docx_part.tmp";
constexpr size_t kMaxInfoBytes = 64 * 1024;

bool readSmallFile(const std::string& path, std::string& out, size_t limit) {
  out.clear();
  HalFile file;
  if (!Storage.openFileForRead("DOCX", path, file)) return false;
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
  if (!Storage.openFileForWrite("DOCX", path, file)) return false;
  const bool ok = data.empty() || file.write(reinterpret_cast<const uint8_t*>(data.data()), data.size()) == data.size();
  file.close();
  return ok;
}

}  // namespace

bool isBodySource(const std::string& sourcePath) { return fictionbook::endsWithIgnoreCase(sourcePath, ".docxbody"); }

bool prepareSource(const std::string& bookPath, const std::string& cachePath,
                   const std::function<void(int percent)>& onProgress, PreparedSource& out) {
  out = PreparedSource();
  const std::string bodyPath = cachePath + kBodyFile;
  const std::string infoPath = cachePath + kInfoFile;
  const std::string tmpPath = cachePath + kPartTmpFile;

  HalFile packageFile;
  if (!Storage.openFileForRead("DOCX", bookPath, packageFile)) return false;
  FsFileReader zip(packageFile);

  const auto fail = [&](const char* why) {
    (void)why;  // only used when logging is compiled in
    packageFile.close();
    Storage.remove(tmpPath.c_str());
    Storage.remove(bodyPath.c_str());
    Storage.remove(infoPath.c_str());
    LOG_ERR("DOCX", "%s: %s", why, bookPath.c_str());
    return false;
  };

  if (!isDocxPackage(zip)) return fail("Not a Word package");
  if (onProgress) onProgress(10);

  // styles.xml / numbering.xml are far too large to hold as text: each is
  // inflated to a short-lived cache file and reduced by streaming over it.
  const PartStager stager = [&](IByteReader& pkg, uint32_t ordinal, const PartConsumer& consume) {
    HalFile tmp;
    if (!Storage.openFileForWrite("DOCX", tmpPath, tmp)) return false;
    bool writeOk = true;
    bool extracted = false;
    {
      BufferedFileWriter writer(tmp);
      extracted = opc::extractEntryByOrdinal(pkg, ordinal, [&](const uint8_t* d, size_t n) {
        if (writeOk && writer.write(d, n) != n) writeOk = false;
      });
      writeOk = writer.finish() && writeOk;
    }
    tmp.close();
    bool ok = false;
    if (extracted && writeOk) {
      HalFile in;
      if (Storage.openFileForRead("DOCX", tmpPath, in)) {
        FsFileReader reader(in);
        ok = consume(reader);
        in.close();
      }
    }
    Storage.remove(tmpPath.c_str());
    return ok;
  };

  PackageInfo info;
  if (!readPackageInfo(zip, stager, info)) return fail("Broken Word package structure");
  if (onProgress) onProgress(20);

  const unsigned long started = millis();
  HalFile bodyOut;
  if (!Storage.openFileForWrite("DOCX", bodyPath, bodyOut)) return fail("Cannot create DOCX body cache");
  bool bodyOk = true;
  bool built = false;
  {
    BufferedFileWriter writer(bodyOut);
    built = writeBodySource(zip, info, [&](const uint8_t* d, size_t n) {
      if (bodyOk && writer.write(d, n) != n) bodyOk = false;
    });
    bodyOk = writer.finish() && bodyOk;
  }
  bodyOut.close();
  if (!built || !bodyOk) return fail("DOCX body extraction failed");

  if (!writeSmallFile(infoPath, serialize(info))) return fail("DOCX info write failed");

  packageFile.close();
  out.bodyPath = bodyPath;
  out.imageCount = static_cast<uint32_t>(info.images.size());
  LOG_INF("DOCX", "prepared: %lums styles=%u images=%u", millis() - started, static_cast<unsigned>(info.styles.size()),
          static_cast<unsigned>(out.imageCount));
  return true;
}

bool loadInfo(const std::string& cachePath, PackageInfo& out) {
  std::string text;
  if (!readSmallFile(cachePath + kInfoFile, text, kMaxInfoBytes)) return false;
  return parse(text, out);
}

std::string packagePath(const std::string& bookPath) {
  return (!bookPath.empty() && Storage.exists(bookPath.c_str())) ? bookPath : std::string();
}

std::unique_ptr<DocxParser> makeRenderParser(const std::string& cachePath) {
  std::unique_ptr<DocxParser> parser(new DocxParser());
  PackageInfo info;
  loadInfo(cachePath, info);  // a missing sidecar degrades to "no styles, no images"
  parser->setOwnedInfo(std::move(info));
  return parser;
}

}  // namespace docx
