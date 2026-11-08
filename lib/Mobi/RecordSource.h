// RecordSource.h - random access to the records of a Palm database.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace mobi {

class RecordSource {
 public:
  virtual ~RecordSource() = default;

  virtual uint32_t recordCount() const = 0;
  // Size of record `index` in bytes; false when the index or table is invalid.
  virtual bool recordSize(uint32_t index, uint32_t& size) = 0;
  // Reads the whole record (at most maxSize bytes, otherwise fails).
  virtual bool readRecord(uint32_t index, std::string& out, size_t maxSize) = 0;
  // Reads len bytes at `offset` inside the record; fails when out of range.
  virtual bool readRecordPart(uint32_t index, uint32_t offset, void* buf, size_t len) = 0;
};

}  // namespace mobi
