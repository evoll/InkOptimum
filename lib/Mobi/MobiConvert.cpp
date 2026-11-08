// MobiConvert.cpp - raw Mobipocket / KF8 markup -> normalised HTML (see MobiBook.h).
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "MobiBook.h"
#include "MobiFormat.h"
#include "MobiIndex.h"

namespace mobi {
namespace {

constexpr uint32_t kNone = 0xFFFFFFFFu;
constexpr size_t kMaxTargets = 8192;
constexpr size_t kMaxNcx = 2048;
constexpr size_t kMaxSkeletons = 20000;
constexpr size_t kMaxFragments = 40000;
constexpr size_t kMaxTagBytes = 16 * 1024;
constexpr size_t kMaxLabelBytes = 160;

struct Skeleton {
  uint32_t pos, length, fragments;
};
struct Fragment {
  uint32_t insertPos, length;
};
struct NcxEntry {
  uint32_t pos;
  uint8_t level;
  std::string label;
};
struct Insertion {
  uint32_t pos;
  int32_t heading;  // index into the NCX list; -1 = link anchor
};

std::string escapeHtml(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (const char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out.push_back(c);
    }
  }
  return out;
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

std::string lower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

// Buffers output and forwards it to the caller's sink.
struct Writer {
  TextSink sink;
  std::string buf;
  bool failed = false;

  void put(const char* s, size_t n) {
    if (failed) return;
    buf.append(s, n);
    if (buf.size() >= 4096) flush();
  }
  void put(const std::string& s) { put(s.data(), s.size()); }
  void put(const char* s) { put(s, strlen(s)); }
  void flush() {
    if (failed || buf.empty()) return;
    if (!sink(buf.data(), buf.size())) failed = true;
    buf.clear();
  }
};

struct Attr {
  std::string name;
  std::string value;
};

// Incremental tag-level rewriter. In collect mode it only gathers link targets
// and counts headings; in emit mode it writes the normalised HTML.
class Rewriter {
 public:
  Rewriter(Book& book, const std::vector<Fragment>& frags, bool collect, Writer& out)
      : book_(book), frags_(frags), collect_(collect), out_(out) {}

  void setPos(uint32_t pos) { pos_ = pos; }
  void partBreak() {
    if (!collect_ && !suppressed()) out_.put("\n");
  }

  // Emit-mode inputs.
  const std::vector<Insertion>* insertions = nullptr;
  const std::vector<NcxEntry>* ncx = nullptr;
  // Collect-mode outputs.
  std::vector<uint32_t> targets;
  uint32_t headingTags = 0;

  bool feed(const uint8_t* d, size_t n);

 private:
  bool suppressed() const { return headSkip_ || svgDepth_ > 0 || !rawSkip_.empty(); }
  void drain();
  void textByte(uint8_t c);
  void finishTag();
  void parseTag(std::string& name, bool& closing, bool& selfClosing, std::vector<Attr>& attrs) const;
  bool kindlePos(const std::string& href, uint32_t& pos) const;
  void emitImage(const std::vector<Attr>& attrs);
  void emitAnchor(const std::vector<Attr>& attrs, bool selfClosing);
  void emitGeneric(const std::string& name, bool closing, bool selfClosing, const std::vector<Attr>& attrs);
  std::string convert(const std::string& s) const {
    std::string out;
    book_.appendText(out, s.data(), s.size());
    return out;
  }

  Book& book_;
  const std::vector<Fragment>& frags_;
  const bool collect_;
  Writer& out_;

