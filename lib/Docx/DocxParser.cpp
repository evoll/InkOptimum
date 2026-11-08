#include "DocxParser.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ReaderWork.h>

#include <OpcPackage.h>
#include <XmlPullReader.h>

#include "DocxXml.h"

namespace {

using docx::attrLocal;
using docx::attrUint;
using docx::isStart;
using docx::toggleOn;
using opc::localName;

// ---------------------------------------------------------------------------
// Element classification (by local name)
// ---------------------------------------------------------------------------

enum class K : uint8_t {
  Other, Body, Footnotes, Endnotes, NoteItem,
  P, PPr, PStyle, OutlineLvl, NumPr, NumId, Ilvl,
  R, RPr, RStyle, Bold, Italic, Underline, Strike, SmallCaps, VertAlign,
  T, Tab, Br, NoBreakHyphen, Hyperlink,
  FootnoteRef, EndnoteRef, Blip, ImageData,
  Tbl, Tr, Tc, TrPr, TblHeader, TcPr, GridSpan, VMerge, VAlign,
  // Subtrees that are skipped entirely.
  Skip,
};

K classify(std::string_view n) {
  switch (n.size()) {
    case 1:
      if (n == "p") return K::P;
      if (n == "r") return K::R;
      if (n == "t") return K::T;
      if (n == "b") return K::Bold;
      if (n == "i") return K::Italic;
      if (n == "u") return K::Underline;
      break;
    case 2:
      if (n == "br") return K::Br;
      if (n == "cr") return K::Br;
      if (n == "tc") return K::Tc;
      if (n == "tr") return K::Tr;
      break;
    case 3:
      if (n == "tab") return K::Tab;
      if (n == "tbl") return K::Tbl;
      if (n == "del") return K::Skip;
      if (n == "pPr") return K::PPr;
      if (n == "rPr") return K::RPr;
      break;
    case 4:
      if (n == "body") return K::Body;
      if (n == "trPr") return K::TrPr;
      if (n == "tcPr") return K::TcPr;
      if (n == "ilvl") return K::Ilvl;
      if (n == "blip") return K::Blip;
      break;
    case 5:
      if (n == "numId") return K::NumId;
      break;
    default: break;
  }
  if (n == "vAlign") return K::VAlign;
  if (n == "pStyle") return K::PStyle;
  if (n == "rStyle") return K::RStyle;
  if (n == "numPr") return K::NumPr;
  if (n == "outlineLvl") return K::OutlineLvl;
  if (n == "strike" || n == "dstrike") return K::Strike;
  if (n == "smallCaps") return K::SmallCaps;
  if (n == "vertAlign") return K::VertAlign;
  if (n == "noBreakHyphen") return K::NoBreakHyphen;
  if (n == "hyperlink") return K::Hyperlink;
  if (n == "footnoteReference") return K::FootnoteRef;
  if (n == "endnoteReference") return K::EndnoteRef;
  if (n == "imagedata") return K::ImageData;
  if (n == "footnotes") return K::Footnotes;
  if (n == "endnotes") return K::Endnotes;
  if (n == "footnote" || n == "endnote") return K::NoteItem;
  if (n == "tblHeader") return K::TblHeader;
  if (n == "gridSpan") return K::GridSpan;
  if (n == "vMerge") return K::VMerge;
  // Skipped subtrees: tracked deletions/moves, text boxes (their content is
  // duplicated in mc:Fallback), field instructions, content-control properties,
  // and "...PrChange" (which carries the OLD formatting).
  if (n == "moveFrom" || n == "txbxContent" || n == "Fallback" || n == "instrText" || n == "sdtPr" ||
      n == "sdtEndPr" || n == "delText") {
    return K::Skip;
  }
  if (n.size() > 8 && n.compare(n.size() - 8, 8, "PrChange") == 0) return K::Skip;
  return K::Other;
}

constexpr size_t kMaxSectionIdBytes = 192;
constexpr size_t kMaxSectionTitleBytes = 384;
constexpr size_t kSectionStringPoolReserve = 28 * 1024;
constexpr size_t kSectionStringPoolHardLimit = 28 * 1024;
constexpr size_t kMaxNestingGuard = 4096;
constexpr uint8_t kMaxSectionHeading = 3;  // Heading 1..3 start chapters

bool cancelled(const reader::ReaderCancellationToken* token) { return token && token->isCancellationRequested(); }

bool isXmlSpace(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// Whitespace-collapsing across successive text chunks / runs. A collapsed gap
// between words is reported as `leadingSpace` instead of being glued into the
// text, so the caller can place it OUTSIDE a styled run.
struct Collapsed {
  bool leadingSpace = false;
  std::string text;
};

class TextCollapser {
 public:
  void reset() {
    atStart_ = true;
    pending_ = false;
  }
  // A forced word boundary (tab, line break, paragraph end inside a cell).
  void separate() {
    if (!atStart_) pending_ = true;
  }
  bool atStart() const { return atStart_; }
  bool pending() const { return pending_; }
  void clearPending() { pending_ = false; }

  Collapsed feed(const std::string& in) {
    Collapsed out;
    out.text.reserve(in.size());
    for (const unsigned char c : in) {
      if (isXmlSpace(c)) {
        if (!atStart_) pending_ = true;
        continue;
      }
      if (pending_) {
        if (out.text.empty()) out.leadingSpace = true;
        else out.text.push_back(' ');
        pending_ = false;
      }
      out.text.push_back(static_cast<char>(c));
      atStart_ = false;
    }
    return out;
  }

 private:
  bool atStart_ = true;
  bool pending_ = false;
};

// ---------------------------------------------------------------------------
// Paragraph properties
// ---------------------------------------------------------------------------

struct ParaProps {
  std::string styleId;
  int outline = -1;  // -1 unset; 0 explicit "body text"; 1..9 heading level
  bool hasNumId = false;
  uint32_t numId = 0;
  bool hasIlvl = false;
  uint8_t ilvl = 0;

  void reset() { *this = ParaProps(); }

  // Feeds one child element of <w:pPr>.
  void collect(K k, const XmlPullReader& xml, bool inNumPr) {
    if (k == K::PStyle) {
      if (const char* v = attrLocal(xml, "val")) styleId = v;
    } else if (k == K::OutlineLvl) {
      const uint32_t lvl = attrUint(xml, "val", 9);
      outline = lvl <= 8 ? static_cast<int>(lvl + 1) : 0;
    } else if (k == K::NumId && inNumPr) {
      hasNumId = true;
      numId = attrUint(xml, "val");
    } else if (k == K::Ilvl && inNumPr) {
      hasIlvl = true;
      ilvl = static_cast<uint8_t>(std::min<uint32_t>(attrUint(xml, "val"), 8));
    }
  }

  const docx::StyleInfo* style(const docx::PackageInfo* info) const {
    return (info && !styleId.empty()) ? info->findStyle(styleId) : nullptr;
  }

  uint8_t headingLevel(const docx::PackageInfo* info) const {
    if (outline >= 0) return static_cast<uint8_t>(outline);
    const docx::StyleInfo* s = style(info);
    return s ? s->heading : 0;
  }

  // 0 = not a list item.
  uint32_t listId(const docx::PackageInfo* info) const {
    if (hasNumId) return numId;
    const docx::StyleInfo* s = style(info);
    return s ? s->numId : 0;
  }

  uint8_t listLevel(const docx::PackageInfo* info) const {
    if (hasIlvl) return ilvl;
    const docx::StyleInfo* s = style(info);
    return s ? s->ilvl : 0;
  }

  uint16_t styleFlags(const docx::PackageInfo* info) const {
    const docx::StyleInfo* s = style(info);
    return s ? s->flags : 0;
  }
};

bool isTocStyle(const std::string& id) {
  return id.size() == 4 && (id[0] == 'T' || id[0] == 't') && (id[1] == 'O' || id[1] == 'o') &&
         (id[2] == 'C' || id[2] == 'c') && id[3] >= '1' && id[3] <= '9';
}

// Run formatting bits (docx::StyleFlag) -> sink style.
Fb2InlineStyle toInlineStyle(uint16_t flags) {
  Fb2InlineStyle s = Fb2InlineStyle::Regular;
  if (flags & docx::kBold) s = s | Fb2InlineStyle::Bold;
  if (flags & docx::kItalic) s = s | Fb2InlineStyle::Italic;
  if (flags & docx::kUnderline) s = s | Fb2InlineStyle::Underline;
  if (flags & docx::kStrike) s = s | Fb2InlineStyle::Strikethrough;
  if (flags & docx::kSuperscript) s = s | Fb2InlineStyle::Superscript;
  if (flags & docx::kSubscript) s = s | Fb2InlineStyle::Subscript;
  if (flags & docx::kSmallCaps) s = s | Fb2InlineStyle::SmallCaps;
  return s;
}

constexpr uint16_t kRunFormatMask = docx::kBold | docx::kItalic | docx::kUnderline | docx::kStrike |
                                    docx::kSmallCaps | docx::kSuperscript | docx::kSubscript;

// Applies one run-property element to `flags`. Returns true if it was one.
bool applyRunProperty(K k, const XmlPullReader& xml, uint16_t& flags, const docx::PackageInfo* info) {
  const auto set = [&](uint16_t bit) {
    if (toggleOn(xml)) flags = static_cast<uint16_t>(flags | bit);
    else flags = static_cast<uint16_t>(flags & ~bit);
  };
  switch (k) {
    case K::RStyle: {
      const char* v = attrLocal(xml, "val");
      const docx::StyleInfo* s = (info && v) ? info->findStyle(v) : nullptr;
      if (s) flags = static_cast<uint16_t>(flags | (s->flags & kRunFormatMask));
      return true;
    }
    case K::Bold: set(docx::kBold); return true;
    case K::Italic: set(docx::kItalic); return true;
    case K::Underline: set(docx::kUnderline); return true;
    case K::Strike: set(docx::kStrike); return true;
    case K::SmallCaps: set(docx::kSmallCaps); return true;
    case K::VertAlign: {
      flags = static_cast<uint16_t>(flags & ~(docx::kSuperscript | docx::kSubscript));
      const char* v = attrLocal(xml, "val");
      if (v && std::string_view(v) == "superscript") flags = static_cast<uint16_t>(flags | docx::kSuperscript);
      else if (v && std::string_view(v) == "subscript") flags = static_cast<uint16_t>(flags | docx::kSubscript);
      return true;
    }
    default: return false;
  }
}

// Relationship id of an image reference, or nullptr.
const char* imageRelId(K k, const XmlPullReader& xml) {
  if (k == K::Blip) return attrLocal(xml, "embed");
  if (k == K::ImageData) return attrLocal(xml, "id");
  return nullptr;
}

bool storePoolCString(Fb2ScanResult& out, const std::string& value, size_t maxLen, uint32_t& offset,
                      uint16_t& length) {
  offset = UINT32_MAX;
  length = 0;
  if (value.empty() || out.stringPool.size() >= kSectionStringPoolHardLimit) return false;
  const size_t available = kSectionStringPoolHardLimit - out.stringPool.size();
  const size_t copyLen = std::min({value.size(), maxLen, available, static_cast<size_t>(UINT16_MAX)});
  if (copyLen == 0) return false;
  offset = static_cast<uint32_t>(out.stringPool.size());
  length = static_cast<uint16_t>(copyLen);
  out.stringPool.append(value, 0, copyLen);
  return true;
}

// Title text is stored whitespace-collapsed.
bool storePoolTitle(Fb2ScanResult& out, const std::string& value, uint32_t& offset, uint16_t& length) {
  offset = UINT32_MAX;
  length = 0;
  if (value.empty() || out.stringPool.size() >= kSectionStringPoolHardLimit) return false;
  const size_t start = out.stringPool.size();
  bool pendingSpace = false;
  for (const unsigned char c : value) {
    if (out.stringPool.size() >= kSectionStringPoolHardLimit) break;
    if (isXmlSpace(c)) {
      pendingSpace = out.stringPool.size() > start;
      continue;
    }
    if (pendingSpace && out.stringPool.size() < kSectionStringPoolHardLimit) out.stringPool.push_back(' ');
    pendingSpace = false;
    if (out.stringPool.size() < kSectionStringPoolHardLimit) out.stringPool.push_back(static_cast<char>(c));
  }
  const size_t stored = out.stringPool.size() - start;
  if (stored == 0) return false;
  offset = static_cast<uint32_t>(start);
  length = static_cast<uint16_t>(std::min(stored, static_cast<size_t>(UINT16_MAX)));
  return true;
}

bool isSeparatorNote(const XmlPullReader& xml) {
  const char* t = attrLocal(xml, "type");
  if (!t) return false;
  const std::string v = opc::asciiLowerCopy(t);
  return v == "separator" || v == "continuationseparator" || v == "continuationnotice";
}

}  // namespace

// ===========================================================================
// scan
// ===========================================================================

bool DocxParser::scan(IByteReader& reader, Fb2ScanResult& out, size_t xmlBufferSize) {
  out = Fb2ScanResult{};
  out.bodies.reserve(2);
  out.stringPool.reserve(reader.size() >= 8ULL * 1024 * 1024 ? kSectionStringPoolReserve : 8 * 1024);

  if (info_) {
    out.metadata.title = info_->metadata.title;
    out.metadata.author = info_->metadata.author;
    out.metadata.language = info_->metadata.language;
    out.metadata.date = info_->metadata.date;
    if (!info_->metadata.author.empty()) {
      Fb2Author a;
      a.nickname = info_->metadata.author;
      out.metadata.authors.push_back(std::move(a));
    }
    for (const docx::Image& img : info_->images) {
      Fb2BinaryIndexEntry b;
      b.id = img.relId;
      b.contentType = opc::mimeFor(img.kind);
      b.payloadStartOffset = img.entryOrdinal;  // ZIP ordinal, not a byte offset
      b.payloadEndOffset = img.entryOrdinal;
      out.binaries.push_back(std::move(b));
    }
  }

  const size_t safeXmlBufferSize = std::max<size_t>(1024, std::min<size_t>(8 * 1024, xmlBufferSize));
  XmlPullReader xml(reader, safeXmlBufferSize);

  std::vector<K> stack;
  stack.reserve(32);
  int skipDepth = 0;

  int currentBody = -1;  // body the next section belongs to
  int notesBody = -1;
  const char* notePrefix = "fn";
  bool inNotes = false;
  int cur = -1;  // index into out.sections: the section text is attributed to

  uint32_t firstBlockOffset = UINT32_MAX;  // start of the body's first block
  int tblDepth = 0;
  bool sawBody = false;

  bool inPara = false;
  bool inPPr = false;
  bool inNumPr = false;
  bool inT = false;
  uint32_t paraStart = 0;
  ParaProps props;
  bool inTitle = false;
  std::string titleBuf;

  const auto countText = [&](size_t bytes) {
    if (cur >= 0) out.sections[static_cast<size_t>(cur)].approxTextBytes += static_cast<uint32_t>(bytes);
  };
  // Content before the first heading lives in an untitled first section that
  // starts at the body's first block.
  const auto ensureImplicitSection = [&]() {
    if (cur >= 0 || currentBody < 0 || inNotes) return;
    Fb2SectionIndexEntry e;
    e.level = 0;
    e.bodyIndex = static_cast<int16_t>(currentBody);
    e.innerStartOffset = firstBlockOffset != UINT32_MAX ? firstBlockOffset : xml.tokenStartOffset();
    out.sections.push_back(e);
    cur = static_cast<int>(out.sections.size()) - 1;
  };

  for (;;) {
    // Text is needed for heading titles and, until a section exists, to tell real
    // content from whitespace between elements.
    xml.setCaptureText(inTitle || (cur < 0 && !inNotes && currentBody >= 0));
    const XmlToken tok = xml.next();
    if (tok == XmlToken::Eof) break;
    if (tok == XmlToken::Error) return false;

    out.tokenCount++;
    if (tok == XmlToken::Text) {
      out.textTokenCount++;
      out.textPayloadBytes += xml.textSize();
    }

    if (isStart(tok)) {
      if (skipDepth > 0) {
        ++skipDepth;
        continue;
      }
      const K k = classify(localName(xml.name()));
      if (k == K::Skip) {
        skipDepth = 1;
        continue;
      }
      if (k == K::NoteItem && inNotes && isSeparatorNote(xml)) {
        skipDepth = 1;
        continue;
      }
      if (stack.size() >= kMaxNestingGuard) return false;
      stack.push_back(k);

      switch (k) {
        case K::Body:
          if (currentBody < 0 && !sawBody) {
            out.bodies.push_back(Fb2BodyIndexEntry{});
            currentBody = static_cast<int>(out.bodies.size()) - 1;
            sawBody = true;
            inNotes = false;
          }
          break;
        case K::Footnotes:
        case K::Endnotes:
          if (notesBody < 0) {
            Fb2BodyIndexEntry b;
            b.name = "notes";
            out.bodies.push_back(std::move(b));
            notesBody = static_cast<int>(out.bodies.size()) - 1;
          }
          currentBody = notesBody;
          inNotes = true;
          cur = -1;
          notePrefix = (k == K::Footnotes) ? "fn" : "en";
          break;
        case K::NoteItem:
          if (inNotes) {
            Fb2SectionIndexEntry e;
            e.level = 0;
            e.bodyIndex = static_cast<int16_t>(currentBody);
            if (const char* id = attrLocal(xml, "id")) {
              storePoolCString(out, std::string(notePrefix) + id, kMaxSectionIdBytes, e.idPoolOffset, e.idLength);
            }
            e.innerStartOffset = xml.streamPos();
            out.sections.push_back(e);
            cur = static_cast<int>(out.sections.size()) - 1;
          }
          break;
        case K::P:
          if (currentBody >= 0) {
            inPara = true;
            inPPr = false;
            paraStart = xml.tokenStartOffset();
            props.reset();
            if (!inNotes && tblDepth == 0 && firstBlockOffset == UINT32_MAX) firstBlockOffset = paraStart;
          }
          break;
        case K::Tbl:
          if (currentBody >= 0) {
            if (!inNotes && tblDepth == 0 && firstBlockOffset == UINT32_MAX) {
              firstBlockOffset = xml.tokenStartOffset();
            }
            ++tblDepth;
            if (tblDepth > 0 && !inNotes) ensureImplicitSection();
          }
          break;
        case K::PPr:
          if (inPara) inPPr = true;
          break;
        case K::NumPr:
          if (inPPr) inNumPr = true;
          break;
        case K::PStyle:
        case K::OutlineLvl:
        case K::NumId:
        case K::Ilvl:
          if (inPPr) props.collect(k, xml, inNumPr);
          break;
        case K::T:
          inT = true;
          break;
        case K::Tab:
        case K::Br:
          if (inTitle && !titleBuf.empty() && titleBuf.back() != ' ') titleBuf.push_back(' ');
          break;
        case K::Blip:
        case K::ImageData: {
          const char* rel = imageRelId(k, xml);
          if (rel && info_ && info_->findImage(rel)) {
            if (!inTitle) ensureImplicitSection();
            if (cur >= 0) {
              Fb2SectionIndexEntry& s = out.sections[static_cast<size_t>(cur)];
              if (s.imageRefCount < UINT16_MAX) s.imageRefCount++;
            }
          }
          break;
        }
        default: break;
      }
    } else if (tok == XmlToken::Text) {
      if (!inT) continue;
      if (inTitle) {
        if (titleBuf.size() < kMaxSectionTitleBytes) {
          titleBuf.append(xml.text(), 0, kMaxSectionTitleBytes - titleBuf.size());
        }
      } else {
        // Visible text outside a heading: make sure a section owns it.
        if (cur < 0 && !inNotes) {
          const std::string& t = xml.text();
          if (std::any_of(t.begin(), t.end(), [](unsigned char c) { return !isXmlSpace(c); })) ensureImplicitSection();
        }
        countText(xml.textSize());
      }
    } else if (tok == XmlToken::EndTag) {
      if (skipDepth > 0) {
        --skipDepth;
        continue;
      }
      if (stack.empty()) continue;
      const K k = stack.back();
      stack.pop_back();
      switch (k) {
        case K::Body:
          currentBody = -1;
          cur = -1;
          break;
        case K::Footnotes:
        case K::Endnotes:
          inNotes = false;
          currentBody = -1;
          cur = -1;
          break;
        case K::NoteItem:
          if (inNotes) cur = -1;
          break;
        case K::NumPr: inNumPr = false; break;
        case K::T: inT = false; break;
        case K::Tbl:
          if (tblDepth > 0) --tblDepth;
          break;
        case K::PPr:
          if (inPPr) {
            inPPr = false;
            inNumPr = false;
            // A heading paragraph (style or outline level 1..3) outside tables
            // and notes starts a new chapter.
            if (inPara && !inNotes && tblDepth == 0 && currentBody >= 0) {
              const uint8_t h = props.headingLevel(info_);
              if (h >= 1 && h <= kMaxSectionHeading) {
                Fb2SectionIndexEntry e;
                e.level = static_cast<uint16_t>(h - 1);
                e.bodyIndex = static_cast<int16_t>(currentBody);
                e.innerStartOffset = paraStart;
                out.sections.push_back(e);
                cur = static_cast<int>(out.sections.size()) - 1;
                inTitle = true;
                titleBuf.clear();
              }
            }
          }
          break;
        case K::P:
          if (inTitle) {
            if (cur >= 0) {
              Fb2SectionIndexEntry& s = out.sections[static_cast<size_t>(cur)];
              storePoolTitle(out, titleBuf, s.titlePoolOffset, s.titleLength);
            }
            inTitle = false;
          }
          inPara = false;
          break;
        default: break;
      }
    }
  }
  return sawBody && !out.sections.empty();
}

// ===========================================================================
// Rendering
// ===========================================================================

namespace {

// Streams one block run (a chapter body, one note, or just a heading) from the
// current reader position into a sink. One instance per call.
class DocxRenderer {
 public:
  enum class Mode {
    Chapter,  // body chapter: first heading paragraph is the chapter's own title (skipped), next one ends it
    Note,     // footnote/endnote: ends at the closing note tag; no heading logic
    Title,    // only the first block, which must be a chapter heading
  };

