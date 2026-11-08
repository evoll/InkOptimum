// HuffCdic.h - HUFF/CDIC decompression (compression type 0x4448).
//
// The code tables (HUFF record) are small and live in RAM. The phrase
// dictionary (CDIC records, easily 100-300 KiB) is NOT loaded: phrases are read
// from the book on demand through a tiny block cache, which keeps the memory use
// flat on devices without PSRAM at the price of more SD reads.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "RecordSource.h"

namespace mobi {

class HuffCdic {
 public:
  // `huffRecord` is the absolute index of the HUFF record; `count` the number of
  // records in the set (HUFF + CDICs), as stored in the MOBI header.
  bool load(RecordSource& src, uint32_t huffRecord, uint32_t count);

  // Appends the decoded record to `out`; false on corrupt data or when more than
  // `maxOut` bytes would be produced.
  bool unpack(const uint8_t* data, size_t len, std::string& out, size_t maxOut);

 private:
  struct Line {
    uint32_t record = 0xFFFFFFFFu;
    uint32_t block = 0;
    uint16_t length = 0;
    uint8_t data[256];
  };

  bool readBytes(uint32_t record, uint32_t offset, uint8_t* dst, size_t n);
  bool phrase(uint32_t index, std::string& bytes, bool& literal);
  bool decode(const uint8_t* data, size_t len, std::string& out, size_t maxOut, int depth);

  RecordSource* src_ = nullptr;
  uint32_t firstCdic_ = 0;
  uint32_t cdicCount_ = 0;
  uint32_t phraseCount_ = 0;
  uint32_t bits_ = 0;

  struct Dict1 {
    uint8_t codeLen;
    bool terminal;
    uint64_t maxCode;
  };
  Dict1 dict1_[256];
  uint64_t minCode_[33];
  uint64_t maxCode_[33];
  Line cache_[4];
};

}  // namespace mobi
