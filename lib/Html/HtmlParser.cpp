#include "HtmlParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <Base64Decoder.h>
#include <ReaderWork.h>

#include "HtmlEntities.h"
#include "HtmlTokenizer.h"

using html::HtmlAttr;
using html::HtmlToken;
using html::HtmlTokenizer;

namespace {

constexpr size_t kPoolHardLimit = 28 * 1024;
constexpr size_t kMaxTitleBytes = 384;
constexpr size_t kMaxIdBytes = 128;
constexpr size_t kMaxParagraphBytes = 64 * 1024;
constexpr size_t kMaxCellBytes = 4096;
constexpr size_t kMaxStyleFrames = 48;
constexpr size_t kMaxListDepth = 8;
constexpr int kMaxChapterHeading = 3;  // h1..h3 start chapters; deeper headings are subtitles

bool cancelled(const reader::ReaderCancellationToken* t) { return t && t->isCancellationRequested(); }

bool isWs(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

std::string lowerCopy(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string trimmed(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && isWs(s[b])) ++b;
  while (e > b && isWs(s[e - 1])) --e;
  return s.substr(b, e - b);
}

// h1..h6 -> 1..6, anything else 0.
int headingLevel(const std::string& name) {
  if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') return name[1] - '0';
  return 0;
}

bool isChapterHeading(const std::string& name) {
  const int l = headingLevel(name);
  return l >= 1 && l <= kMaxChapterHeading;
}

bool isSkippedSubtree(const std::string& n) {
  static const char* const kSkip[] = {"noscript", "svg", "math", "template", "iframe", "object", "canvas", "select", "audio", "video", "applet"};
  for (const char* v : kSkip) {
    if (n == v) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Which tokens are document content? Shared by scan and render so both agree
// on exactly which headings start chapters.
// ---------------------------------------------------------------------------

class ContentFilter {
 public:
  enum class Kind : uint8_t { Skipped, Head, Live };

  Kind classify(const HtmlToken& t) {
    if (skipDepth_ > 0) {
      if (t.type == HtmlToken::Type::StartTag && t.name == skipName_ && !t.selfClosing) ++skipDepth_;
      else if (t.type == HtmlToken::Type::EndTag && t.name == skipName_) --skipDepth_;
      return Kind::Skipped;
    }
    if (t.type == HtmlToken::Type::StartTag) {
      if (isSkippedSubtree(t.name)) {
        if (!t.selfClosing) {
          skipName_ = t.name;
          skipDepth_ = 1;
        }
        return Kind::Skipped;
      }
      if (t.name == "head") inHead_ = true;
      else if (t.name == "body") inHead_ = false;
      else if (t.name == "title") inTitle_ = true;
      if (t.name == "script" || t.name == "style") return Kind::Skipped;
    } else if (t.type == HtmlToken::Type::EndTag) {
      if (t.name == "script" || t.name == "style") return Kind::Skipped;
      if (t.name == "head") {
        inHead_ = false;
        return Kind::Head;
      }
      if (t.name == "title") {
        inTitle_ = false;
        return Kind::Head;
      }
    }
    if (inHead_ || inTitle_) return Kind::Head;
    if (t.type == HtmlToken::Type::StartTag && (t.name == "title" || t.name == "meta" || t.name == "link")) return Kind::Head;
    return Kind::Live;
  }

  bool inHead() const { return inHead_; }
  bool inTitle() const { return inTitle_; }

 private:
  bool inHead_ = false;
  bool inTitle_ = false;
  std::string skipName_;
  int skipDepth_ = 0;
};

// ---------------------------------------------------------------------------
// Images
// ---------------------------------------------------------------------------

struct ImageRef {
  char kind = 0;
  std::string contentType;
  uint64_t start = 0;
  uint64_t end = 0;
};

std::string mimeFromExtension(const std::string& pathLower) {
  auto ends = [&](const char* ext) {
    const size_t n = strlen(ext);
    return pathLower.size() >= n && pathLower.compare(pathLower.size() - n, n, ext) == 0;
  };
  if (ends(".jpg") || ends(".jpeg") || ends(".jpe")) return "image/jpeg";
  if (ends(".png")) return "image/png";
  if (ends(".gif")) return "image/gif";
  return std::string();
}

// "x-raw:START-END:mime" / "x-hex:START-END:mime"
bool parseRangeSource(const std::string& v, ImageRef& ref) {
  if (v.compare(0, 6, "x-raw:") != 0 && v.compare(0, 6, "x-hex:") != 0) return false;
  const char kind = v[2] == 'r' ? 'r' : 'h';
  size_t i = 6;
  auto number = [&](uint64_t& out) {
    if (i >= v.size() || !std::isdigit(static_cast<unsigned char>(v[i]))) return false;
    out = 0;
    while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) out = out * 10 + static_cast<uint64_t>(v[i++] - '0');
    return true;
  };
  uint64_t a = 0, b = 0;
  if (!number(a) || i >= v.size() || v[i] != '-') return false;
  ++i;
  if (!number(b) || b < a) return false;
  std::string mime;
  if (i < v.size() && v[i] == ':') mime = v.substr(i + 1);
  if (mime.empty()) mime = "image/jpeg";
  ref.kind = kind;
  ref.start = a;
  ref.end = b;
  ref.contentType = mime;
  return true;
}

// Classifies the value of an src-like attribute. Returns false for sources the
// reader cannot show (remote URLs, SVG, WebP, ...).
bool describeImageSource(const HtmlAttr& src, ImageRef& ref) {
  const std::string& v = src.value;
  if (v.empty()) return false;
  if (parseRangeSource(v, ref)) return true;
  if (v.compare(0, 5, "data:") == 0) {
    const size_t comma = v.find(',');
    if (comma == std::string::npos || comma > 128) return false;
    const std::string head = lowerCopy(v.substr(5, comma - 5));
    const size_t semi = head.find(';');
    const std::string mime = head.substr(0, semi);
    if (mime.compare(0, 6, "image/") != 0 || head.find("base64") == std::string::npos) return false;
    if (mime != "image/jpeg" && mime != "image/jpg" && mime != "image/png" && mime != "image/gif") return false;
    ref.kind = 'd';
    ref.contentType = mime;
    ref.start = src.valueStart + comma + 1;
    ref.end = src.valueEnd;
    return ref.end >= ref.start;
  }
  const size_t colon = v.find(':');
  const size_t slash = v.find('/');
  if (colon != std::string::npos && (slash == std::string::npos || colon < slash)) return false;  // http:, kindle:, ...
  std::string path = v.substr(0, v.find_first_of("?#"));
  const std::string mime = mimeFromExtension(lowerCopy(path));
  if (mime.empty()) return false;
  ref.kind = 'f';
  ref.contentType = mime;
  ref.start = src.valueStart;
  ref.end = std::min<uint64_t>(src.valueEnd, src.valueStart + 4096);
  return true;
}

std::string imageId(char kind, uint64_t tagStart) {
  static const char* hex = "0123456789abcdef";
  std::string id(1, kind);
  char tmp[17];
  int n = 0;
  uint64_t v = tagStart;
  do {
    tmp[n++] = hex[v & 0xF];
    v >>= 4;
  } while (v != 0);
  while (n > 0) id.push_back(tmp[--n]);
  return id;
}

// ---------------------------------------------------------------------------
// Inline style tracking (b, i, span style="...", ...)
// ---------------------------------------------------------------------------

bool hasClassToken(const std::string& classValue, const char* token) {
  const std::string v = lowerCopy(classValue);
  size_t i = 0;
  const size_t n = strlen(token);
  while (i < v.size()) {
    while (i < v.size() && isWs(v[i])) ++i;
    size_t j = i;
    while (j < v.size() && !isWs(v[j])) ++j;
    if (j - i == n && v.compare(i, n, token) == 0) return true;
    i = j;
  }
  return false;
}

Fb2InlineStyle cssStyleOf(const std::string& styleAttr, const std::string& classAttr) {
  Fb2InlineStyle s = Fb2InlineStyle::Regular;
  if (!classAttr.empty() &&
      (hasClassToken(classAttr, "smallcaps") || hasClassToken(classAttr, "small-caps") || hasClassToken(classAttr, "sc"))) {
    s = s | Fb2InlineStyle::SmallCaps;
  }
  if (styleAttr.empty()) return s;
  const std::string css = lowerCopy(styleAttr);
  size_t pos = 0;
  while (pos < css.size()) {
    size_t end = css.find(';', pos);
    if (end == std::string::npos) end = css.size();
    const std::string decl = css.substr(pos, end - pos);
    pos = end + 1;
    const size_t colon = decl.find(':');
    if (colon == std::string::npos) continue;
    const std::string prop = trimmed(decl.substr(0, colon));
    const std::string val = trimmed(decl.substr(colon + 1));
    if (prop == "font-weight") {
      if (val == "bold" || val == "bolder" || (val.size() == 3 && val[0] >= '6' && val[0] <= '9')) s = s | Fb2InlineStyle::Bold;
    } else if (prop == "font-style") {
      if (val == "italic" || val == "oblique") s = s | Fb2InlineStyle::Italic;
    } else if (prop == "text-decoration" || prop == "text-decoration-line") {
      if (val.find("underline") != std::string::npos) s = s | Fb2InlineStyle::Underline;
      if (val.find("line-through") != std::string::npos) s = s | Fb2InlineStyle::Strikethrough;
    } else if (prop == "font-variant" || prop == "font-variant-caps") {
      if (val.find("small-caps") != std::string::npos) s = s | Fb2InlineStyle::SmallCaps;
    } else if (prop == "vertical-align") {
      if (val == "super") s = s | Fb2InlineStyle::Superscript;
      else if (val == "sub") s = s | Fb2InlineStyle::Subscript;
    }
  }
  return s;
}

bool inlineTagStyle(const std::string& n, Fb2InlineStyle& out) {
  struct Entry {
    const char* name;
    Fb2InlineStyle style;
  };
  static const Entry kTags[] = {
      {"b", Fb2InlineStyle::Bold},          {"strong", Fb2InlineStyle::Bold},
      {"i", Fb2InlineStyle::Italic},        {"em", Fb2InlineStyle::Italic},
      {"cite", Fb2InlineStyle::Italic},     {"var", Fb2InlineStyle::Italic},
      {"dfn", Fb2InlineStyle::Italic},      {"u", Fb2InlineStyle::Underline},
      {"ins", Fb2InlineStyle::Underline},   {"s", Fb2InlineStyle::Strikethrough},
      {"strike", Fb2InlineStyle::Strikethrough}, {"del", Fb2InlineStyle::Strikethrough},
      {"sup", Fb2InlineStyle::Superscript}, {"sub", Fb2InlineStyle::Subscript},
      {"span", Fb2InlineStyle::Regular},    {"font", Fb2InlineStyle::Regular},
      {"small", Fb2InlineStyle::Regular},   {"big", Fb2InlineStyle::Regular},
      {"mark", Fb2InlineStyle::Regular},    {"abbr", Fb2InlineStyle::Regular},
      {"acronym", Fb2InlineStyle::Regular}, {"code", Fb2InlineStyle::Regular},
      {"tt", Fb2InlineStyle::Regular},      {"kbd", Fb2InlineStyle::Regular},
      {"samp", Fb2InlineStyle::Regular},    {"q", Fb2InlineStyle::Regular},
      {"label", Fb2InlineStyle::Regular},   {"time", Fb2InlineStyle::Regular},
      {"a", Fb2InlineStyle::Regular},
  };
  for (const Entry& e : kTags) {
    if (n == e.name) {
      out = e.style;
      return true;
    }
  }
  return false;
}

class StyleTracker {
 public:
  // Called for every start tag; pushes a frame for inline elements.
  void start(const HtmlToken& t) {
    Fb2InlineStyle added;
    if (!inlineTagStyle(t.name, added)) return;
    if (t.selfClosing) return;
    const std::string* style = t.attrValue("style");
    const std::string* cls = t.attrValue("class");
    added = added | cssStyleOf(style ? *style : std::string(), cls ? *cls : std::string());
    if (frames_.size() >= kMaxStyleFrames) return;
    frames_.push_back({t.name, added});
    current_ = current_ | added;
  }

  void end(const std::string& name) {
    Fb2InlineStyle dummy;
    if (!inlineTagStyle(name, dummy)) return;
    // Pop to the nearest matching frame (tolerates mis-nested markup) but never
    // reach far down the stack for a stray end tag.
    const size_t limit = frames_.size() > 8 ? frames_.size() - 8 : 0;
    for (size_t i = frames_.size(); i > limit; --i) {
      if (frames_[i - 1].name == name) {
        frames_.resize(i - 1);
        recompute();
        return;
      }
    }
  }

  Fb2InlineStyle current() const { return current_; }

 private:
  struct Frame {
    std::string name;
    Fb2InlineStyle added;
  };
  void recompute() {
    current_ = Fb2InlineStyle::Regular;
    for (const Frame& f : frames_) current_ = current_ | f.added;
  }
  std::vector<Frame> frames_;
  Fb2InlineStyle current_ = Fb2InlineStyle::Regular;
};

// ---------------------------------------------------------------------------
// Fragment links
// ---------------------------------------------------------------------------

// "#target" -> "target"; anything else (other files, URLs) -> empty.
std::string fragmentTarget(const std::string& href) {
  if (href.size() > 1 && href[0] == '#') return href.substr(1);
  return std::string();
}

// ---------------------------------------------------------------------------
// Pool strings
// ---------------------------------------------------------------------------

bool storePoolString(Fb2ScanResult& out, const std::string& value, size_t maxBytes, bool collapse, uint32_t& offset,
                     uint16_t& length) {
  offset = UINT32_MAX;
  length = 0;
  if (value.empty() || out.stringPool.size() >= kPoolHardLimit) return false;
  const size_t start = out.stringPool.size();
  bool pendingSpace = false;
  size_t taken = 0;
  for (const unsigned char c : value) {
    if (taken >= maxBytes || out.stringPool.size() >= kPoolHardLimit) break;
    if (collapse && isWs(static_cast<char>(c))) {
      pendingSpace = out.stringPool.size() > start;
      continue;
    }
    if (pendingSpace) out.stringPool.push_back(' ');
    pendingSpace = false;
    out.stringPool.push_back(static_cast<char>(c));
    ++taken;
  }
  const size_t stored = out.stringPool.size() - start;
  if (stored == 0) return false;
  offset = static_cast<uint32_t>(start);
  length = static_cast<uint16_t>(std::min<size_t>(stored, UINT16_MAX));
  return true;
}

bool isBlankText(const std::string& s) {
  for (size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (isWs(static_cast<char>(c))) continue;
    if (c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA0) {
      ++i;
      continue;
    }
    return false;
  }
  return true;
}

std::string metaName(const HtmlToken& t) {
  for (const char* key : {"name", "property", "itemprop"}) {
    if (const std::string* v = t.attrValue(key)) return lowerCopy(*v);
  }
  return std::string();
}

}  // namespace

// ===========================================================================
// scan
// ===========================================================================

bool HtmlParser::scan(IByteReader& reader, Fb2ScanResult& out, size_t) {
  out = Fb2ScanResult{};
  out.bodies.push_back(Fb2BodyIndexEntry{});
  out.stringPool.reserve(8 * 1024);

  HtmlTokenizer tokenizer(reader, 0);
  ContentFilter filter;
  HtmlToken tok;

  std::vector<int> headingStack;  // html levels of the open chapters
  bool haveSection = false;
  bool capturing = false;  // inside a chapter heading
  int captureLevel = 0;
  std::string captureText;
  std::string captureId;
  std::string anchorId;  // last standalone id seen
  bool anchorFresh = false;
  std::string titleText;  // <title>
  std::string metaTitle;
  bool titleDone = false;
  bool titleAny = false;
  size_t sectionsBeforeHeading = 0;

  auto addSection = [&](uint64_t offset, uint16_t level) -> Fb2SectionIndexEntry& {
    Fb2SectionIndexEntry e;
    e.level = level;
    e.bodyIndex = 0;
    e.innerStartOffset = static_cast<uint32_t>(std::min<uint64_t>(offset, UINT32_MAX));
    out.sections.push_back(e);
    haveSection = true;
    return out.sections.back();
  };

  auto addImage = [&](const ImageRef& ref, uint64_t tagStart) {
    Fb2BinaryIndexEntry b;
    b.id = imageId(ref.kind, tagStart);
    b.contentType = ref.contentType;
    b.payloadStartOffset = static_cast<uint32_t>(std::min<uint64_t>(ref.start, UINT32_MAX));
    b.payloadEndOffset = static_cast<uint32_t>(std::min<uint64_t>(ref.end, UINT32_MAX));
    out.binaries.push_back(std::move(b));
    return out.binaries.back().id;
  };

  while (tokenizer.next(tok)) {
    out.tokenCount++;
    const ContentFilter::Kind kind = filter.classify(tok);
    if (kind == ContentFilter::Kind::Skipped) continue;

    if (kind == ContentFilter::Kind::Head) {
      if (tok.type == HtmlToken::Type::Text) {
        if (filter.inTitle() && !titleDone) titleText += tok.text;
      } else if (tok.type == HtmlToken::Type::EndTag && tok.name == "title") {
        titleDone = !trimmed(titleText).empty();
        titleAny = true;
      } else if (tok.type == HtmlToken::Type::StartTag && tok.name == "meta") {
        const std::string name = metaName(tok);
        const HtmlAttr* content = tok.attr("content");
        if (content && !content->value.empty()) {
          const std::string& v = content->value;
          if (name == "author" || name == "dc.creator" || name == "creator" || name == "dc:creator") {
            if (out.metadata.author.empty()) {
              out.metadata.author = trimmed(v);
              Fb2Author a;
              a.nickname = out.metadata.author;
              out.metadata.authors.push_back(a);
            }
          } else if (name == "description" || name == "dc.description") {
            if (out.metadata.annotationText.empty()) out.metadata.annotationText = trimmed(v);
          } else if (name == "language" || name == "dc.language" || name == "dc:language") {
            if (out.metadata.language.empty()) out.metadata.language = trimmed(v);
          } else if (name == "date" || name == "dc.date" || name == "dcterms.date" || name == "article:published_time") {
            if (out.metadata.date.empty()) out.metadata.date = trimmed(v);
          } else if ((name == "title" || name == "dc.title" || name == "og:title") && metaTitle.empty()) {
            metaTitle = trimmed(v);
          } else if (name == "cover" && out.metadata.coverBinaryId.empty()) {
            ImageRef ref;
            if (describeImageSource(*content, ref)) out.metadata.coverBinaryId = addImage(ref, tok.start);
          }
        }
      }
      continue;
    }

    // ---- live content ----
    if (tok.type == HtmlToken::Type::StartTag) {
      if (tok.name == "html") {
        const std::string* lang = tok.attrValue("lang");
        if (!lang) lang = tok.attrValue("xml:lang");
        if (lang && out.metadata.language.empty()) out.metadata.language = trimmed(*lang);
        continue;
      }
      if (isChapterHeading(tok.name)) {
        const int level = headingLevel(tok.name);
        while (!headingStack.empty() && headingStack.back() >= level) headingStack.pop_back();
        Fb2SectionIndexEntry& s = addSection(tok.start, static_cast<uint16_t>(headingStack.size()));
        headingStack.push_back(level);
        capturing = true;
        captureLevel = level;
        captureText.clear();
        captureId.clear();
        if (const std::string* id = tok.attrValue("id")) captureId = *id;
        if (captureId.empty() && anchorFresh) captureId = anchorId;
        sectionsBeforeHeading = out.sections.size();
        (void)s;
        anchorFresh = false;
        continue;
      }
      // standalone ids (anchors) just before a heading, or inside one
      const std::string* id = tok.attrValue("id");
      if (!id && tok.name == "a") id = tok.attrValue("name");
      if (id && !id->empty()) {
        if (capturing && captureId.empty()) captureId = *id;
        anchorId = *id;
        anchorFresh = true;
      }
      if (tok.name == "img") {
        const HtmlAttr* src = tok.attr("src");
        ImageRef ref;
        if (src && describeImageSource(*src, ref)) {
          addImage(ref, tok.start);
          if (!capturing) {
            if (!haveSection) addSection(0, 0);
            Fb2SectionIndexEntry& s = out.sections.back();
            if (s.imageRefCount < UINT16_MAX) ++s.imageRefCount;
          }
        }
        continue;
      }
      continue;
    }

    if (tok.type == HtmlToken::Type::EndTag) {
      if (capturing && headingLevel(tok.name) == captureLevel) {
        capturing = false;
        if (!out.sections.empty()) {
          Fb2SectionIndexEntry& s = out.sections.back();
          storePoolString(out, trimmed(captureText), kMaxTitleBytes, true, s.titlePoolOffset, s.titleLength);
          if (!captureId.empty()) storePoolString(out, captureId, kMaxIdBytes, false, s.idPoolOffset, s.idLength);
          if (captureLevel == 1 && out.metadata.title.empty() && titleText.empty() && metaTitle.empty()) {
            out.metadata.title = trimmed(captureText);
          }
        }
        (void)sectionsBeforeHeading;
      }
      continue;
    }

    // text
    if (capturing) {
      captureText += tok.text;
      continue;
    }
    if (isBlankText(tok.text)) continue;
    anchorFresh = false;
    if (!haveSection) addSection(0, 0);  // untitled opening chapter
    Fb2SectionIndexEntry& s = out.sections.back();
    s.approxTextBytes += static_cast<uint32_t>(std::min<size_t>(tok.text.size(), UINT32_MAX / 2));
    out.textTokenCount++;
    out.textPayloadBytes += tok.text.size();
  }
  (void)titleAny;

  if (out.metadata.title.empty()) {
    const std::string t = trimmed(titleText);
    out.metadata.title = !t.empty() ? t : metaTitle;
  }
  if (!haveSection) addSection(0, 0);  // empty document: one empty chapter, not a failure
  return true;
}

// ===========================================================================
// render
// ===========================================================================

namespace {

// Many converted books (MOBI/AZW3 with NCX-injected headings, FB2 exports) repeat the chapter title
// as the first paragraph(s) of the chapter body, so it would be shown twice. This sink drops the
// leading paragraphs of a section when, together, they spell exactly the chapter heading that
// renderSectionTitle() already shows. Anything else is passed through unchanged and unbuffered
// after the decision, so the cost is at most a few leading paragraphs.
class LeadingTitleFilter final : public Fb2ContentSink {
 public:
  LeadingTitleFilter(Fb2ContentSink& out, const std::string& headingText) : out_(out), head_(normalise(headingText)) {
    state_ = head_.empty() ? State::Done : State::Idle;
  }

  void onParagraphBegin() override {
    if (state_ == State::Done) return out_.onParagraphBegin();
    if (state_ == State::Idle || state_ == State::BetweenParas) {
      state_ = State::InPara;
      paras_.emplace_back();
      return;
    }
    flushAndPass();
    out_.onParagraphBegin();
  }
  void onText(const std::string& text, Fb2InlineStyle style) override {
    if (state_ == State::InPara) {
      paras_.back().push_back({text, style});
      return;
    }
    if (state_ != State::Done) flushAndPass();
    out_.onText(text, style);
  }
  void onParagraphEnd() override {
    if (state_ != State::InPara) {
      if (state_ != State::Done) flushAndPass();
      return out_.onParagraphEnd();
    }
    for (const auto& piece : paras_.back()) acc_ += normalise(piece.text);
    if (acc_ == head_) {  // the buffered paragraphs repeat the heading: drop them
      paras_.clear();
      state_ = State::Done;
    } else if (head_.compare(0, acc_.size(), acc_) == 0 && paras_.size() < kMaxParas) {
      state_ = State::BetweenParas;
    } else {
      flushAndPass();
    }
  }

  // Every other event ends the leading-title window and is forwarded untouched.
  void onSubtitle(const std::string& t) override { pass(), out_.onSubtitle(t); }
  void onSubtitleBegin() override { pass(), out_.onSubtitleBegin(); }
  void onSubtitleEnd() override { pass(), out_.onSubtitleEnd(); }
  void onTitleBegin(uint8_t l) override { pass(), out_.onTitleBegin(l); }
  void onTitleEnd(uint8_t l) override { pass(), out_.onTitleEnd(l); }
  void onTitleLineBreak() override { pass(), out_.onTitleLineBreak(); }
  void onEmptyLine() override { pass(), out_.onEmptyLine(); }
  void onHorizontalRule() override { pass(), out_.onHorizontalRule(); }
  void onPoemBegin() override { pass(), out_.onPoemBegin(); }
  void onPoemEnd() override { pass(), out_.onPoemEnd(); }
  void onStanzaBegin() override { pass(), out_.onStanzaBegin(); }
  void onStanzaEnd() override { pass(), out_.onStanzaEnd(); }
  void onVerseLine(const std::string& t) override { pass(), out_.onVerseLine(t); }
  void onVerseBegin() override { pass(), out_.onVerseBegin(); }
  void onVerseEnd() override { pass(), out_.onVerseEnd(); }
  void onCiteBegin() override { pass(), out_.onCiteBegin(); }
  void onCiteEnd() override { pass(), out_.onCiteEnd(); }
  void onEpigraphBegin() override { pass(), out_.onEpigraphBegin(); }
  void onEpigraphEnd() override { pass(), out_.onEpigraphEnd(); }
  void onTextAuthor(const std::string& t) override { pass(), out_.onTextAuthor(t); }
  void onTextAuthorBegin() override { pass(), out_.onTextAuthorBegin(); }
  void onTextAuthorEnd() override { pass(), out_.onTextAuthorEnd(); }
  void onImage(const std::string& id) override { pass(), out_.onImage(id); }
  void onLinkBegin(const std::string& t) override { pass(), out_.onLinkBegin(t); }
  void onLinkEnd() override { pass(), out_.onLinkEnd(); }
  void onTableBegin() override { pass(), out_.onTableBegin(); }
  void onTableEnd() override { pass(), out_.onTableEnd(); }
  void onTableRowBegin() override { pass(), out_.onTableRowBegin(); }
  void onTableCell(const std::string& t, const Fb2TableCellAttrs& a) override { pass(), out_.onTableCell(t, a); }
  void onTableRowEnd() override { pass(), out_.onTableRowEnd(); }

  // Called at the end of the section: a still-undecided buffer is real content.
  void finish() { pass(); }

 private:
  enum class State { Idle, InPara, BetweenParas, Done };
  struct Piece {
    std::string text;
    Fb2InlineStyle style;
  };
  static constexpr size_t kMaxParas = 4;

  // Letters and digits only, ASCII-lowercased: ignores spacing, NBSP and punctuation differences.
  static std::string normalise(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
      const unsigned char c = static_cast<unsigned char>(s[i]);
      if (c >= 0x80) {
        // NBSP (C2 A0) is whitespace; every other multi-byte sequence is kept as-is.
        if (c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA0) {
          ++i;
          continue;
        }
        r.push_back(static_cast<char>(c));
      } else if (std::isalnum(c)) {
        r.push_back(static_cast<char>(std::tolower(c)));
      }
    }
    return r;
  }

  void pass() {
    if (state_ != State::Done) flushAndPass();
  }
  void flushAndPass() {
    for (const auto& para : paras_) {
      out_.onParagraphBegin();
      for (const auto& piece : para) out_.onText(piece.text, piece.style);
      out_.onParagraphEnd();
    }
    paras_.clear();
    state_ = State::Done;
  }

  Fb2ContentSink& out_;
  std::string head_;
  std::string acc_;
  std::vector<std::vector<Piece>> paras_;
  State state_ = State::Done;
};

class Renderer {
 public:
  explicit Renderer(Fb2ContentSink& sink) : sink_(sink) {}

  void handle(const HtmlToken& t) {
    if (t.type == HtmlToken::Type::Text) {
      addText(t.text);
    } else if (t.type == HtmlToken::Type::StartTag) {
      startTag(t);
    } else if (t.type == HtmlToken::Type::EndTag) {
      endTag(t);
    }
  }

  void finish() {
    closeParagraph();
    closeCell();
    closeRow();
    if (tableOpen_) sink_.onTableEnd();
    tableOpen_ = false;
    tableDepth_ = 0;
    if (inPre_) sink_.onCiteEnd();
    inPre_ = 0;
    while (quoteDepth_ > 0) {
      sink_.onCiteEnd();
      --quoteDepth_;
    }
  }

 private:
  struct ListState {
    bool ordered = false;
    int counter = 0;
  };

  // ---- text flow ----

  void flushRun() {
    if (run_.empty()) return;
    sink_.onText(run_, runStyle_);
    run_.clear();
  }

  void setStyleBoundary() {
    if (!run_.empty() && runStyle_ != styles_.current()) flushRun();
  }

  void openParagraph() {
    if (paraOpen_) return;
    if (subtitle_) {
      sink_.onSubtitleBegin();
    } else {
      sink_.onParagraphBegin();
    }
    paraOpen_ = true;
    lastSpace_ = true;
    brPending_ = false;
    paraBytes_ = 0;
    if (!pendingMarker_.empty()) {
      sink_.onText(pendingMarker_, Fb2InlineStyle::Regular);
      pendingMarker_.clear();
    }
  }

  void closeParagraph() {
    if (!paraOpen_) return;
    while (!run_.empty() && (run_.back() == ' ')) run_.pop_back();
    flushRun();
    if (linkOpen_) {
      sink_.onLinkEnd();
      linkOpen_ = false;
    }
    if (subtitle_) {
      sink_.onSubtitleEnd();
    } else {
      sink_.onParagraphEnd();
    }
    paraOpen_ = false;
  }

  void emitLinkIfPending() {
    if (!pendingLink_.empty() && !linkOpen_ && paraOpen_) {
      flushRun();
      sink_.onLinkBegin(pendingLink_);
      linkOpen_ = true;
    }
  }

  void appendRun(const char* data, size_t len) {
    if (len == 0) return;
    const Fb2InlineStyle style = styles_.current() | (subtitle_ ? Fb2InlineStyle::Bold : Fb2InlineStyle::Regular);
    if (!run_.empty() && runStyle_ != style) flushRun();
    runStyle_ = style;
    run_.append(data, len);
    paraBytes_ += len;
  }

  void addText(const std::string& text) {
    if (tableDepth_ > 0) {
      if (inCell_) {
        for (size_t i = 0; i < text.size(); ++i) {
          const char c = text[i];
          if (isWs(c)) {
            if (!cellText_.empty() && cellText_.back() != ' ') cellText_.push_back(' ');
          } else if (cellText_.size() < kMaxCellBytes) {
            cellText_.push_back(c);
          }
        }
        return;
      }
      if (tableOpen_) return;  // stray text between table elements
    }
    if (inPre_ > 0) {
      addPreText(text);
      return;
    }
    // collapse whitespace
    std::string collapsed;
    collapsed.reserve(text.size());
    bool space = lastSpace_ || !paraOpen_;
    for (const char c : text) {
      if (isWs(c)) {
        if (!space) {
          collapsed.push_back(' ');
          space = true;
        }
      } else {
        collapsed.push_back(c);
        space = false;
      }
    }
    if (collapsed.empty()) return;
    if (!paraOpen_) {
      if (collapsed == " ") return;
      if (isBlankText(collapsed)) return;
      openParagraph();
      // text may begin with the collapsed leading space: drop it at paragraph start
      if (collapsed[0] == ' ') collapsed.erase(0, 1);
      if (collapsed.empty()) return;
    }
    emitLinkIfPending();
    if (paraBytes_ + collapsed.size() > kMaxParagraphBytes) {
      // split an enormous paragraph rather than truncating it
      closeParagraph();
      openParagraph();
    }
    appendRun(collapsed.data(), collapsed.size());
    lastSpace_ = (collapsed.back() == ' ');
  }

  void addPreText(const std::string& text) {
    size_t i = 0;
    while (i <= text.size()) {
      size_t nl = text.find('\n', i);
      const bool last = nl == std::string::npos;
      std::string line = text.substr(i, last ? std::string::npos : nl - i);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      preLine_ += line;
      if (last) break;
      flushPreLine();
      i = nl + 1;
    }
  }

  void flushPreLine() {
    if (preLine_.empty()) {
      if (preHadLine_) sink_.onEmptyLine();
    } else {
      std::string indented;
      size_t j = 0;
      for (; j < preLine_.size() && (preLine_[j] == ' ' || preLine_[j] == '\t'); ++j) {
        indented += preLine_[j] == '\t' ? "\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0" : "\xC2\xA0";
      }
      indented += preLine_.substr(j);
      sink_.onParagraphBegin();
      sink_.onText(indented, Fb2InlineStyle::Regular);
      sink_.onParagraphEnd();
    }
    preHadLine_ = true;
    preLine_.clear();
  }

  // ---- tags ----

  std::string indentFor(size_t depth) const {
    std::string s;
    for (size_t i = 1; i < depth; ++i) s += "\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0";
    return s;
  }

  void startTag(const HtmlToken& t) {
    const std::string& n = t.name;
    const int hl = headingLevel(n);
    if (hl >= 4) {
      closeParagraph();
      subtitle_ = true;
      styles_.start(t);
      return;
    }
    if (n == "p" || n == "div" || n == "section" || n == "article" || n == "header" || n == "footer" || n == "aside" ||
        n == "nav" || n == "address" || n == "center" || n == "figure" || n == "figcaption" || n == "main" ||
        n == "form" || n == "fieldset" || n == "dd" || n == "details" || n == "summary" || n == "caption") {
      if (tableDepth_ > 0 && (inCell_ || tableOpen_)) {
        addText(" ");
        return;
      }
      closeParagraph();
      return;
    }
    if (n == "dt") {
      closeParagraph();
      HtmlToken bold;
      bold.type = HtmlToken::Type::StartTag;
      bold.name = "b";
      styles_.start(bold);
      return;
    }
    if (n == "ul" || n == "ol") {
      closeParagraph();
      ListState ls;
      ls.ordered = n == "ol";
      if (ls.ordered) {
        if (const std::string* st = t.attrValue("start")) ls.counter = std::atoi(st->c_str()) - 1;
      }
      if (lists_.size() < kMaxListDepth) lists_.push_back(ls);
      else ++overflowLists_;
      return;
    }
    if (n == "li") {
      closeParagraph();
      std::string marker = indentFor(lists_.size());
      if (!lists_.empty()) {
        ListState& ls = lists_.back();
        if (ls.ordered) {
          if (const std::string* v = t.attrValue("value")) ls.counter = std::atoi(v->c_str());
          else ++ls.counter;
          marker += std::to_string(ls.counter) + ".";
        } else {
          marker += "\xE2\x80\xA2";  // •
        }
      } else {
        marker += "\xE2\x80\xA2";
      }
      marker += "\xC2\xA0";
      pendingMarker_ = marker;
      return;
    }
    if (n == "blockquote") {
      closeParagraph();
      if (quoteDepth_++ == 0) sink_.onCiteBegin();
      return;
    }
    if (n == "pre") {
      closeParagraph();
      if (inPre_++ == 0) {
        sink_.onCiteBegin();
        preLine_.clear();
        preHadLine_ = false;
      }
      return;
    }
    if (n == "hr") {
      closeParagraph();
      sink_.onHorizontalRule();
      return;
    }
    if (n == "br") {
      if (tableDepth_ > 0) {
        addText(" ");
        return;
      }
      if (inPre_ > 0) {
        flushPreLine();
        return;
      }
      if (paraOpen_) {
        closeParagraph();
        brPending_ = true;
      } else if (brPending_) {
        sink_.onEmptyLine();
      } else {
        brPending_ = true;
      }
      return;
    }
    if (n == "table") {
      closeParagraph();
      ++tableDepth_;
      return;
    }
    if (n == "tr") {
      if (tableDepth_ != 1) {
        if (tableDepth_ > 1) addText(" ");
        return;
      }
      closeCell();
      closeRow();
      if (!tableOpen_) {
        sink_.onTableBegin();
        tableOpen_ = true;
      }
      sink_.onTableRowBegin();
      rowOpen_ = true;
      return;
    }
    if (n == "td" || n == "th") {
      if (tableDepth_ != 1) {
        if (tableDepth_ > 1) addText(" ");
        return;
      }
      if (!rowOpen_) {  // td without tr
        if (!tableOpen_) {
          sink_.onTableBegin();
          tableOpen_ = true;
        }
        sink_.onTableRowBegin();
        rowOpen_ = true;
      }
      closeCell();
      cellAttrs_ = Fb2TableCellAttrs();
      cellAttrs_.isHeader = n == "th";
      if (const std::string* v = t.attrValue("colspan")) cellAttrs_.colspan = clampSpan(std::atoi(v->c_str()));
      if (const std::string* v = t.attrValue("rowspan")) cellAttrs_.rowspan = clampSpan(std::atoi(v->c_str()));
      cellAttrs_.align = alignOf(t);
      if (const std::string* v = t.attrValue("valign")) cellAttrs_.valign = lowerCopy(*v);
      inCell_ = true;
      cellText_.clear();
      return;
    }
    if (n == "img") {
      if (tableDepth_ > 0) return;
      const HtmlAttr* src = t.attr("src");
      ImageRef ref;
      if (src && describeImageSource(*src, ref)) {
        closeParagraph();
        sink_.onImage(imageId(ref.kind, t.start));
      }
      return;
    }
    if (n == "a") {
      styles_.start(t);
      const std::string* href = t.attrValue("href");
      pendingLink_ = href ? fragmentTarget(*href) : std::string();
      return;
    }
    styles_.start(t);
  }

  void endTag(const HtmlToken& t) {
    const std::string& n = t.name;
    const int hl = headingLevel(n);
    if (hl >= 4) {
      closeParagraph();
      subtitle_ = false;
      styles_.end(n);
      return;
    }
    if (n == "p" || n == "div" || n == "section" || n == "article" || n == "header" || n == "footer" || n == "aside" ||
        n == "nav" || n == "address" || n == "center" || n == "figure" || n == "figcaption" || n == "main" ||
        n == "form" || n == "fieldset" || n == "dd" || n == "details" || n == "summary" || n == "caption") {
      if (tableDepth_ > 0 && (inCell_ || tableOpen_)) {
        addText(" ");
        return;
      }
      closeParagraph();
      return;
    }
    if (n == "dt") {
      closeParagraph();
      styles_.end("b");
      return;
    }
    if (n == "ul" || n == "ol") {
      closeParagraph();
      if (overflowLists_ > 0) --overflowLists_;
      else if (!lists_.empty()) lists_.pop_back();
      pendingMarker_.clear();
      return;
    }
    if (n == "li") {
      closeParagraph();
      pendingMarker_.clear();
      return;
    }
    if (n == "blockquote") {
      closeParagraph();
      if (quoteDepth_ > 0 && --quoteDepth_ == 0) sink_.onCiteEnd();
      return;
    }
    if (n == "pre") {
      if (inPre_ > 0 && --inPre_ == 0) {
        if (!preLine_.empty()) flushPreLine();
        sink_.onCiteEnd();
      }
      return;
    }
    if (n == "table") {
      if (tableDepth_ == 1) {
        closeCell();
        closeRow();
        if (tableOpen_) sink_.onTableEnd();
        tableOpen_ = false;
      }
      if (tableDepth_ > 0) --tableDepth_;
      return;
    }
    if (n == "tr") {
      if (tableDepth_ == 1) {
        closeCell();
        closeRow();
      }
      return;
    }
    if (n == "td" || n == "th") {
      if (tableDepth_ == 1) closeCell();
      else if (tableDepth_ > 1) addText(" ");
      return;
    }
    if (n == "a") {
      if (linkOpen_) {
        flushRun();
        sink_.onLinkEnd();
        linkOpen_ = false;
      }
      pendingLink_.clear();
      styles_.end(n);
      return;
    }
    styles_.end(n);
  }

  static uint16_t clampSpan(int v) { return static_cast<uint16_t>(v < 1 ? 1 : (v > 99 ? 99 : v)); }

  static std::string alignOf(const HtmlToken& t) {
    if (const std::string* a = t.attrValue("align")) return lowerCopy(trimmed(*a));
    if (const std::string* st = t.attrValue("style")) {
      const std::string css = lowerCopy(*st);
      const size_t p = css.find("text-align");
      if (p != std::string::npos) {
        const size_t colon = css.find(':', p);
        if (colon != std::string::npos) {
          size_t e = css.find(';', colon);
          return trimmed(css.substr(colon + 1, e == std::string::npos ? std::string::npos : e - colon - 1));
        }
      }
    }
    return std::string();
  }

  void closeCell() {
    if (!inCell_) return;
    sink_.onTableCell(trimmed(cellText_), cellAttrs_);
    inCell_ = false;
    cellText_.clear();
  }

  void closeRow() {
    if (!rowOpen_) return;
    sink_.onTableRowEnd();
    rowOpen_ = false;
  }

  Fb2ContentSink& sink_;
  StyleTracker styles_;
  std::string run_;
  Fb2InlineStyle runStyle_ = Fb2InlineStyle::Regular;
  bool paraOpen_ = false;
  bool lastSpace_ = true;
  bool brPending_ = false;
  bool subtitle_ = false;
  size_t paraBytes_ = 0;
  std::string pendingMarker_;
  std::string pendingLink_;
  bool linkOpen_ = false;
  std::vector<ListState> lists_;
  int overflowLists_ = 0;
  int quoteDepth_ = 0;
  int inPre_ = 0;
  std::string preLine_;
  bool preHadLine_ = false;
  int tableDepth_ = 0;
  bool tableOpen_ = false;
  bool rowOpen_ = false;
  bool inCell_ = false;
  std::string cellText_;
  Fb2TableCellAttrs cellAttrs_;
};

}  // namespace

bool HtmlParser::renderSection(IByteReader& reader, const Fb2SectionIndexEntry& section, Fb2ContentSink& sink,
                               const reader::ReaderCancellationToken* token) {
  HtmlTokenizer tokenizer(reader, section.innerStartOffset);
  ContentFilter filter;
  // Created lazily once the chapter's own heading has been read, so that a repeated title
  // paragraph can be recognised (see LeadingTitleFilter).
  std::unique_ptr<LeadingTitleFilter> titleFilter;
  std::unique_ptr<Renderer> rendererPtr(new Renderer(sink));
  Renderer* renderer = rendererPtr.get();
  HtmlToken tok;
  bool first = true;
  int skipLevel = 0;  // inside this chapter's own heading
  std::string headingText;
  while (tokenizer.next(tok)) {
    if (cancelled(token)) return false;
    const ContentFilter::Kind kind = filter.classify(tok);
    if (kind != ContentFilter::Kind::Live) {
      continue;
    }
    if (skipLevel != 0) {  // swallow the chapter's own heading (rendered by renderSectionTitle)
      if (tok.type == HtmlToken::Type::Text && headingText.size() < 512) {
        headingText += tok.text;
        headingText += ' ';
      }
      if (tok.type == HtmlToken::Type::EndTag && headingLevel(tok.name) == skipLevel) {
        skipLevel = 0;
        titleFilter.reset(new LeadingTitleFilter(sink, headingText));
        rendererPtr.reset(new Renderer(*titleFilter));
        renderer = rendererPtr.get();
      }
      continue;
    }
    if (tok.type == HtmlToken::Type::StartTag && isChapterHeading(tok.name)) {
      if (first) {
        first = false;
        skipLevel = headingLevel(tok.name);
        continue;
      }
      break;  // the next chapter starts here
    }
    if (tok.type == HtmlToken::Type::StartTag && tok.name == "html") continue;
    first = false;
    renderer->handle(tok);
  }
  renderer->finish();
  if (titleFilter) titleFilter->finish();
  return true;
}

bool HtmlParser::renderSectionTitle(IByteReader& reader, const Fb2SectionIndexEntry& section, Fb2ContentSink& sink,
                                    uint8_t level, const reader::ReaderCancellationToken* token) {
  HtmlTokenizer tokenizer(reader, section.innerStartOffset);
  ContentFilter filter;
  HtmlToken tok;
  // The chapter must start with its own h1-h3 tag (the untitled opening chapter does not).
  if (!tokenizer.next(tok) || filter.classify(tok) != ContentFilter::Kind::Live ||
      tok.type != HtmlToken::Type::StartTag || !isChapterHeading(tok.name)) {
    return false;
  }
  const int headLevel = headingLevel(tok.name);
  struct Piece {
    std::string text;
    Fb2InlineStyle style;
    bool lineBreak;
  };
  std::vector<Piece> pieces;
  StyleTracker styles;
  bool any = false;
  bool lastSpace = true;
  size_t total = 0;
  while (tokenizer.next(tok)) {
    if (cancelled(token)) return false;
    const ContentFilter::Kind kind = filter.classify(tok);
    if (kind != ContentFilter::Kind::Live) continue;
    if (tok.type == HtmlToken::Type::EndTag) {
      if (headingLevel(tok.name) == headLevel) break;
      styles.end(tok.name);
    } else if (tok.type == HtmlToken::Type::StartTag) {
      if (isChapterHeading(tok.name)) break;  // unterminated heading
      if (tok.name == "br") {
        if (any) pieces.push_back({std::string(), Fb2InlineStyle::Regular, true});
        lastSpace = true;
      } else {
        styles.start(tok);
      }
    } else {
      std::string collapsed;
      for (const char c : tok.text) {
        if (isWs(c)) {
          if (!lastSpace) {
            collapsed.push_back(' ');
            lastSpace = true;
          }
        } else {
          collapsed.push_back(c);
          lastSpace = false;
        }
      }
      if (collapsed.empty()) continue;
      if (!isBlankText(collapsed)) any = true;
      total += collapsed.size();
      if (total > 4096) break;
      pieces.push_back({std::move(collapsed), styles.current(), false});
    }
  }
  if (!any) return false;
  // trim the final trailing space
  for (size_t i = pieces.size(); i > 0; --i) {
    Piece& p = pieces[i - 1];
    if (p.lineBreak) continue;
    while (!p.text.empty() && p.text.back() == ' ') p.text.pop_back();
    break;
  }
  sink.onTitleBegin(level);
  for (const Piece& p : pieces) {
    if (p.lineBreak) sink.onTitleLineBreak();
    else if (!p.text.empty()) sink.onText(p.text, p.style);
  }
  sink.onTitleEnd(level);
  return true;
}

// ===========================================================================
// binaries
// ===========================================================================

namespace {

std::string percentDecode(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
        std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      out.push_back(static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16)));
      i += 2;
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

int hexValue(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

bool HtmlParser::decodeBinary(IByteReader& reader, const Fb2BinaryIndexEntry& b, const BinaryOutputFn& out,
                              const reader::ReaderCancellationToken* token) {
  if (b.id.empty() || b.payloadEndOffset < b.payloadStartOffset) return false;
  const char kind = b.id[0];
  const uint64_t start = b.payloadStartOffset;
  const uint64_t end = b.payloadEndOffset;
  if (!reader.seek(start)) return false;

  if (kind == 'f') {
    if (!externalLoader_ || end - start > 4096) return false;
    std::string raw(static_cast<size_t>(end - start), '\0');
    if (reader.read(raw.data(), raw.size()) != raw.size()) return false;
    std::string path = percentDecode(html::decodeEntities(raw));
    const size_t cut = path.find_first_of("?#");
    if (cut != std::string::npos) path.resize(cut);
    return externalLoader_(path, out);
  }

  uint8_t buf[512];
  uint64_t remaining = end - start;
  if (kind == 'r') {
    while (remaining > 0) {
      if (cancelled(token)) return false;
      const size_t want = static_cast<size_t>(std::min<uint64_t>(remaining, sizeof(buf)));
      const size_t got = reader.read(buf, want);
      if (got == 0) return false;
      out(buf, got);
      remaining -= got;
    }
    return true;
  }
  if (kind == 'd') {
    Base64Decoder decoder(out);
    while (remaining > 0) {
      if (cancelled(token)) return false;
      const size_t want = static_cast<size_t>(std::min<uint64_t>(remaining, sizeof(buf)));
      const size_t got = reader.read(buf, want);
      if (got == 0) return false;
      decoder.feed(reinterpret_cast<const char*>(buf), got);
      remaining -= got;
    }
    decoder.finish();
    return true;
  }
  if (kind == 'h') {
    int high = -1;
    uint8_t outBuf[256];
    size_t outLen = 0;
    while (remaining > 0) {
      if (cancelled(token)) return false;
      const size_t want = static_cast<size_t>(std::min<uint64_t>(remaining, sizeof(buf)));
      const size_t got = reader.read(buf, want);
      if (got == 0) return false;
      for (size_t i = 0; i < got; ++i) {
        const int v = hexValue(buf[i]);
        if (v < 0) continue;  // whitespace / line breaks
        if (high < 0) {
          high = v;
        } else {
          outBuf[outLen++] = static_cast<uint8_t>((high << 4) | v);
          high = -1;
          if (outLen == sizeof(outBuf)) {
            out(outBuf, outLen);
            outLen = 0;
          }
        }
      }
      remaining -= got;
    }
    if (outLen > 0) out(outBuf, outLen);
    return true;
  }
  return false;
}
