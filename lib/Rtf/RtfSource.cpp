#include "RtfSource.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>

#include <BufferedFileWriter.h>
#include <FsFileReader.h>
#include <HtmlFormat.h>

#include "RtfConverter.h"

namespace rtf {

bool prepareSource(const std::string& bookPath, const std::string& cachePath,
                   const std::function<void(int percent)>& onProgress, PreparedSource& out) {
  out = PreparedSource();
  const std::string bodyPath = cachePath + html::kConvertedBodyName;

  HalFile input;
  if (!Storage.openFileForRead("RTF", bookPath, input)) return false;
  FsFileReader reader(input);
  HalFile body;
  if (!Storage.openFileForWrite("RTF", bodyPath, body)) {
    input.close();
    return false;
  }

  const unsigned long started = millis();
  bool converted = false;
  bool writeOk = true;
  {
    BufferedFileWriter writer(body);
    uint32_t calls = 0;
    const uint64_t total = reader.size();
    converted = convertToHtml(reader, [&](const char* d, size_t n) {
      if (writer.write(reinterpret_cast<const uint8_t*>(d), n) != n) writeOk = false;
      if (++calls % 512 == 0) {
        vTaskDelay(1);  // keep the idle task (and its watchdog) alive on long books
        if (onProgress && total > 0) onProgress(10 + static_cast<int>(reader.tell() * 20 / total));
      }
      return writeOk;
    });
    writeOk = writer.finish() && writeOk;
  }
  body.close();
  input.close();
  if (!converted || !writeOk) {
    Storage.remove(bodyPath.c_str());
    LOG_ERR("RTF", "Conversion failed: %s", bookPath.c_str());
    return false;
  }
  LOG_INF("RTF", "converted: %lums", millis() - started);
  out.bodyPath = bodyPath;
  return true;
}

}  // namespace rtf
