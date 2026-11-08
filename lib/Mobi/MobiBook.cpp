#include "MobiBook.h"

#include <Codepage.h>

#include <algorithm>
#include <cstring>

#include "MobiFormat.h"
#include "PalmDoc.h"

namespace mobi {
namespace {

constexpr size_t kMaxHeaderRecord = 256 * 1024;
constexpr size_t kMaxTextRecord = 64 * 1024;
constexpr uint64_t kMaxTextTotal = 64ull * 1024 * 1024;
constexpr uint32_t kNone = 0xFFFFFFFFu;

void convertText(uint32_t codepage, std::string& out, const char* s, size_t n) {
  if (codepage == 65001) {
    out.append(s, n);
    return;
  }
  const int page = fictionbook::isSupportedCodepage(static_cast<int>(codepage)) ? static_cast<int>(codepage) : 1252;
  for (size_t i = 0; i < n; ++i) {
    const uint8_t c = static_cast<uint8_t>(s[i]);
    if (c < 0x80) {
      out.push_back(static_cast<char>(c));
    } else {
      fictionbook::appendUtf8CodePoint(out, fictionbook::codepageToUnicode(page, c));
    }
  }
}

const char* languageFromId(uint32_t code) {
  switch (code & 0xFF) {
    case 0x01: return "ar";
    case 0x02: return "bg";
    case 0x05: return "cs";
    case 0x06: return "da";
    case 0x07: return "de";
    case 0x08: return "el";
    case 0x09: return "en";
    case 0x0A: return "es";
    case 0x0B: return "fi";
    case 0x0C: return "fr";
    case 0x0D: return "he";
    case 0x0E: return "hu";
    case 0x10: return "it";
    case 0x11: return "ja";
    case 0x12: return "ko";
    case 0x13: return "nl";
    case 0x14: return "no";
    case 0x15: return "pl";
    case 0x16: return "pt";
    case 0x18: return "ro";
    case 0x19: return "ru";
    case 0x1A: return "hr";
    case 0x1B: return "sk";
    case 0x1D: return "sv";
    case 0x1E: return "th";
    case 0x1F: return "tr";
    case 0x22: return "uk";
    case 0x23: return "be";
    case 0x25: return "et";
    case 0x26: return "lv";
    case 0x27: return "lt";
    case 0x04: return "zh";
    default: return "";
  }
}

}  // namespace

// ---- RecordSource ----------------------------------------------------------

bool Book::recordOffset(uint32_t index, uint32_t& offset) {
  if (index >= numRecords_) return false;
  uint8_t b[4];
  if (!file_->seek(78 + 8ull * index) || file_->read(b, 4) != 4) return false;
  offset = be32(b);
  return true;
}

bool Book::recordSize(uint32_t index, uint32_t& size) {
  uint32_t start = 0, end = 0;
  if (!recordOffset(index, start)) return false;
  if (index + 1 < numRecords_) {
    if (!recordOffset(index + 1, end)) return false;
  } else {
    end = static_cast<uint32_t>(std::min<uint64_t>(fileSize_, 0xFFFFFFFFu));
  }
  if (end < start || end > fileSize_) return false;
  size = end - start;
  return true;
}

bool Book::readRecord(uint32_t index, std::string& out, size_t maxSize) {
  uint32_t start = 0, size = 0;
  if (!recordOffset(index, start) || !recordSize(index, size) || size > maxSize) return false;
  out.resize(size);
  if (size == 0) return true;
  return file_->seek(start) && file_->read(&out[0], size) == size;
}

bool Book::readRecordPart(uint32_t index, uint32_t offset, void* buf, size_t len) {
  uint32_t start = 0, size = 0;
  if (!recordOffset(index, start) || !recordSize(index, size)) return false;
  if (static_cast<uint64_t>(offset) + len > size) return false;
  return file_->seek(static_cast<uint64_t>(start) + offset) && file_->read(buf, len) == len;
}

void Book::appendText(std::string& out, const char* s, size_t n) const { convertText(header_.codepage, out, s, n); }

bool Book::imageRange(uint32_t number, uint64_t& start, uint64_t& end, const char*& mime) {
  if (number == 0 || header_.firstImage == kNone) return false;
  const uint32_t rec = header_.firstImage + number - 1;
  uint32_t off = 0, size = 0;
  if (rec >= numRecords_ || !recordOffset(rec, off) || !recordSize(rec, size) || size < 8) return false;
  uint8_t magic[8];
  if (!readRecordPart(rec, 0, magic, sizeof(magic))) return false;
  if (magic[0] == 0xFF && magic[1] == 0xD8 && magic[2] == 0xFF) {
    mime = "image/jpeg";
  } else if (memcmp(magic, "\x89PNG", 4) == 0) {
    mime = "image/png";
  } else if (memcmp(magic, "GIF8", 4) == 0) {
    mime = "image/gif";
  } else {
    return false;
  }
  start = off;
  end = static_cast<uint64_t>(off) + size;
  return true;
}

// ---- headers ---------------------------------------------------------------

bool Book::parseHeader(uint32_t startRecord, Header& h, Metadata& md) {
  std::string rec;
  if (!readRecord(startRecord, rec, kMaxHeaderRecord) || rec.size() < 16) return false;
  const auto* d = reinterpret_cast<const uint8_t*>(rec.data());
  h = Header();
  h.start = startRecord;
  h.compression = be16(d);
  h.textLength = be32(d + 4);
  h.textRecords = be16(d + 8);
  h.crypto = be16(d + 12);
  if (rec.size() < 24 || memcmp(d + 16, "MOBI", 4) != 0) return true;  // plain PalmDOC

  h.mobi = true;
  h.headerLen = be32(d + 20);
  const size_t limit = std::min<size_t>(rec.size(), static_cast<size_t>(h.headerLen) + 16);
  auto f32 = [&](size_t off, uint32_t def) { return off + 4 <= limit ? be32(d + off) : def; };

  h.codepage = f32(0x1C, 1252);
  h.version = f32(0x24, 0);
  h.langCode = f32(0x5C, 0);
  h.minVersion = f32(0x68, 0);
  const bool k8 = startRecord != 0 || h.version == 8;

  const uint32_t firstImage = f32(0x6C, kNone);
  // In combined MOBI7+KF8 files the shared resources sit in the MOBI7 half, before the BOUNDARY
  // record, and the KF8 header addresses them absolutely (seen in real Calibre output: KF8 header
  // at record 253, first image 242). A value below the KF8 start therefore cannot be relative.
  const bool absoluteImages = startRecord != 0 && firstImage != kNone && firstImage < startRecord;
  h.firstImage = firstImage == kNone ? startRecord + h.textRecords + 1
                                     : (absoluteImages ? firstImage : firstImage + startRecord);
  const uint32_t huff = f32(0x70, kNone);
  h.huffRecord = huff != kNone ? huff + startRecord : kNone;
  h.huffCount = f32(0x74, 0);
  if (h.headerLen >= 0xE4 && h.minVersion >= 5 && 0xF2 + 2 <= limit) h.trailFlags = be16(d + 0xF2);
  const uint32_t ncx = f32(0xF4, kNone);
  h.ncx = ncx != kNone ? ncx + startRecord : kNone;
  if (k8) {
    const uint32_t frag = f32(0xF8, kNone);
    const uint32_t skel = f32(0xFC, kNone);
    h.frag = frag != kNone ? frag + startRecord : kNone;
    h.skel = skel != kNone ? skel + startRecord : kNone;
    const uint32_t fdst = f32(0xC0, kNone);
    h.fdstCount = f32(0xC4, 0);
    h.fdst = (h.fdstCount > 1 && fdst != kNone) ? fdst + startRecord : kNone;
  }

  // Title.
  const uint32_t titleOff = f32(0x54, 0);
  const uint32_t titleLen = f32(0x58, 0);
  if (titleLen > 0 && static_cast<uint64_t>(titleOff) + titleLen <= rec.size()) {
    md.title.clear();
    convertText(h.codepage, md.title, rec.data() + titleOff, titleLen);
  }

  // EXTH metadata.
  const uint32_t exthFlags = f32(0x80, 0);
  const size_t exth = static_cast<size_t>(h.headerLen) + 16;
  if ((exthFlags & 0x40) && exth + 12 <= rec.size() && memcmp(d + exth, "EXTH", 4) == 0) {
    const uint32_t count = be32(d + exth + 8);
    size_t pos = exth + 12;
    std::string updatedTitle;
    for (uint32_t i = 0; i < count && pos + 8 <= rec.size(); ++i) {
      const uint32_t id = be32(d + pos);
      const uint32_t size = be32(d + pos + 4);
      if (size < 8 || pos + size > rec.size()) break;
      const char* data = rec.data() + pos + 8;
      const size_t len = size - 8;
      auto text = [&](std::string& dst, const char* sep) {
        std::string t;
        convertText(h.codepage, t, data, len);
        if (t.empty()) return;
        if (!dst.empty() && sep) dst += sep;
        if (!sep) dst.clear();
        dst += t;
      };
      switch (id) {
        case 100: text(md.author, ", "); break;
        case 101: text(md.publisher, nullptr); break;
        case 103: text(md.description, nullptr); break;
        case 106: text(md.published, nullptr); break;
        case 503: text(updatedTitle, nullptr); break;
        case 524: text(md.language, nullptr); break;
        case 201:
          if (len == 4) h.coverOffset = be32(reinterpret_cast<const uint8_t*>(data));
          break;
        default: break;
      }
      pos += size;
    }
    if (!updatedTitle.empty()) md.title = updatedTitle;
  }
  if (md.language.empty()) md.language = languageFromId(h.langCode);
  return true;
}

bool Book::open(IByteReader& file) {
  file_ = &file;
  error_ = Error::None;
  metadata_ = Metadata();
  kf8_ = false;
  fileSize_ = file.size();

  uint8_t pdb[78];
  if (!file.seek(0) || file.read(pdb, sizeof(pdb)) != sizeof(pdb)) {
    error_ = Error::Truncated;
    return false;
  }
  if (!looksLikeMobi(pdb, sizeof(pdb))) {
    error_ = Error::NotMobi;
    return false;
  }
  numRecords_ = be16(pdb + 76);
  if (numRecords_ < 2) {
    error_ = Error::Corrupt;
    return false;
  }
  char name[33];
  memcpy(name, pdb, 32);
  name[32] = 0;
  const std::string palmName(name);

  Header main;
  Metadata mainMeta;
  if (!parseHeader(0, main, mainMeta)) {
    error_ = Error::Corrupt;
    return false;
  }
  Header chosen = main;
  Metadata chosenMeta = mainMeta;

  // Combined MOBI7 + KF8 file: the KF8 header follows a "BOUNDARY" record.
  if (main.mobi && main.version < 8) {
    for (uint32_t i = static_cast<uint32_t>(main.textRecords) + 1; i + 1 < numRecords_; ++i) {
      uint32_t size = 0;
      if (!recordSize(i, size)) break;
      if (size != 8) continue;
      uint8_t b[8];
      if (!readRecordPart(i, 0, b, 8) || memcmp(b, "BOUNDARY", 8) != 0) continue;
      Header k8;
      Metadata k8Meta;
      if (parseHeader(i + 1, k8, k8Meta) && k8.mobi && k8.version == 8) {
        chosen = k8;
        chosen.coverBase = main.firstImage;
        chosenMeta = k8Meta;
        if (chosenMeta.title.empty()) chosenMeta.title = mainMeta.title;
        if (chosenMeta.author.empty()) chosenMeta.author = mainMeta.author;
        if (chosenMeta.language.empty()) chosenMeta.language = mainMeta.language;
      }
      break;
    }
  }

  header_ = chosen;
  metadata_ = chosenMeta;
  kf8_ = header_.mobi && (header_.start != 0 || header_.version == 8);
  if (metadata_.title.empty()) metadata_.title = palmName;

  if (header_.crypto != 0) {
    error_ = Error::Drm;
    return false;
  }
  if (header_.compression != 1 && header_.compression != 2 && header_.compression != 0x4448) {
    error_ = Error::Unsupported;
    return false;
  }
  if (header_.textRecords == 0 || static_cast<uint64_t>(header_.start) + header_.textRecords >= numRecords_) {
    error_ = Error::Truncated;
    return false;
  }
  uint8_t first[4] = {0, 0, 0, 0};
  if (readRecordPart(header_.start + 1, 0, first, 4) && memcmp(first, "%MOP", 4) == 0) {
    error_ = Error::Unsupported;  // print replica
    return false;
  }
  return true;
}

// ---- text ------------------------------------------------------------------

void Book::stripTrailingEntries(std::string& rec) const {
  uint16_t flags = header_.trailFlags;
  const bool multibyte = (flags & 1) != 0;
  int trailers = 0;
  while (flags > 1) {
    if (flags & 2) ++trailers;
    flags >>= 1;
  }
  for (int i = 0; i < trailers; ++i) {
    size_t num = 0;
    const size_t from = rec.size() >= 4 ? rec.size() - 4 : 0;
    for (size_t k = from; k < rec.size(); ++k) {
      const uint8_t v = static_cast<uint8_t>(rec[k]);
      if (v & 0x80) num = 0;
      num = (num << 7) | (v & 0x7F);
    }
    if (num == 0 || num > rec.size()) return;
    rec.resize(rec.size() - num);
  }
  if (multibyte && !rec.empty()) {
    const size_t num = (static_cast<uint8_t>(rec.back()) & 3) + 1;
    if (num <= rec.size()) rec.resize(rec.size() - num);
  }
}

bool Book::extractText(const TextSink& out, const CancelFn& cancel) {
  error_ = Error::None;
  if (header_.compression == 0x4448) {
    if (header_.huffRecord == kNone || !huff_.load(*this, header_.huffRecord, header_.huffCount)) {
      error_ = Error::Corrupt;
      return false;
    }
  }
  std::string rec, text;
  uint64_t total = 0;
  for (uint32_t r = 1; r <= header_.textRecords; ++r) {
    if (cancel && cancel()) {
      error_ = Error::Cancelled;
      return false;
    }
    if (!readRecord(header_.start + r, rec, kMaxTextRecord)) {
      error_ = Error::Io;
      return false;
    }
    stripTrailingEntries(rec);
    text.clear();
    bool ok = true;
    const auto* data = reinterpret_cast<const uint8_t*>(rec.data());
    if (header_.compression == 1) {
      text.assign(rec);
    } else if (header_.compression == 2) {
      ok = palmDocDecompress(data, rec.size(), text, kMaxTextRecord);
    } else {
      ok = huff_.unpack(data, rec.size(), text, kMaxTextRecord);
    }
    if (!ok) {
      error_ = Error::Corrupt;
      return false;
    }
    total += text.size();
    if (total > kMaxTextTotal) {
      error_ = Error::Corrupt;
      return false;
    }
    if (!text.empty() && !out(text.data(), text.size())) {
      error_ = Error::Io;
      return false;
    }
  }
  return true;
}

}  // namespace mobi