  uint32_t pos_ = 0;
  size_t insIdx_ = 0;
  bool inTag_ = false;
  bool comment_ = false;
  char quote_ = 0;
  std::string tag_;
  bool headSkip_ = false;
  int svgDepth_ = 0;
  std::string rawSkip_;
};

void Rewriter::drain() {
  if (collect_ || !insertions || suppressed()) return;
  while (insIdx_ < insertions->size() && (*insertions)[insIdx_].pos <= pos_) {
    const Insertion& ins = (*insertions)[insIdx_++];
    if (ins.heading < 0) {
      char buf[48];
      const int n = snprintf(buf, sizeof(buf), "<a id=\"p%u\"></a>", ins.pos);
      out_.put(buf, static_cast<size_t>(n));
    } else {
      const NcxEntry& e = (*ncx)[static_cast<size_t>(ins.heading)];
      const int level = std::min<int>(3, e.level + 1);
      char open[8];
      snprintf(open, sizeof(open), "<h%d>", level);
      out_.put(open);
      out_.put(escapeHtml(e.label));
      char close[8];
      snprintf(close, sizeof(close), "</h%d>\n", level);
      out_.put(close);
    }
  }
}

void Rewriter::textByte(uint8_t c) {
  if (collect_ || suppressed()) return;
  if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') return;
  if (c < 0x80) {
    out_.put(reinterpret_cast<const char*>(&c), 1);
  } else {
    std::string s;
    book_.appendText(s, reinterpret_cast<const char*>(&c), 1);
    out_.put(s);
  }
}

bool Rewriter::feed(const uint8_t* d, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (!inTag_) drain();
    const uint8_t c = d[i];
    if (inTag_) {
      tag_.push_back(static_cast<char>(c));
      if (comment_) {
        if (c == '>' && tag_.size() >= 7 && tag_.compare(tag_.size() - 3, 3, "-->") == 0) {
          inTag_ = false;
          comment_ = false;
          tag_.clear();
        } else if (tag_.size() > 65536) {
          inTag_ = false;
          comment_ = false;
          tag_.clear();
        }
      } else if (quote_) {
        if (c == static_cast<uint8_t>(quote_)) quote_ = 0;
      } else if (c == '"' || c == '\'') {
        size_t k = tag_.size() - 1;
        while (k > 0 && isSpace(tag_[k - 1])) --k;
        if (k > 0 && tag_[k - 1] == '=') quote_ = static_cast<char>(c);
      } else if (c == '>') {
        finishTag();
      } else if (tag_.size() == 4 && tag_ == "<!--") {
        comment_ = true;
      } else if (tag_.size() > kMaxTagBytes) {
        inTag_ = false;
        tag_.clear();
      }
    } else if (c == '<') {
      inTag_ = true;
      quote_ = 0;
      comment_ = false;
      tag_.assign(1, '<');
    } else {
      textByte(c);
    }
    ++pos_;
  }
  return !out_.failed;
}

void Rewriter::parseTag(std::string& name, bool& closing, bool& selfClosing, std::vector<Attr>& attrs) const {
  const std::string& t = tag_;
  size_t i = 1;
  closing = false;
  selfClosing = false;
  if (i < t.size() && t[i] == '/') {
    closing = true;
    ++i;
  }
  const size_t nameStart = i;
  while (i < t.size() && !isSpace(t[i]) && t[i] != '/' && t[i] != '>') ++i;
  name = lower(t.substr(nameStart, i - nameStart));
  const size_t end = t.size() - 1;  // the closing '>'
  while (i < end) {
    while (i < end && (isSpace(t[i]) || t[i] == '/')) {
      if (t[i] == '/' && i + 1 == end) selfClosing = true;
      ++i;
    }
    if (i >= end) break;
    const size_t an = i;
    while (i < end && !isSpace(t[i]) && t[i] != '=' && t[i] != '/') ++i;
    Attr a;
    a.name = lower(t.substr(an, i - an));
    while (i < end && isSpace(t[i])) ++i;
    if (i < end && t[i] == '=') {
      ++i;
      while (i < end && isSpace(t[i])) ++i;
      if (i < end && (t[i] == '"' || t[i] == '\'')) {
        const char q = t[i++];
        const size_t vs = i;
        while (i < end && t[i] != q) ++i;
        a.value = t.substr(vs, i - vs);
        if (i < end) ++i;
      } else {
        const size_t vs = i;
        while (i < end && !isSpace(t[i])) ++i;
        size_t ve = i;
        if (ve > vs && t[ve - 1] == '/' && ve == end) {
          selfClosing = true;
          --ve;
        }
        a.value = t.substr(vs, ve - vs);
      }
    }
    if (!a.name.empty()) attrs.push_back(std::move(a));
  }
}

bool Rewriter::kindlePos(const std::string& href, uint32_t& pos) const {
  static const char kPrefix[] = "kindle:pos:fid:";
  if (href.compare(0, sizeof(kPrefix) - 1, kPrefix) != 0) return false;
  const char* p = href.data() + sizeof(kPrefix) - 1;
  const char* end = href.data() + href.size();
  const char* stop = nullptr;
  const uint32_t fid = parseBase32(p, end, &stop);
  if (end - stop < 5 || memcmp(stop, ":off:", 5) != 0) return false;
  const uint32_t off = parseBase32(stop + 5, end);
  if (fid >= frags_.size()) return false;
  pos = frags_[fid].insertPos + off;
  return true;
}

void Rewriter::emitImage(const std::vector<Attr>& attrs) {
  uint32_t number = 0;
  std::string alt;
  for (const Attr& a : attrs) {
    if (a.name == "recindex") {
      number = static_cast<uint32_t>(strtoul(a.value.c_str(), nullptr, 10));
    } else if ((a.name == "src" || a.name == "xlink:href" || a.name == "href") &&
               a.value.compare(0, 13, "kindle:embed:") == 0) {
      number = parseBase32(a.value.data() + 13, a.value.data() + a.value.size());
    } else if (a.name == "alt") {
      alt = a.value;
    }
  }
  uint64_t start = 0, end = 0;
  const char* mime = nullptr;
  if (!book_.imageRange(number, start, end, mime)) return;
  char buf[96];
  const int n = snprintf(buf, sizeof(buf), "<img src=\"x-raw:%llu-%llu:%s\"", static_cast<unsigned long long>(start),
                         static_cast<unsigned long long>(end), mime);
  out_.put(buf, static_cast<size_t>(n));
  if (!alt.empty()) {
    out_.put(" alt=\"");
    out_.put(convert(alt));
    out_.put("\"");
  }
  out_.put("/>");
}

void Rewriter::emitAnchor(const std::vector<Attr>& attrs, bool selfClosing) {
  out_.put("<a");
  std::string href;
  bool haveHref = false;
  for (const Attr& a : attrs) {
    if (a.name == "filepos") {
      href = "#p" + std::to_string(strtoul(a.value.c_str(), nullptr, 10));
      haveHref = true;
    } else if (a.name == "href" && !haveHref) {
      uint32_t pos = 0;
      if (kindlePos(a.value, pos)) {
        href = "#p" + std::to_string(pos);
        haveHref = true;
      } else if (a.value.compare(0, 7, "kindle:") != 0) {
        href = convert(a.value);
        haveHref = true;
      }
    } else if (a.name == "id" || a.name == "name") {
      out_.put(" ");
      out_.put(a.name);
      out_.put("=\"");
      out_.put(convert(a.value));
      out_.put("\"");
    }
  }
  if (haveHref) {
    out_.put(" href=\"");
    for (const char c : href) {
      if (c == '"') {
        out_.put("&quot;");
      } else {
        out_.put(&c, 1);
      }
    }
    out_.put("\"");
  }
  out_.put(selfClosing ? "/>" : ">");
}

void Rewriter::emitGeneric(const std::string& name, bool closing, bool selfClosing, const std::vector<Attr>& attrs) {
  out_.put(closing ? "</" : "<");
  out_.put(name);
  if (!closing) {
    static const char* const kKeep[] = {"id", "class", "style", "alt", "title", "lang", "name"};
    for (const Attr& a : attrs) {
      bool keep = false;
      for (const char* k : kKeep) keep = keep || a.name == k;
      if (!keep) continue;
      out_.put(" ");
      out_.put(a.name);
      out_.put("=\"");
      const std::string v = convert(a.value);
      for (const char c : v) {
        if (c == '"') {
          out_.put("&quot;");
        } else {
          out_.put(&c, 1);
        }
      }
      out_.put("\"");
    }
  }
  out_.put(selfClosing ? "/>" : ">");
}

void Rewriter::finishTag() {
  inTag_ = false;
  if (tag_.size() < 3 || tag_[1] == '!' || tag_[1] == '?') {
    tag_.clear();
    return;
  }
  std::string name;
  bool closing = false, selfClosing = false;
  std::vector<Attr> attrs;
  parseTag(name, closing, selfClosing, attrs);
  tag_.clear();
  if (name.empty()) return;

  if (!rawSkip_.empty()) {
    if (closing && name == rawSkip_) rawSkip_.clear();
    return;
  }
  if (headSkip_) {
    if (closing && name == "head") headSkip_ = false;
    if (!closing && name == "body") headSkip_ = false;  // unterminated head
    return;
  }
  if (name == "head") {
    if (!closing && !selfClosing) headSkip_ = true;
    return;
  }
  if (name == "script" || name == "style") {
    if (!closing && !selfClosing) rawSkip_ = name;
    return;
  }
  if (name == "html" || name == "body") return;
  if (name == "svg") {
    if (!closing) {
      if (!selfClosing) ++svgDepth_;
    } else if (svgDepth_ > 0) {
      --svgDepth_;
    }
    return;
  }
  if (svgDepth_ > 0) {
    if (!collect_ && !closing && name == "image") emitImage(attrs);
    return;
  }
  if (name.find(':') != std::string::npos) return;  // mbp:pagebreak, mbp:section, ...

  if (collect_) {
    if (closing) return;
    if (name == "a") {
      for (const Attr& a : attrs) {
        uint32_t pos = 0;
        if (a.name == "filepos") {
          pos = static_cast<uint32_t>(strtoul(a.value.c_str(), nullptr, 10));
        } else if (!(a.name == "href" && kindlePos(a.value, pos))) {
          continue;
        }
        if (targets.size() < kMaxTargets) targets.push_back(pos);
      }
    } else if (name == "h1" || name == "h2" || name == "h3") {
      ++headingTags;
    }
    return;
  }

  if (name == "img") {
    if (!closing) emitImage(attrs);
  } else if (name == "a" && !closing) {
    emitAnchor(attrs, selfClosing);
  } else {
    emitGeneric(name, closing, selfClosing, attrs);
  }
}

}  // namespace

