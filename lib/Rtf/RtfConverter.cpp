#include "RtfConverter.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <Codepage.h>

namespace rtf {

namespace {

constexpr size_t kMaxGroupDepth = 96;
constexpr size_t kMaxCapture = 512;  // title/author/field text
constexpr size_t kOutChunk = 4096;

enum class Dest : uint8_t {
  Normal,
  Skip,
  FontTable,
  Stylesheet,
  Info,
  InfoTitle,
  InfoAuthor,
  Pict,
  FldInst,
  FldRslt,
  Bookmark,
};

struct Group {
  bool bold = false, italic = false, underline = false, strike = false, sup = false, sub = false, caps = false;
  Dest dest = Dest::Normal;
  int font = -1;
  int uc = 1;
  int skip = 0;           // fallback characters still to drop after \uN
  bool link = false;      // this group opened an <a href>
  bool owner = false;     // this group itself started its destination
};

bool isSkipDestination(const std::string& w) {
  static const char* const kSkip[] = {
      "colortbl",   "listtable",   "listoverridetable", "rsidtbl",    "generator",   "xmlnstbl", "themedata",
      "colorschememapping", "latentstyles", "datastore", "pgptbl",    "revtbl",      "filetbl",  "mmathpr",
      "userprops",  "wgrffmtfilter", "background", "header",     "headerl",     "headerr",  "headerf",
      "footer",     "footerl",     "footerr",     "footerf",   "pn",          "pntxta",   "pntxtb",
      "pnseclvl",   "footnote",    "object",      "objdata",   "result",      "nonshppict", "bkmkend",
      "annotation", "atnauthor",   "atnid",       "atndate",   "atnref",      "comment",  "xe",
      "tc",         "txe",         "blipuid",     "picprop",   "shpinst",     "shprslt",  "sp",
      "fchars",     "lchars",      "ftnsep",      "ftnsepc",   "aftnsep",     "aftnsepc", "docvar",
      "private",    "template",    "protusertbl", "falt",      "fname",       "panose",   "fontemb",
      "fontfile",   "listtext_",   "listlevel",   "levelnumbers", "leveltext", "stylerestrictions",
      "keycode",    "ud",          "password",    "passwordhash", "fldtype",   "datafield", "formfield",
      "do",         "mmath",       "oleclsid",    "objclass",  "objname",     "rsid",
  };
  for (const char* s : kSkip) {
    if (w == s) return true;
  }
  return false;
}

class Converter {
 public:
  Converter(IByteReader& in, const TextSink& out, const CancelFn& cancel) : in_(in), out_(out), cancel_(cancel) {}

  bool run() {
    if (!in_.seek(0)) return false;
    // Skip BOM / leading whitespace; require "{\rtf".
    int c = peek();
    while (c == 0xEF || c == 0xBB || c == 0xBF || c == ' ' || c == '\r' || c == '\n') {
      get();
      c = peek();
    }
    if (c != '{') return false;
    groups_.reserve(16);
    Group root;
    groups_.push_back(root);
    bool first = true;
    uint32_t counter = 0;
    while ((c = get()) >= 0) {
      if ((++counter & 0x3FFF) == 0 && cancel_ && cancel_()) return false;
      if (failed_) return false;
      if (c == '{') {
        pushGroup();
        atGroupStart_ = true;
        star_ = false;
        continue;
      }
      if (c == '}') {
        if (!popGroup()) break;
        continue;
      }
      if (c == '\\') {
        control();
        continue;
      }
      if (c == '\r' || c == '\n') continue;
      if (first) {
        first = false;
      }
      atGroupStart_ = false;
      plainChar(static_cast<uint8_t>(c));
    }
    while (groups_.size() > 1) popGroup();
    finishDocument();
    return !failed_ && flushOut();
  }

 private:
  // ---- input ----
  int peek() {
    if (bufPos_ == bufLen_ && !fill()) return -1;
    return buf_[bufPos_];
  }
  int get() {
    if (bufPos_ == bufLen_ && !fill()) return -1;
    ++pos_;
    return buf_[bufPos_++];
  }
  bool fill() {
    bufLen_ = in_.read(buf_, sizeof(buf_));
    bufPos_ = 0;
    return bufLen_ > 0;
  }

