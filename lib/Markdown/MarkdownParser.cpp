#include "MarkdownParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <ReaderWork.h>

namespace {

constexpr size_t kMaxPhysicalLine = 32 * 1024;   // longer lines are split, never truncated
constexpr size_t kMaxParagraphBytes = 64 * 1024; // one paragraph / list item / quote block
constexpr size_t kMaxFrontMatterLines = 64;
constexpr size_t kPoolHardLimit = 28 * 1024;
constexpr size_t kMaxTitleBytes = 384;
constexpr int kMaxChapterHeading = 3;  // H1..H3 start chapters; deeper headings are subtitles

bool cancelled(const reader::ReaderCancellationToken* t) { return t && t->isCancellationRequested(); }

bool isBlank(char c) { return c == ' ' || c == '\t'; }

std::string ltrim(const std::string& s) {
  size_t i = 0;
  while (i < s.size() && isBlank(s[i])) ++i;
  return s.substr(i);
}

std::string trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && isBlank(s[b])) ++b;
  while (e > b && (isBlank(s[e - 1]) || s[e - 1] == '\r')) --e;
  return s.substr(b, e - b);
}

size_t leadingSpaces(const std::string& s) {
  size_t n = 0;
  for (const char c : s) {
    if (c == ' ') ++n;
    else if (c == '\t') n += 4;
    else break;
  }
  return n;
}

// ---------------------------------------------------------------------------
// Physical lines with byte offsets
// ---------------------------------------------------------------------------

class LineSource {
 public:
  LineSource(IByteReader& reader, uint64_t startOffset) : reader_(reader), fileSize_(reader.size()) {
    pos_ = startOffset;
    reader_.seek(pos_);
  }

  // `start` = offset of the first byte of the line, `end` = offset just past
  // its terminator (or past the last byte if it has none). A physical line
  // longer than kMaxPhysicalLine is split at its last space (the remainder is
  // returned by the next call), so nothing is ever dropped and re-joining the
  // pieces with one space reproduces the original text.
  bool next(std::string& line, uint64_t& start, uint64_t& end) {
    line = std::move(carry_);
    carry_.clear();
    start = pos_ - line.size();  // carried bytes were already consumed from the file
    if (line.empty() && pos_ >= fileSize_) return false;
    for (;;) {
      if (bufPos_ == bufLen_ && !fill()) {
        end = pos_;
        return !line.empty() || start != pos_;
      }
      const char c = buf_[bufPos_++];
      ++pos_;
      if (c == '\n') {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        end = pos_;
        return true;
      }
      line.push_back(c);
      if (line.size() >= kMaxPhysicalLine) {
        const size_t sp = line.find_last_of(' ');
        if (sp != std::string::npos && sp > kMaxPhysicalLine / 2) {
          carry_ = line.substr(sp + 1);
          line.resize(sp);
        }
        end = pos_ - carry_.size();
        return true;
      }
    }
  }

 private:
  bool fill() {
    bufLen_ = reader_.read(buf_, sizeof(buf_));
    bufPos_ = 0;
    return bufLen_ > 0;
  }

  IByteReader& reader_;
  uint64_t fileSize_;
  uint64_t pos_ = 0;
  char buf_[1024];
  size_t bufPos_ = 0, bufLen_ = 0;
  std::string carry_;
};

// ---------------------------------------------------------------------------
// Inline spans (adapted from Paperio Reader's MdParser, MIT)
// ---------------------------------------------------------------------------

struct Span {
  std::string text;
  Fb2InlineStyle style = Fb2InlineStyle::Regular;
};

Fb2InlineStyle styleOf(bool bold, bool italic, bool strike) {
  Fb2InlineStyle s = Fb2InlineStyle::Regular;
  if (bold) s = s | Fb2InlineStyle::Bold;
  if (italic) s = s | Fb2InlineStyle::Italic;
  if (strike) s = s | Fb2InlineStyle::Strikethrough;
  return s;
}

bool isWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Intraword underscores (snake_case) are not emphasis.
bool isUnderscoreEmphasis(const std::string& text, size_t pos, size_t count) {
  if (pos == 0 || pos + count >= text.size()) return true;
  return !(isWordChar(text[pos - 1]) && isWordChar(text[pos + count]));
}