  DocxRenderer(XmlPullReader& xml, Fb2ContentSink& sink, const reader::ReaderCancellationToken* token,
               const docx::PackageInfo* info, Mode mode, uint8_t titleLevel = 0)
      : xml_(xml), sink_(sink), token_(token), info_(info), mode_(mode), titleLevel_(titleLevel) {
    stack_.reserve(24);
  }

  // True on success. In Title mode: true only if a non-empty title was emitted.
  bool run() {
    for (;;) {
      if (cancelled(token_)) return false;
      const XmlToken tok = xml_.next();
      if (tok == XmlToken::Eof) {
        closeParagraph();
        return finishOnEnd();
      }
      if (tok == XmlToken::Error) return false;

      if (tok == XmlToken::Text) {
        if (skipDepth_ == 0 && inT_) onText();
        continue;
      }
      if (isStart(tok)) {
        if (skipDepth_ > 0) {
          ++skipDepth_;
          continue;
        }
        const K k = classify(localName(xml_.name()));
        if (k == K::Skip) {
          skipDepth_ = 1;
          continue;
        }
        const Verdict v = onStart(k);
        if (v != Verdict::Continue) return v == Verdict::DoneOk ? finishOnEnd() : false;
      } else {
        if (skipDepth_ > 0) {
          --skipDepth_;
          continue;
        }
        if (stack_.empty()) {
          // A closing tag with nothing open: either the end of the container we
          // started in (body / note), or the end of a transparent wrapper such as
          // <w:sdtContent> that the section began inside. Only the former ends us.
          const std::string_view name = localName(xml_.name());
          const K ek = classify(name);
          if (ek == K::Body || ek == K::NoteItem || ek == K::Footnotes || ek == K::Endnotes ||
              name == "docx-book") {
            closeParagraph();
            return finishOnEnd();
          }
          continue;
        }
        const Verdict v = onEnd();
        if (v != Verdict::Continue) return v == Verdict::DoneOk ? finishOnEnd() : false;
      }
    }
  }

