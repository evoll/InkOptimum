#include "ReadingSessionLog.h"

#include <ReadingSessions.h>  // lets the library finder pull lib/ReadingSessions into the link

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

#include "InkMODSettings.h"

namespace ReadingSessionLog {
namespace {
constexpr const char* TAG = "RSLOG";
constexpr const char* DIR = "/.crosspoint";
constexpr const char* LOG_PATH = "/.crosspoint/reading_sessions.jsonl";
constexpr const char* STATE_PATH = "/.crosspoint/reading_sessions_sync.json";
constexpr uint32_t MAX_LOG_BYTES = 512 * 1024;  // beyond this the oldest acknowledged part is dropped

uint32_t loadOffset() {
  const String json = Storage.readFile(STATE_PATH);
  if (json.isEmpty()) return 0;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return 0;
  return doc["offset"] | 0u;
}
}  // namespace

bool append(const Entry& e) {
  readingsessions::Session s;
  s.doc = e.document;
  s.start = e.startEpoch;
  s.secs = e.seconds;
  s.tz = (static_cast<int>(SETTINGS.clockUtcOffsetQ) - 48) * 15;
  s.from = static_cast<uint16_t>(std::clamp(e.startBp, 0, 10000));
  s.to = static_cast<uint16_t>(std::clamp(e.endBp, 0, 10000));
  s.title = e.title;
  s.path = e.path;
  const std::string line = readingsessions::encodeLine(s);
  if (line.empty()) return false;
  Storage.mkdir(DIR);
  HalFile f = Storage.open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND);
  if (!f) {
    LOG_ERR(TAG, "Cannot open %s", LOG_PATH);
    return false;
  }
  const std::string out = line + "\n";
  const bool ok = f.write(reinterpret_cast<const uint8_t*>(out.data()), out.size()) == out.size();
  f.close();
  return ok;
}

Pending readPending(const int64_t now) {
  Pending p;
  uint32_t offset = loadOffset();
  HalFile f;
  if (!Storage.openFileForRead(TAG, LOG_PATH, f)) return p;
  const uint32_t size = static_cast<uint32_t>(f.size());
  if (offset > size) offset = 0;  // the log was rotated or replaced
  p.endOffset = offset;
  if (!f.seek(offset)) {
    f.close();
    return p;
  }
  std::string line;
  uint32_t pos = offset;
  while (pos < size) {
    const int c = f.read();
    if (c < 0) break;
    ++pos;
    if (c != '\n') {
      if (line.size() <= readingsessions::MAX_LINE) line.push_back(static_cast<char>(c));
      continue;
    }
    readingsessions::Session s;
    if (line.size() <= readingsessions::MAX_LINE && readingsessions::decodeLine(line, s)) {
      // A session from a wrong clock (future) stays handled-but-unsent only once the clock is trusted.
      if (readingsessions::uploadable(s, now)) {
        if (p.sessions.size() >= readingsessions::BATCH_SIZE) {
          p.more = true;
          break;
        }
        p.sessions.push_back(std::move(s));
      }
    }
    line.clear();
    p.endOffset = pos;
  }
  f.close();
  return p;
}

bool markUploaded(const uint32_t offset) {
  JsonDocument doc;
  doc["offset"] = offset;
  std::string out;
  serializeJson(doc, out);
  Storage.mkdir(DIR);
  const std::string tmp = std::string(STATE_PATH) + ".tmp";
  if (!Storage.writeFile(tmp.c_str(), String(out.c_str()))) return false;
  Storage.remove(STATE_PATH);
  if (!Storage.rename(tmp.c_str(), STATE_PATH)) return false;

  // Keep the log bounded: once everything is acknowledged and the file is large, start over.
  HalFile f;
  if (Storage.openFileForRead(TAG, LOG_PATH, f)) {
    const uint32_t size = static_cast<uint32_t>(f.size());
    f.close();
    if (size >= MAX_LOG_BYTES && offset >= size) {
      Storage.remove(LOG_PATH);
      JsonDocument reset;
      reset["offset"] = 0;
      std::string r;
      serializeJson(reset, r);
      Storage.writeFile(STATE_PATH, String(r.c_str()));
    }
  }
  return true;
}

}  // namespace ReadingSessionLog