// Drops an inline HTML tag at `i` (returns its length) so "<br>" / "<b>" do not
// show up literally. Only recognises tag-shaped text, never a bare '<'.
size_t htmlTagLength(const std::string& t, size_t i) {
  if (t[i] != '<' || i + 2 >= t.size()) return 0;
  size_t j = i + 1;
  if (t[j] == '/') ++j;
  if (j >= t.size() || !std::isalpha(static_cast<unsigned char>(t[j]))) return 0;
  const size_t close = t.find('>', j);
  if (close == std::string::npos || close - i > 200) return 0;
  return close - i + 1;
}

std::vector<Span> parseInline(const std::string& text) {
  std::vector<Span> spans;
  std::string current;
  bool bold = false, italic = false, strike = false;
  char boldMarker = 0, italicMarker = 0;
  size_t i = 0;

  auto emit = [&]() {
    if (current.empty()) return;
    spans.push_back({std::move(current), styleOf(bold, italic, strike)});
    current.clear();
  };

  while (i < text.size()) {
    const char c = text[i];

    if (c == '\\' && i + 1 < text.size()) {
      const char n = text[i + 1];
      if (n == '~' && i + 2 < text.size() && text[i + 2] == '~') {
        current += "~~";
        i += 3;
        continue;
      }
      if (std::strchr("*_`[]!~\\#<>()+-.|{}", n)) {
        current += n;
        i += 2;
        continue;
      }
    }

    if (c == '<') {
      // <https://example.org> and <me@example.org>: show the address
      const size_t close = text.find('>', i + 1);
      if (close != std::string::npos && close - i < 200) {
        const std::string inner = text.substr(i + 1, close - i - 1);
        if (inner.find(' ') == std::string::npos &&
            (inner.find("://") != std::string::npos || inner.find('@') != std::string::npos)) {
          current += inner;
          i = close + 1;
          continue;
        }
      }
      if (const size_t tag = htmlTagLength(text, i)) {
        const std::string name = text.substr(i + 1, std::min<size_t>(tag - 1, 3));
        if (name.compare(0, 2, "br") == 0) current += ' ';
        i += tag;
        continue;
      }
    }

    // *** / ___ : bold + italic
    if ((c == '*' || c == '_') && i + 2 < text.size() && text[i + 1] == c && text[i + 2] == c) {
      if (c == '_' && !isUnderscoreEmphasis(text, i, 3)) {
        current.append(3, c);
        i += 3;
        continue;
      }
      emit();
      if (bold && italic && boldMarker == c && italicMarker == c) {
        bold = italic = false;
        boldMarker = italicMarker = 0;
      } else {
        bold = italic = true;
        boldMarker = italicMarker = c;
      }
      i += 3;
      continue;
    }

    // ~~ strikethrough (only opens when a closing ~~ exists)
    if (c == '~' && i + 1 < text.size() && text[i + 1] == '~') {
      if (!strike && text.find("~~", i + 2) == std::string::npos) {
        current += "~~";
        i += 2;
        continue;
      }
      emit();
      strike = !strike;
      i += 2;
      continue;
    }

    // ** / __ : bold
    if ((c == '*' || c == '_') && i + 1 < text.size() && text[i + 1] == c) {
      if (c == '_' && !isUnderscoreEmphasis(text, i, 2)) {
        current.append(2, c);
        i += 2;
        continue;
      }
      emit();
      if (bold && boldMarker == c) {
        bold = false;
        boldMarker = 0;
      } else {
        bold = true;
        boldMarker = c;
      }
      i += 2;
      continue;
    }

    // * / _ : italic. "* " (a bullet left in the text) is literal.
    if (c == '*' || c == '_') {
      if (c == '_' && !isUnderscoreEmphasis(text, i, 1)) {
        current += c;
        ++i;
        continue;
      }
      if (c == '*' && !italic && i + 1 < text.size() && isBlank(text[i + 1])) {
        current += c;
        ++i;
        continue;
      }
      emit();
      if (italic && italicMarker == c) {
        italic = false;
        italicMarker = 0;
      } else {
        italic = true;
        italicMarker = c;
      }
      ++i;
      continue;
    }

    // `code` span: no monospace face on the device, show the text plain.
    if (c == '`') {
      size_t ticks = 1;
      while (i + ticks < text.size() && text[i + ticks] == '`') ++ticks;
      const std::string fence(ticks, '`');
      const size_t end = text.find(fence, i + ticks);
      if (end != std::string::npos) {
        emit();
        std::string code = text.substr(i + ticks, end - i - ticks);
        spans.push_back({trim(code), styleOf(bold, italic, strike)});
        i = end + ticks;
        continue;
      }
      current.append(ticks, '`');
      i += ticks;
      continue;
    }

    // ![alt](url) -> [alt]
    if (c == '!' && i + 1 < text.size() && text[i + 1] == '[') {
      const size_t cb = text.find(']', i + 2);
      if (cb != std::string::npos && cb + 1 < text.size() && text[cb + 1] == '(') {
        const size_t cp = text.find(')', cb + 2);
        if (cp != std::string::npos) {
          current += "[" + text.substr(i + 2, cb - i - 2) + "]";
          i = cp + 1;
          continue;
        }
      }
      current += c;
      ++i;
      continue;
    }

    // [text](url) -> text
    if (c == '[') {
      const size_t cb = text.find(']', i + 1);
      if (cb != std::string::npos && cb + 1 < text.size() && text[cb + 1] == '(') {
        const size_t cp = text.find(')', cb + 2);
        if (cp != std::string::npos) {
          const std::string label = text.substr(i + 1, cb - i - 1);
          // Labels may carry emphasis themselves: parse them recursively.
          emit();
          for (Span& s : parseInline(label)) {
            s.style = s.style | styleOf(bold, italic, strike);
            spans.push_back(std::move(s));
          }
          i = cp + 1;
          continue;
        }
      }
    }

    current += c;
    ++i;
  }
  emit();
  return spans;
}