 private:
  enum class Verdict { Continue, DoneOk, DoneFail };
  enum class PKind : uint8_t { None, Normal, List, Subtitle, Quote, OwnTitle, TitleHeading, Cell, Muted };
  struct Frame {
    K kind = K::Other;
    bool linkEmitted = false;
    uint16_t savedRunFlags = 0;
  };
  struct ListCounter {
    uint32_t numId = 0;
    uint16_t count[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  };

  bool finishOnEnd() { return mode_ == Mode::Title ? titleEmitted_ : true; }

  bool inCell() const { return tblDepth_ > 0; }

  // -------------------------------------------------------------------------
  // Paragraph lifecycle
  // -------------------------------------------------------------------------

  void beginParagraph() {
    props_.reset();
    kind_ = PKind::None;
    decided_ = false;
    inPPr_ = false;
    inNumPr_ = false;
    paraFlags_ = 0;
    runFlags_ = 0;
    textStarted_ = false;
    opened_ = false;
    collapse_.reset();
    lastRunFlags_ = 0;
    haveLastRun_ = false;
  }

  // Called once the paragraph's properties are complete (end of pPr, or the
  // first non-pPr child, or the paragraph end for a paragraph without any).
  Verdict decideParagraph() {
    if (decided_) return Verdict::Continue;
    decided_ = true;
    paraFlags_ = props_.styleFlags(info_);
    const uint8_t heading = props_.headingLevel(info_);

    if (inCell()) {
      kind_ = PKind::Cell;
      return Verdict::Continue;
    }
    if (mode_ == Mode::Title) {
      if (heading >= 1 && heading <= kMaxSectionHeading && !firstBlockSeen_) {
        firstBlockSeen_ = true;
        kind_ = PKind::TitleHeading;
        sink_.onTitleBegin(titleLevel_);
        titleOpen_ = true;
        return Verdict::Continue;
      }
      return Verdict::DoneFail;  // no heading of its own
    }
    if (mode_ == Mode::Chapter && heading >= 1 && heading <= kMaxSectionHeading) {
      if (!firstBlockSeen_) {
        firstBlockSeen_ = true;
        kind_ = PKind::OwnTitle;  // rendered by renderSectionTitle()
        return Verdict::Continue;
      }
      return Verdict::DoneOk;  // next chapter starts here
    }
    firstBlockSeen_ = true;
    if (isTocStyle(props_.styleId)) {
      kind_ = PKind::Muted;
    } else if (heading >= 1 || (paraFlags_ & (docx::kTitle | docx::kSubtitle))) {
      kind_ = PKind::Subtitle;
    } else if (paraFlags_ & docx::kQuote) {
      kind_ = PKind::Quote;
    } else if (props_.listId(info_) != 0) {
      kind_ = PKind::List;
    } else {
      kind_ = PKind::Normal;
    }
    return Verdict::Continue;
  }