  // ---- output ----
  void write(const char* s, size_t n) {
    out_buf_.append(s, n);
    if (out_buf_.size() >= kOutChunk) flushOut();
  }
  void write(const char* s) { write(s, strlen(s)); }
  bool flushOut() {
    if (!out_buf_.empty()) {
      if (!out_(out_buf_.data(), out_buf_.size())) failed_ = true;
      out_buf_.clear();
    }
    return !failed_;
  }
  void writeEscaped(const std::string& s) {
    for (const char c : s) {
      switch (c) {
        case '&': write("&amp;"); break;
        case '<': write("&lt;"); break;
        case '>': write("&gt;"); break;
        case '"': write("&quot;"); break;
        default: out_buf_.push_back(c); break;
      }
    }
    if (out_buf_.size() >= kOutChunk) flushOut();
  }

  void ensureHead() {
    if (headWritten_) return;
    headWritten_ = true;
    write("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>");
    writeEscaped(title_);
    write("</title>");
    if (!author_.empty()) {
      write("<meta name=\"author\" content=\"");
      writeEscaped(author_);
      write("\">");
    }
    write("</head><body>\n");
  }

  // ---- groups ----
  Group& cur() { return groups_.back(); }

  void pushGroup() {
    if (groups_.size() >= kMaxGroupDepth) {
      ++overflow_;
      return;
    }
    Group g = groups_.back();
    g.skip = 0;
    g.link = false;
    g.owner = false;
    if (g.dest == Dest::Stylesheet) {
      curStyleId_ = -1;
      styleName_.clear();
    }
    groups_.push_back(g);
  }

  bool popGroup() {
    if (overflow_ > 0) {
      --overflow_;
      return true;
    }
    if (groups_.size() <= 1) return false;
    const Group g = groups_.back();
    groups_.pop_back();
    endDestination(g);
    return true;
  }

  void endDestination(const Group& g) {
    if (!g.owner) {
      return;
    }
    switch (g.dest) {
      case Dest::InfoTitle:
        if (title_.empty()) title_ = trimCapture();
        capture_.clear();
        break;
      case Dest::InfoAuthor:
        if (author_.empty()) author_ = trimCapture();
        capture_.clear();
        break;
      case Dest::Pict:
        endPicture();
        break;
      case Dest::FldInst:
        parseFieldInstruction();
        capture_.clear();
        break;
      case Dest::Bookmark: {
        const std::string name = trimCapture();
        capture_.clear();
        if (!name.empty()) {
          ensureHead();
          write("<a id=\"");
          writeEscaped(name);
          write("\"></a>");
        }
        break;
      }
      default:
        break;
    }
    if (g.link) {
      write("</a>");
    }
  }

  std::string trimCapture() {
    size_t b = 0, e = capture_.size();
    while (b < e && isspace(static_cast<unsigned char>(capture_[b]))) ++b;
    while (e > b && isspace(static_cast<unsigned char>(capture_[e - 1]))) --e;
    return capture_.substr(b, e - b);
  }

  // ---- control words ----
  void control() {
    const int c = get();
    if (c < 0) return;
    if (std::isalpha(c)) {
      std::string word(1, static_cast<char>(std::tolower(c)));
      while (std::isalpha(peek())) {
        if (word.size() < 32) word.push_back(static_cast<char>(std::tolower(get())));
        else get();
      }
      bool hasParam = false;
      long param = 0;
      bool neg = false;
      if (peek() == '-') {
        neg = true;
        get();
      }
      if (std::isdigit(peek())) {
        hasParam = true;
        while (std::isdigit(peek())) {
          param = param * 10 + (get() - '0');
          if (param > 100000000) param = 100000000;
        }
        if (neg) param = -param;
      } else if (neg) {
        // a lone '-' after the word is literal text
        plainChar('-');
      }
      if (peek() == ' ') get();  // the delimiter space belongs to the control word
      controlWord(word, hasParam, static_cast<int>(param));
      return;
    }
    atGroupStart_ = atGroupStart_ && c == '*';
    switch (c) {
      case '\'': {
        const int h1 = get();
        const int h2 = get();
        auto hv = [](int x) { return std::isdigit(x) ? x - '0' : (std::tolower(x) - 'a' + 10); };
        if (std::isxdigit(h1) && std::isxdigit(h2)) {
          if (cur().skip > 0 && cur().dest != Dest::Skip) --cur().skip;  // fallback for a preceding \uN
          else byteChar(static_cast<uint8_t>(hv(h1) * 16 + hv(h2)));
        }
        break;
      }
      case '*':
        star_ = true;
        atGroupStart_ = true;  // the next control word names the destination
        break;
      case '~': codePointText(0x00A0); break;
      case '_': codePointText('-'); break;
      case '-': break;  // optional hyphen
      case '\n':
      case '\r': endParagraph(); break;
      case '\\': codePointText('\\'); break;
      case '{': codePointText('{'); break;
      case '}': codePointText('}'); break;
      default: break;
    }
  }