// ---- converter -------------------------------------------------------------

class HtmlConverter {
 public:
  HtmlConverter(Book& book, IByteReader& raw, const TextSink& sink, const CancelFn& cancel)
      : book_(book), raw_(raw), cancel_(cancel) {
    out_.sink = sink;
  }

  bool run();

 private:
  bool loadKf8Layout();
  void loadNcx();
  bool pass(Rewriter& rw);
  bool copyRange(uint64_t from, uint64_t len, Rewriter& rw);
  bool streamKf8(Rewriter& rw);
  void prologue();

  Book& book_;
  IByteReader& raw_;
  const CancelFn& cancel_;
  Writer out_;
  std::vector<Skeleton> skeletons_;
  std::vector<Fragment> fragments_;
  std::vector<NcxEntry> ncx_;
};

bool HtmlConverter::loadKf8Layout() {
  const Book::Header& h = book_.header_;
  if (h.skel == kNone || h.frag == kNone) return true;  // flat text
  IndexReader ir(book_);
  bool ok = ir.read(h.skel, [&](const IndexEntry& e) {
    const auto* t1 = e.find(1);
    const auto* t6 = e.find(6);
    if (!t1 || t1->empty() || !t6 || t6->size() < 2 || skeletons_.size() >= kMaxSkeletons) return false;
    skeletons_.push_back({(*t6)[0], (*t6)[1], (*t1)[0]});
    return true;
  });
  if (!ok) return false;
  ok = ir.read(h.frag, [&](const IndexEntry& e) {
    const auto* t6 = e.find(6);
    if (!t6 || t6->size() < 2 || fragments_.size() >= kMaxFragments || e.text.empty()) return false;
    fragments_.push_back({static_cast<uint32_t>(strtoul(e.text.c_str(), nullptr, 10)), (*t6)[1]});
    return true;
  });
  return ok;
}

