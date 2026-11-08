#include "DocxPackage.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <OpcNames.h>
#include <OpcPackage.h>
#include <XmlPullReader.h>

#include "DocxXml.h"

namespace docx {

namespace {

using namespace opc;

constexpr size_t kMaxCorePropsBytes = 16 * 1024;
constexpr size_t kMaxAbstractNums = 512;

// ---------------------------------------------------------------------------
// styles.xml
// ---------------------------------------------------------------------------

struct RawStyle {
  std::string id;
  std::string name;
  std::string basedOn;
  int outline = -1;  // outlineLvl 0..8, -1 = unset
  uint16_t flags = 0;
  uint32_t numId = 0;
  uint8_t ilvl = 0;
};

void classifyName(const std::string& nameLower, uint8_t& heading, uint16_t& flags) {
  if (nameLower.compare(0, 8, "heading ") == 0 && nameLower.size() == 9 && nameLower[8] >= '1' &&
      nameLower[8] <= '9') {
    heading = static_cast<uint8_t>(nameLower[8] - '0');
  } else if (nameLower == "title") {
    flags |= kTitle;
  } else if (nameLower == "subtitle") {
    flags |= kSubtitle;
  } else if (nameLower == "quote" || nameLower == "intense quote" || nameLower == "block text") {
    flags |= kQuote;
  }
}

}  // namespace

bool parseStyles(IByteReader& part, PackageInfo& info) {
  XmlPullReader xml(part, 2048);
  std::vector<RawStyle> raw;
  RawStyle cur;
  bool inStyle = false;
  enum class Ctx : uint8_t { Other, PPr, RPr, NumPr };
  std::vector<Ctx> ctx;

  for (;;) {
    xml.setCaptureText(false);
    const XmlToken tok = xml.next();
    if (tok == XmlToken::Eof) break;
    if (tok == XmlToken::Error) return false;

    if (isStart(tok)) {
      const std::string_view n = localName(xml.name());
      const Ctx parent = ctx.empty() ? Ctx::Other : ctx.back();
      Ctx mine = Ctx::Other;
      if (n == "style") {
        cur = RawStyle();
        inStyle = true;
        if (const char* id = attrLocal(xml, "styleId")) cur.id = id;
      } else if (inStyle) {
        if (n == "pPr") mine = Ctx::PPr;
        else if (n == "rPr" && parent != Ctx::PPr) mine = Ctx::RPr;  // pPr/rPr styles the paragraph mark only
        else if (n == "numPr" && parent == Ctx::PPr) mine = Ctx::NumPr;
        else if (n == "name" && parent == Ctx::Other) {
          if (const char* v = attrLocal(xml, "val")) cur.name = v;
        } else if (n == "basedOn" && parent == Ctx::Other) {
          if (const char* v = attrLocal(xml, "val")) cur.basedOn = v;
        } else if (n == "outlineLvl" && parent == Ctx::PPr) {
          const uint32_t lvl = attrUint(xml, "val", 9);
          cur.outline = lvl <= 8 ? static_cast<int>(lvl) : -1;
        } else if (n == "numId" && parent == Ctx::NumPr) {
          cur.numId = attrUint(xml, "val");
        } else if (n == "ilvl" && parent == Ctx::NumPr) {
          cur.ilvl = static_cast<uint8_t>(std::min<uint32_t>(attrUint(xml, "val"), 8));
        } else if (parent == Ctx::RPr) {
          if (n == "b" && toggleOn(xml)) cur.flags |= kBold;
          else if (n == "i" && toggleOn(xml)) cur.flags |= kItalic;
          else if (n == "u" && toggleOn(xml)) cur.flags |= kUnderline;
          else if ((n == "strike" || n == "dstrike") && toggleOn(xml)) cur.flags |= kStrike;
          else if (n == "smallCaps" && toggleOn(xml)) cur.flags |= kSmallCaps;
          else if (n == "vertAlign") {
            const char* v = attrLocal(xml, "val");
            if (v && std::string_view(v) == "superscript") cur.flags |= kSuperscript;
            else if (v && std::string_view(v) == "subscript") cur.flags |= kSubscript;
          }
        }
      }
      ctx.push_back(mine);  // self-closing elements get a synthetic end tag too
    } else if (tok == XmlToken::EndTag) {
      const std::string_view n = localName(xml.name());
      if (!ctx.empty()) ctx.pop_back();
      if (n == "style" && inStyle) {
        inStyle = false;
        if (!cur.id.empty() && raw.size() < 4 * kMaxStyles) raw.push_back(std::move(cur));
        cur = RawStyle();
      }
    }
  }

  // Resolve: names -> kinds, then inherit through basedOn (bounded).
  struct Resolved {
    uint8_t heading = 0;
    uint16_t flags = 0;
    uint32_t numId = 0;
    uint8_t ilvl = 0;
  };
  std::vector<Resolved> own(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) {
    classifyName(asciiLowerCopy(raw[i].name), own[i].heading, own[i].flags);
    if (own[i].heading == 0 && raw[i].outline >= 0) own[i].heading = static_cast<uint8_t>(raw[i].outline + 1);
    own[i].flags |= raw[i].flags;
    own[i].numId = raw[i].numId;
    own[i].ilvl = raw[i].ilvl;
  }
  const auto indexOf = [&](const std::string& id) -> int {
    for (size_t i = 0; i < raw.size(); ++i) {
      if (raw[i].id == id) return static_cast<int>(i);
    }
    return -1;
  };

  info.styles.clear();
  for (size_t i = 0; i < raw.size(); ++i) {
    Resolved r = own[i];
    int parent = raw[i].basedOn.empty() ? -1 : indexOf(raw[i].basedOn);
    for (int hops = 0; parent >= 0 && hops < 6; ++hops) {
      const Resolved& p = own[static_cast<size_t>(parent)];
      if (r.heading == 0) r.heading = p.heading;
      r.flags |= p.flags;
      if (r.numId == 0) {
        r.numId = p.numId;
        r.ilvl = p.ilvl;
      }
      const std::string& next = raw[static_cast<size_t>(parent)].basedOn;
      parent = next.empty() ? -1 : indexOf(next);
    }
    if (r.heading == 0 && r.flags == 0 && r.numId == 0) continue;  // nothing a reader cares about
    if (info.styles.size() >= kMaxStyles) break;
    StyleInfo s;
    s.id = raw[i].id;
    s.heading = r.heading;
    s.flags = r.flags;
    s.numId = r.numId;
    s.ilvl = r.ilvl;
    info.styles.push_back(std::move(s));
  }
  info.sortTables();
  return true;
}

// ---------------------------------------------------------------------------
// numbering.xml
// ---------------------------------------------------------------------------

bool parseNumbering(IByteReader& part, PackageInfo& info) {
  XmlPullReader xml(part, 2048);
  std::vector<std::pair<uint32_t, uint16_t>> abstractMasks;  // abstractNumId -> ordered levels
  std::vector<std::pair<uint32_t, uint32_t>> nums;           // numId -> abstractNumId
  bool inAbstract = false;
  uint32_t abstractId = 0;
  uint16_t mask = 0;
  int level = -1;
  bool inNum = false;
  uint32_t numId = 0;

  for (;;) {
    xml.setCaptureText(false);
    const XmlToken tok = xml.next();
    if (tok == XmlToken::Eof) break;
    if (tok == XmlToken::Error) return false;
    const std::string_view n = localName(xml.name());
    if (isStart(tok)) {
      if (n == "abstractNum") {
        inAbstract = true;
        abstractId = attrUint(xml, "abstractNumId");
        mask = 0;
        level = -1;
      } else if (inAbstract && n == "lvl") {
        level = static_cast<int>(std::min<uint32_t>(attrUint(xml, "ilvl"), 8));
      } else if (inAbstract && n == "numFmt" && level >= 0) {
        const char* v = attrLocal(xml, "val");
        const std::string f = v ? asciiLowerCopy(v) : std::string();
        if (!f.empty() && f != "bullet" && f != "none") mask |= static_cast<uint16_t>(1u << level);
      } else if (n == "num") {
        inNum = true;
        numId = attrUint(xml, "numId");
      } else if (inNum && n == "abstractNumId") {
        if (nums.size() < kMaxNumberings) nums.emplace_back(numId, attrUint(xml, "val"));
      }
    } else if (tok == XmlToken::EndTag) {
      if (n == "abstractNum" && inAbstract) {
        inAbstract = false;
        if (abstractMasks.size() < kMaxAbstractNums) abstractMasks.emplace_back(abstractId, mask);
      } else if (n == "lvl") {
        level = -1;
      } else if (n == "num") {
        inNum = false;
      }
    }
  }

  info.numberings.clear();
  for (const auto& num : nums) {
    uint16_t ordered = 0;
    for (const auto& a : abstractMasks) {
      if (a.first == num.second) {
        ordered = a.second;
        break;
      }
    }
    info.numberings.push_back({num.first, ordered});
  }
  info.sortTables();
  return true;
}

namespace {

// ---------------------------------------------------------------------------
// docProps/core.xml
// ---------------------------------------------------------------------------

void parseCoreProperties(const std::string& xmlText, Metadata& md) {
  MemoryByteReader mem(xmlText);
  XmlPullReader xml(mem, 1024);
  std::string* target = nullptr;
  for (;;) {
    xml.setCaptureText(true);
    const XmlToken tok = xml.next();
    if (tok == XmlToken::Eof || tok == XmlToken::Error) break;
    if (isStart(tok)) {
      const std::string_view n = localName(xml.name());
      target = nullptr;
      if (n == "title") target = &md.title;
      else if (n == "creator") target = &md.author;
      else if (n == "language") target = &md.language;
      else if (n == "created" && md.date.empty()) target = &md.date;
    } else if (tok == XmlToken::Text) {
      if (target && target->size() < kMaxMetadataBytes) target->append(xml.text(), 0, kMaxMetadataBytes - target->size());
    } else if (tok == XmlToken::EndTag) {
      target = nullptr;
    }
  }
  const auto trim = [](std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    const size_t b = s.find_last_not_of(" \t\r\n");
    s = a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
  };
  trim(md.title);
  trim(md.author);
  trim(md.language);
  trim(md.date);
  if (md.date.size() >= 10) md.date.resize(10);  // 2024-05-01T10:00:00Z -> 2024-05-01
}

// Drops a leading UTF-8 BOM and "<?xml ... ?>" declaration from a part so parts
// can be concatenated into one well-formed-enough stream.
class DeclStripper {
 public:
  explicit DeclStripper(const ByteSink& out) : out_(out) {}