  void setDestination(Dest d) {
    cur().dest = d;
    cur().owner = true;
    atGroupStart_ = false;
    star_ = false;
  }

  void controlWord(const std::string& w, bool hasParam, int param) {
    Group& g = cur();
    const bool startOfGroup = atGroupStart_;
    const bool starred = star_;
    atGroupStart_ = false;
    star_ = false;

    // ---- destinations ----
    if (startOfGroup || starred) {
      if (w == "fonttbl") return setDestination(Dest::FontTable);
      if (w == "stylesheet") return setDestination(Dest::Stylesheet);
      if (w == "info") return setDestination(Dest::Info);
      if (w == "title" && g.dest == Dest::Info) return setDestination(Dest::InfoTitle);
      if (w == "author" && g.dest == Dest::Info) return setDestination(Dest::InfoAuthor);
      if (w == "pict") return startPicture();
      if (w == "shppict") return;
      if (w == "fldinst") {
        capture_.clear();
        return setDestination(Dest::FldInst);
      }
      if (w == "fldrslt") {
        g.dest = Dest::FldRslt;
        g.owner = true;
        if (!pendingLink_.empty()) {
          openParagraphIfNeeded();
          write("<a href=\"#");
          writeEscaped(pendingLink_);
          write("\">");
          g.link = true;
          pendingLink_.clear();
        }
        return;
      }
      if (w == "bkmkstart") {
        capture_.clear();
        return setDestination(Dest::Bookmark);
      }
      if (w == "listtext" || w == "pntext" || w == "field" || w == "fldtype") {
        if (w == "fldtype") return setDestination(Dest::Skip);
        return;
      }
      if (isSkipDestination(w) || starred) {
        if (g.dest == Dest::Info) return setDestination(Dest::Skip);  // subject, keywords, ...
        return setDestination(Dest::Skip);
      }
    }

    if (g.dest == Dest::Skip) return;
    if (g.dest == Dest::FontTable) {
      if (w == "f" && hasParam) fontDef_ = param;
      else if (w == "fcharset" && hasParam && fontDef_ >= 0) {
        const int cp = fictionbook::codepageForCharset(param);
        if (cp != 0 && param != 0) fontCp_[fontDef_] = cp;
      }
      return;
    }
    if (g.dest == Dest::Stylesheet) {
      if (w == "s" && hasParam) curStyleId_ = param;
      else if (w == "outlinelevel" && hasParam && curStyleId_ >= 0 && param >= 0 && param <= 8) {
        if (!styleLevel_.count(curStyleId_)) styleLevel_[curStyleId_] = param + 1;
      }
      return;
    }
    if (g.dest == Dest::Pict) {
      if (w == "pngblip") pictMime_ = "image/png";
      else if (w == "jpegblip") pictMime_ = "image/jpeg";
      else if (w == "emfblip" || w == "wmetafile" || w == "dibitmap" || w == "wbitmap" || w == "macpict" ||
               w == "pmmetafile") pictBad_ = true;
      else if (w == "bin") pictBad_ = true;
      return;
    }
    if (g.dest != Dest::Normal && g.dest != Dest::FldRslt) {
      // Info text groups, field instructions, bookmarks: only \u and \' matter.
      if (w == "u" && hasParam) unicodeChar(param);
      return;
    }

    // ---- document-level ----
    if (w == "ansicpg") {
      if (hasParam && fictionbook::isSupportedCodepage(param)) docCodepage_ = param;
      return;
    }
    if (w == "deff") return;

    // ---- character formatting ----
    if (w == "b") { g.bold = !hasParam || param != 0; return; }
    if (w == "i") { g.italic = !hasParam || param != 0; return; }
    if (w == "ul" || w == "uld" || w == "uldb" || w == "ulw" || w == "uldash" || w == "uldashd" || w == "uldashdd" ||
        w == "ulth" || w == "ulwave" || w == "ulhwave" || w == "ululdbwave") {
      g.underline = !hasParam || param != 0;
      return;
    }
    if (w == "ulnone") { g.underline = false; return; }
    if (w == "strike" || w == "striked") { g.strike = !hasParam || param != 0; return; }
    if (w == "super") { g.sup = true; g.sub = false; return; }
    if (w == "sub") { g.sub = true; g.sup = false; return; }
    if (w == "nosupersub") { g.sup = g.sub = false; return; }
    if (w == "scaps") { g.caps = !hasParam || param != 0; return; }
    if (w == "plain") {
      g.bold = g.italic = g.underline = g.strike = g.sup = g.sub = g.caps = false;
      return;
    }
    if (w == "f" && hasParam) { g.font = param; return; }
    if (w == "uc" && hasParam) { g.uc = param < 0 ? 0 : (param > 8 ? 8 : param); return; }
    if (w == "u" && hasParam) { unicodeChar(param); return; }

    // ---- paragraph formatting ----
    if (w == "pard") {
      paraLevel_ = 0;
      inTablePara_ = false;
      return;
    }
    if (w == "s" && hasParam) {
      auto it = styleLevel_.find(param);
      if (it != styleLevel_.end()) paraLevel_ = it->second;
      return;
    }
    if (w == "outlinelevel" && hasParam) {
      if (param >= 0 && param <= 8) paraLevel_ = param + 1;
      return;
    }
    if (w == "intbl") { inTablePara_ = true; return; }
    if (w == "trowd") { return; }
    if (w == "par" || w == "sect" || w == "page" || w == "pagebb") { endParagraph(); return; }
    if (w == "cell" || w == "nestcell") { endCell(); return; }
    if (w == "row" || w == "nestrow") { endRow(); return; }
    if (w == "line") {
      openParagraphIfNeeded();
      syncInline();
      write("<br/>");
      return;
    }
    if (w == "tab") { codePointText(0x00A0); codePointText(0x00A0); codePointText(0x00A0); codePointText(0x00A0); return; }
    if (w == "emdash") { codePointText(0x2014); return; }
    if (w == "endash") { codePointText(0x2013); return; }
    if (w == "emspace" || w == "enspace" || w == "qmspace") { codePointText(0x2003); return; }
    if (w == "bullet") { codePointText(0x2022); return; }
    if (w == "lquote") { codePointText(0x2018); return; }
    if (w == "rquote") { codePointText(0x2019); return; }
    if (w == "ldblquote") { codePointText(0x201C); return; }
    if (w == "rdblquote") { codePointText(0x201D); return; }
    if (w == "zwj" || w == "zwnj") { return; }
  }