void HtmlConverter::loadNcx() {
  const Book::Header& h = book_.header_;
  if (h.ncx == kNone) return;
  IndexReader ir(book_);
  ir.read(h.ncx, [&](const IndexEntry& e) {
    if (ncx_.size() >= kMaxNcx) return false;
    const auto* t3 = e.find(3);
    if (!t3 || t3->empty()) return true;
    uint32_t pos = 0;
    const auto* t6 = e.find(6);
    const auto* t1 = e.find(1);
    if (book_.kf8_ && t6 && t6->size() >= 2) {
      if ((*t6)[0] >= fragments_.size()) return true;
      pos = fragments_[(*t6)[0]].insertPos + (*t6)[1];
    } else if (t1 && !t1->empty()) {
      pos = (*t1)[0];
    } else {
      return true;
    }
    std::string label;
    if (!ir.ctocString((*t3)[0], label) || label.empty()) return true;
    if (label.size() > kMaxLabelBytes) label.resize(kMaxLabelBytes);
    NcxEntry entry;
    entry.pos = pos;
    const auto* t4 = e.find(4);
    entry.level = (t4 && !t4->empty()) ? static_cast<uint8_t>(std::min<uint32_t>((*t4)[0], 8)) : 0;
    book_.appendText(entry.label, label.data(), label.size());
    ncx_.push_back(std::move(entry));
    return true;
  });
  std::stable_sort(ncx_.begin(), ncx_.end(), [](const NcxEntry& a, const NcxEntry& b) { return a.pos < b.pos; });
}

