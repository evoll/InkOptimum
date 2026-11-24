#include "VocabLog.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <ctime>
#include <string>

#include "InkMODSettings.h"

namespace VocabLog {
namespace {
constexpr uint32_t MAX_BYTES = 256 * 1024;  // the file stops growing here; a full file is kept, not rotated
}

void append(const char* selected, const char* headword) {
  if (!SETTINGS.vocabularyLog || !selected || !selected[0]) return;
  JsonDocument doc;
  doc["w"] = selected;
  doc["h"] = (headword && headword[0]) ? headword : selected;
  const time_t now = time(nullptr);
  doc["t"] = now > 946684800 ? static_cast<int64_t>(now) : 0;
  std::string line;
  serializeJson(doc, line);
  line += '\n';
  Storage.mkdir("/.crosspoint");
  HalFile f = Storage.open(PATH, O_WRONLY | O_CREAT | O_APPEND);
  if (!f) {
    LOG_ERR("VOCAB", "Cannot open %s", PATH);
    return;
  }
  if (f.size() + line.size() <= MAX_BYTES) f.write(reinterpret_cast<const uint8_t*>(line.data()), line.size());
  f.close();
}

}  // namespace VocabLog