  // ---- text ----
  int currentCodepage() {
    const int f = cur().font;
    if (f >= 0) {
      auto it = fontCp_.find(f);
      if (it != fontCp_.end()) return it->second;
    }
    return docCodepage_;
  }

  void captureAppend(uint32_t cp) {
    if (capture_.size() < kMaxCapture) fictionbook::appendUtf8CodePoint(capture_, cp);
  }

  // A literal source byte (already known not to be structural).
  void plainChar(uint8_t b) {
    Group& g = cur();
    switch (g.dest) {
      case Dest::Skip:
      case Dest::FontTable:
        return;
      case Dest::Pict:
        if (std::isxdigit(b)) {
          if (!pictHexStarted_) {
            pictHexStarted_ = true;
            pictHexStart_ = pos_ - 1;
          }
          pictHexEnd_ = pos_;
        }
        return;
      case Dest::Stylesheet:
        if (b == ';') {
          commitStyleName();
        } else if (styleName_.size() < 48) {
          styleName_.push_back(static_cast<char>(b));
        }
        return;
      default:
        break;
    }
    if (g.skip > 0) {
      --g.skip;
      return;
    }
    if (b < 0x20) return;
    byteChar(b);
  }

  void commitStyleName() {
    std::string n;
    for (const char c : styleName_) n.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    while (!n.empty() && n.back() == ' ') n.pop_back();
    while (!n.empty() && n.front() == ' ') n.erase(0, 1);
    int level = 0;
    if (n.size() == 9 && n.compare(0, 8, "heading ") == 0 && n[8] >= '1' && n[8] <= '9') level = n[8] - '0';
    else if (n == "title") level = 1;
    else if (n == "subtitle") level = 2;
    if (level > 0 && curStyleId_ >= 0) styleLevel_[curStyleId_] = level;
    styleName_.clear();
  }