bool HtmlConverter::copyRange(uint64_t from, uint64_t len, Rewriter& rw) {
  uint8_t buf[2048];
  if (len == 0) return true;
  if (!raw_.seek(from)) return false;
  while (len > 0) {
    if (cancel_ && cancel_()) return false;
    const size_t got = raw_.read(buf, static_cast<size_t>(std::min<uint64_t>(len, sizeof(buf))));
    if (got == 0) return false;
    if (!rw.feed(buf, got)) return false;
    len -= got;
  }
  return true;
}

bool HtmlConverter::streamKf8(Rewriter& rw) {
  size_t fragPtr = 0;
  const uint64_t rawSize = raw_.size();
  for (const Skeleton& s : skeletons_) {
    if (static_cast<uint64_t>(s.pos) + s.length > rawSize) return false;
    uint64_t base = static_cast<uint64_t>(s.pos) + s.length;
    uint64_t cursor = 0;  // inside the skeleton
    uint64_t inserted = 0;
    rw.setPos(s.pos);
    for (uint32_t k = 0; k < s.fragments; ++k) {
      if (fragPtr >= fragments_.size()) return false;
      const Fragment& f = fragments_[fragPtr++];
      if (base + f.length > rawSize) return false;
      int64_t rel = static_cast<int64_t>(f.insertPos) - static_cast<int64_t>(s.pos) - static_cast<int64_t>(inserted);
      rel = std::max<int64_t>(static_cast<int64_t>(cursor), std::min<int64_t>(rel, s.length));
      if (!copyRange(s.pos + cursor, static_cast<uint64_t>(rel) - cursor, rw)) return false;
      if (!copyRange(base, f.length, rw)) return false;
      base += f.length;
      inserted += f.length;
      cursor = static_cast<uint64_t>(rel);
    }
    if (!copyRange(s.pos + cursor, s.length - cursor, rw)) return false;
    rw.partBreak();
  }
  return true;
}

