// BookTestSupport.h - helpers shared by the native book-format tests: an
// in-memory IByteReader, a STORED-entry ZIP writer, and a sink that serialises
// content events into a compact, comparable log.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <FictionBookTypes.h>
#include <IByteReader.h>

namespace booktest {

class MemoryReader final : public IByteReader {
 public:
  explicit MemoryReader(std::vector<uint8_t> data) : data_(std::move(data)) {}
  explicit MemoryReader(const std::string& text) : data_(text.begin(), text.end()) {}

  size_t read(void* dst, size_t len) override {
    const size_t available = position_ < data_.size() ? data_.size() - position_ : 0;
    const size_t count = std::min(len, available);
    if (count != 0) std::memcpy(dst, data_.data() + position_, count);
    position_ += count;
    return count;
  }
  bool seek(uint64_t pos) override {
    if (pos > data_.size()) return false;
    position_ = static_cast<size_t>(pos);
    return true;
  }
  uint64_t tell() const override { return position_; }
  uint64_t size() const override { return data_.size(); }

 private:
  std::vector<uint8_t> data_;
  size_t position_ = 0;
};

inline void put16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v));
  out.push_back(static_cast<uint8_t>(v >> 8));
}
inline void put32(std::vector<uint8_t>& out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

// Builds a ZIP of STORED entries (the reader does not verify CRCs).
inline std::vector<uint8_t> makeZip(const std::vector<std::pair<std::string, std::string>>& entries) {
  std::vector<uint8_t> out;
  std::vector<uint8_t> central;
  for (const auto& [name, data] : entries) {
    const uint32_t offset = static_cast<uint32_t>(out.size());
    put32(out, 0x04034b50);
    put16(out, 20);
    put16(out, 0);
    put16(out, 0);  // stored
    put16(out, 0);
    put16(out, 0);
    put32(out, 0);
    put32(out, static_cast<uint32_t>(data.size()));
    put32(out, static_cast<uint32_t>(data.size()));
    put16(out, static_cast<uint16_t>(name.size()));
    put16(out, 0);
    out.insert(out.end(), name.begin(), name.end());
    out.insert(out.end(), data.begin(), data.end());

    put32(central, 0x02014b50);
    put16(central, 20);
    put16(central, 20);
    put16(central, 0);
    put16(central, 0);
    put16(central, 0);
    put16(central, 0);
    put32(central, 0);
    put32(central, static_cast<uint32_t>(data.size()));
    put32(central, static_cast<uint32_t>(data.size()));
    put16(central, static_cast<uint16_t>(name.size()));
    put16(central, 0);
    put16(central, 0);
    put16(central, 0);
    put16(central, 0);
    put32(central, 0);
    put32(central, offset);
    central.insert(central.end(), name.begin(), name.end());
  }
  const uint32_t cdOffset = static_cast<uint32_t>(out.size());
  out.insert(out.end(), central.begin(), central.end());
  put32(out, 0x06054b50);
  put16(out, 0);
  put16(out, 0);
  put16(out, static_cast<uint16_t>(entries.size()));
  put16(out, static_cast<uint16_t>(entries.size()));
  put32(out, static_cast<uint32_t>(central.size()));
  put32(out, cdOffset);
  put16(out, 0);
  return out;
}

// Serialises sink events, e.g. "<p>Hello {B:bold}</p>".
class LogSink final : public Fb2ContentSink {
 public:
  std::string log;
  int imageCalls = 0;

  void onParagraphBegin() override { log += "<p>"; }
  void onParagraphEnd() override { log += "</p>"; }
  void onSubtitleBegin() override { log += "<sub>"; }
  void onSubtitleEnd() override { log += "</sub>"; }
  void onTitleBegin(uint8_t level) override { log += "<title" + std::to_string(level) + ">"; }
  void onTitleEnd(uint8_t) override { log += "</title>"; }
  void onTitleLineBreak() override { log += "<br/>"; }
  void onEmptyLine() override { log += "<empty/>"; }
  void onCiteBegin() override { log += "<cite>"; }
  void onCiteEnd() override { log += "</cite>"; }
  void onText(const std::string& text, Fb2InlineStyle style) override {
    const uint8_t b = static_cast<uint8_t>(style);
    if (b == 0) {
      log += text;
      return;
    }
    std::string tag;
    if (b & static_cast<uint8_t>(Fb2InlineStyle::Bold)) tag += 'B';
    if (b & static_cast<uint8_t>(Fb2InlineStyle::Italic)) tag += 'I';
    if (b & static_cast<uint8_t>(Fb2InlineStyle::Underline)) tag += 'U';
    if (b & static_cast<uint8_t>(Fb2InlineStyle::Strikethrough)) tag += 'S';
    if (b & static_cast<uint8_t>(Fb2InlineStyle::Superscript)) tag += '^';
    if (b & static_cast<uint8_t>(Fb2InlineStyle::Subscript)) tag += '_';
    if (b & static_cast<uint8_t>(Fb2InlineStyle::SmallCaps)) tag += 'C';
    log += "{" + tag + ":" + text + "}";
  }
  void onImage(const std::string& id) override {
    ++imageCalls;
    log += "<img:" + id + ">";
  }
  void onLinkBegin(const std::string& target) override { log += "<a:" + target + ">"; }
  void onLinkEnd() override { log += "</a>"; }
  void onTableBegin() override { log += "<table>"; }
  void onTableEnd() override { log += "</table>"; }
  void onTableRowBegin() override { log += "<tr>"; }
  void onTableRowEnd() override { log += "</tr>"; }
  void onTableCell(const std::string& text, const Fb2TableCellAttrs& a) override {
    log += std::string(a.isHeader ? "<th" : "<td");
    if (a.colspan != 1) log += " cs=" + std::to_string(a.colspan);
    if (a.rowspan != 1) log += " rs=" + std::to_string(a.rowspan);
    if (!a.valign.empty()) log += " va=" + a.valign;
    log += ":" + text + ">";
  }
};

}  // namespace booktest