  // A byte in the current code page (from text or \'hh).
  void byteChar(uint8_t b) {
    Group& g = cur();
    if (g.dest == Dest::Skip || g.dest == Dest::FontTable || g.dest == Dest::Pict || g.dest == Dest::Stylesheet) return;
    const uint32_t cp = fictionbook::codepageToUnicode(currentCodepage(), b);
    if (cp == 0xFFFD && b >= 0x80) return;  // unsupported (e.g. CJK) byte
    codePointText(cp);
  }

  void unicodeChar(int param) {
    Group& g = cur();
    uint32_t cp = static_cast<uint32_t>(param < 0 ? param + 65536 : param);
    g.skip = g.uc;
    codePointText(cp);
  }

  void codePointText(uint32_t cp) {
    Group& g = cur();
    if (g.dest == Dest::Skip || g.dest == Dest::FontTable || g.dest == Dest::Pict || g.dest == Dest::Stylesheet) return;
    if (g.dest == Dest::InfoTitle || g.dest == Dest::InfoAuthor || g.dest == Dest::FldInst || g.dest == Dest::Bookmark) {
      captureAppend(cp);
      return;
    }
    if (g.dest == Dest::Info) return;
    openParagraphIfNeeded();
    syncInline();
    std::string utf8;
    fictionbook::appendUtf8CodePoint(utf8, cp);
    writeEscaped(utf8);
  }

  // ---- paragraphs, tables, inline tags ----
  void openParagraphIfNeeded() {
    if (paraOpen_) return;
    ensureHead();
    if (inTablePara_) {
      if (!tableOpen_) {
        write("<table>");
        tableOpen_ = true;
      }
      if (!rowOpen_) {
        write("<tr>");
        rowOpen_ = true;
      }
      if (!cellOpen_) {
        write("<td>");
        cellOpen_ = true;
      }
      write("<p>");
      paraTag_ = "p";
    } else {
      closeTable();
      if (paraLevel_ >= 1) {
        const int l = paraLevel_ > 6 ? 6 : paraLevel_;
        paraTag_ = std::string("h") + static_cast<char>('0' + l);
        write("<");
        write(paraTag_.c_str());
        write(">");
      } else {
        paraTag_ = "p";
        write("<p>");
      }
    }
    paraOpen_ = true;
  }

  void closeInline() {
    if (em_.caps) write("</span>");
    if (em_.sub) write("</sub>");
    if (em_.sup) write("</sup>");
    if (em_.strike) write("</s>");
    if (em_.underline) write("</u>");
    if (em_.italic) write("</i>");
    if (em_.bold) write("</b>");
    em_ = Group();
  }

  void syncInline() {
    const Group& g = cur();
    if (g.bold == em_.bold && g.italic == em_.italic && g.underline == em_.underline && g.strike == em_.strike &&
        g.sup == em_.sup && g.sub == em_.sub && g.caps == em_.caps) {
      return;
    }
    closeInline();
    if (g.bold) write("<b>");
    if (g.italic) write("<i>");
    if (g.underline) write("<u>");
    if (g.strike) write("<s>");
    if (g.sup) write("<sup>");
    if (g.sub) write("<sub>");
    if (g.caps) write("<span style=\"font-variant:small-caps\">");
    em_.bold = g.bold;
    em_.italic = g.italic;
    em_.underline = g.underline;
    em_.strike = g.strike;
    em_.sup = g.sup;
    em_.sub = g.sub;
    em_.caps = g.caps;
  }

  void closeParagraph() {
    if (!paraOpen_) return;
    closeInline();
    write("</");
    write(paraTag_.c_str());
    write(">\n");
    paraOpen_ = false;
  }

  void endParagraph() {
    if (cur().dest == Dest::Skip) return;
    closeParagraph();
  }

  void closeTable() {
    if (!tableOpen_) return;
    if (cellOpen_) write("</td>");
    if (rowOpen_) write("</tr>");
    write("</table>\n");
    tableOpen_ = rowOpen_ = cellOpen_ = false;
  }