  // Opens the sink container on the first visible content (empty paragraphs
  // produce nothing).
  void openIfNeeded() {
    if (opened_) return;
    switch (kind_) {
      case PKind::Normal:
        sink_.onParagraphBegin();
        break;
      case PKind::List:
        sink_.onParagraphBegin();
        sink_.onText(listMarker(), Fb2InlineStyle::Regular);
        break;
      case PKind::Subtitle:
        sink_.onSubtitleBegin();
        break;
      case PKind::Quote:
        sink_.onCiteBegin();
        sink_.onParagraphBegin();
        break;
      default:
        return;  // cells, titles and muted paragraphs have no sink container
    }
    opened_ = true;
  }

  void closeParagraph() {
    if (opened_) {
      switch (kind_) {
        case PKind::Normal:
        case PKind::List:
          sink_.onParagraphEnd();
          break;
        case PKind::Subtitle:
          sink_.onSubtitleEnd();
          break;
        case PKind::Quote:
          sink_.onParagraphEnd();
          sink_.onCiteEnd();
          break;
        default: break;
      }
      opened_ = false;
    }
    if (titleOpen_) {
      sink_.onTitleEnd(titleLevel_);
      titleOpen_ = false;
    }
  }

  std::string listMarker() {
    const uint32_t numId = props_.listId(info_);
    const uint8_t lvl = props_.listLevel(info_);
    std::string marker;
    for (uint8_t i = 0; i < lvl; ++i) marker += "\xC2\xA0\xC2\xA0";
    if (info_ && info_->isOrdered(numId, lvl)) {
      ListCounter* c = nullptr;
      for (ListCounter& existing : counters_) {
        if (existing.numId == numId) c = &existing;
      }
      if (!c) {
        if (counters_.size() >= 32) counters_.erase(counters_.begin());
        counters_.push_back(ListCounter{numId});
        c = &counters_.back();
      }
      ++c->count[lvl];
      for (int deeper = lvl + 1; deeper < 9; ++deeper) c->count[deeper] = 0;
      marker += std::to_string(c->count[lvl]) + ".";
    } else {
      marker += lvl == 0 ? "\xE2\x80\xA2" : "\xE2\x80\x93";
    }
    marker += "\xC2\xA0";
    return marker;
  }

