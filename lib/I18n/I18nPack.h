#pragma once

#include <cstddef>
#include <cstdint>

// UI language pack stored on the SD card: /.inkmod/languages/<code>.lang
// Built by scripts/make_language_pack.py from a translation yaml, against the exact
// key order of the firmware it is meant for (checked through I18N_KEY_HASH).
//
//   0   "ILNG"
//   4   u8  version (1)
//   5   u8  reserved
//   6   u16 string count          (must equal StrId::_COUNT)
//   8   u32 key hash              (must equal I18N_KEY_HASH)
//   12  u16 name length N         (display name, UTF-8, no terminator)
//   14  u16 reserved
//   16  name bytes
//   ..  u32 offsets[count]        (into the data blob; 0xFFFFFFFF = use English)
//   ..  data blob                 (NUL-terminated UTF-8 strings, last byte is NUL)
// All integers little-endian.
namespace I18nPack {

constexpr size_t kHeaderSize = 16;
constexpr uint32_t kMissing = 0xFFFFFFFFu;
constexpr size_t kMaxFileSize = 96 * 1024;
constexpr size_t kMaxNameLength = 40;

struct Parsed {
  const char* name = nullptr;  // not terminated; use nameLength
  size_t nameLength = 0;
  const uint8_t* offsets = nullptr;  // count little-endian u32 values
  const char* data = nullptr;
  size_t dataSize = 0;
  size_t count = 0;
};

inline uint32_t readU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
inline uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

// Validates the pack. `expectedCount` / `expectedHash` come from the firmware (StrId::_COUNT, I18N_KEY_HASH).
// Every offset is checked, so string lookups afterwards cannot leave the buffer.
inline bool parse(const uint8_t* buf, const size_t len, const size_t expectedCount, const uint32_t expectedHash,
                  Parsed& out) {
  if (!buf || len < kHeaderSize + 1) return false;
  if (buf[0] != 'I' || buf[1] != 'L' || buf[2] != 'N' || buf[3] != 'G' || buf[4] != 1) return false;
  const size_t count = readU16(buf + 6);
  if (count != expectedCount || readU32(buf + 8) != expectedHash) return false;
  const size_t nameLen = readU16(buf + 12);
  if (nameLen == 0 || nameLen > kMaxNameLength) return false;
  const size_t offsetsStart = kHeaderSize + nameLen;
  const size_t dataStart = offsetsStart + count * 4;
  if (dataStart >= len) return false;
  const size_t dataSize = len - dataStart;
  const char* data = reinterpret_cast<const char*>(buf + dataStart);
  if (data[dataSize - 1] != '\0') return false;
  for (size_t i = 0; i < count; ++i) {
    const uint32_t off = readU32(buf + offsetsStart + i * 4);
    if (off != kMissing && off >= dataSize) return false;
  }
  out.name = reinterpret_cast<const char*>(buf + kHeaderSize);
  out.nameLength = nameLen;
  out.offsets = buf + offsetsStart;
  out.data = data;
  out.dataSize = dataSize;
  out.count = count;
  return true;
}

// Returns the translated string, or nullptr when the pack has none (caller falls back to English).
inline const char* get(const Parsed& p, const size_t index) {
  if (index >= p.count) return nullptr;
  const uint32_t off = readU32(p.offsets + index * 4);
  return off == kMissing ? nullptr : p.data + off;
}

}  // namespace I18nPack
