#pragma once

#include <Print.h>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

// Finds the cover page named by an EPUB 3 navigation document's landmarks:
//   <nav epub:type="landmarks"><ol><li><a epub:type="cover" href="cover.xhtml">Cover</a></li>...
// (or role="doc-cover"). Some books carry no <meta name="cover">, no cover-image property and no guide,
// and say where the cover is only here. The document streams through; only one tag is held at a time.
// The scan is done at the first cover link or after MAX_SCAN_BYTES; hand stopFlag() to the stream so the
// rest is not inflated.
class NavCoverLandmark final : public Print {
 public:
  static constexpr size_t MAX_TAG = 512;
  static constexpr size_t MAX_SCAN_BYTES = 128 * 1024;

  size_t write(const uint8_t c) override {
    feed(static_cast<char>(c));
    return 1;
  }
  size_t write(const uint8_t* buffer, const size_t size) override {
    for (size_t i = 0; i < size && !done_; i++) feed(static_cast<char>(buffer[i]));
    return size;
  }

  // The href as written (fragment included), not yet resolved against the document's folder; empty if none.
  const std::string& result() const { return result_; }
  const bool* stopFlag() const { return &done_; }

  // The value of attribute `name` in a tag's text, or empty. Matches whole names: "type" does not find
  // "epub:type" but "epub:type" does.
  static std::string_view attribute(const std::string_view tag, const std::string_view name) {
    size_t from = 0;
    while (true) {
      const size_t at = tag.find(name, from);
      if (at == std::string_view::npos) return {};
      from = at + 1;
      if (at == 0 || !isspace(static_cast<unsigned char>(tag[at - 1]))) continue;  // must start a name
      size_t i = at + name.size();
      while (i < tag.size() && isspace(static_cast<unsigned char>(tag[i]))) i++;
      if (i >= tag.size() || tag[i] != '=') continue;
      i++;
      while (i < tag.size() && isspace(static_cast<unsigned char>(tag[i]))) i++;
      if (i >= tag.size() || (tag[i] != '"' && tag[i] != '\'')) continue;
      const char quote = tag[i++];
      const size_t end = tag.find(quote, i);
      if (end == std::string_view::npos) return {};
      return tag.substr(i, end - i);
    }
  }

  // True when the space-separated `list` holds `token`.
  static bool hasToken(const std::string_view list, const std::string_view token) {
    size_t i = 0;
    while (i < list.size()) {
      while (i < list.size() && isspace(static_cast<unsigned char>(list[i]))) i++;
      size_t j = i;
      while (j < list.size() && !isspace(static_cast<unsigned char>(list[j]))) j++;
      if (j > i && list.substr(i, j - i) == token) return true;
      i = j;
    }
    return false;
  }

 private:
  void feed(const char c) {
    if (++scanned_ >= MAX_SCAN_BYTES) done_ = true;
    if (!inTag_) {
      if (c == '<') {
        inTag_ = true;
        tagLen_ = 0;
        tooLong_ = false;
      }
      return;
    }
    if (c == '>') {
      inTag_ = false;
      if (!tooLong_) handleTag(std::string_view{tag_, tagLen_});
      return;
    }
    if (tagLen_ < MAX_TAG) {
      tag_[tagLen_++] = c;
    } else {
      tooLong_ = true;
    }
  }

  void handleTag(const std::string_view tag) {
    if (tag.empty()) return;
    if (tag[0] == '/') {
      if (tag.substr(1, 3) == "nav" && inLandmarks_) inLandmarks_ = false;
      return;
    }
    size_t nameEnd = 0;
    while (nameEnd < tag.size() && !isspace(static_cast<unsigned char>(tag[nameEnd])) && tag[nameEnd] != '/') nameEnd++;
    const std::string_view name = tag.substr(0, nameEnd);
    if (name == "nav") {
      inLandmarks_ = hasToken(attribute(tag, "epub:type"), "landmarks");
      return;
    }
    if (name != "a" || !inLandmarks_) return;
    const bool isCover = hasToken(attribute(tag, "epub:type"), "cover") || attribute(tag, "role") == "doc-cover";
    if (!isCover) return;
    const std::string_view href = attribute(tag, "href");
    if (href.empty()) return;
    result_.assign(href);
    done_ = true;
  }

  char tag_[MAX_TAG] = {};
  size_t tagLen_ = 0;
  bool inTag_ = false;
  bool tooLong_ = false;
  bool inLandmarks_ = false;
  bool done_ = false;
  size_t scanned_ = 0;
  std::string result_;
};