  // -------------------------------------------------------------------------
  // Text
  // -------------------------------------------------------------------------

  uint16_t effectiveRunFlags() const {
    uint16_t flags = static_cast<uint16_t>(runFlags_ & kRunFormatMask);
    // Headings, subtitles and titles are already emphasised by the sink.
    if (kind_ == PKind::Subtitle || kind_ == PKind::TitleHeading) flags = static_cast<uint16_t>(flags & ~docx::kBold);
    return flags;
  }

  void onText() {
    if (kind_ == PKind::OwnTitle || kind_ == PKind::Muted || kind_ == PKind::None) return;
    if (kind_ == PKind::Cell) {
      const Collapsed c = cellCollapse_.feed(xml_.text());
      if (c.text.empty()) return;
      if (c.leadingSpace && !cellBuf_.empty()) cellBuf_.push_back(' ');
      cellBuf_ += c.text;
      return;
    }
    const Collapsed c = collapse_.feed(xml_.text());
    if (c.text.empty()) return;
    emitText(c);
  }

  void emitText(const Collapsed& c) {
    const uint16_t flags = effectiveRunFlags();
    if (kind_ == PKind::TitleHeading) {
      if (c.leadingSpace && textStarted_) sink_.onText(" ", toInlineStyle(haveLastRun_ && lastRunFlags_ == flags ? flags : 0));
      sink_.onText(c.text, toInlineStyle(flags));
      textStarted_ = true;
      titleEmitted_ = true;
      lastRunFlags_ = flags;
      haveLastRun_ = true;
      return;
    }
    openIfNeeded();
    if (c.leadingSpace && textStarted_) {
      // The gap between two words belongs outside a styled run unless both sides
      // share the style (then an underline stays unbroken).
      sink_.onText(" ", toInlineStyle(haveLastRun_ && lastRunFlags_ == flags ? flags : 0));
    }
    sink_.onText(c.text, toInlineStyle(flags));
    textStarted_ = true;
    lastRunFlags_ = flags;
    haveLastRun_ = true;
  }