std::string plainText(const std::vector<Span>& spans) {
  std::string out;
  for (const Span& s : spans) out += s.text;
  return out;
}

// ---------------------------------------------------------------------------
// Blocks
// ---------------------------------------------------------------------------

enum class BT : uint8_t { Heading, Paragraph, ListItem, Quote, CodeBegin, CodeLine, CodeEnd, TableRow, Rule };

struct Block {
  BT type = BT::Paragraph;
  int level = 0;           // Heading: 1..6; ListItem: nesting depth
  std::string text;        // raw inline text (Paragraph/Heading/ListItem/Quote/CodeLine)
  bool ordered = false;    // ListItem
  std::string number;      // ListItem: "3." for ordered items
  int task = 0;            // ListItem: 0 none, 1 open box, 2 checked box
  std::vector<std::string> cells;  // TableRow
  std::vector<std::string> aligns; // TableRow: "left"/"center"/"right"/"" per column
  bool isHeaderRow = false;        // TableRow
  uint64_t start = 0;      // offset of the block's first line
  uint64_t end = 0;        // offset just past the block's last line
};

bool isHorizontalRule(const std::string& trimmed) {
  if (trimmed.size() < 3) return false;
  char marker = 0;
  int count = 0;
  for (const char c : trimmed) {
    if (isBlank(c)) continue;
    if (c != '-' && c != '*' && c != '_') return false;
    if (!marker) marker = c;
    if (c != marker) return false;
    ++count;
  }
  return count >= 3;
}

// "#"-style heading: returns level (1..6) and the content, or 0.
int parseAtxHeading(const std::string& trimmed, std::string& content) {
  size_t level = 0;
  while (level < trimmed.size() && trimmed[level] == '#') ++level;
  if (level == 0 || level > 6) return 0;
  if (level < trimmed.size() && !isBlank(trimmed[level])) return 0;
  content = trim(level < trimmed.size() ? trimmed.substr(level) : std::string());
  size_t e = content.size();
  while (e > 0 && content[e - 1] == '#') --e;  // optional closing hashes
  if (e < content.size() && (e == 0 || isBlank(content[e - 1]))) content = trim(content.substr(0, e));
  return static_cast<int>(level);
}

// 0 = not a fence; otherwise the fence character; `len` receives its length.
char parseFence(const std::string& trimmed, size_t& len) {
  if (trimmed.size() < 3) return 0;
  const char c = trimmed[0];
  if (c != '`' && c != '~') return 0;
  size_t n = 0;
  while (n < trimmed.size() && trimmed[n] == c) ++n;
  if (n < 3) return 0;
  if (c == '`' && trimmed.find('`', n) != std::string::npos) return 0;  // inline code, not a fence
  len = n;
  return c;
}