  void endCell() {
    if (cur().dest == Dest::Skip) return;
    ensureHead();
    closeParagraph();
    if (!tableOpen_) {
      write("<table>");
      tableOpen_ = true;
    }
    if (!rowOpen_) {
      write("<tr>");
      rowOpen_ = true;
    }
    if (!cellOpen_) write("<td>");
    write("</td>");
    cellOpen_ = false;
  }

  void endRow() {
    if (cur().dest == Dest::Skip) return;
    closeParagraph();
    if (cellOpen_) {
      write("</td>");
      cellOpen_ = false;
    }
    if (rowOpen_) {
      write("</tr>");
      rowOpen_ = false;
    }
  }

  // ---- pictures ----
  void startPicture() {
    setDestination(Dest::Pict);
    pictMime_.clear();
    pictBad_ = false;
    pictHexStarted_ = false;
    pictHexStart_ = pictHexEnd_ = 0;
  }

  void endPicture() {
    if (pictBad_ || pictMime_.empty() || !pictHexStarted_ || pictHexEnd_ <= pictHexStart_) return;
    ensureHead();
    closeParagraph();
    if (inTablePara_ && tableOpen_) return;  // images inside cells are not shown
    write("<img src=\"x-hex:");
    write(std::to_string(pictHexStart_).c_str());
    write("-");
    write(std::to_string(pictHexEnd_).c_str());
    write(":");
    write(pictMime_.c_str());
    write("\"/>\n");
  }

  // ---- fields ----
  void parseFieldInstruction() {
    // HYPERLINK \l "bookmark"  -> internal link; PAGEREF/REF are left as plain text.
    pendingLink_.clear();
    std::string lower;
    for (const char c : capture_) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    const size_t h = lower.find("hyperlink");
    if (h == std::string::npos) return;
    const size_t l = lower.find("\\l", h);
    if (l == std::string::npos) return;
    const size_t q1 = capture_.find('"', l);
    if (q1 == std::string::npos) return;
    const size_t q2 = capture_.find('"', q1 + 1);
    if (q2 == std::string::npos) return;
    pendingLink_ = capture_.substr(q1 + 1, q2 - q1 - 1);
  }

  void finishDocument() {
    ensureHead();
    closeParagraph();
    closeTable();
    write("</body></html>\n");
  }

  IByteReader& in_;
  const TextSink& out_;
  const CancelFn& cancel_;
  uint8_t buf_[1024];
  size_t bufPos_ = 0, bufLen_ = 0;
  uint64_t pos_ = 0;

  std::string out_buf_;
  bool failed_ = false;

  std::vector<Group> groups_;
  size_t overflow_ = 0;
  bool atGroupStart_ = false;
  bool star_ = false;

  int docCodepage_ = 1252;
  std::map<int, int> fontCp_;
  int fontDef_ = -1;
  std::map<int, int> styleLevel_;
  int curStyleId_ = -1;
  std::string styleName_;

  std::string title_, author_;
  std::string capture_;
  std::string pendingLink_;

  bool headWritten_ = false;
  bool paraOpen_ = false;
  std::string paraTag_ = "p";
  int paraLevel_ = 0;
  bool inTablePara_ = false;
  bool tableOpen_ = false, rowOpen_ = false, cellOpen_ = false;
  Group em_;  // inline tags currently open in the output

  std::string pictMime_;
  bool pictBad_ = false;
  bool pictHexStarted_ = false;
  uint64_t pictHexStart_ = 0, pictHexEnd_ = 0;
};

}  // namespace

bool looksLikeRtf(const uint8_t* head, size_t len) {
  size_t i = 0;
  while (i < len && (head[i] == 0xEF || head[i] == 0xBB || head[i] == 0xBF || head[i] == ' ' || head[i] == '\r' || head[i] == '\n')) ++i;
  return i + 5 <= len && memcmp(head + i, "{\\rtf", 5) == 0;
}

bool convertToHtml(IByteReader& in, const TextSink& out, const CancelFn& cancel) {
  uint8_t head[16];
  if (!in.seek(0)) return false;
  const size_t n = in.read(head, sizeof(head));
  if (!looksLikeRtf(head, n)) return false;
  Converter c(in, out, cancel);
  return c.run();
}

}  // namespace rtf