  // A forced word boundary inside the paragraph (tab, line break).
  void separator() {
    if (kind_ == PKind::Cell) cellCollapse_.separate();
    else collapse_.separate();
  }

  void emitLiteral(const char* text) {
    if (kind_ == PKind::Cell) {
      cellBuf_ += text;
      cellCollapse_.clearPending();
      return;
    }
    if (kind_ == PKind::OwnTitle || kind_ == PKind::Muted || kind_ == PKind::None) return;
    Collapsed c;
    c.text = text;
    c.leadingSpace = collapse_.pending() && !collapse_.atStart();
    collapse_.clearPending();
    // Mark text started so later words get a separating gap.
    collapse_.feed("x");
    emitText(c);
  }

  // -------------------------------------------------------------------------
  // Elements
  // -------------------------------------------------------------------------

  Verdict onStart(K k) {
    const K parent = stack_.empty() ? K::Other : stack_.back().kind;

    // Paragraph properties are complete once a non-pPr child shows up.
    if (parent == K::P && inPara_ && !decided_ && k != K::PPr) {
      const Verdict v = decideParagraph();
      if (v != Verdict::Continue) return v;
    }

    Frame f;
    f.kind = k;
    const bool push = true;  // a synthetic end tag follows self-closing elements

    switch (k) {
      case K::Footnotes:
      case K::Endnotes:
      case K::Body:
      case K::NoteItem:
        break;

      case K::P:
        if (!inPara_) {
          if (mode_ == Mode::Title && !inCell() && firstBlockSeen_) return Verdict::DoneOk;
          inPara_ = true;
          beginParagraph();
        }
        break;

      case K::PPr:
        if (inPara_ && !decided_) inPPr_ = true;
        break;
      case K::NumPr:
        if (inPPr_) inNumPr_ = true;
        break;
      case K::PStyle:
      case K::OutlineLvl:
      case K::NumId:
      case K::Ilvl:
        if (inPPr_) props_.collect(k, xml_, inNumPr_);
        break;

      case K::R:
        f.savedRunFlags = runFlags_;
        runFlags_ = static_cast<uint16_t>(paraFlags_ & kRunFormatMask);
        break;
      case K::RPr:
        break;
      case K::RStyle:
      case K::Bold:
      case K::Italic:
      case K::Underline:
      case K::Strike:
      case K::SmallCaps:
      case K::VertAlign:
        // Only run properties (not the paragraph mark's rPr inside pPr).
        if (!inPPr_ && parent == K::RPr) applyRunProperty(k, xml_, runFlags_, info_);
        break;

      case K::T:
        inT_ = true;
        break;
      case K::Tab:
      case K::Br:
        separator();
        break;
      case K::NoBreakHyphen:
        emitLiteral("-");
        break;

      case K::Hyperlink: {
        const char* anchor = attrLocal(xml_, "anchor");
        if (anchor && *anchor && (kind_ == PKind::Normal || kind_ == PKind::List || kind_ == PKind::Quote)) {
          flushGap();
          openIfNeeded();
          sink_.onLinkBegin(anchor);
          f.linkEmitted = true;
        }
        break;
      }

      case K::FootnoteRef:
      case K::EndnoteRef: {
        const char* id = attrLocal(xml_, "id");
        if (id && *id && (kind_ == PKind::Normal || kind_ == PKind::List || kind_ == PKind::Quote)) {
          flushGap();
          openIfNeeded();
          sink_.onLinkBegin(std::string(k == K::FootnoteRef ? "fn" : "en") + id);
          sink_.onText(id, Fb2InlineStyle::Superscript);
          sink_.onLinkEnd();
          textStarted_ = true;
          haveLastRun_ = false;
        }
        break;
      }

      case K::Blip:
      case K::ImageData: {
        const char* rel = imageRelId(k, xml_);
        if (rel && info_ && info_->findImage(rel) && kind_ != PKind::Cell && kind_ != PKind::Muted &&
            kind_ != PKind::TitleHeading) {
          flushGap();
          sink_.onImage(rel);
          textStarted_ = true;
          haveLastRun_ = false;
        }
        break;
      }

      case K::Tbl:
        if (mode_ == Mode::Title) return Verdict::DoneFail;
        if (tblDepth_ == 0) {
          firstBlockSeen_ = true;
          sink_.onTableBegin();
        }
        ++tblDepth_;
        break;
      case K::Tr:
        if (tblDepth_ == 1) {
          sink_.onTableRowBegin();
          rowHeader_ = false;
        }
        break;
      case K::TblHeader:
        if (tblDepth_ == 1 && toggleOn(xml_)) rowHeader_ = true;
        break;
      case K::Tc:
        if (tblDepth_ == 1) {
          cellBuf_.clear();
          cellCollapse_.reset();
          cellAttrs_ = Fb2TableCellAttrs{};
          cellAttrs_.isHeader = rowHeader_;
          vMergeContinue_ = false;
          cellOpen_ = true;
        } else if (cellOpen_) {
          cellCollapse_.separate();  // nested table: flatten into the outer cell
        }
        break;
      case K::GridSpan:
        if (tblDepth_ == 1 && cellOpen_) {
          cellAttrs_.colspan = static_cast<uint16_t>(std::max<uint32_t>(1, std::min<uint32_t>(attrUint(xml_, "val", 1), UINT16_MAX)));
        }
        break;
      case K::VMerge:
        if (tblDepth_ == 1 && cellOpen_) {
          const char* v = attrLocal(xml_, "val");
          vMergeContinue_ = !v || std::string_view(v) != "restart";
        }
        break;
      case K::VAlign:
        if (tblDepth_ == 1 && cellOpen_) {
          const char* v = attrLocal(xml_, "val");
          if (v) {
            const std::string_view s(v);
            if (s == "center") cellAttrs_.valign = "middle";
            else if (s == "top" || s == "bottom") cellAttrs_.valign = std::string(s);
          }
        }
        break;
      default: break;
    }

    if (push) {
      if (stack_.size() >= kMaxNestingGuard) return Verdict::DoneFail;
      stack_.push_back(f);
    }
    return Verdict::Continue;
  }