bool isSetextUnderline(const std::string& trimmed, char& marker) {
  if (trimmed.empty()) return false;
  const char c = trimmed[0];
  if (c != '=' && c != '-') return false;
  size_t i = 0;
  while (i < trimmed.size() && trimmed[i] == c) ++i;
  while (i < trimmed.size() && isBlank(trimmed[i])) ++i;
  if (i != trimmed.size()) return false;
  marker = c;
  return true;
}

bool parseListMarker(const std::string& trimmed, bool& ordered, std::string& number, std::string& content) {
  if (trimmed.size() >= 2 && isBlank(trimmed[1]) && (trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+')) {
    ordered = false;
    number.clear();
    content = trim(trimmed.substr(2));
    return true;
  }
  size_t d = 0;
  while (d < trimmed.size() && d < 9 && std::isdigit(static_cast<unsigned char>(trimmed[d]))) ++d;
  if (d > 0 && d + 1 < trimmed.size() && (trimmed[d] == '.' || trimmed[d] == ')') && isBlank(trimmed[d + 1])) {
    ordered = true;
    number = trimmed.substr(0, d) + ".";
    content = trim(trimmed.substr(d + 2));
    return true;
  }
  return false;
}

std::vector<std::string> splitTableCells(const std::string& line) {
  std::vector<std::string> cells;
  size_t i = 0;
  if (i < line.size() && line[i] == '|') ++i;
  while (i < line.size()) {
    const size_t s = i;
    while (i < line.size() && !(line[i] == '|' && line[i - (i > 0 ? 1 : 0)] != '\\')) ++i;
    cells.push_back(trim(line.substr(s, i - s)));
    if (i < line.size()) ++i;
  }
  if (!cells.empty() && cells.back().empty() && !line.empty() && line.back() == '|') cells.pop_back();
  return cells;
}

bool isTableLine(const std::string& trimmed) { return !trimmed.empty() && trimmed[0] == '|'; }

bool isTableSeparatorLine(const std::string& trimmed) {
  if (!isTableLine(trimmed)) return false;
  for (const char c : trimmed) {
    if (c != '|' && c != '-' && c != ':' && !isBlank(c)) return false;
  }
  return trimmed.find('-') != std::string::npos;
}

std::vector<std::string> alignmentsOf(const std::string& separatorLine) {
  std::vector<std::string> out;
  for (const std::string& cell : splitTableCells(separatorLine)) {
    const bool left = !cell.empty() && cell.front() == ':';
    const bool right = !cell.empty() && cell.back() == ':';
    out.push_back(left && right ? "center" : right ? "right" : left ? "left" : "");
  }
  return out;
}

// Reads logical blocks one at a time. Scan and render both use it, so chapter
// boundaries can never disagree between the index and the pages.
class BlockReader {
 public:
  BlockReader(IByteReader& reader, uint64_t startOffset) : src_(reader, startOffset) {}

  bool next(Block& b) {
    for (;;) {
      std::string line;
      uint64_t start = 0, end = 0;
      if (!take(line, start, end)) return false;

      if (inFence_) {
        size_t len = 0;
        const char fc = parseFence(ltrim(line), len);
        Block out;
        out.start = start;
        out.end = end;
        if (fc == fenceChar_ && len >= fenceLen_ && trim(ltrim(line)).size() == len) {
          inFence_ = false;
          out.type = BT::CodeEnd;
        } else {
          out.type = BT::CodeLine;
          out.text = line;
        }
        b = std::move(out);
        return true;
      }

      const std::string t = ltrim(line);
      if (t.empty()) {  // blank lines only separate blocks (and end a table)
        inTable_ = false;
        continue;
      }
      if (!isTableLine(t)) inTable_ = false;

      size_t fenceLen = 0;
      if (const char fc = parseFence(t, fenceLen)) {
        inFence_ = true;
        fenceChar_ = fc;
        fenceLen_ = fenceLen;
        indents_.clear();
        b = Block();
        b.type = BT::CodeBegin;
        b.start = start;
        b.end = end;
        return true;
      }

      std::string content;
      if (const int level = parseAtxHeading(t, content)) {
        indents_.clear();
        b = Block();
        b.type = BT::Heading;
        b.level = level;
        b.text = content;
        b.start = start;
        b.end = end;
        return true;
      }

      if (isHorizontalRule(t)) {
        indents_.clear();
        b = Block();
        b.type = BT::Rule;
        b.start = start;
        b.end = end;
        return true;
      }

      if (t[0] == '>') return readQuote(t, start, end, b);

      if (isTableLine(t)) return readTableRow(t, start, end, b);

      bool ordered = false;
      std::string number;
      if (parseListMarker(t, ordered, number, content)) return readListItem(line, ordered, number, content, start, end, b);

      indents_.clear();
      return readParagraph(t, start, end, b);
    }
  }

 private:
  // One-line pushback so block readers can look ahead without re-seeking.
  bool take(std::string& line, uint64_t& start, uint64_t& end) {
    if (havePending_) {
      line = std::move(pendingLine_);
      start = pendingStart_;
      end = pendingEnd_;
      havePending_ = false;
      return true;
    }
    return src_.next(line, start, end);
  }
  bool peek(std::string& line, uint64_t& start, uint64_t& end) {
    if (!havePending_) {
      if (!src_.next(pendingLine_, pendingStart_, pendingEnd_)) return false;
      havePending_ = true;
    }
    line = pendingLine_;
    start = pendingStart_;
    end = pendingEnd_;
    return true;
  }
  void drop() { havePending_ = false; }

  // Does `line` (already ltrimmed) start a block that ends the current paragraph?
  static bool startsBlock(const std::string& t) {
    if (t.empty()) return true;
    std::string c;
    size_t fl = 0;
    bool o = false;
    std::string n;
    return parseAtxHeading(t, c) != 0 || parseFence(t, fl) != 0 || t[0] == '>' || isTableLine(t) ||
           isHorizontalRule(t) || parseListMarker(t, o, n, c);
  }

  static void appendJoined(std::string& dst, const std::string& piece) {
    if (!dst.empty()) dst.push_back(' ');
    dst += piece;
  }
  // A block that has grown past the cap ends here; the rest becomes the next
  // block of the same kind (split, never truncated).
  static bool full(const std::string& s) { return s.size() >= kMaxParagraphBytes; }

  bool readParagraph(const std::string& first, uint64_t start, uint64_t end, Block& b) {
    std::string text = trim(first);
    uint64_t blockEnd = end;
    for (;;) {
      if (full(text)) break;
      std::string line;
      uint64_t s = 0, e = 0;
      if (!peek(line, s, e)) break;
      const std::string t = ltrim(line);
      char marker = 0;
      if (isSetextUnderline(t, marker)) {
        drop();
        b = Block();
        b.type = BT::Heading;
        b.level = marker == '=' ? 1 : 2;
        b.text = text;
        b.start = start;
        b.end = e;
        return true;
      }
      if (startsBlock(t)) break;
      drop();
      appendJoined(text, trim(t));
      blockEnd = e;
    }
    b = Block();
    b.type = BT::Paragraph;
    b.text = text;
    b.start = start;
    b.end = blockEnd;
    return true;
  }

  bool readQuote(const std::string& first, uint64_t start, uint64_t end, Block& b) {
    indents_.clear();
    auto strip = [](const std::string& t) {
      size_t i = 0;
      while (i < t.size() && (t[i] == '>' || isBlank(t[i]))) ++i;
      return trim(t.substr(i));
    };
    std::string text = strip(first);
    uint64_t blockEnd = end;
    if (text.empty()) {  // a bare ">" line separates paragraphs inside the quote
      b = Block();
      b.type = BT::Quote;
      b.start = start;
      b.end = end;
      // Emit nothing for it: continue with the next quote paragraph.
      return next(b);
    }
    for (;;) {
      if (full(text)) break;
      std::string line;
      uint64_t s = 0, e = 0;
      if (!peek(line, s, e)) break;
      const std::string t = ltrim(line);
      if (t.empty() || t[0] != '>') break;
      const std::string piece = strip(t);
      if (piece.empty()) {  // paragraph break inside the quote
        drop();
        blockEnd = e;
        break;
      }
      drop();
      appendJoined(text, piece);
      blockEnd = e;
    }
    b = Block();
    b.type = BT::Quote;
    b.text = text;
    b.start = start;
    b.end = blockEnd;
    return true;
  }

  bool readTableRow(const std::string& t, uint64_t start, uint64_t end, Block& b) {
    indents_.clear();
    if (isTableSeparatorLine(t)) {  // stray separator without a header: ignore it
      tableAligns_ = alignmentsOf(t);
      return next(b);
    }
    if (!inTable_) tableAligns_.clear();  // a new table starts with no inherited alignment
    Block out;
    out.type = BT::TableRow;
    out.cells = splitTableCells(t);
    out.start = start;
    out.end = end;
    // GFM: a header row is the one directly followed by the separator row.
    std::string line;
    uint64_t s = 0, e = 0;
    if (!inTable_ && peek(line, s, e) && isTableSeparatorLine(ltrim(line))) {
      drop();
      tableAligns_ = alignmentsOf(ltrim(line));
      out.isHeaderRow = true;
      out.end = e;
    }
    inTable_ = true;
    out.aligns = tableAligns_;
    b = std::move(out);
    return true;
  }

  bool readListItem(const std::string& rawLine, bool ordered, const std::string& number, std::string content,
                    uint64_t start, uint64_t end, Block& b) {
    inTable_ = false;
    const int indent = static_cast<int>(leadingSpaces(rawLine));
    while (!indents_.empty() && indents_.back() > indent) indents_.pop_back();
    if (indents_.empty() || indents_.back() < indent) indents_.push_back(indent);

    Block out;
    out.type = BT::ListItem;
    out.level = static_cast<int>(indents_.size()) - 1;
    out.ordered = ordered;
    out.number = number;
    out.start = start;
    // Task list: "[ ] text" / "[x] text"
    if (content.size() >= 3 && content[0] == '[' && content[2] == ']' &&
        (content[1] == ' ' || content[1] == 'x' || content[1] == 'X')) {
      out.task = content[1] == ' ' ? 1 : 2;
      content = trim(content.size() > 3 ? content.substr(3) : std::string());
    }
    uint64_t blockEnd = end;
    // Continuation lines (indented or lazy) belong to the same item.
    for (;;) {
      if (full(content)) break;
      std::string line;
      uint64_t s = 0, e = 0;
      if (!peek(line, s, e)) break;
      const std::string t = ltrim(line);
      if (startsBlock(t)) break;
      drop();
      appendJoined(content, trim(t));
      blockEnd = e;
    }
    out.text = content;
    out.end = blockEnd;
    b = std::move(out);
    return true;
  }

  LineSource src_;
  bool havePending_ = false;
  std::string pendingLine_;
  uint64_t pendingStart_ = 0, pendingEnd_ = 0;
  bool inFence_ = false;
  char fenceChar_ = 0;
  size_t fenceLen_ = 0;
  std::vector<int> indents_;
  bool inTable_ = false;
  std::vector<std::string> tableAligns_;
};

// ---------------------------------------------------------------------------
// Front matter
// ---------------------------------------------------------------------------

struct FrontMatter {
  uint64_t contentStart = 0;
  std::string title, author, language, date;
};

std::string unquote(std::string v) {
  v = trim(v);
  if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\''))) {
    v = v.substr(1, v.size() - 2);
  }
  return v;
}

