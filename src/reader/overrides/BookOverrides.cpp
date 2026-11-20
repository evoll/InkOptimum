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
  char buf[64];
  const Values& v = record.values;
  snprintf(buf, sizeof(buf), "v3 %u %u %u %u %u %u %u %u %u %u\n", static_cast<unsigned>(v.fontSize),
           static_cast<unsigned>(v.lineHeightPercent), static_cast<unsigned>(v.screenMargin),
           static_cast<unsigned>(v.paragraphAlignment), static_cast<unsigned>(v.extraParagraphSpacing),
           static_cast<unsigned>(v.hyphenationEnabled), static_cast<unsigned>(v.screenMarginTop),
           static_cast<unsigned>(v.screenMarginBottom), static_cast<unsigned>(v.bionicReadingEnabled),
           static_cast<unsigned>(v.guideReadingEnabled));
  return buf;
}

namespace {
// Reads up to `count` unsigned numbers separated by single spaces. Returns the number read.
int readNumbers(const char* p, long* out, int count) {
  int n = 0;
  while (n < count) {
    char* end = nullptr;
    const long v = strtol(p, &end, 10);
    if (end == p) break;
    out[n++] = v;
    if (*end == ' ') {
      p = end + 1;
    } else {
      p = end;
      break;
    }
  }
  return n;
}
bool inheritOr(const long v, const long max) { return v == kInherit || (v >= 0 && v <= max); }
}  // namespace

bool parse(const char* text, size_t length, Record& out) {
  out = Record{};
  if (!text || length < 6 || length > 60) return false;
  char buf[64];
  memcpy(buf, text, length);
  buf[length] = '\0';

  long n[10] = {};
  int count = 0;
  bool v2 = false;
  bool v3 = false;
  if (strncmp(buf, "v1 ", 3) == 0) {
    count = readNumbers(buf + 3, n, 2);
    if (count != 2) return false;
  } else if (strncmp(buf, "v2 ", 3) == 0) {
    v2 = true;
    count = readNumbers(buf + 3, n, 6);
    if (count != 6) return false;
  } else if (strncmp(buf, "v3 ", 3) == 0) {
    v2 = v3 = true;
    count = readNumbers(buf + 3, n, 10);
    if (count != 10) return false;
  } else {
    return false;
  }
  if (n[0] < 0 || n[0] > 255 || n[1] < 50 || n[1] > 255) return false;
  if (v2 && !(inheritOr(n[2], 60) && inheritOr(n[3], 10) && inheritOr(n[4], 3) && inheritOr(n[5], 1))) return false;
  if (v3 && !(inheritOr(n[6], 60) && inheritOr(n[7], 60) && inheritOr(n[8], 1) && inheritOr(n[9], 1))) return false;

  out.present = true;
  out.values.fontSize = static_cast<uint8_t>(n[0]);
  out.values.lineHeightPercent = static_cast<uint8_t>(n[1]);
  if (v2) {
    out.values.screenMargin = static_cast<uint8_t>(n[2]);
    out.values.paragraphAlignment = static_cast<uint8_t>(n[3]);
    out.values.extraParagraphSpacing = static_cast<uint8_t>(n[4]);
    out.values.hyphenationEnabled = static_cast<uint8_t>(n[5]);
  }
  if (v3) {
    out.values.screenMarginTop = static_cast<uint8_t>(n[6]);
    out.values.screenMarginBottom = static_cast<uint8_t>(n[7]);
    out.values.bionicReadingEnabled = static_cast<uint8_t>(n[8]);
    out.values.guideReadingEnabled = static_cast<uint8_t>(n[9]);
  }
  return true;
}

bool Session::apply(Values& live, const Record& record) {
  if (!record.present) return restore(live);
  if (!active_) {
    globals_ = live;
    active_ = true;
  }
  // Fields left at kInherit follow the remembered globals.
  Values resolved = record.values;
  auto resolve = [](uint8_t& field, const uint8_t global) {
    if (field == kInherit) field = global;
  };
  resolve(resolved.fontSize, globals_.fontSize);
  resolve(resolved.lineHeightPercent, globals_.lineHeightPercent);
  resolve(resolved.screenMargin, globals_.screenMargin);
  resolve(resolved.paragraphAlignment, globals_.paragraphAlignment);
  resolve(resolved.extraParagraphSpacing, globals_.extraParagraphSpacing);
  resolve(resolved.hyphenationEnabled, globals_.hyphenationEnabled);
  resolve(resolved.screenMarginTop, globals_.screenMarginTop);
  resolve(resolved.screenMarginBottom, globals_.screenMarginBottom);
  resolve(resolved.bionicReadingEnabled, globals_.bionicReadingEnabled);
  resolve(resolved.guideReadingEnabled, globals_.guideReadingEnabled);
  const bool changed = !(live == resolved);
  live = resolved;
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
  char buf[64] = {};
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
