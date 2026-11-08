// MobiTestBuilder.h - writes synthetic Mobipocket / KF8 files for the tests.
//
// The layouts follow the public MOBI documentation and the KindleUnpack source
// (which was used as the format reference). They are synthetic: no real
// Kindle-produced file is involved, so these tests prove the code agrees with the
// documented format, not that every real-world book converts.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mobitest {

inline void be16(std::string& s, uint32_t v) {
  s.push_back(static_cast<char>(v >> 8));
  s.push_back(static_cast<char>(v));
}
inline void be32(std::string& s, uint32_t v) {
  for (int i = 3; i >= 0; --i) s.push_back(static_cast<char>(v >> (8 * i)));
}
inline void set16(std::string& s, size_t off, uint32_t v) {
  s[off] = static_cast<char>(v >> 8);
  s[off + 1] = static_cast<char>(v);
}
inline void set32(std::string& s, size_t off, uint32_t v) {
  for (int i = 0; i < 4; ++i) s[off + i] = static_cast<char>(v >> (8 * (3 - i)));
}

// PalmDOC compression with literals, space+char pairs and back-references.
inline std::string palmCompress(const std::string& in) {
  std::string out;
  size_t i = 0;
  while (i < in.size()) {
    const uint8_t c = static_cast<uint8_t>(in[i]);
    // Back-reference: longest match (3..10) within the previous 2047 bytes.
    size_t bestLen = 0, bestDist = 0;
    const size_t from = i > 2047 ? i - 2047 : 0;
    for (size_t j = from; j < i; ++j) {
      size_t n = 0;
      while (n < 10 && i + n < in.size() && in[j + n] == in[i + n]) ++n;
      if (n >= 3 && n > bestLen) {
        bestLen = n;
        bestDist = i - j;
      }
    }
    if (bestLen >= 3) {
      const uint32_t pair = 0x8000 | (static_cast<uint32_t>(bestDist) << 3) | static_cast<uint32_t>(bestLen - 3);
      out.push_back(static_cast<char>(pair >> 8));
      out.push_back(static_cast<char>(pair & 0xFF));
      i += bestLen;
    } else if (c == ' ' && i + 1 < in.size() && static_cast<uint8_t>(in[i + 1]) >= 0x40 &&
               static_cast<uint8_t>(in[i + 1]) < 0x80) {
      out.push_back(static_cast<char>(static_cast<uint8_t>(in[i + 1]) ^ 0x80));
      i += 2;
    } else if ((c >= 9 && c < 0x80) ) {
      out.push_back(static_cast<char>(c));
      ++i;
    } else {
      size_t n = 1;
      while (n < 8 && i + n < in.size()) {
        const uint8_t d = static_cast<uint8_t>(in[i + n]);
        if (d >= 9 && d < 0x80) break;
        ++n;
      }
      out.push_back(static_cast<char>(n));
      out.append(in, i, n);
      i += n;
    }
  }
  return out;
}

struct Pdb {
  std::string bytes;
  std::vector<uint32_t> offsets;  // start of each record in `bytes`
};

inline Pdb makePdb(const std::string& name, const std::vector<std::string>& records, const char* ident = "BOOKMOBI") {
  Pdb p;
  std::string& s = p.bytes;
  std::string nm = name.substr(0, 31);
  nm.resize(32, '\0');
  s += nm;
  be16(s, 0);
  be16(s, 0);
  for (int i = 0; i < 6; ++i) be32(s, 0);  // dates, modification number, appinfo, sortinfo
  s += std::string(ident, 8);
  be32(s, 0);
  be32(s, 0);
  be16(s, static_cast<uint32_t>(records.size()));
  const size_t tableStart = s.size();
  s.resize(tableStart + 8 * records.size() + 2, '\0');
  for (size_t i = 0; i < records.size(); ++i) {
    const uint32_t off = static_cast<uint32_t>(s.size());
    p.offsets.push_back(off);
    set32(s, tableStart + 8 * i, off);
    s += records[i];
  }
  return p;
}

struct Rec0 {
  uint32_t compression = 2;
  uint32_t textLength = 0;
  uint32_t textRecords = 0;
  uint32_t crypto = 0;
  uint32_t codepage = 1252;
  uint32_t version = 6;
  uint32_t minVersion = 6;
  uint32_t firstImage = 0xFFFFFFFFu;
  uint32_t huff = 0xFFFFFFFFu;
  uint32_t huffCount = 0;
  uint32_t trailFlags = 0;
  uint32_t ncx = 0xFFFFFFFFu;
  uint32_t frag = 0xFFFFFFFFu;
  uint32_t skel = 0xFFFFFFFFu;
  uint32_t fdst = 0xFFFFFFFFu;
  uint32_t fdstCount = 0;
  uint32_t lang = 9;
  std::string title = "Test Book";
  std::vector<std::pair<uint32_t, std::string>> exth;
};

