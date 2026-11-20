#pragma once

#include <DictWordForms.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Streaming full-text search over chapter (X)HTML, without laying the chapter out.
//
// The chapter is fed in arbitrary chunks. Tags are dropped, the basic entities are decoded, scripts,
// styles and <head> are skipped, whitespace is collapsed and the text is split into blocks at
// block-level tags. Matching ignores case (the reader's dictionary lowercase table), the stress mark
// U+0301, the soft hyphen and ё/е. Each hit remembers the paragraph number (count of <p> elements,
// the same numbering the reader's section table and bookmarks use), the byte offset of its block in
// the chapter source and a short text snippet.
namespace book_search {

struct Hit {
  uint16_t paragraphIndex = 0;  // number of <p> opened so far; 0 before the first one
  uint32_t byteOffset = 0;      // offset of the block start in the fed data
  std::string snippet;
};

class Matcher {
 public:
  static constexpr size_t kMaxBlockBytes = 8192;
  static constexpr size_t kSnippetBefore = 28;
  static constexpr size_t kSnippetAfter = 60;

  Matcher(const std::string& query, const size_t maxHits) : maxHits_(maxHits) {
    bool lastSpace = true;
    size_t pos = 0;
    while (pos < query.size()) {
      const uint32_t cp = dict_word_forms::detail::decode(query, pos);
      const uint32_t f = fold(cp);
      if (f == 0) continue;
      if (f == ' ') {
        if (!lastSpace) needle_.push_back(' ');
        lastSpace = true;
      } else {
        needle_.push_back(f);
        lastSpace = false;
      }
    }
    while (!needle_.empty() && needle_.back() == ' ') needle_.pop_back();
  }

  bool valid() const { return needle_.size() >= 2; }
  bool full() const { return hits_.size() >= maxHits_; }
  const std::vector<Hit>& hits() const { return hits_; }
  size_t paragraphCount() const { return paragraph_; }
  size_t bytesConsumed() const { return consumed_; }

  // Restart for the next chapter; hits found so far are kept by the caller (take them first).
  void reset() {
    state_ = State::Text;
    tag_.clear();
    entity_.clear();
    skipName_.clear();
    cps_.clear();
    blockHasText_ = false;
    lastSpace_ = true;
    paragraph_ = 0;
    consumed_ = 0;
    blockStart_ = 0;
    hits_.clear();
  }

  std::vector<Hit> takeHits() {
    std::vector<Hit> out;
    out.swap(hits_);
    return out;
  }

  void feed(const char* data, const size_t len) {
    if (!valid()) return;
    for (size_t i = 0; i < len; ++i) {
      step(static_cast<uint8_t>(data[i]));
      ++consumed_;
    }
  }

  void finish() {
    if (state_ == State::Entity) flushEntity();
    flushBlock();
  }

 private:
  enum class State { Text, Tag, Entity };

  static uint32_t fold(const uint32_t cp) {
    if (cp == 0x00AD || cp == 0x0301 || cp == 0x200B || cp == 0xFEFF) return 0;  // ignorable
    if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0x00A0 || cp == 0x2009 || cp == 0x202F ||
        cp == 0x2003 || cp == 0x2002) {
      return ' ';
    }
    uint32_t f = dict_word_forms::lowerCodepoint(cp);
    if (f == 0x0451) f = 0x0435;  // ё -> е
    return f;
  }