  // A pending inter-word gap is emitted before a link / image opens, in the
  // plain style.
  void flushGap() {
    if (kind_ == PKind::Cell || kind_ == PKind::OwnTitle || kind_ == PKind::Muted || kind_ == PKind::None) return;
    if (collapse_.pending() && !collapse_.atStart() && textStarted_) {
      openIfNeeded();
      if (kind_ != PKind::TitleHeading) sink_.onText(" ", Fb2InlineStyle::Regular);
    }
    collapse_.clearPending();
  }

  void endParagraph() {
    if (kind_ == PKind::Cell) {
      cellCollapse_.separate();
    }
    closeParagraph();
    kind_ = PKind::None;
  }

  Verdict onEnd() {
    const Frame f = stack_.back();
    stack_.pop_back();
    switch (f.kind) {
      case K::P: {
        if (inPara_) {
          if (!decided_) {
            const Verdict v = decideParagraph();
            if (v != Verdict::Continue) return v;
          }
          endParagraph();
          inPara_ = false;
          if (mode_ == Mode::Title) return Verdict::DoneOk;
        }
        break;
      }
      case K::PPr:
        if (inPPr_) {
          inPPr_ = false;
          inNumPr_ = false;
          return decideParagraph();
        }
        break;
      case K::NumPr: inNumPr_ = false; break;
      case K::R: runFlags_ = f.savedRunFlags; break;
      case K::T: inT_ = false; break;
      case K::Hyperlink:
        if (f.linkEmitted) sink_.onLinkEnd();
        break;
      case K::Tc:
        if (tblDepth_ == 1 && cellOpen_) {
          cellOpen_ = false;
          sink_.onTableCell(vMergeContinue_ ? std::string() : cellBuf_, cellAttrs_);
          cellBuf_.clear();
        }
        break;
      case K::Tr:
        if (tblDepth_ == 1) sink_.onTableRowEnd();
        break;
      case K::Tbl:
        if (tblDepth_ > 0) --tblDepth_;
        if (tblDepth_ == 0) sink_.onTableEnd();
        break;
      default: break;
    }
    return Verdict::Continue;
  }

