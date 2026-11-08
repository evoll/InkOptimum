#include "MobiSource.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>

#include <BufferedFileWriter.h>
#include <FsFileReader.h>
#include <HtmlFormat.h>

#include <memory>

#include "MobiBook.h"

namespace mobi {

namespace {
constexpr char kRawTmpFile[] = "/.mobi_raw.tmp";
}

bool prepareSource(const std::string& bookPath, const std::string& cachePath,
                   const std::function<void(int percent)>& onProgress, PreparedSource& out) {
  out = PreparedSource();
  const std::string bodyPath = cachePath + html::kConvertedBodyName;
  const std::string rawPath = cachePath + kRawTmpFile;

  HalFile input;
  if (!Storage.openFileForRead("MOBI", bookPath, input)) return false;
  FsFileReader reader(input);
  std::unique_ptr<Book> book(new (std::nothrow) Book());
  if (!book) {
    input.close();
    return false;
  }
  if (!book->open(reader)) {
    out.drm = book->error() == Error::Drm;
    LOG_ERR("MOBI", "Cannot open (error %d): %s", static_cast<int>(book->error()), bookPath.c_str());
    input.close();
    return false;
  }

  const unsigned long started = millis();
  const auto fail = [&](const char* why) {
    input.close();
    Storage.remove(rawPath.c_str());
    Storage.remove(bodyPath.c_str());
    LOG_ERR("MOBI", "%s (error %d): %s", why, static_cast<int>(book->error()), bookPath.c_str());
    return false;
  };

  // Step 1: raw markup into a scratch file.
  {
    HalFile raw;
    if (!Storage.openFileForWrite("MOBI", rawPath, raw)) return fail("Cannot create scratch file");
    bool ok = false;
    bool writeOk = true;
    {
      BufferedFileWriter writer(raw);
      uint32_t calls = 0;
      const uint64_t expected = book->textLength();
      uint64_t written = 0;
      ok = book->extractText([&](const char* d, size_t n) {
        if (writer.write(reinterpret_cast<const uint8_t*>(d), n) != n) writeOk = false;
        written += n;
        if (++calls % 16 == 0) {
          vTaskDelay(1);
          if (onProgress && expected > 0) onProgress(10 + static_cast<int>(std::min<uint64_t>(written, expected) * 10 / expected));
        }
        return writeOk;
      });
      writeOk = writer.finish() && writeOk;
    }
    raw.close();
    if (!ok || !writeOk) return fail("Text extraction failed");
  }
  if (onProgress) onProgress(20);

  // Step 2: raw markup -> HTML body.
  HalFile rawIn;
  if (!Storage.openFileForRead("MOBI", rawPath, rawIn)) return fail("Cannot reopen scratch file");
  FsFileReader rawReader(rawIn);
  HalFile body;
  if (!Storage.openFileForWrite("MOBI", bodyPath, body)) {
    rawIn.close();
    return fail("Cannot create HTML body");
  }
  bool converted = false;
  bool writeOk = true;
  {
    BufferedFileWriter writer(body);
    uint32_t calls = 0;
    const uint64_t total = rawReader.size();
    converted = book->convertToHtml(rawReader, [&](const char* d, size_t n) {
      if (writer.write(reinterpret_cast<const uint8_t*>(d), n) != n) writeOk = false;
      if (++calls % 64 == 0) {
        vTaskDelay(1);
        if (onProgress && total > 0) onProgress(20 + static_cast<int>(rawReader.tell() * 10 / total));
      }
      return writeOk;
    });
    writeOk = writer.finish() && writeOk;
  }
  body.close();
  rawIn.close();
  input.close();
  Storage.remove(rawPath.c_str());
  if (!converted || !writeOk) {
    Storage.remove(bodyPath.c_str());
    LOG_ERR("MOBI", "Conversion failed (error %d): %s", static_cast<int>(book->error()), bookPath.c_str());
    return false;
  }
  LOG_INF("MOBI", "converted: %lums kf8=%d", millis() - started, book->isKf8() ? 1 : 0);
  out.bodyPath = bodyPath;
  return true;
}

}  // namespace mobi
