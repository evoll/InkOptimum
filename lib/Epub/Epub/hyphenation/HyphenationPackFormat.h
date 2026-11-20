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

}  // namespace HyphenationPack
