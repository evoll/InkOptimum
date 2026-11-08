#include "Fb3Parser.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

#include <ReaderWork.h>

#include <XmlPullReader.h>
#include "OpcNames.h"
#include <ZipReader.h>

namespace {

// ---------------------------------------------------------------------------
// Element classification
// ---------------------------------------------------------------------------

enum class K : uint8_t {
  Other, Body, Section, Preamble, Notes, Notebody, Title, Subtitle, Annotation, Epigraph, Blockquote, Pre, Div,
  Poem, Stanza, Subscription, P, Ul, Ol, Li, Table, Tr, Td, Th, Note, A, Img, Br,
  Strong, Em, Underline, Strike, Sub, Sup, SmallCaps
};

std::string_view localName(const std::string& qname) {
  const size_t colon = qname.find_last_of(':');
  return colon == std::string::npos ? std::string_view(qname) : std::string_view(qname).substr(colon + 1);
}

K classify(std::string_view n) {
  switch (n.size()) {
    case 1:
      if (n == "p") return K::P;
      if (n == "a") return K::A;
      break;
    case 2:
      if (n == "em") return K::Em;
      if (n == "ol") return K::Ol;
      if (n == "ul") return K::Ul;
      if (n == "li") return K::Li;
      if (n == "tr") return K::Tr;
      if (n == "td") return K::Td;
      if (n == "th") return K::Th;
      if (n == "br") return K::Br;
      break;
    default: break;
  }
  if (n == "section") return K::Section;
  if (n == "preamble") return K::Preamble;
  if (n == "notes") return K::Notes;
  if (n == "notebody") return K::Notebody;
  if (n == "fb3-body") return K::Body;
  if (n == "title") return K::Title;
  if (n == "subtitle") return K::Subtitle;
  if (n == "annotation") return K::Annotation;
  if (n == "epigraph") return K::Epigraph;
  if (n == "blockquote") return K::Blockquote;
  if (n == "pre") return K::Pre;
  if (n == "div") return K::Div;
  if (n == "poem") return K::Poem;
  if (n == "stanza") return K::Stanza;
  if (n == "subscription") return K::Subscription;
  if (n == "table") return K::Table;
  if (n == "note") return K::Note;
  if (n == "img") return K::Img;
  if (n == "strong") return K::Strong;
  if (n == "underline") return K::Underline;
  if (n == "strikethrough") return K::Strike;
  if (n == "sub") return K::Sub;
  if (n == "sup") return K::Sup;
  if (n == "smallcaps") return K::SmallCaps;
  return K::Other;
}

bool isSectionLike(K k) { return k == K::Section || k == K::Preamble || k == K::Notebody; }

bool isFlowBlock(K k) {
  switch (k) {
    case K::Title: case K::Subtitle: case K::Annotation: case K::Epigraph: case K::Blockquote: case K::Pre:
    case K::Div: case K::Poem: case K::Stanza: case K::Subscription: case K::P: case K::Ul: case K::Ol:
    case K::Li: case K::Table: case K::Tr: case K::Notes:
      return true;
    default:
      return false;
  }
}

// ---------------------------------------------------------------------------
// Shared helpers (kept local so the FB2 parser stays untouched)
// ---------------------------------------------------------------------------

std::string stripHash(const char* v) {
  if (!v) return {};
  return (v[0] == '#') ? std::string(v + 1) : std::string(v);
}

// xlink:href regardless of the prefix the producer chose.
const char* hrefAttr(const XmlPullReader& xml) {
  if (const char* v = xml.attr("xlink:href")) return v;
  if (const char* v = xml.attr("l:href")) return v;
  if (const char* v = xml.attr("href")) return v;
  for (const XmlAttr& a : xml.attrs()) {
    if (localName(a.name) == "href") return a.value.c_str();
  }
  return nullptr;
}

std::string formatAuthorName(const Fb2Author& a) {
  std::string out;
  auto append = [&](const std::string& part) {
    if (part.empty()) return;
    if (!out.empty()) out.push_back(' ');
    out += part;
  };
  append(a.firstName);
  append(a.middleName);
  append(a.lastName);
  if (out.empty()) out = a.nickname;
  return out;
}

uint16_t parseSpan(const char* v) {
  if (!v) return 1;
  const long n = std::strtol(v, nullptr, 10);
  return n > 0 ? static_cast<uint16_t>(std::min<long>(n, UINT16_MAX)) : 1;
}

constexpr size_t kMaxAnnotationBytes = 4000;
constexpr size_t kMaxSectionIdBytes = 192;
constexpr size_t kMaxSectionTitleBytes = 384;
constexpr size_t kMaxBodyNameBytes = 80;
constexpr size_t kSectionStringPoolReserve = 28 * 1024;
constexpr size_t kSectionStringPoolHardLimit = 28 * 1024;
constexpr size_t kMaxNestingGuard = 4096;

bool storePoolCString(Fb2ScanResult& out, const char* value, size_t maxLen, uint32_t& offset, uint16_t& length) {
  offset = UINT32_MAX;
  length = 0;
  if (!value || out.stringPool.size() >= kSectionStringPoolHardLimit) return false;
  const size_t sourceLen = strnlen(value, maxLen);
  const size_t available = kSectionStringPoolHardLimit - out.stringPool.size();
  const size_t copyLen = std::min({sourceLen, available, static_cast<size_t>(UINT16_MAX)});
  if (copyLen == 0) return false;
  offset = static_cast<uint32_t>(out.stringPool.size());
  length = static_cast<uint16_t>(copyLen);
  out.stringPool.append(value, copyLen);
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
    if (c == ' ' || c == '\r' || c == '\n' || c == '\t') {
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

void appendBounded(std::string& dst, const std::string& src, size_t limit) {
  if (dst.size() >= limit || src.empty()) return;
  dst.append(src.data(), std::min(limit - dst.size(), src.size()));
}

bool isXmlSpace(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// Collapses whitespace runs to one space and drops leading whitespace at the
// start of a text container. State carries across successive XML text chunks.
// Returns the collapsed chunk (possibly empty).
std::string collapseChunk(const std::string& in, bool& atStart, bool& pendingSpace) {
  std::string out;
  out.reserve(in.size());
  for (const unsigned char c : in) {
    if (isXmlSpace(c)) {
      if (!atStart) pendingSpace = true;
      continue;
    }
    if (pendingSpace) {
      out.push_back(' ');
      pendingSpace = false;
    }
    out.push_back(static_cast<char>(c));
    atStart = false;
  }
  return out;
}

// Some producers fake strike-through with U+0336; convert to a real style run
// (same handling as the FB2 parser so both formats render alike).
void emitTextNormalizingCombiningStrike(Fb2ContentSink& sink, const std::string& text, Fb2InlineStyle baseStyle) {
  constexpr unsigned char kMark0 = 0xCC;
  constexpr unsigned char kMark1 = 0xB6;
  if (text.find("\xCC\xB6") == std::string::npos) {
    sink.onText(text, baseStyle);
    return;
  }
  auto isMarkAt = [&](size_t pos) {
    return pos + 1 < text.size() && static_cast<unsigned char>(text[pos]) == kMark0 &&
           static_cast<unsigned char>(text[pos + 1]) == kMark1;
  };
  auto codepointLenAt = [&](size_t pos) -> size_t {
    const unsigned char c = static_cast<unsigned char>(text[pos]);
    if ((c & 0x80u) == 0) return 1;
    if ((c & 0xE0u) == 0xC0u) return std::min<size_t>(2, text.size() - pos);
    if ((c & 0xF0u) == 0xE0u) return std::min<size_t>(3, text.size() - pos);
    if ((c & 0xF8u) == 0xF0u) return std::min<size_t>(4, text.size() - pos);
    return 1;
  };
  std::string run;
  run.reserve(text.size());
  bool runStrike = false, haveRun = false, markBeforeNext = false;
  auto flush = [&]() {
    if (run.empty()) return;
    sink.onText(run, runStrike ? (baseStyle | Fb2InlineStyle::Strikethrough) : baseStyle);
    run.clear();
  };
  for (size_t i = 0; i < text.size();) {
    if (isMarkAt(i)) {
      markBeforeNext = true;
      i += 2;
      continue;
    }
    const size_t cpLen = codepointLenAt(i);
    const bool markAfter = isMarkAt(i + cpLen);
    const bool struck = markBeforeNext || markAfter;
    markBeforeNext = false;
    if (!haveRun) {
      runStrike = struck;
      haveRun = true;
    } else if (runStrike != struck) {
      flush();
      runStrike = struck;
    }
    run.append(text, i, cpLen);
    i += cpLen;
    if (markAfter) i += 2;
  }
  flush();
}

struct StyleState {
  int bold = 0, italic = 0, underline = 0, strike = 0, sup = 0, sub = 0, smallCaps = 0;
  void apply(K k, int delta) {
    switch (k) {
      case K::Strong: bold += delta; break;
      case K::Em: italic += delta; break;
      case K::Underline: underline += delta; break;
      case K::Strike: strike += delta; break;
      case K::Sup: sup += delta; break;
      case K::Sub: sub += delta; break;
      case K::SmallCaps: smallCaps += delta; break;
      default: break;
    }
  }
  static bool isStyleKind(K k) {
    return k == K::Strong || k == K::Em || k == K::Underline || k == K::Strike || k == K::Sup || k == K::Sub ||
           k == K::SmallCaps;
  }
  Fb2InlineStyle current() const {
    Fb2InlineStyle s = Fb2InlineStyle::Regular;
    if (bold > 0) s = s | Fb2InlineStyle::Bold;
    if (italic > 0) s = s | Fb2InlineStyle::Italic;
    if (underline > 0) s = s | Fb2InlineStyle::Underline;
    if (strike > 0) s = s | Fb2InlineStyle::Strikethrough;
    if (sup > 0) s = s | Fb2InlineStyle::Superscript;
    if (sub > 0) s = s | Fb2InlineStyle::Subscript;
    if (smallCaps > 0) s = s | Fb2InlineStyle::SmallCaps;
    return s;
  }
};

bool cancelled(const reader::ReaderCancellationToken* token) {
  return token && token->isCancellationRequested();
}

// ---------------------------------------------------------------------------
// Description (metadata)
// ---------------------------------------------------------------------------

void parseDescription(const std::string& xmlText, Fb2Metadata& md) {
  if (xmlText.empty()) return;
  fb3::MemoryByteReader mem(xmlText);
  XmlPullReader xml(mem, 2048);

  std::string path;
  std::vector<size_t> lens;
  bool authorActive = false;
  Fb2Author cur;
  std::string* target = nullptr;
  bool titleSeen = false, langSeen = false, dateSeen = false, seqNameSeen = false;
  bool inAnnotation = false;
  bool annotationNeedsSep = false;
  std::string annotation;

  for (;;) {
    xml.setCaptureText(true);
    const XmlToken tok = xml.next();
    if (tok == XmlToken::Eof || tok == XmlToken::Error) break;

    if (tok == XmlToken::StartTag || tok == XmlToken::SelfClosing) {
      lens.push_back(path.size());
      path.push_back('/');
      path.append(localName(xml.name()));
      target = nullptr;
      if (path == "/fb3-description/title/main") {
        if (!titleSeen) { titleSeen = true; target = &md.title; }
      } else if (path == "/fb3-description/fb3-relations/subject") {
        const char* link = xml.attr("link");
        authorActive = link && std::strcmp(link, "author") == 0;
        cur = Fb2Author{};
      } else if (authorActive && path == "/fb3-description/fb3-relations/subject/first-name") {
        target = &cur.firstName;
      } else if (authorActive && path == "/fb3-description/fb3-relations/subject/middle-name") {
        target = &cur.middleName;
      } else if (authorActive && path == "/fb3-description/fb3-relations/subject/last-name") {
        target = &cur.lastName;
      } else if (authorActive && path == "/fb3-description/fb3-relations/subject/title/main") {
        target = &cur.nickname;
      } else if (path == "/fb3-description/lang") {
        if (!langSeen) { langSeen = true; target = &md.language; }
      } else if (path == "/fb3-description/written/date") {
        if (!dateSeen) {
          dateSeen = true;
          if (const char* v = xml.attr("value")) md.date = v;
          else target = &md.date;
        }
      } else if (path == "/fb3-description/sequence") {
        if (md.sequenceNumber == 0) {
          if (const char* n = xml.attr("number")) md.sequenceNumber = static_cast<uint32_t>(std::strtoul(n, nullptr, 10));
        }
      } else if (path == "/fb3-description/sequence/title/main") {
        if (!seqNameSeen) { seqNameSeen = true; target = &md.sequenceName; }
      } else if (path == "/fb3-description/annotation") {
        inAnnotation = true;
        annotation.clear();
        annotationNeedsSep = false;
      } else if (inAnnotation && path == "/fb3-description/annotation/p") {
        if (annotationNeedsSep) annotation += "\n\n";
        annotationNeedsSep = true;
      }
      if (inAnnotation) target = nullptr;
    } else if (tok == XmlToken::Text) {
      if (inAnnotation) {
        if (annotation.size() < kMaxAnnotationBytes) annotation += xml.text();
      } else if (target) {
        *target += xml.text();
      }
    } else if (tok == XmlToken::EndTag) {
      if (path == "/fb3-description/fb3-relations/subject" && authorActive) {
        authorActive = false;
        md.authors.push_back(cur);
        const std::string display = formatAuthorName(cur);
        if (!display.empty()) {
          if (!md.author.empty()) md.author += "; ";
          md.author += display;
        }
      } else if (path == "/fb3-description/annotation") {
        inAnnotation = false;
        md.annotationText = annotation;
      }
      target = nullptr;
      if (!lens.empty()) {
        path.resize(lens.back());
        lens.pop_back();
      }
    }
  }
}

}  // namespace

// ===========================================================================
// scan
// ===========================================================================

bool Fb3Parser::scan(IByteReader& reader, Fb2ScanResult& out, size_t xmlBufferSize) {
  out = Fb2ScanResult{};
  out.bodies.reserve(4);
  out.stringPool.reserve(reader.size() >= 8ULL * 1024 * 1024 ? kSectionStringPoolReserve : 8 * 1024);

  if (info_) {
    parseDescription(info_->descriptionXml, out.metadata);
    out.metadata.coverBinaryId = info_->coverBinaryId;
    for (const fb3::Image& img : info_->images) {
      Fb2BinaryIndexEntry b;
      b.id = img.relId;
      b.contentType = fb3::mimeFor(img.kind);
      b.payloadStartOffset = img.entryOrdinal;  // central-directory ordinal, not a byte offset
      b.payloadEndOffset = img.entryOrdinal;
      out.binaries.push_back(std::move(b));
    }
  }

  const size_t safeXmlBufferSize = std::max<size_t>(1024, std::min<size_t>(8 * 1024, xmlBufferSize));
  XmlPullReader xml(reader, safeXmlBufferSize);

  std::vector<K> stack;
  stack.reserve(32);
  // Indices into out.sections; -1 marks a self-closing (empty) section that
  // got no index entry but still has to balance its end tag.
  std::vector<int> sectionStack;
  int currentBody = -1;
  int notesBody = -1;
  bool inTitle = false;
  size_t titleDepth = 0;
  std::string titleBuf;
  bool titleNeedsSep = false;
  bool sawBody = false;

  auto innermost = [&]() -> Fb2SectionIndexEntry* {
    if (sectionStack.empty() || sectionStack.back() < 0) return nullptr;
    return &out.sections[static_cast<size_t>(sectionStack.back())];
  };

  for (;;) {
    xml.setCaptureText(inTitle);
    const XmlToken tok = xml.next();
    if (tok == XmlToken::Eof) break;
    if (tok == XmlToken::Error) return false;

    out.tokenCount++;
    if (tok == XmlToken::Text) {
      out.textTokenCount++;
      out.textPayloadBytes += xml.textSize();
    }

    if (tok == XmlToken::StartTag || tok == XmlToken::SelfClosing) {
      const K k = classify(localName(xml.name()));
      if (stack.size() >= kMaxNestingGuard) return false;
      stack.push_back(k);

      if (k == K::Body) {
        if (currentBody < 0) {
          out.bodies.push_back(Fb2BodyIndexEntry{});
          currentBody = static_cast<int>(out.bodies.size()) - 1;
          sectionStack.clear();
          sawBody = true;
        }
      } else if (k == K::Notes) {
        if (notesBody < 0) {
          Fb2BodyIndexEntry b;
          b.name.assign("notes", std::min<size_t>(5, kMaxBodyNameBytes));
          out.bodies.push_back(std::move(b));
          notesBody = static_cast<int>(out.bodies.size()) - 1;
        }
        currentBody = notesBody;
      } else if (isSectionLike(k) && currentBody >= 0) {
        if (tok == XmlToken::SelfClosing) {
          sectionStack.push_back(-1);  // empty section: nothing to render or index
        } else {
          Fb2SectionIndexEntry e;
          e.level = static_cast<uint16_t>(sectionStack.size());
          e.bodyIndex = static_cast<int16_t>(currentBody);
          if (const char* id = xml.attr("id")) storePoolCString(out, id, kMaxSectionIdBytes, e.idPoolOffset, e.idLength);
          e.innerStartOffset = xml.streamPos();
          out.sections.push_back(e);
          sectionStack.push_back(static_cast<int>(out.sections.size()) - 1);
        }
      } else if (k == K::Title && !sectionStack.empty() && stack.size() >= 2 && isSectionLike(stack[stack.size() - 2])) {
        // Only a section's DIRECT title names it (poem/quote titles do not).
        inTitle = true;
        titleDepth = stack.size();
        titleBuf.clear();
        titleNeedsSep = false;
      } else if ((k == K::P || k == K::Br) && inTitle) {
        if (titleNeedsSep) titleBuf += " ";
        titleNeedsSep = true;
      } else if (k == K::Img) {
        const char* src = xml.attr("src");
        if (src && *src) {
          if (Fb2SectionIndexEntry* s = innermost()) {
            if (s->imageRefCount < UINT16_MAX) s->imageRefCount++;
          }
        }
      }
    } else if (tok == XmlToken::Text) {
      if (inTitle) {
        appendBounded(titleBuf, xml.text(), kMaxSectionTitleBytes);
      } else if (Fb2SectionIndexEntry* s = innermost()) {
        s->approxTextBytes += static_cast<uint32_t>(xml.textSize());
      }
    } else if (tok == XmlToken::EndTag) {
      if (stack.empty()) continue;
      const K k = stack.back();
      stack.pop_back();
      if (k == K::Title && inTitle && stack.size() + 1 == titleDepth) {
        if (Fb2SectionIndexEntry* s = innermost()) {
          storePoolTitle(out, titleBuf, s->titlePoolOffset, s->titleLength);
        }
        inTitle = false;
      } else if (isSectionLike(k) && !sectionStack.empty()) {
        sectionStack.pop_back();
      } else if (k == K::Body) {
        currentBody = -1;
        sectionStack.clear();
      }
    }
  }
  return sawBody;
}

// ===========================================================================
// Cover / annotation
// ===========================================================================

bool Fb3Parser::renderCoverForFirstSection(IByteReader&, const Fb2SectionIndexEntry&, Fb2ContentSink& sink) {
  const std::string& id = coverId();
  if (id.empty()) return false;
  sink.onImage(id);
  return true;
}

bool Fb3Parser::renderAnnotation(IByteReader&, Fb2ContentSink& sink,
                                 const reader::ReaderCancellationToken* cancellationToken) {
  const std::string& desc = descriptionXml();
  if (desc.empty()) return true;  // no description: nothing to show, not an error
  fb3::MemoryByteReader mem(desc);
  XmlPullReader xml(mem, 2048);

  bool inAnnotation = false;
  int depth = 0;  // nesting relative to <annotation>
  bool paragraphOpen = false;
  bool atStart = true, pendingSpace = false;
  StyleState style;
  std::vector<K> kinds;

  for (;;) {
    if (cancelled(cancellationToken)) return false;
    const XmlToken tok = xml.next();
    if (tok == XmlToken::Eof) break;
    if (tok == XmlToken::Error) return paragraphOpen ? (sink.onParagraphEnd(), true) : true;
    const std::string_view name = localName(xml.name());

    if (tok == XmlToken::StartTag || tok == XmlToken::SelfClosing) {
      if (!inAnnotation) {
        if (name == "annotation") inAnnotation = true, depth = 0;
        continue;
      }
      ++depth;
      const K k = classify(name);
      kinds.push_back(k);
      if (k == K::P || k == K::Subtitle) {
        if (paragraphOpen) sink.onParagraphEnd();
        sink.onParagraphBegin();
        paragraphOpen = true;
        atStart = true;
        pendingSpace = false;
      } else if (k == K::Br) {
        if (paragraphOpen) { if (!atStart) pendingSpace = true; }
        else sink.onEmptyLine();
      } else {
        if (paragraphOpen && pendingSpace && !atStart) {
          sink.onText(" ", style.current());
          pendingSpace = false;
        }
        style.apply(k, +1);
      }
    } else if (tok == XmlToken::Text) {
      if (!inAnnotation || !paragraphOpen) continue;
      const std::string chunk = collapseChunk(xml.text(), atStart, pendingSpace);
      if (!chunk.empty()) emitTextNormalizingCombiningStrike(sink, chunk, style.current());
    } else if (tok == XmlToken::EndTag) {
      if (!inAnnotation) continue;
      if (depth == 0) {
        if (paragraphOpen) sink.onParagraphEnd();
        return true;
      }
      --depth;
      const K k = kinds.empty() ? K::Other : kinds.back();
      if (!kinds.empty()) kinds.pop_back();
      if (k == K::P || k == K::Subtitle) {
        if (paragraphOpen) sink.onParagraphEnd();
        paragraphOpen = false;
      } else {
        style.apply(k, -1);
      }
    }
  }
  if (paragraphOpen) sink.onParagraphEnd();
  return true;
}

// ===========================================================================
// Section rendering
// ===========================================================================

namespace {

// Renders the content of one section (or the body-level epigraph preamble)
// into a sink. One instance per call; no heap use beyond small vectors.
class SectionRenderer {
 public:
  enum class Mode { Section, BodyPreamble };

  SectionRenderer(XmlPullReader& xml, Fb2ContentSink& sink, const reader::ReaderCancellationToken* token, Mode mode)
      : xml_(xml), sink_(sink), token_(token), mode_(mode) {
    st_.reserve(24);
  }

  bool run() {
    for (;;) {
      if (cancelled(token_)) return false;
      const XmlToken tok = xml_.next();
      if (tok == XmlToken::Eof) {
        closeParagraph();
        return true;
      }
      if (tok == XmlToken::Error) return false;
      const std::string& qname = xml_.name();
      const K k = classify(localName(qname));

      if (tok == XmlToken::Text) {
        onText();
        continue;
      }
      const bool isStart = tok == XmlToken::StartTag || tok == XmlToken::SelfClosing;

      // Body-preamble mode first locates <fb3-body>, then only epigraphs
      // before the first section are rendered.
      if (mode_ == Mode::BodyPreamble) {
        const int r = preambleGate(isStart, k);
        if (r == 1) return true;
        if (r == 2) continue;
      }

      if (isStart) {
        if (skipSections_ > 0) {
          if (isSectionLike(k)) ++skipSections_;
          continue;
        }
        if (inDirectTitle_) {
          if (k == K::Img) emitImage();
          continue;
        }
        if (isSectionLike(k)) {
          skipSections_ = 1;  // nested section = separate spine entry
          continue;
        }
        if (k == K::Title && st_.empty() && mode_ == Mode::Section) {
          inDirectTitle_ = true;  // rendered by renderSectionTitle()
          continue;
        }
        onStart(k);
      } else {  // EndTag
        if (skipSections_ > 0) {
          if (isSectionLike(k)) --skipSections_;
          continue;
        }
        if (inDirectTitle_) {
          if (k == K::Title) inDirectTitle_ = false;
          continue;
        }
        if (st_.empty()) {
          if (mode_ == Mode::Section && isSectionLike(k)) {
            closeParagraph();
            return true;  // end of this section
          }
          continue;
        }
        onEnd();
      }
    }
  }

 private:
  struct Frame {
    K kind = K::Other;
    bool flat = false;        // emitted no events at start; emits none at end
    bool linkEmitted = false;
    bool cellOwner = false;
    bool listPushed = false;
    bool flatOwner = false;   // incremented flatDepth_
    enum class PMode : uint8_t { Normal, Verse, VerseStanza, Flat } pmode = PMode::Normal;
  };
  enum class Container : uint8_t { None, Paragraph, Verse, TextAuthor, Subtitle };
  struct ListState {
    bool ordered;
    uint16_t counter;
  };

  // Returns 0 = proceed, 1 = finished (return true), 2 = consumed (continue).
  int preambleGate(bool isStart, K k) {
    if (!inBody_) {
      if (isStart && k == K::Body) inBody_ = true;
      return 2;
    }
    if (st_.empty() && epigraphDepth_ == 0) {
      if (isStart && isSectionLike(k)) return 1;
      if (!isStart && k == K::Body) return 1;
      if (isStart && k != K::Epigraph) {
        ++bodyLevelSkip_;
        return 2;
      }
      if (!isStart && bodyLevelSkip_ > 0) {
        --bodyLevelSkip_;
        return 2;
      }
    }
    if (bodyLevelSkip_ > 0) {
      if (isStart) ++bodyLevelSkip_;
      else --bodyLevelSkip_;
      return 2;
    }
    return 0;
  }

  void emitImage() {
    if (const char* src = xml_.attr("src")) {
      if (*src) sink_.onImage(src);
    }
  }

  void closeParagraph() {
    if (container_ == Container::Paragraph) sink_.onParagraphEnd();
    if (container_ == Container::Paragraph) container_ = Container::None;
    lazyPara_ = false;
  }

  void markSeparator() {
    if (!atStart_) pendingSpace_ = true;
  }

  void resetText() {
    atStart_ = true;
    pendingSpace_ = false;
  }

  // A collapsed inter-word space belongs to the text AROUND an inline element,
  // not inside it: emit it (in the style active before the element opens) so an
  // underlined/linked/emphasised run never starts with a styled gap.
  void flushSpace() {
    if (pendingSpace_ && !atStart_ && container_ != Container::None) {
      sink_.onText(" ", style_.current());
    }
    pendingSpace_ = false;
  }

  void openLazyParagraph() {
    if (!lazyPara_) return;
    sink_.onParagraphBegin();
    container_ = Container::Paragraph;
    lazyPara_ = false;
  }

  bool flatContext() const { return cellDepth_ > 0 || flatDepth_ > 0 || mute_ > 0; }

  // Word boundary request that works both inside a table cell (plain-text
  // buffer) and inside an open text container (sink events).
  void separator() {
    if (cellDepth_ > 0) markCellSeparator();
    else markSeparator();
  }

  Frame::PMode paragraphMode(K parent) const {
    if (flatContext()) return Frame::PMode::Flat;
    if (parent == K::Stanza) return Frame::PMode::Verse;
    if (parent == K::Poem) return Frame::PMode::VerseStanza;
    return Frame::PMode::Normal;
  }

  void onStart(K k) {
    const K parent = st_.empty() ? K::Other : st_.back().kind;
    Frame f;
    f.kind = k;

    if (StyleState::isStyleKind(k)) {
      flushSpace();
      style_.apply(k, +1);
      st_.push_back(f);
      return;
    }

    // Everything block-ish inside a table cell / text-only container is text.
    if (flatContext() && (isFlowBlock(k) || k == K::Br || k == K::Td || k == K::Th)) {
      if (k == K::Td || k == K::Th) {
        ++cellDepth_;
        f.flat = true;
        st_.push_back(f);
        return;
      }
      f.flat = true;
      f.pmode = Frame::PMode::Flat;
      separator();
      st_.push_back(f);
      return;
    }

    switch (k) {
      case K::P: {
        f.pmode = paragraphMode(parent);
        if (f.pmode == Frame::PMode::Normal) {
          closeParagraph();
          lazyPara_ = true;  // opened on first visible content
          resetText();
        } else if (f.pmode == Frame::PMode::Verse) {
          sink_.onVerseBegin();
          container_ = Container::Verse;
          resetText();
        } else if (f.pmode == Frame::PMode::VerseStanza) {
          sink_.onStanzaBegin();
          sink_.onVerseBegin();
          container_ = Container::Verse;
          resetText();
        } else {
          f.flat = true;
          separator();
        }
        st_.push_back(f);
        break;
      }
      case K::Li: {
        closeParagraph();
        sink_.onParagraphBegin();
        container_ = Container::Paragraph;
        std::string marker;
        for (size_t i = 1; i < lists_.size(); ++i) marker += "\xC2\xA0\xC2\xA0";
        if (!lists_.empty() && lists_.back().ordered) {
          marker += std::to_string(++lists_.back().counter) + ".";
        } else {
          marker += "\xE2\x80\xA2";
        }
        marker += "\xC2\xA0";
        sink_.onText(marker, Fb2InlineStyle::Regular);
        resetText();
        ++flatDepth_;
        f.flatOwner = true;
        f.pmode = Frame::PMode::Flat;
        st_.push_back(f);
        break;
      }
      case K::Ul:
      case K::Ol:
        closeParagraph();
        lists_.push_back({k == K::Ol, 0});
        f.listPushed = true;
        st_.push_back(f);
        break;
      case K::Blockquote:
      case K::Pre:
        closeParagraph();
        sink_.onCiteBegin();
        st_.push_back(f);
        break;
      case K::Epigraph:
        closeParagraph();
        sink_.onEpigraphBegin();
        ++epigraphDepth_;
        st_.push_back(f);
        break;
      case K::Poem:
        closeParagraph();
        sink_.onPoemBegin();
        st_.push_back(f);
        break;
      case K::Stanza:
        closeParagraph();
        sink_.onStanzaBegin();
        st_.push_back(f);
        break;
      case K::Subscription:
        closeParagraph();
        sink_.onTextAuthorBegin();
        container_ = Container::TextAuthor;
        resetText();
        ++flatDepth_;
        f.flatOwner = true;
        f.pmode = Frame::PMode::Flat;
        st_.push_back(f);
        break;
      case K::Subtitle:
        closeParagraph();
        sink_.onSubtitleBegin();
        container_ = Container::Subtitle;
        resetText();
        ++flatDepth_;
        f.flatOwner = true;
        f.pmode = Frame::PMode::Flat;
        st_.push_back(f);
        break;
      case K::Title:
        // A title that is not the section's own heading (poem, quote, list or
        // table title) reads as a subtitle.
        if (parent == K::Table || parent == K::Tr) {
          f.flat = true;
          st_.push_back(f);
          ++mute_;
        } else {
          closeParagraph();
          sink_.onSubtitleBegin();
          container_ = Container::Subtitle;
          resetText();
          ++flatDepth_;
          f.flatOwner = true;
          f.pmode = Frame::PMode::Flat;
          st_.push_back(f);
        }
        break;
      case K::Table:
        closeParagraph();
        sink_.onTableBegin();
        st_.push_back(f);
        break;
      case K::Tr:
        sink_.onTableRowBegin();
        st_.push_back(f);
        break;
      case K::Td:
      case K::Th:
        ++cellDepth_;
        if (cellDepth_ == 1) {
          f.cellOwner = true;
          cellBuf_.clear();
          cellAttrs_ = Fb2TableCellAttrs{};
          cellAttrs_.isHeader = (k == K::Th);
          cellAttrs_.colspan = parseSpan(xml_.attr("colspan"));
          cellAttrs_.rowspan = parseSpan(xml_.attr("rowspan"));
          if (const char* a = xml_.attr("align")) cellAttrs_.align = a;
          if (const char* v = xml_.attr("valign")) cellAttrs_.valign = v;
          cellAtStart_ = true;
          cellPending_ = false;
        }
        st_.push_back(f);
        break;
      case K::Br:
        if (container_ != Container::None) {
          markSeparator();
        } else if (!lazyPara_ && parent != K::Stanza && parent != K::Poem) {
          sink_.onEmptyLine();
        }
        st_.push_back(f);
        break;
      case K::Img:
        flushSpace();
        emitImage();
        st_.push_back(f);
        break;
      case K::A:
      case K::Note: {
        const char* href = (k == K::Note) ? xml_.attr("href") : hrefAttr(xml_);
        if (cellDepth_ > 0) {
          st_.push_back(f);  // no link markup inside a cell's plain text
          break;
        }
        flushSpace();
        openLazyParagraph();
        sink_.onLinkBegin(stripHash(href));
        f.linkEmitted = true;
        st_.push_back(f);
        break;
      }
      default:
        st_.push_back(f);  // div, annotation, span, marker, ...: transparent
        break;
    }
  }

  void onEnd() {
    const Frame f = st_.back();
    st_.pop_back();
    const K k = f.kind;

    if (StyleState::isStyleKind(k)) {
      style_.apply(k, -1);
      return;
    }
    if (k == K::Td || k == K::Th) {
      --cellDepth_;
      if (f.cellOwner) {
        sink_.onTableCell(cellBuf_, cellAttrs_);
        cellBuf_.clear();
      }
      return;
    }
    if (f.flat) {
      if (k == K::Title && mute_ > 0) --mute_;
      separator();
      return;
    }
    switch (k) {
      case K::P:
        if (f.pmode == Frame::PMode::Normal) {
          if (container_ == Container::Paragraph) sink_.onParagraphEnd();
          container_ = Container::None;
          lazyPara_ = false;
        } else if (f.pmode == Frame::PMode::Verse) {
          sink_.onVerseEnd();
          container_ = Container::None;
        } else if (f.pmode == Frame::PMode::VerseStanza) {
          sink_.onVerseEnd();
          sink_.onStanzaEnd();
          container_ = Container::None;
        }
        break;
      case K::Li:
        if (container_ == Container::Paragraph) sink_.onParagraphEnd();
        container_ = Container::None;
        if (f.flatOwner) --flatDepth_;
        break;
      case K::Ul:
      case K::Ol:
        if (f.listPushed && !lists_.empty()) lists_.pop_back();
        break;
      case K::Blockquote:
      case K::Pre:
        closeParagraph();
        sink_.onCiteEnd();
        break;
      case K::Epigraph:
        closeParagraph();
        sink_.onEpigraphEnd();
        if (epigraphDepth_ > 0) --epigraphDepth_;
        if (mode_ == Mode::BodyPreamble && epigraphDepth_ == 0) { /* keep scanning for more */ }
        break;
      case K::Poem:
        sink_.onPoemEnd();
        break;
      case K::Stanza:
        sink_.onStanzaEnd();
        break;
      case K::Subscription:
        sink_.onTextAuthorEnd();
        container_ = Container::None;
        if (f.flatOwner) --flatDepth_;
        break;
      case K::Subtitle:
      case K::Title:
        sink_.onSubtitleEnd();
        container_ = Container::None;
        if (f.flatOwner) --flatDepth_;
        break;
      case K::Table:
        sink_.onTableEnd();
        break;
      case K::Tr:
        sink_.onTableRowEnd();
        break;
      case K::A:
      case K::Note:
        if (f.linkEmitted) sink_.onLinkEnd();
        break;
      default:
        break;
    }
  }

  void markCellSeparator() {
    if (!cellAtStart_) cellPending_ = true;
  }

  void onText() {
    if (inDirectTitle_ || skipSections_ > 0 || mute_ > 0) return;
    if (mode_ == Mode::BodyPreamble && !inBody_) return;
    if (cellDepth_ > 0) {
      const std::string chunk = collapseChunk(xml_.text(), cellAtStart_, cellPending_);
      if (!chunk.empty()) cellBuf_ += chunk;
      return;
    }
    if (container_ == Container::None && !lazyPara_) return;  // whitespace between blocks
    const std::string chunk = collapseChunk(xml_.text(), atStart_, pendingSpace_);
    if (chunk.empty()) return;
    openLazyParagraph();
    emitTextNormalizingCombiningStrike(sink_, chunk, style_.current());
  }

  XmlPullReader& xml_;
  Fb2ContentSink& sink_;
  const reader::ReaderCancellationToken* token_;
  Mode mode_;

  std::vector<Frame> st_;
  std::vector<ListState> lists_;
  StyleState style_;
  Container container_ = Container::None;
  bool lazyPara_ = false;
  bool atStart_ = true, pendingSpace_ = false;
  int skipSections_ = 0;
  bool inDirectTitle_ = false;
  int flatDepth_ = 0;
  int cellDepth_ = 0;
  int mute_ = 0;
  int epigraphDepth_ = 0;
  bool inBody_ = false;
  int bodyLevelSkip_ = 0;
  std::string cellBuf_;
  bool cellAtStart_ = true, cellPending_ = false;
  Fb2TableCellAttrs cellAttrs_;
};

}  // namespace

bool Fb3Parser::renderSection(IByteReader& reader, const Fb2SectionIndexEntry& section, Fb2ContentSink& sink,
                              const reader::ReaderCancellationToken* cancellationToken) {
  XmlPullReader xml(reader, 4096);
  xml.seekTo(section.innerStartOffset);
  SectionRenderer renderer(xml, sink, cancellationToken, SectionRenderer::Mode::Section);
  return renderer.run();
}

bool Fb3Parser::renderBodyPreambleForFirstSection(IByteReader& reader, Fb2ContentSink& sink,
                                                  const reader::ReaderCancellationToken* cancellationToken) {
  XmlPullReader xml(reader, 4096);
  xml.seekTo(0);
  SectionRenderer renderer(xml, sink, cancellationToken, SectionRenderer::Mode::BodyPreamble);
  return renderer.run();
}

bool Fb3Parser::renderSectionTitle(IByteReader& reader, const Fb2SectionIndexEntry& section, Fb2ContentSink& sink,
                                   uint8_t level, const reader::ReaderCancellationToken* cancellationToken) {
  XmlPullReader xml(reader, 2048);
  xml.seekTo(section.innerStartOffset);

  int depth = 0;  // nesting relative to the section's children
  bool inTitle = false;
  bool emitted = false;
  bool paragraphSeen = false;
  bool atStart = true, pendingSpace = false;
  StyleState style;
  std::vector<K> kinds;

  for (;;) {
    if (cancelled(cancellationToken)) return false;
    const XmlToken tok = xml.next();
    if (tok == XmlToken::Eof || tok == XmlToken::Error) return false;
    const K k = classify(localName(xml.name()));

    if (!inTitle) {
      if (tok == XmlToken::StartTag || tok == XmlToken::SelfClosing) {
        if (depth == 0) {
          if (k == K::Title) {
            inTitle = true;
            sink.onTitleBegin(level);
            kinds.clear();
            continue;
          }
          // Any other first block means the section has no heading of its own.
          if (isSectionLike(k) || isFlowBlock(k) || k == K::Img) return false;
        }
        ++depth;
      } else if (tok == XmlToken::EndTag) {
        if (depth == 0) return false;  // section ended before any title
        --depth;
      }
      continue;
    }

    if (tok == XmlToken::StartTag || tok == XmlToken::SelfClosing) {
      kinds.push_back(k);
      if (k == K::P || k == K::Br) {
        if (paragraphSeen) sink.onTitleLineBreak();
        paragraphSeen = true;
        atStart = true;
        pendingSpace = false;
      } else {
        if (pendingSpace && !atStart) {
          sink.onText(" ", style.current());
          pendingSpace = false;
        }
        style.apply(k, +1);
      }
    } else if (tok == XmlToken::Text) {
      const std::string chunk = collapseChunk(xml.text(), atStart, pendingSpace);
      if (!chunk.empty()) {
        emitTextNormalizingCombiningStrike(sink, chunk, style.current());
        emitted = true;
      }
    } else if (tok == XmlToken::EndTag) {
      if (kinds.empty()) {  // </title>
        sink.onTitleEnd(level);
        return emitted;
      }
      const K top = kinds.back();
      kinds.pop_back();
      if (top != K::P && top != K::Br) style.apply(top, -1);
    }
  }
}

// ===========================================================================
// Images
// ===========================================================================

bool Fb3Parser::decodeBinary(IByteReader& reader, const Fb2BinaryIndexEntry& binary, const BinaryOutputFn& out,
                             const reader::ReaderCancellationToken* cancellationToken) {
  if (cancelled(cancellationToken)) return false;
  return fb3::extractEntryByOrdinal(reader, binary.payloadStartOffset,
                                    [&](const uint8_t* d, size_t n) { out(d, n); });
}
