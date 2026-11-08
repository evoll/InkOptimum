#include "MobiIndex.h"

#include <cstring>

#include "MobiFormat.h"

namespace mobi {
namespace {

constexpr size_t kMaxIndexRecord = 128 * 1024;

struct TagDef {
  uint8_t tag, valuesPerEntry, mask, endFlag;
};

bool readVarint(const uint8_t* d, size_t size, size_t& pos, uint32_t& value, size_t& consumed) {
  value = 0;
  consumed = 0;
  while (true) {
    if (pos >= size) return false;
    const uint8_t b = d[pos++];
    ++consumed;
    value = (value << 7) | (b & 0x7F);
    if (b & 0x80) return true;
    if (consumed > 5) return false;
  }
}

int bitCount(uint8_t v) {
  int n = 0;
  for (; v; v >>= 1) n += v & 1;
  return n;
}

// Decodes the tag values of one entry, following the TAGX control bytes.
bool readTagMap(const std::vector<TagDef>& defs, uint32_t controlBytes, const uint8_t* d, size_t start, size_t end,
                std::vector<std::pair<uint8_t, std::vector<uint32_t>>>& out) {
  struct Pending {
    uint8_t tag;
    uint32_t count;  // value count, or byte count when byteMode
    bool byteMode;
    uint8_t perEntry;
  };
  std::vector<Pending> pending;
  size_t controlIndex = 0;
  size_t dataPos = start + controlBytes;
  if (dataPos > end) return false;

  for (const TagDef& def : defs) {
    if (def.endFlag == 0x01) {
      ++controlIndex;
      continue;
    }
    if (start + controlIndex >= end) return false;
    uint8_t mask = def.mask;
    uint32_t value = d[start + controlIndex] & mask;
    if (value == 0) continue;
    if (value == mask) {
      if (bitCount(mask) > 1) {
        uint32_t bytes = 0;
        size_t used = 0;
        if (!readVarint(d, end, dataPos, bytes, used)) return false;
        pending.push_back({def.tag, bytes, true, def.valuesPerEntry});
      } else {
        pending.push_back({def.tag, 1, false, def.valuesPerEntry});
      }
    } else {
      while ((mask & 1) == 0) {
        mask >>= 1;
        value >>= 1;
      }
      pending.push_back({def.tag, value, false, def.valuesPerEntry});
    }
  }

  for (const Pending& p : pending) {
    std::vector<uint32_t> values;
    if (!p.byteMode) {
      const uint64_t total = static_cast<uint64_t>(p.count) * p.perEntry;
      if (total > 4096) return false;
      for (uint64_t i = 0; i < total; ++i) {
        uint32_t v = 0;
        size_t used = 0;
        if (!readVarint(d, end, dataPos, v, used)) return false;
        values.push_back(v);
      }
    } else {
      uint32_t consumed = 0;
      while (consumed < p.count) {
        uint32_t v = 0;
        size_t used = 0;
        if (!readVarint(d, end, dataPos, v, used)) return false;
        consumed += static_cast<uint32_t>(used);
        values.push_back(v);
        if (values.size() > 4096) return false;
      }
    }
    out.emplace_back(p.tag, std::move(values));
  }
  return true;
}

}  // namespace

bool IndexReader::read(uint32_t indexRecord, const EntryFn& fn) {
  std::string main;
  if (!src_.readRecord(indexRecord, main, kMaxIndexRecord)) return false;
  const auto* m = reinterpret_cast<const uint8_t*>(main.data());
  if (main.size() < 56 || memcmp(m, "INDX", 4) != 0) return false;
  const uint32_t headerLen = be32(m + 4);
  const uint32_t count = be32(m + 24);
  const uint32_t nctoc = be32(m + 52);
  if (count > 4096 || nctoc > 64) return false;

  uint32_t controlBytes = 0;
  std::vector<TagDef> defs;
  if (static_cast<uint64_t>(headerLen) + 12 > main.size() || memcmp(m + headerLen, "TAGX", 4) != 0) return false;
  const uint32_t firstEntry = be32(m + headerLen + 4);
  controlBytes = be32(m + headerLen + 8);
  if (controlBytes == 0 || controlBytes > 16 || firstEntry < 12 ||
      static_cast<uint64_t>(headerLen) + firstEntry > main.size()) {
    return false;
  }
  for (uint32_t i = 12; i + 4 <= firstEntry; i += 4) {
    const uint8_t* e = m + headerLen + i;
    defs.push_back({e[0], e[1], e[2], e[3]});
  }

  ctocFirst_ = indexRecord + count + 1;
  ctocCount_ = nctoc;
  cachedCtoc_ = 0xFFFFFFFFu;

  std::string rec;
  for (uint32_t i = 1; i <= count; ++i) {
    if (!src_.readRecord(indexRecord + i, rec, kMaxIndexRecord)) return false;
    const auto* d = reinterpret_cast<const uint8_t*>(rec.data());
    if (rec.size() < 32 || memcmp(d, "INDX", 4) != 0) return false;
    const uint32_t idxt = be32(d + 20);
    const uint32_t entries = be32(d + 24);
    if (static_cast<uint64_t>(idxt) + 4 + 2ull * entries > rec.size() || memcmp(d + idxt, "IDXT", 4) != 0) {
      return false;
    }
    for (uint32_t j = 0; j < entries; ++j) {
      const size_t startPos = be16(d + idxt + 4 + 2 * j);
      const size_t endPos = (j + 1 < entries) ? be16(d + idxt + 4 + 2 * (j + 1)) : idxt;
      if (startPos >= endPos || endPos > rec.size()) return false;
      const size_t textLen = d[startPos];
      if (startPos + 1 + textLen > endPos) return false;
      IndexEntry entry;
      entry.text.assign(reinterpret_cast<const char*>(d + startPos + 1), textLen);
      if (!readTagMap(defs, controlBytes, d, startPos + 1 + textLen, endPos, entry.tags)) return false;
      if (!fn(entry)) return true;
    }
  }
  return true;
}

bool IndexReader::ctocString(uint32_t offset, std::string& out) {
  const uint32_t part = offset >> 16;
  const uint32_t at = offset & 0xFFFF;
  if (part >= ctocCount_) return false;
  if (cachedCtoc_ != part) {
    if (!src_.readRecord(ctocFirst_ + part, ctocData_, kMaxIndexRecord)) return false;
    cachedCtoc_ = part;
  }
  const auto* d = reinterpret_cast<const uint8_t*>(ctocData_.data());
  size_t pos = at;
  uint32_t len = 0;
  size_t used = 0;
  if (!readVarint(d, ctocData_.size(), pos, len, used)) return false;
  if (pos + len > ctocData_.size()) return false;
  out.assign(ctocData_, pos, len);
  return true;
}

}  // namespace mobi