  XmlPullReader& xml_;
  Fb2ContentSink& sink_;
  const reader::ReaderCancellationToken* token_;
  const docx::PackageInfo* info_;
  Mode mode_;
  uint8_t titleLevel_;

  std::vector<Frame> stack_;
  int skipDepth_ = 0;

  // Paragraph state
  bool inPara_ = false;
  bool decided_ = false;
  bool inPPr_ = false;
  bool inNumPr_ = false;
  bool inT_ = false;
  bool opened_ = false;
  bool textStarted_ = false;
  bool titleOpen_ = false;
  bool titleEmitted_ = false;
  bool firstBlockSeen_ = false;
  PKind kind_ = PKind::None;
  ParaProps props_;
  uint16_t paraFlags_ = 0;
  uint16_t runFlags_ = 0;
  uint16_t lastRunFlags_ = 0;
  bool haveLastRun_ = false;
  TextCollapser collapse_;
  std::vector<ListCounter> counters_;

  // Table state (only the outermost table emits events)
  int tblDepth_ = 0;
  bool rowHeader_ = false;
  bool cellOpen_ = false;
  bool vMergeContinue_ = false;
  std::string cellBuf_;
  TextCollapser cellCollapse_;
  Fb2TableCellAttrs cellAttrs_;
};

}  // namespace

bool DocxParser::renderSection(IByteReader& reader, const Fb2SectionIndexEntry& section, Fb2ContentSink& sink,
                               const reader::ReaderCancellationToken* cancellationToken) {
  XmlPullReader xml(reader, 4096);
  xml.seekTo(section.innerStartOffset);
  // Body 0 is the document; the notes body (footnotes/endnotes) comes after it.
  const DocxRenderer::Mode mode = section.bodyIndex == 0 ? DocxRenderer::Mode::Chapter : DocxRenderer::Mode::Note;
  DocxRenderer renderer(xml, sink, cancellationToken, info_, mode);
  return renderer.run();
}

bool DocxParser::renderSectionTitle(IByteReader& reader, const Fb2SectionIndexEntry& section, Fb2ContentSink& sink,
                                    uint8_t level, const reader::ReaderCancellationToken* cancellationToken) {
  if (section.bodyIndex != 0) return false;  // notes have no title
  XmlPullReader xml(reader, 2048);
  xml.seekTo(section.innerStartOffset);
  DocxRenderer renderer(xml, sink, cancellationToken, info_, DocxRenderer::Mode::Title, level);
  return renderer.run();
}

bool DocxParser::decodeBinary(IByteReader& reader, const Fb2BinaryIndexEntry& binary, const BinaryOutputFn& out,
                              const reader::ReaderCancellationToken* cancellationToken) {
  if (cancelled(cancellationToken)) return false;
  return opc::extractEntryByOrdinal(reader, binary.payloadStartOffset,
                                    [&](const uint8_t* d, size_t n) { out(d, n); });
}
