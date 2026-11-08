#include "HuffCdic.h"

#include <algorithm>
#include <cstring>

#include "MobiFormat.h"

namespace mobi {

bool HuffCdic::load(RecordSource& src, uint32_t huffRecord, uint32_t count) {
  src_ = &src;
  for (Line& l : cache_) l = Line();
  if (count < 2 || huffRecord >= src.recordCount()) return false;

  std::string huff;
  if (!src.readRecord(huffRecord, huff, 4096)) return false;
  const auto* h = reinterpret_cast<const uint8_t*>(huff.data());
  if (huff.size() < 16 || memcmp(h, "HUFF\x00\x00\x00\x18", 8) != 0) return false;
  const uint32_t off1 = be32(h + 8);
  const uint32_t off2 = be32(h + 12);
  if (static_cast<uint64_t>(off1) + 256 * 4 > huff.size() || static_cast<uint64_t>(off2) + 64 * 4 > huff.size()) {
    return false;
  }

  for (int i = 0; i < 256; ++i) {
    const uint32_t v = be32(h + off1 + 4 * i);
    const uint8_t codeLen = static_cast<uint8_t>(v & 0x1F);
    const bool terminal = (v & 0x80) != 0;
    if (codeLen == 0 || (codeLen <= 8 && !terminal)) return false;
    dict1_[i].codeLen = codeLen;
    dict1_[i].terminal = terminal;
    dict1_[i].maxCode = (((static_cast<uint64_t>(v >> 8)) + 1) << (32 - codeLen)) - 1;
  }
  minCode_[0] = maxCode_[0] = 0;
  for (int len = 1; len <= 32; ++len) {
    const uint32_t mn = be32(h + off2 + 8 * (len - 1));
    const uint32_t mx = be32(h + off2 + 8 * (len - 1) + 4);
    minCode_[len] = static_cast<uint64_t>(mn) << (32 - len);
    maxCode_[len] = ((static_cast<uint64_t>(mx) + 1) << (32 - len)) - 1;
  }

  // The first CDIC carries the totals.
  firstCdic_ = huffRecord + 1;
  cdicCount_ = count - 1;
  uint8_t head[16];
  if (!src.readRecordPart(firstCdic_, 0, head, sizeof(head))) return false;
  if (memcmp(head, "CDIC\x00\x00\x00\x10", 8) != 0) return false;
  phraseCount_ = be32(head + 8);
  bits_ = be32(head + 12);
  if (bits_ == 0 || bits_ > 16) return false;
  return true;
}

bool HuffCdic::readBytes(uint32_t record, uint32_t offset, uint8_t* dst, size_t n) {
  while (n > 0) {
    const uint32_t block = offset >> 8;
    Line& line = cache_[(block + record) & 3];
    if (line.record != record || line.block != block) {
      uint32_t size = 0;
      if (!src_->recordSize(record, size)) return false;
      const uint32_t start = block << 8;
      if (start >= size) return false;
      const uint32_t want = std::min<uint32_t>(256, size - start);
      if (!src_->readRecordPart(record, start, line.data, want)) {
        line.record = 0xFFFFFFFFu;
        return false;
      }
      line.record = record;
      line.block = block;
      line.length = static_cast<uint16_t>(want);
    }
    const uint32_t inBlock = offset & 255;
    if (inBlock >= line.length) return false;
    const size_t take = std::min<size_t>(n, line.length - inBlock);
    memcpy(dst, line.data + inBlock, take);
    dst += take;
    offset += static_cast<uint32_t>(take);
    n -= take;
  }
  return true;
}

bool HuffCdic::phrase(uint32_t index, std::string& bytes, bool& literal) {
  if (index >= phraseCount_) return false;
  const uint32_t cdic = index >> bits_;
  const uint32_t entry = index & ((1u << bits_) - 1);
  if (cdic >= cdicCount_) return false;
  const uint32_t record = firstCdic_ + cdic;
  uint8_t buf[2];
  if (!readBytes(record, 16 + 2 * entry, buf, 2)) return false;
  const uint32_t off = be16(buf);
  if (!readBytes(record, 16 + off, buf, 2)) return false;
  const uint16_t blen = be16(buf);
  literal = (blen & 0x8000) != 0;
  const size_t len = blen & 0x7FFF;
  bytes.resize(len);
  return len == 0 || readBytes(record, 18 + off, reinterpret_cast<uint8_t*>(&bytes[0]), len);
}

bool HuffCdic::decode(const uint8_t* data, size_t len, std::string& out, size_t maxOut, int depth) {
  if (depth > 12) return false;
  const size_t base = out.size();
  int64_t bitsLeft = static_cast<int64_t>(len) * 8;
  size_t pos = 0;

  auto word64 = [&](size_t at) {
    uint64_t v = 0;
    for (size_t i = 0; i < 8; ++i) v = (v << 8) | (at + i < len ? data[at + i] : 0);
    return v;
  };
  uint64_t x = word64(0);
  int n = 32;

  std::string tmp;
  while (true) {
    if (n <= 0) {
      pos += 4;
      x = word64(pos);
      n += 32;
    }
    const uint32_t code = static_cast<uint32_t>((x >> n) & 0xFFFFFFFFu);
    const Dict1& d = dict1_[code >> 24];
    int codeLen = d.codeLen;
    uint64_t maxCode = d.maxCode;
    if (!d.terminal) {
      while (codeLen <= 32 && code < minCode_[codeLen]) ++codeLen;
      if (codeLen > 32) return false;
      maxCode = maxCode_[codeLen];
    }
    n -= codeLen;
    bitsLeft -= codeLen;
    if (bitsLeft < 0) break;
    if (maxCode < code) return false;
    const uint32_t index = static_cast<uint32_t>((maxCode - code) >> (32 - codeLen));
    bool literal = false;
    if (!phrase(index, tmp, literal)) return false;
    if (literal) {
      if (out.size() - base + tmp.size() > maxOut) return false;
      out.append(tmp);
    } else {
      std::string inner = tmp;  // tmp is reused by the recursive lookups
      if (!decode(reinterpret_cast<const uint8_t*>(inner.data()), inner.size(), out, maxOut, depth + 1)) return false;
    }
  }
  return true;
}

bool HuffCdic::unpack(const uint8_t* data, size_t len, std::string& out, size_t maxOut) {
  if (!src_) return false;
  return decode(data, len, out, maxOut, 0);
}

}  // namespace mobi
