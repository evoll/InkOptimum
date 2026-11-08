// MobiIndex.h - reader for the INDX/TAGX/CTOC index structures used by the
// NCX (table of contents) and, in KF8, by the skeleton and fragment tables.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "RecordSource.h"

namespace mobi {

struct IndexEntry {
  std::string text;  // the entry key (a name, or decimal digits for fragments)
  std::vector<std::pair<uint8_t, std::vector<uint32_t>>> tags;

  const std::vector<uint32_t>* find(uint8_t tag) const {
    for (const auto& t : tags) {
      if (t.first == tag) return &t.second;
    }
    return nullptr;
  }
};

class IndexReader {
 public:
  using EntryFn = std::function<bool(const IndexEntry&)>;  // return false to stop

  explicit IndexReader(RecordSource& src) : src_(src) {}

  // Walks every entry of the index whose first (INDX) record is `indexRecord`.
  bool read(uint32_t indexRecord, const EntryFn& fn);

  // Label stored in the CTOC records; valid inside the EntryFn callback and
  // afterwards. `offset` is the value of a tag such as NCX tag 3.
  bool ctocString(uint32_t offset, std::string& out);

 private:
  RecordSource& src_;
  uint32_t ctocFirst_ = 0;
  uint32_t ctocCount_ = 0;
  uint32_t cachedCtoc_ = 0xFFFFFFFFu;
  std::string ctocData_;
};

}  // namespace mobi