// Detects a leading "---" ... "---" (or "...") block. Returns where content starts.
FrontMatter readFrontMatter(IByteReader& reader) {
  FrontMatter fm;
  LineSource src(reader, 0);
  std::string line;
  uint64_t s = 0, e = 0;
  if (!src.next(line, s, e)) return fm;
  // BOM
  size_t bom = 0;
  if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF && static_cast<unsigned char>(line[1]) == 0xBB &&
      static_cast<unsigned char>(line[2]) == 0xBF) {
    bom = 3;
  }
  fm.contentStart = bom;
  if (trim(line.substr(bom)) != "---") return fm;

  std::vector<std::pair<std::string, std::string>> kv;
  bool closed = false;
  for (size_t n = 0; n < kMaxFrontMatterLines && src.next(line, s, e); ++n) {
    const std::string t = trim(line);
    if (t == "---" || t == "...") {
      closed = true;
      fm.contentStart = e;
      break;
    }
    const size_t colon = t.find(':');
    if (colon != std::string::npos && colon > 0 && !isBlank(line[0])) {
      kv.emplace_back(trim(t.substr(0, colon)), unquote(t.substr(colon + 1)));
    }
  }
  if (!closed) {
    fm.contentStart = bom;
    return fm;
  }
  for (const auto& p : kv) {
    std::string key = p.first;
    for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (key == "title" && fm.title.empty()) fm.title = p.second;
    else if ((key == "author" || key == "authors") && fm.author.empty()) fm.author = p.second;
    else if ((key == "lang" || key == "language") && fm.language.empty()) fm.language = p.second;
    else if (key == "date" && fm.date.empty()) fm.date = p.second;
  }
  return fm;
}