  static bool ieq(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; ++i) {
      char c = a[i];
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
      if (c != b[i]) return false;
    }
    return i == a.size() && b[i] == '\0';
  }

  static bool isBlockTag(const std::string& n) {
    static const char* kBlock[] = {"p",  "div",  "br",    "hr",     "h1",         "h2",    "h3",    "h4",
                                   "h5", "h6",   "li",    "ul",     "ol",         "tr",    "td",    "th",
                                   "dt", "dd",   "dl",    "table",  "blockquote", "pre",   "section", "article",
                                   "aside", "title", "caption", "figcaption", "body", "header", "footer",
                                   "nav", "stanza", "v", "poem",  "cite",  "epigraph", "subtitle", "text-author"};
    for (const char* b : kBlock) {
      if (ieq(n, b)) return true;
    }
    return false;
  }

  void step(const uint8_t b) {
    switch (state_) {
      case State::Tag:
        stepTag(b);
        return;
      case State::Entity:
        if (b == ';' || entity_.size() >= 10 || b == '<' || b == ' ' || b == '&') {
          const bool closed = (b == ';');
          flushEntity(closed);
          if (!closed) step(b);  // not an entity after all: handle the byte normally
        } else {
          entity_ += static_cast<char>(b);
        }
        return;
      case State::Text:
        break;
    }
    if (b == '<') {
      state_ = State::Tag;
      tag_.clear();
      quote_ = 0;
      return;
    }
    if (!skipName_.empty()) return;
    if (b == '&') {
      state_ = State::Entity;
      entity_.clear();
      return;
    }
    appendByte(b);
  }

  void stepTag(const uint8_t b) {
    if (quote_ != 0) {
      if (b == quote_) quote_ = 0;
      if (tag_.size() < 96) tag_ += static_cast<char>(b);
      return;
    }
    if (b == '"' || b == '\'') {
      // Only attribute values are quoted; a quote right after "<" can't start one.
      if (!tag_.empty()) quote_ = b;
      if (tag_.size() < 96) tag_ += static_cast<char>(b);
      return;
    }
    if (b == '>') {
      state_ = State::Text;
      endTag();
      return;
    }
    if (tag_.size() < 96) tag_ += static_cast<char>(b);
  }

  void endTag() {
    if (tag_.empty() || tag_[0] == '!' || tag_[0] == '?') return;
    size_t i = 0;
    const bool closing = tag_[0] == '/';
    if (closing) i = 1;
    size_t j = i;
    while (j < tag_.size() && tag_[j] != ' ' && tag_[j] != '\t' && tag_[j] != '\n' && tag_[j] != '\r' &&
           tag_[j] != '/') {
      ++j;
    }
    const std::string name = tag_.substr(i, j - i);
    if (name.empty()) return;
    const bool selfClosed = !closing && tag_.back() == '/';

    if (!skipName_.empty()) {
      if (closing && ieq(name, skipName_.c_str())) skipName_.clear();
      return;
    }
    if (!closing && !selfClosed && (ieq(name, "script") || ieq(name, "style") || ieq(name, "head"))) {
      flushBlock();
      skipName_ = name;
      for (auto& c : skipName_) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
      }
      return;
    }
    if (isBlockTag(name)) {
      flushBlock();
      if (!closing && ieq(name, "p")) {
        if (paragraph_ < UINT16_MAX) ++paragraph_;
      }
      blockStart_ = consumed_;
    }
  }

  void flushEntity(const bool closed = true) {
    state_ = State::Text;
    if (!closed) {
      // A bare "&" (or "&text" without ';'): keep it literally.
      appendCodepoint('&');
      for (const char c : entity_) appendByte(static_cast<uint8_t>(c));
      return;
    }
    uint32_t cp = 0;
    if (!entity_.empty() && entity_[0] == '#') {
      const bool hex = entity_.size() > 1 && (entity_[1] == 'x' || entity_[1] == 'X');
      for (size_t i = hex ? 2 : 1; i < entity_.size(); ++i) {
        const char c = entity_[i];
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (hex && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (hex && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        if (d < 0) {
          cp = 0;
          break;
        }
        cp = cp * (hex ? 16 : 10) + static_cast<uint32_t>(d);
        if (cp > 0x10FFFF) {
          cp = 0;
          break;
        }
      }
    } else {
      static const struct {
        const char* name;
        uint32_t cp;
      } kNamed[] = {{"amp", '&'},       {"lt", '<'},        {"gt", '>'},        {"quot", '"'},
                    {"apos", '\''},     {"nbsp", 0xA0},     {"shy", 0xAD},      {"mdash", 0x2014},
                    {"ndash", 0x2013},  {"hellip", 0x2026}, {"laquo", 0xAB},    {"raquo", 0xBB},
                    {"ldquo", 0x201C},  {"rdquo", 0x201D},  {"lsquo", 0x2018},  {"rsquo", 0x2019},
                    {"bdquo", 0x201E},  {"copy", 0xA9},     {"eacute", 0xE9},   {"egrave", 0xE8},
                    {"agrave", 0xE0},   {"aacute", 0xE1},   {"uuml", 0xFC},     {"ouml", 0xF6},
                    {"auml", 0xE4},     {"szlig", 0xDF},    {"ccedil", 0xE7},   {"ntilde", 0xF1}};
      for (const auto& n : kNamed) {
        if (entity_ == n.name) {
          cp = n.cp;
          break;
        }
      }
    }
    if (cp != 0) appendCodepoint(cp);
  }

  void appendByte(const uint8_t b) {
    if (b < 0x80) {
      appendCodepoint(b);
      return;
    }
    // Reassemble UTF-8 sequences across feed() chunk boundaries.
    if (b >= 0xC0) {
      pending_.clear();
    }
    pending_ += static_cast<char>(b);
    const auto lead = static_cast<uint8_t>(pending_[0]);
    const size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    if (pending_.size() >= need) {
      size_t pos = 0;
      const uint32_t cp = dict_word_forms::detail::decode(pending_, pos);
      pending_.clear();
      appendCodepoint(cp);
    }
  }

  void appendCodepoint(const uint32_t cp) {
    const uint32_t f = fold(cp);
    if (f == 0) return;
    if (f == ' ') {
      if (lastSpace_) return;
      lastSpace_ = true;
    } else {
      lastSpace_ = false;
      blockHasText_ = true;
    }
    cps_.push_back(cp == 0xA0 || f == ' ' ? static_cast<uint32_t>(' ') : cp);
    if (cps_.size() * 3 >= kMaxBlockBytes) {
      flushBlock();
      blockStart_ = consumed_;
    }
  }

  void flushBlock() {
    pending_.clear();
    if (blockHasText_ && !full()) searchBlock();
    cps_.clear();
    blockHasText_ = false;
    lastSpace_ = true;
  }

  void searchBlock() {
    const size_t n = cps_.size();
    const size_t m = needle_.size();
    if (n < m) return;
    folded_.clear();
    folded_.reserve(n);
    for (const uint32_t cp : cps_) folded_.push_back(fold(cp));
    size_t i = 0;
    while (i + m <= n && !full()) {
      bool ok = true;
      for (size_t k = 0; k < m; ++k) {
        if (folded_[i + k] != needle_[k]) {
          ok = false;
          break;
        }
      }
      if (!ok) {
        ++i;
        continue;
      }
      Hit h;
      h.paragraphIndex = static_cast<uint16_t>(paragraph_);
      h.byteOffset = static_cast<uint32_t>(blockStart_);
      size_t from = i > kSnippetBefore ? i - kSnippetBefore : 0;
      size_t to = i + m + kSnippetAfter < n ? i + m + kSnippetAfter : n;
      // Start and end the snippet on word boundaries when the text allows it.
      if (from > 0) {
        while (from < i && cps_[from] != ' ') ++from;
        while (from < i && cps_[from] == ' ') ++from;
      }
      if (to < n) {
        size_t t = to;
        while (t > i + m && cps_[t] != ' ') --t;
        if (t > i + m) to = t;
      }
      if (from > 0) h.snippet += "\xE2\x80\xA6";
      for (size_t k = from; k < to; ++k) dict_word_forms::detail::append(h.snippet, cps_[k]);
      if (to < n) h.snippet += "\xE2\x80\xA6";
      hits_.push_back(std::move(h));
      i += m;
    }
  }

  std::vector<uint32_t> needle_;
  size_t maxHits_;
  State state_ = State::Text;
  std::string tag_;
  std::string entity_;
  std::string skipName_;
  std::string pending_;
  uint8_t quote_ = 0;
  std::vector<uint32_t> cps_;
  std::vector<uint32_t> folded_;
  bool blockHasText_ = false;
  bool lastSpace_ = true;
  size_t paragraph_ = 0;
  size_t consumed_ = 0;
  size_t blockStart_ = 0;
  std::vector<Hit> hits_;
};

}  // namespace book_search
