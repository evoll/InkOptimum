#pragma once

#include <cstddef>
#include <cstdint>

#include "SerializedHyphenationTrie.h"

// Hyphenation pack stored on the SD card: /.inkmod/hyphenation/hyph-<primary tag>.trie
//
//   bytes 0..3   magic "IHYP"
//   byte  4      script: 0 = Latin, 1 = Cyrillic
//   byte  5      minimum prefix (letters kept before a break), 0 = default
//   byte  6      minimum suffix, 0 = default
//   byte  7      reserved (0)
//   bytes 8..    the hypher/Typst `.bin` trie exactly as produced by `scripts/make_hyphenation_pack.py`
//                (4-byte big-endian root address followed by the levels tape and nodes)
//
// The same blob that scripts/generate_hyphenation_trie.py embeds into the built-in
// languages, so any hypher pattern set can be shipped as a pack.
namespace HyphenationPack {

constexpr size_t kHeaderSize = 8;
constexpr size_t kMaxFileSize = 160 * 1024;

struct Parsed {
  uint8_t script = 0;
  uint8_t minPrefix = 0;
  uint8_t minSuffix = 0;
  size_t rootOffset = 0;      // relative to `data`
  const uint8_t* data = nullptr;
  size_t size = 0;
};

// Validates `buf` and fills `out`; `out.data` points into `buf` (which must outlive it).
inline bool parse(const uint8_t* buf, const size_t len, Parsed& out) {
  if (!buf || len < kHeaderSize + 5) return false;
  if (buf[0] != 'I' || buf[1] != 'H' || buf[2] != 'Y' || buf[3] != 'P') return false;
  if (buf[4] > 1) return false;
  const uint8_t* blob = buf + kHeaderSize;
  const size_t blobLen = len - kHeaderSize;
  const size_t root = (static_cast<size_t>(blob[0]) << 24) | (static_cast<size_t>(blob[1]) << 16) |
                      (static_cast<size_t>(blob[2]) << 8) | static_cast<size_t>(blob[3]);
  if (root < 4 || root >= blobLen) return false;
  out.script = buf[4];
  out.minPrefix = buf[5];
  out.minSuffix = buf[6];
  out.data = blob + 4;
  out.size = blobLen - 4;
  out.rootOffset = root - 4;
  return true;
}

// CPHY v1: the hyphenation pack of CrossPoint (PR #3706, "hyphenation manager", Uri Tauber), also read by CrossInk and
// witchhunt-reader: /.crosspoint/hyphenation/hyph-<code>.cphyph. Little-endian, 24-byte header:
//   "CPHY", version 1, language code (2 lowercase letters), min prefix, min suffix, flags 0, reserved 0 (2 bytes),
//   root offset u32, payload size u32, CRC32 (zlib) of the payload; then the payload: the hypher trie WITHOUT its
//   4-byte root prefix. A CPHY pack has no script byte: the script follows from the language (Cyrillic for the
//   Cyrillic languages below, Latin otherwise), and the file is refused unless it is for the language asked for.
constexpr size_t kCphyHeaderSize = 24;
constexpr uint32_t kCphyMaxPayload = 512u * 1024u;

inline uint32_t crc32(const uint8_t* data, const size_t len) {
  static constexpr uint32_t kNibble[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
                                           0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                                           0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    crc = (crc >> 4) ^ kNibble[crc & 0x0F];
    crc = (crc >> 4) ^ kNibble[crc & 0x0F];
  }
  return ~crc;
}

inline bool isCyrillicLanguage(const char* code) {
  static constexpr const char* kCyrillic[] = {"ru", "uk", "bg", "be", "sr", "mk", "mn", "kk", "ky", "tg"};
  for (const char* c : kCyrillic) {
    if (code[0] == c[0] && code[1] == c[1]) return true;
  }
  return false;
}

inline uint32_t readLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

// Validates a whole CPHY file for language `tag` (the caller's primary language tag, lower case) and fills `out`;
// `out.data` points into `buf`.
inline bool parseCphy(const uint8_t* buf, const size_t len, const char* tag, Parsed& out) {
  if (!buf || !tag || tag[0] == '\0' || tag[1] == '\0' || len < kCphyHeaderSize + 1) return false;
  if (buf[0] != 'C' || buf[1] != 'P' || buf[2] != 'H' || buf[3] != 'Y' || buf[4] != 1) return false;
  if (buf[5] != static_cast<uint8_t>(tag[0]) || buf[6] != static_cast<uint8_t>(tag[1])) return false;
  if (tag[2] != '\0' && tag[2] != '-') return false;  // "de" or "de-..." only; a longer tag is another language
  if (buf[9] != 0 || buf[10] != 0 || buf[11] != 0) return false;  // flags, reserved
  const uint32_t rootOffset = readLe32(buf + 12);
  const uint32_t payloadSize = readLe32(buf + 16);
  if (payloadSize == 0 || payloadSize > kCphyMaxPayload || len != kCphyHeaderSize + payloadSize) return false;
  if (rootOffset >= payloadSize) return false;
  if (crc32(buf + kCphyHeaderSize, payloadSize) != readLe32(buf + 20)) return false;
  out.script = isCyrillicLanguage(tag) ? 1 : 0;
  out.minPrefix = buf[7];
  out.minSuffix = buf[8];
  out.rootOffset = rootOffset;
  out.data = buf + kCphyHeaderSize;
  out.size = payloadSize;
  return true;
}

}  // namespace HyphenationPack
