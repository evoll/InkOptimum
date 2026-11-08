#include "HtmlTokenizer.h"

#include <cctype>
#include <cstring>

#include "HtmlEntities.h"

namespace html {

namespace {

bool isSpace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
bool isAlpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
char lower(int c) { return static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c); }

}  // namespace

HtmlTokenizer::HtmlTokenizer(IByteReader& reader, uint64_t startOffset) : reader_(reader), pos_(startOffset) {
  if (!reader_.seek(startOffset)) {
    eof_ = true;
    return;
  }
  if (startOffset == 0 && fill() && bufLen_ >= 3 && buf_[0] == 0xEF && buf_[1] == 0xBB && buf_[2] == 0xBF) {
    bufPos_ = 3;  // UTF-8 byte order mark
    pos_ = 3;
  }
}

bool HtmlTokenizer::fill() {
  if (eof_) return false;
  bufLen_ = reader_.read(buf_, sizeof(buf_));
  bufPos_ = 0;
  if (bufLen_ == 0) eof_ = true;
  return bufLen_ > 0;
}

int HtmlTokenizer::peek() {
  if (bufPos_ == bufLen_ && !fill()) return -1;
  return buf_[bufPos_];
}

int HtmlTokenizer::get() {
  if (bufPos_ == bufLen_ && !fill()) return -1;
  ++pos_;
  return buf_[bufPos_++];
}

void HtmlTokenizer::skipUntil(const char* terminator) {
  const size_t n = strlen(terminator);
  size_t matched = 0;
  int c;
  while ((c = get()) >= 0) {
    if (c == static_cast<unsigned char>(terminator[matched])) {
      if (++matched == n) return;
    } else {
      matched = (c == static_cast<unsigned char>(terminator[0])) ? 1 : 0;
    }
  }
}

// Skips the body of <script>/<style>. Returns true when "</name" was consumed
// (the caller then finishes the end tag); `endStart` is the offset of its '<'.
bool HtmlTokenizer::skipRawText(const std::string& name, uint64_t& endStart) {
  const std::string needle = "</" + name;
  size_t matched = 0;
  int c;
  while ((c = get()) >= 0) {
    if (lower(c) == needle[matched]) {
      if (++matched == needle.size()) {
        endStart = pos_ - needle.size();
        return true;
      }
    } else {
      matched = (c == '<') ? 1 : 0;
    }
  }
  return false;
}

bool HtmlTokenizer::readTag(HtmlToken& token, bool isEnd) {
  // The leading '<' (and '/') are already consumed.
  token.type = isEnd ? HtmlToken::Type::EndTag : HtmlToken::Type::StartTag;
  token.name.clear();
  token.attrs.clear();
  token.selfClosing = false;
  int c;
  while ((c = peek()) >= 0 && !isSpace(c) && c != '>' && c != '/') {
    token.name.push_back(lower(c));
    get();
    if (token.name.size() > 64) break;
  }
  return readTagRest(token);
}

bool HtmlTokenizer::readTagRest(HtmlToken& token) {
  int c;
  for (;;) {
    while ((c = peek()) >= 0 && isSpace(c)) get();
    c = peek();
    if (c < 0) break;
    if (c == '>') {
      get();
      break;
    }
    if (c == '/') {
      get();
      if (peek() == '>') {
        get();
        token.selfClosing = true;
        break;
      }
      continue;
    }
    // attribute name
    HtmlAttr attr;
    while ((c = peek()) >= 0 && !isSpace(c) && c != '=' && c != '>' && c != '/') {
      attr.name.push_back(lower(c));
      get();
      if (attr.name.size() > 64) break;
    }
    while ((c = peek()) >= 0 && isSpace(c)) get();
    if (peek() == '=') {
      get();
      while ((c = peek()) >= 0 && isSpace(c)) get();
      c = peek();
      std::string raw;
      if (c == '"' || c == '\'') {
        const int quote = get();
        attr.valueStart = pos_;
        while ((c = peek()) >= 0 && c != quote) {
          if (raw.size() < kMaxAttrValue) raw.push_back(static_cast<char>(c));
          else attr.truncated = true;
          get();
        }
        attr.valueEnd = pos_;
        if (c == quote) get();
      } else {
        attr.valueStart = pos_;
        while ((c = peek()) >= 0 && !isSpace(c) && c != '>') {
          if (raw.size() < kMaxAttrValue) raw.push_back(static_cast<char>(c));
          else attr.truncated = true;
          get();
        }
        attr.valueEnd = pos_;
      }
      attr.value = decodeEntities(raw);
    } else {
      attr.valueStart = attr.valueEnd = pos_;
    }
    if (!attr.name.empty() && token.attrs.size() < kMaxAttrs) token.attrs.push_back(std::move(attr));
  }
  return true;
}

void HtmlTokenizer::readText(HtmlToken& token) {
  token.type = HtmlToken::Type::Text;
  token.text.clear();
  std::string raw;
  int c;
  while ((c = peek()) >= 0) {
    if (c == '<') {
      // '<' starts a token only before a letter, '/', '!' or '?'.
      // Look one byte beyond '<' without consuming anything we cannot rewind:
      // the buffer may end here, so consume '<' and decide from the next byte.
      break;
    }
    raw.push_back(static_cast<char>(c));
    get();
    if (raw.size() >= kMaxTextChunk && (static_cast<unsigned char>(c) & 0xC0) != 0x80 && c != '&') {
      // Flush at a character boundary that is not inside a reference.
      const size_t amp = raw.rfind('&');
      if (amp == std::string::npos || raw.find(';', amp) != std::string::npos) break;
    }
  }
  token.text = decodeEntities(raw);
}

bool HtmlTokenizer::next(HtmlToken& token) {
  token.type = HtmlToken::Type::Eof;
  for (;;) {
    if (!rawTextName_.empty()) {
      uint64_t endStart = 0;
      const bool found = skipRawText(rawTextName_, endStart);
      token.type = HtmlToken::Type::EndTag;
      token.name = rawTextName_;
      token.attrs.clear();
      token.selfClosing = false;
      rawTextName_.clear();
      if (!found) return false;
      token.start = endStart;
      readTagRest(token);
      token.end = pos_;
      return true;
    }
    token.start = pos_;
    const int c = peek();
    if (c < 0) return false;
    if (c != '<') {
      readText(token);
      token.end = pos_;
      if (token.text.empty()) continue;
      return true;
    }
    get();  // '<'
    const int n = peek();
    if (n == '/') {
      get();
      if (isAlpha(peek())) {
        readTag(token, true);
        token.end = pos_;
        return true;
      }
      // "</" not followed by a letter: bogus comment up to '>'.
      skipUntil(">");
      continue;
    }
    if (isAlpha(n)) {
      readTag(token, false);
      token.end = pos_;
      if (!token.selfClosing && (token.name == "script" || token.name == "style")) rawTextName_ = token.name;
      return true;
    }
    if (n == '!') {
      get();
      if (peek() == '-') {
        get();
        if (peek() == '-') {
          get();
          skipUntil("-->");
          continue;
        }
      }
      if (peek() == '[') {  // <![CDATA[ ... ]]>
        skipUntil("]]>");
        continue;
      }
      skipUntil(">");
      continue;
    }
    if (n == '?') {
      skipUntil(">");
      continue;
    }
    // A literal '<' in text.
    token.type = HtmlToken::Type::Text;
    token.text = "<";
    token.end = pos_;
    return true;
  }
}

}  // namespace html