  void write(const uint8_t* data, size_t len) {
    if (decided_) {
      if (len) out_(data, len);
      return;
    }
    head_.append(reinterpret_cast<const char*>(data), len);
    if (head_.size() >= kHeadLimit || head_.find("?>") != std::string::npos) decide();
  }

  void finish() {
    if (!decided_) decide();
  }

 private:
  static constexpr size_t kHeadLimit = 512;

  void decide() {
    decided_ = true;
    size_t start = 0;
    if (head_.compare(0, 3, "\xEF\xBB\xBF") == 0) start = 3;
    size_t probe = start;
    while (probe < head_.size() && (head_[probe] == ' ' || head_[probe] == '\t' || head_[probe] == '\r' ||
                                    head_[probe] == '\n')) {
      ++probe;
    }
    if (head_.compare(probe, 5, "<?xml") == 0) {
      const size_t close = head_.find("?>", probe);
      if (close != std::string::npos) start = close + 2;
    }
    if (start < head_.size()) out_(reinterpret_cast<const uint8_t*>(head_.data()) + start, head_.size() - start);
    head_.clear();
    head_.shrink_to_fit();
  }

  const ByteSink& out_;
  std::string head_;
  bool decided_ = false;
};

bool isWordMainMime(const std::string& mimeLower) { return mimeLower.find("wordprocessingml") != std::string::npos; }

bool findMainPart(IByteReader& zip, std::string& docNorm, std::string& coreNorm, ContentTypes* contentTypes) {
  std::vector<Relationship> rels;
  bool exists = false;
  if (!readRelationships(zip, "", rels, &exists) || !exists) return false;
  docNorm.clear();
  coreNorm.clear();
  for (const Relationship& r : rels) {
    if (docNorm.empty() && relationshipTypeIs(r.type, "officedocument")) {
      docNorm = r.target;
    } else if (coreNorm.empty() && relationshipTypeIs(r.type, "metadata/core-properties")) {
      coreNorm = r.target;
    }
  }
  if (docNorm.empty()) return false;
  ContentTypes local;
  ContentTypes& ct = contentTypes ? *contentTypes : local;
  readContentTypes(zip, ct);
  const std::string mime = ct.mimeOf(docNorm);
  // xlsx and pptx also have an "officeDocument" relationship; the declared main
  // part type (or, if undeclared, the conventional word/ folder) tells them apart.
  return mime.empty() ? docNorm.compare(0, 5, "word/") == 0 : isWordMainMime(mime);
}

}  // namespace

bool isDocxPackage(IByteReader& zip) {
  std::string doc, core;
  if (!findMainPart(zip, doc, core, nullptr)) return false;
  PartRef part;
  return findPart(zip, doc, part);
}

bool readPackageInfo(IByteReader& zip, const PartStager& stage, PackageInfo& out) {
  out = PackageInfo();
  std::string docNorm, coreNorm;
  ContentTypes ct;
  if (!findMainPart(zip, docNorm, coreNorm, &ct)) return false;

  std::vector<Relationship> rels;
  bool exists = false;
  if (!readRelationships(zip, docNorm, rels, &exists)) return false;
  std::string stylesNorm, numberingNorm, footnotesNorm, endnotesNorm;
  struct Candidate {
    std::string relId, target;
  };
  std::vector<Candidate> images;
  for (const Relationship& r : rels) {
    if (relationshipTypeIs(r.type, "styles") && stylesNorm.empty()) stylesNorm = r.target;
    else if (relationshipTypeIs(r.type, "numbering") && numberingNorm.empty()) numberingNorm = r.target;
    else if (relationshipTypeIs(r.type, "footnotes") && footnotesNorm.empty()) footnotesNorm = r.target;
    else if (relationshipTypeIs(r.type, "endnotes") && endnotesNorm.empty()) endnotesNorm = r.target;
    else if (relationshipTypeIs(r.type, "image") && images.size() < kMaxImages) images.push_back({r.id, r.target});
  }

  std::vector<PartRef> wanted;
  wanted.reserve(images.size() + 6);
  wanted.push_back(makePartRef(docNorm));
  wanted.push_back(makePartRef(stylesNorm));
  wanted.push_back(makePartRef(numberingNorm));
  wanted.push_back(makePartRef(footnotesNorm));
  wanted.push_back(makePartRef(endnotesNorm));
  wanted.push_back(makePartRef(coreNorm));
  for (const Candidate& c : images) wanted.push_back(makePartRef(c.target));
  if (!lookupParts(zip, wanted)) return false;
  if (!wanted[0].found) return false;
  out.documentOrdinal = wanted[0].ordinal;
  if (wanted[3].found) {
    out.hasFootnotes = true;
    out.footnotesOrdinal = wanted[3].ordinal;
  }
  if (wanted[4].found) {
    out.hasEndnotes = true;
    out.endnotesOrdinal = wanted[4].ordinal;
  }

  for (size_t i = 0; i < images.size(); ++i) {
    const PartRef& w = wanted[i + 6];
    if (!w.found || w.entry.uncompressedSize == 0) continue;
    if (w.entry.method != 0 && w.entry.method != 8) continue;
    const ImageKind kind = ct.imageKindFor(images[i].target);
    if (kind == ImageKind::None) continue;
    Image img;
    img.relId = images[i].relId;
    img.kind = kind;
    img.entryOrdinal = w.ordinal;
    out.images.push_back(std::move(img));
  }

  if (wanted[5].found) {
    std::string core;
    if (readEntryToString(zip, wanted[5].entry, kMaxCorePropsBytes, core)) parseCoreProperties(core, out.metadata);
  }

  // Styles and numbering are best effort: a book without them still reads, just
  // without heading detection / numbered lists.
  if (wanted[1].found && stage) {
    stage(zip, wanted[1].ordinal, [&](IByteReader& part) { return parseStyles(part, out); });
  }
  if (wanted[2].found && stage) {
    stage(zip, wanted[2].ordinal, [&](IByteReader& part) { return parseNumbering(part, out); });
  }
  out.sortTables();
  return true;
}

bool writeBodySource(IByteReader& zip, const PackageInfo& info, const ByteSink& out) {
  static const char kOpen[] = "<docx-book>";
  static const char kClose[] = "</docx-book>";
  out(reinterpret_cast<const uint8_t*>(kOpen), sizeof(kOpen) - 1);

  const auto appendPart = [&](uint32_t ordinal) {
    DeclStripper stripper(out);
    const bool ok = extractEntryByOrdinal(zip, ordinal, [&](const uint8_t* d, size_t n) { stripper.write(d, n); });
    stripper.finish();
    return ok;
  };
  if (!appendPart(info.documentOrdinal)) return false;
  // A broken notes part must not take the whole book down.
  if (info.hasFootnotes) appendPart(info.footnotesOrdinal);
  if (info.hasEndnotes) appendPart(info.endnotesOrdinal);

  out(reinterpret_cast<const uint8_t*>(kClose), sizeof(kClose) - 1);
  return true;
}

}  // namespace docx