bool storePoolTitle(Fb2ScanResult& out, const std::string& value, uint32_t& offset, uint16_t& length) {
  offset = UINT32_MAX;
  length = 0;
  if (value.empty() || out.stringPool.size() >= kPoolHardLimit) return false;
  const size_t start = out.stringPool.size();
  bool pendingSpace = false;
  size_t taken = 0;
  for (const unsigned char c : value) {
    if (taken >= kMaxTitleBytes || out.stringPool.size() >= kPoolHardLimit) break;
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
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

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void emitSpans(Fb2ContentSink& sink, const std::vector<Span>& spans, Fb2InlineStyle extra = Fb2InlineStyle::Regular) {
  for (const Span& s : spans) {
    if (!s.text.empty()) sink.onText(s.text, s.style | extra);
  }
}

// Leading spaces/tabs of code lines become no-break spaces so indentation survives layout.
std::string preserveIndent(const std::string& line) {
  std::string out;
  size_t i = 0;
  for (; i < line.size() && isBlank(line[i]); ++i) out += (line[i] == '\t') ? "\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0" : "\xC2\xA0";
  out += line.substr(i);
  return out;
}

class Renderer {
 public:
  explicit Renderer(Fb2ContentSink& sink) : sink_(sink) {}

  void block(const Block& b) {
    if (b.type != BT::Quote) closeQuote();
    if (b.type != BT::TableRow) closeTable();
    switch (b.type) {
      case BT::Heading: {  // H4..H6 inside a chapter
        sink_.onSubtitleBegin();
        emitSpans(sink_, parseInline(b.text), Fb2InlineStyle::Bold);
        sink_.onSubtitleEnd();
        break;
      }
      case BT::Paragraph:
        sink_.onParagraphBegin();
        emitSpans(sink_, parseInline(b.text));
        sink_.onParagraphEnd();
        break;
      case BT::ListItem: {
        std::string marker;
        for (int i = 0; i < b.level; ++i) marker += "\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0";
        if (b.task == 1) marker += "\xE2\x98\x90";       // ☐
        else if (b.task == 2) marker += "\xE2\x98\x91";  // ☑
        else if (b.ordered) marker += b.number;
        else marker += "\xE2\x80\xA2";                   // •
        marker += "\xC2\xA0";
        sink_.onParagraphBegin();
        sink_.onText(marker, Fb2InlineStyle::Regular);
        emitSpans(sink_, parseInline(b.text));
        sink_.onParagraphEnd();
        break;
      }
      case BT::Quote:
        if (!inQuote_) {
          sink_.onCiteBegin();
          inQuote_ = true;
        }
        sink_.onParagraphBegin();
        emitSpans(sink_, parseInline(b.text));
        sink_.onParagraphEnd();
        break;
      case BT::CodeBegin:
        sink_.onCiteBegin();
        inCode_ = true;
        break;
      case BT::CodeLine:
        if (b.text.empty()) {
          sink_.onEmptyLine();
        } else {
          sink_.onParagraphBegin();
          sink_.onText(preserveIndent(b.text), Fb2InlineStyle::Regular);
          sink_.onParagraphEnd();
        }
        break;
      case BT::CodeEnd:
        if (inCode_) sink_.onCiteEnd();
        inCode_ = false;
        break;
      case BT::TableRow: {
        if (!inTable_) {
          sink_.onTableBegin();
          inTable_ = true;
        }
        sink_.onTableRowBegin();
        for (size_t i = 0; i < b.cells.size(); ++i) {
          Fb2TableCellAttrs attrs;
          attrs.isHeader = b.isHeaderRow;
          if (i < b.aligns.size()) attrs.align = b.aligns[i];
          sink_.onTableCell(plainText(parseInline(b.cells[i])), attrs);
        }
        sink_.onTableRowEnd();
        break;
      }
      case BT::Rule:
        sink_.onEmptyLine();
        break;
    }
  }

  void finish() {
    closeQuote();
    closeTable();
    if (inCode_) sink_.onCiteEnd();  // unterminated fence runs to the end of the chapter
    inCode_ = false;
  }

 private:
  void closeQuote() {
    if (inQuote_) sink_.onCiteEnd();
    inQuote_ = false;
  }
  void closeTable() {
    if (inTable_) sink_.onTableEnd();
    inTable_ = false;
  }

  Fb2ContentSink& sink_;
  bool inQuote_ = false;
  bool inCode_ = false;
  bool inTable_ = false;
};

}  // namespace

// ===========================================================================
// scan
// ===========================================================================

bool MarkdownParser::scan(IByteReader& reader, Fb2ScanResult& out, size_t) {
  out = Fb2ScanResult{};
  out.bodies.push_back(Fb2BodyIndexEntry{});
  out.stringPool.reserve(8 * 1024);

  const FrontMatter fm = readFrontMatter(reader);
  out.metadata.title = fm.title;
  out.metadata.author = fm.author;
  out.metadata.language = fm.language;
  out.metadata.date = fm.date;
  if (!fm.author.empty()) {
    Fb2Author a;
    a.nickname = fm.author;
    out.metadata.authors.push_back(a);
  }

  BlockReader blocks(reader, fm.contentStart);
  std::vector<int> headingStack;  // markdown levels of the open chapters
  Block b;
  bool haveSection = false;
  bool firstH1Taken = !fm.title.empty();

  auto addSection = [&](uint64_t offset, uint16_t level) -> Fb2SectionIndexEntry& {
    Fb2SectionIndexEntry e;
    e.level = level;
    e.bodyIndex = 0;
    e.innerStartOffset = static_cast<uint32_t>(std::min<uint64_t>(offset, UINT32_MAX));
    out.sections.push_back(e);
    haveSection = true;
    return out.sections.back();
  };

  while (blocks.next(b)) {
    out.tokenCount++;
    if (b.type == BT::Heading && b.level <= kMaxChapterHeading) {
      while (!headingStack.empty() && headingStack.back() >= b.level) headingStack.pop_back();
      Fb2SectionIndexEntry& s = addSection(b.start, static_cast<uint16_t>(headingStack.size()));
      headingStack.push_back(b.level);
      const std::string title = plainText(parseInline(b.text));
      storePoolTitle(out, title, s.titlePoolOffset, s.titleLength);
      if (!firstH1Taken && b.level == 1) {
        out.metadata.title = title;
        firstH1Taken = true;
      }
      continue;
    }
    if (!haveSection) addSection(fm.contentStart, 0);  // untitled opening chapter
    Fb2SectionIndexEntry& s = out.sections.back();
    s.approxTextBytes += static_cast<uint32_t>(std::min<size_t>(b.text.size(), UINT32_MAX / 2));
    out.textTokenCount++;
    out.textPayloadBytes += b.text.size();
  }
  if (!haveSection) addSection(fm.contentStart, 0);  // empty document: one empty chapter, not a failure
  return true;
}

// ===========================================================================
// render
// ===========================================================================

bool MarkdownParser::renderSectionTitle(IByteReader& reader, const Fb2SectionIndexEntry& section, Fb2ContentSink& sink,
                                        uint8_t level, const reader::ReaderCancellationToken* token) {
  if (cancelled(token)) return false;
  BlockReader blocks(reader, section.innerStartOffset);
  Block b;
  if (!blocks.next(b) || b.type != BT::Heading || b.level > kMaxChapterHeading) return false;  // untitled opening chapter
  const std::vector<Span> spans = parseInline(b.text);
  if (plainText(spans).empty()) return false;
  sink.onTitleBegin(level);
  emitSpans(sink, spans);
  sink.onTitleEnd(level);
  return true;
}

bool MarkdownParser::renderSection(IByteReader& reader, const Fb2SectionIndexEntry& section, Fb2ContentSink& sink,
                                   const reader::ReaderCancellationToken* token) {
  BlockReader blocks(reader, section.innerStartOffset);
  Renderer renderer(sink);
  Block b;
  bool first = true;
  while (blocks.next(b)) {
    if (cancelled(token)) return false;
    if (b.type == BT::Heading && b.level <= kMaxChapterHeading) {
      if (first) {  // this chapter's own heading: rendered by renderSectionTitle()
        first = false;
        continue;
      }
      break;  // the next chapter starts here
    }
    first = false;
    renderer.block(b);
  }
  renderer.finish();
  return true;
}