inline std::string makeRecord0(const Rec0& o) {
  const uint32_t headerLen = 0xF8;
  std::string r(16 + headerLen, '\0');
  set16(r, 0, o.compression);
  set32(r, 4, o.textLength);
  set16(r, 8, o.textRecords);
  set16(r, 10, 4096);
  set16(r, 12, o.crypto);
  r.replace(16, 4, "MOBI");
  set32(r, 0x14, headerLen);
  set32(r, 0x1C, o.codepage);
  set32(r, 0x24, o.version);
  set32(r, 0x5C, o.lang);
  set32(r, 0x68, o.minVersion);
  set32(r, 0x6C, o.firstImage);
  set32(r, 0x70, o.huff);
  set32(r, 0x74, o.huffCount);
  set32(r, 0x80, o.exth.empty() ? 0 : 0x40);
  set32(r, 0xC0, o.fdst);
  set32(r, 0xC4, o.fdstCount);
  set16(r, 0xF2, o.trailFlags);
  set32(r, 0xF4, o.ncx);
  set32(r, 0xF8, o.frag);
  set32(r, 0xFC, o.skel);

  if (!o.exth.empty()) {
    std::string e = "EXTH";
    std::string items;
    for (const auto& [id, data] : o.exth) {
      be32(items, id);
      be32(items, static_cast<uint32_t>(8 + data.size()));
      items += data;
    }
    be32(e, static_cast<uint32_t>(12 + items.size()));
    be32(e, static_cast<uint32_t>(o.exth.size()));
    e += items;
    while (e.size() % 4) e.push_back('\0');
    r += e;
  }
  set32(r, 0x54, static_cast<uint32_t>(r.size()));
  set32(r, 0x58, static_cast<uint32_t>(o.title.size()));
  r += o.title;
  r += std::string(4, '\0');
  return r;
}

// Splits `raw` into 4096-byte records, compressing each. `trail` and
// `multibyte` append synthetic trailing entries (set Rec0::trailFlags = 3).
inline std::vector<std::string> makeTextRecords(const std::string& raw, bool compress, bool trailing = false) {
  std::vector<std::string> out;
  for (size_t i = 0; i < raw.size() || out.empty(); i += 4096) {
    std::string chunk = raw.substr(i, 4096);
    std::string rec = compress ? palmCompress(chunk) : chunk;
    if (trailing) {
      rec += "\x41\x01";            // multibyte overlap: (0x01 & 3) + 1 = 2 bytes
      rec += std::string("xyz\x84");  // one trailing entry: data + size varint (4)
    }
    out.push_back(rec);
    if (raw.empty()) break;
  }
  return out;
}

// ---- INDX ------------------------------------------------------------------

struct TagDef {
  uint8_t tag, perEntry, mask, end;
};
struct IndexEntryB {
  std::string text;
  std::vector<std::pair<uint8_t, std::vector<uint32_t>>> values;  // tag -> values
};

inline std::string varintFwd(uint32_t v) {  // MSB-first groups, last byte has bit 7 set
  std::string groups;
  groups.push_back(static_cast<char>(v & 0x7F));
  v >>= 7;
  while (v) {
    groups.insert(groups.begin(), static_cast<char>(v & 0x7F));
    v >>= 7;
  }
  groups.back() = static_cast<char>(groups.back() | 0x80);
  return groups;
}

// Returns the INDX records: [main, data..., ctoc...]. `ctoc` is the CTOC payload
// (already encoded); `ctocCount` is how many CTOC records follow.
inline std::vector<std::string> makeIndex(const std::vector<TagDef>& defs, const std::vector<IndexEntryB>& entries,
                                          const std::string& ctoc) {
  std::string main(192, '\0');
  main.replace(0, 4, "INDX");
  set32(main, 4, 192);
  set32(main, 24, 1);                    // one data record
  set32(main, 28, 65001);
  set32(main, 52, ctoc.empty() ? 0 : 1);  // CTOC records
  main += "TAGX";
  be32(main, static_cast<uint32_t>(12 + 4 * defs.size()));
  be32(main, 1);  // one control byte
  for (const TagDef& d : defs) {
    main.push_back(static_cast<char>(d.tag));
    main.push_back(static_cast<char>(d.perEntry));
    main.push_back(static_cast<char>(d.mask));
    main.push_back(static_cast<char>(d.end));
  }

  std::string data(192, '\0');
  data.replace(0, 4, "INDX");
  set32(data, 4, 192);
  std::vector<uint32_t> starts;
  for (const IndexEntryB& e : entries) {
    starts.push_back(static_cast<uint32_t>(data.size()));
    data.push_back(static_cast<char>(e.text.size()));
    data += e.text;
    uint8_t control = 0;
    std::string payload;
    for (const TagDef& d : defs) {
      if (d.end) continue;
      for (const auto& [tag, values] : e.values) {
        if (tag != d.tag) continue;
        const uint32_t groups = static_cast<uint32_t>(values.size() / d.perEntry);
        uint32_t shift = 0;
        while (((d.mask >> shift) & 1) == 0) ++shift;
        control |= static_cast<uint8_t>(groups << shift);
        for (const uint32_t v : values) payload += varintFwd(v);
      }
    }
    data.push_back(static_cast<char>(control));
    data += payload;
  }
  const uint32_t idxt = static_cast<uint32_t>(data.size());
  data += "IDXT";
  for (const uint32_t s : starts) be16(data, s);
  set32(data, 20, idxt);
  set32(data, 24, static_cast<uint32_t>(entries.size()));

  std::vector<std::string> out = {main, data};
  if (!ctoc.empty()) out.push_back(ctoc);
  return out;
}

inline std::string ctocEntry(const std::string& label, uint32_t* offsetOut, const std::string& soFar) {
  *offsetOut = static_cast<uint32_t>(soFar.size());
  std::string e(1, static_cast<char>(label.size() | 0x80));  // labels < 128 bytes: one varint byte
  return soFar + e + label;
}

}  // namespace mobitest
