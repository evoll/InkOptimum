// HtmlTokenizer.h - forgiving streaming HTML tokenizer.
//
// Produces start tags, end tags and decoded text from an IByteReader without
// ever holding more than one tag or one bounded text chunk in memory, and
// reports the absolute byte offsets of every token so HtmlParser can index a
// document once and re-open it at any chapter later.
//
//   * Tag and attribute names are lower-cased. Attribute values keep at most
//     kMaxAttrValue bytes (entity-decoded); their exact byte range in the
//     stream is reported separately, which is how multi-megabyte data: URIs
//     are located without being loaded.
//   * Comments, doctypes, CDATA and processing instructions are skipped.
//   * The content of <script> and <style> is skipped without producing text.
//   * Input is expected to be UTF-8 (HtmlParser's caller transcodes legacy
//     encodings first); bytes are passed through unchanged.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <IByteReader.h>

namespace html {

struct HtmlAttr {
  std::string name;
  std::string value;       // entity-decoded, at most kMaxAttrValue raw bytes
  uint64_t valueStart = 0; // stream offset of the first value byte
  uint64_t valueEnd = 0;   // stream offset just past the last value byte
  bool truncated = false;
};

struct HtmlToken {
  enum class Type : uint8_t { Eof, StartTag, EndTag, Text };

  Type type = Type::Eof;
  std::string name;  // lower-case tag name
  std::vector<HtmlAttr> attrs;
  bool selfClosing = false;
  std::string text;  // decoded text (Type::Text)
  uint64_t start = 0;  // offset of the first byte of the token
  uint64_t end = 0;    // offset just past the token

  const HtmlAttr* attr(const char* attrName) const {
    for (const HtmlAttr& a : attrs) {
      if (a.name == attrName) return &a;
    }
    return nullptr;
  }
  const std::string* attrValue(const char* attrName) const {
    const HtmlAttr* a = attr(attrName);
    return a ? &a->value : nullptr;
  }
};

class HtmlTokenizer {
 public:
  static constexpr size_t kMaxAttrValue = 2048;
  static constexpr size_t kMaxAttrs = 24;
  static constexpr size_t kMaxTextChunk = 2048;

  HtmlTokenizer(IByteReader& reader, uint64_t startOffset);

  // Fills `token`; returns false (token.type == Eof) at the end of the stream.
  bool next(HtmlToken& token);

  // Offset of the next unread byte.
  uint64_t position() const { return pos_; }

 private:
  int peek();
  int get();
  bool fill();
  void skipUntil(const char* terminator);
  bool skipRawText(const std::string& name, uint64_t& endStart);
  bool readTag(HtmlToken& token, bool isEnd);
  bool readTagRest(HtmlToken& token);
  void readText(HtmlToken& token);

  IByteReader& reader_;
  uint64_t pos_;
  uint8_t buf_[1024];
  size_t bufPos_ = 0, bufLen_ = 0;
  bool eof_ = false;
  std::string rawTextName_;  // set after <script>/<style>: contents are skipped
};

}  // namespace html