bool HtmlConverter::pass(Rewriter& rw) {
  if (book_.kf8_ && !skeletons_.empty()) return streamKf8(rw);
  uint64_t size = raw_.size();
  if (book_.kf8_ && book_.header_.fdst != kNone) {
    // Flat KF8 text without a skeleton: flow 0 ends where flow 1 starts.
    std::string rec;
    if (book_.readRecord(book_.header_.fdst, rec, 64 * 1024) && rec.size() >= 20 && memcmp(rec.data(), "FDST", 4) == 0) {
      const uint32_t end0 = be32(reinterpret_cast<const uint8_t*>(rec.data()) + 20);
      if (end0 > 0 && end0 < size) size = end0;
    }
  }
  return copyRange(0, size, rw);
}

void HtmlConverter::prologue() {
  const Metadata& md = book_.metadata();
  out_.put("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>");
  out_.put(escapeHtml(md.title));
  out_.put("</title>");
  auto meta = [&](const char* name, const std::string& value) {
    if (value.empty()) return;
    out_.put("<meta name=\"");
    out_.put(name);
    out_.put("\" content=\"");
    out_.put(escapeHtml(value));
    out_.put("\">");
  };
  meta("author", md.author);
  meta("language", md.language);
  meta("description", md.description);
  meta("date", md.published);
  const uint32_t cover = book_.header_.coverOffset;
  uint64_t start = 0, end = 0;
  const char* mime = nullptr;
  // EXTH 201 counts from the first image of the MOBI7 half in combined files.
  const uint32_t coverBase = book_.header_.coverBase != kNone ? book_.header_.coverBase : book_.header_.firstImage;
  const uint64_t coverRecord = static_cast<uint64_t>(coverBase) + cover;
  if (cover != kNone && coverRecord >= book_.header_.firstImage &&
      book_.imageRange(static_cast<uint32_t>(coverRecord - book_.header_.firstImage + 1), start, end, mime)) {
    char buf[96];
    const int n = snprintf(buf, sizeof(buf), "<meta name=\"cover\" content=\"x-raw:%llu-%llu:%s\">",
                           static_cast<unsigned long long>(start), static_cast<unsigned long long>(end), mime);
    out_.put(buf, static_cast<size_t>(n));
  }
  out_.put("</head><body>\n");
}

bool HtmlConverter::run() {
  book_.error_ = Error::None;
  if (book_.kf8_ && !loadKf8Layout()) {
    book_.error_ = Error::Corrupt;
    return false;
  }
  loadNcx();

  // Pass 1: which positions are link targets, and does the text have headings?
  Rewriter scan(book_, fragments_, true, out_);
  if (!pass(scan)) {
    book_.error_ = (cancel_ && cancel_()) ? Error::Cancelled : Error::Corrupt;
    return false;
  }

  std::sort(scan.targets.begin(), scan.targets.end());
  scan.targets.erase(std::unique(scan.targets.begin(), scan.targets.end()), scan.targets.end());
  std::vector<Insertion> insertions;
  insertions.reserve(scan.targets.size() + ncx_.size());
  for (const uint32_t t : scan.targets) insertions.push_back({t, -1});
  if (scan.headingTags == 0) {
    for (size_t i = 0; i < ncx_.size(); ++i) insertions.push_back({ncx_[i].pos, static_cast<int32_t>(i)});
  }
  std::stable_sort(insertions.begin(), insertions.end(),
                   [](const Insertion& a, const Insertion& b) { return a.pos < b.pos; });

  // Pass 2: write the HTML.
  prologue();
  Rewriter emit(book_, fragments_, false, out_);
  emit.insertions = &insertions;
  emit.ncx = &ncx_;
  if (!pass(emit)) {
    book_.error_ = out_.failed ? Error::Io : ((cancel_ && cancel_()) ? Error::Cancelled : Error::Corrupt);
    return false;
  }
  out_.put("\n</body></html>\n");
  out_.flush();
  if (out_.failed) {
    book_.error_ = Error::Io;
    return false;
  }
  return true;
}

bool Book::convertToHtml(IByteReader& rawText, const TextSink& out, const CancelFn& cancel) {
  HtmlConverter converter(*this, rawText, out, cancel);
  return converter.run();
}

}  // namespace mobi
