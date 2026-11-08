#include "BookOverrides.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef BOOK_OVERRIDES_NO_STORAGE
#include <HalStorage.h>
#endif

namespace book_overrides {

namespace {
constexpr char kFileName[] = "/overrides.cfg";
}

std::string serialize(const Record& record) {
  if (!record.present) return {};
  char buf[48];
  snprintf(buf, sizeof(buf), "v1 %u %u\n", static_cast<unsigned>(record.values.fontSize),
           static_cast<unsigned>(record.values.lineHeightPercent));
  return buf;
}

bool parse(const char* text, size_t length, Record& out) {
  out = Record{};
  if (!text || length < 6 || length > 40) return false;
  char buf[48];
  memcpy(buf, text, length);
  buf[length] = '\0';
  if (strncmp(buf, "v1 ", 3) != 0) return false;

  char* end = nullptr;
  const long font = strtol(buf + 3, &end, 10);
  if (end == buf + 3 || *end != ' ') return false;
  char* end2 = nullptr;
  const long line = strtol(end + 1, &end2, 10);
  if (end2 == end + 1) return false;
  if (font < 0 || font > 255 || line < 50 || line > 255) return false;

  out.present = true;
  out.values.fontSize = static_cast<uint8_t>(font);
  out.values.lineHeightPercent = static_cast<uint8_t>(line);
  return true;
}

bool Session::apply(Values& live, const Record& record) {
  if (!record.present) return restore(live);
  if (!active_) {
    globals_ = live;
    active_ = true;
  }
  const bool changed = !(live == record.values);
  live = record.values;
  return changed;
}

bool Session::restore(Values& live) {
  if (!active_) return false;
  active_ = false;
  const bool changed = !(live == globals_);
  live = globals_;
  return changed;
}

Session& session() {
  static Session instance;
  return instance;
}

#ifndef BOOK_OVERRIDES_NO_STORAGE
bool load(const std::string& cacheDir, Record& out) {
  out = Record{};
  if (cacheDir.empty()) return false;
  const std::string path = cacheDir + kFileName;
  if (!Storage.exists(path.c_str())) return false;
  FsFile file;
  if (!Storage.openFileForRead("BKO", path.c_str(), file)) return false;
  char buf[48] = {};
  const int n = file.read(buf, sizeof(buf) - 1);
  file.close();
  if (n <= 0) return false;
  return parse(buf, static_cast<size_t>(n), out);
}

bool save(const std::string& cacheDir, const Record& record) {
  if (cacheDir.empty()) return false;
  const std::string path = cacheDir + kFileName;
  if (!record.present) {
    if (Storage.exists(path.c_str())) return Storage.remove(path.c_str());
    return true;
  }
  const std::string text = serialize(record);
  FsFile file;
  if (!Storage.openFileForWrite("BKO", path.c_str(), file)) return false;
  const size_t written = file.write(reinterpret_cast<const uint8_t*>(text.data()), text.size());
  file.close();
  return written == text.size();
}
#endif

}  // namespace book_overrides
