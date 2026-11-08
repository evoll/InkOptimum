#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "MarkdownParser.h"

namespace {

class MemoryReader final : public IByteReader {
 public:
  explicit MemoryReader(std::string data) : data_(std::move(data)) {}
  size_t read(void* dst, size_t len) override {
    const size_t avail = pos_ < data_.size() ? data_.size() - pos_ : 0;
    const size_t n = std::min(len, avail);
    if (n) std::memcpy(dst, data_.data() + pos_, n);
    pos_ += n;
    return n;
  }
  bool seek(uint64_t p) override {
    if (p > data_.size()) return false;
    pos_ = static_cast<size_t>(p);
    return true;
  }
  uint64_t tell() const override { return pos_; }
  uint64_t size() const override { return data_.size(); }

 private:
  std::string data_;
  size_t pos_ = 0;
};

// Compact serialisation of sink events, e.g. "<p>Hello {B:bold}</p>".
class LogSink final : public Fb2ContentSink {
 public:
  std::string log;
  void onParagraphBegin() override { log += "<p>"; }
  void onParagraphEnd() override { log += "</p>"; }
  void onSubtitleBegin() override { log += "<sub>"; }
  void onSubtitleEnd() override { log += "</sub>"; }
  void onTitleBegin(uint8_t level) override { log += "<title" + std::to_string(level) + ">"; }
  void onTitleEnd(uint8_t) override { log += "</title>"; }
  void onTitleLineBreak() override { log += "<br/>"; }
  void onEmptyLine() override { log += "<empty/>"; }
  void onPoemBegin() override { log += "<poem>"; }
  void onPoemEnd() override { log += "</poem>"; }
  void onStanzaBegin() override { log += "<stanza>"; }
  void onStanzaEnd() override { log += "</stanza>"; }
  void onVerseBegin() override { log += "<v>"; }
  void onVerseEnd() override { log += "</v>"; }
  void onCiteBegin() override { log += "<cite>"; }
  void onCiteEnd() override { log += "</cite>"; }
  void onEpigraphBegin() override { log += "<epi>"; }
  void onEpigraphEnd() override { log += "</epi>"; }
  void onTextAuthorBegin() override { log += "<ta>"; }
  void onTextAuthorEnd() override { log += "</ta>"; }
  void onText(const std::string& text, Fb2InlineStyle style) override {
    const uint8_t b = static_cast<uint8_t>(style);
    if (b == 0) {
      log += text;
      return;
    }
    std::string tag;
    if (b & static_cast<uint8_t>(Fb2InlineStyle::Bold)) tag += 'B';
    if (b & static_cast<uint8_t>(Fb2InlineStyle::Italic)) tag += 'I';
    if (b & static_cast<uint8_t>(Fb2InlineStyle::Strikethrough)) tag += 'S';
    log += "{" + tag + ":" + text + "}";
  }
  void onImage(const std::string& id) override { log += "<img:" + id + ">"; }
  void onLinkBegin(const std::string& t) override { log += "<a:" + t + ">"; }
  void onLinkEnd() override { log += "</a>"; }
  void onTableBegin() override { log += "<table>"; }
  void onTableEnd() override { log += "</table>"; }
  void onTableRowBegin() override { log += "<tr>"; }
  void onTableRowEnd() override { log += "</tr>"; }
  void onTableCell(const std::string& text, const Fb2TableCellAttrs& a) override {
    log += std::string(a.isHeader ? "<th" : "<td");
    if (!a.align.empty()) log += " al=" + a.align;
    log += ":" + text + ">";
  }
};

struct Doc {
  std::string text;
  MemoryReader reader;
  Fb2ScanResult scan;
  MarkdownParser parser;
  explicit Doc(const std::string& md) : text(md), reader(md) { EXPECT_TRUE(parser.scan(reader, scan)); }

  std::string section(size_t i) {
    LogSink s;
    EXPECT_TRUE(parser.renderSection(reader, scan.sections[i], s));
    return s.log;
  }
  std::string title(size_t i, uint8_t level = 0) {
    LogSink s;
    return parser.renderSectionTitle(reader, scan.sections[i], s, level) ? s.log : std::string("<none>");
  }
};

}  // namespace

TEST(MarkdownScan, HeadingsBecomeNestedChaptersWithTitles) {
  Doc d("# Book\n\nIntro text.\n\n## One\n\nA.\n\n### One-A\n\nB.\n\n## Two\n\nC.\n\n# Part II\n\nD.\n");
  ASSERT_EQ(d.scan.sections.size(), 5u);
  EXPECT_EQ(d.scan.sectionTitle(d.scan.sections[0]), "Book");
  EXPECT_EQ(d.scan.sections[0].level, 0);
  EXPECT_EQ(d.scan.sectionTitle(d.scan.sections[1]), "One");
  EXPECT_EQ(d.scan.sections[1].level, 1);
  EXPECT_EQ(d.scan.sections[2].level, 2);  // ### under ##
  EXPECT_EQ(d.scan.sections[3].level, 1);  // ## Two pops back
  EXPECT_EQ(d.scan.sectionTitle(d.scan.sections[4]), "Part II");
  EXPECT_EQ(d.scan.sections[4].level, 0);
  EXPECT_EQ(d.scan.metadata.title, "Book");  // first H1
  ASSERT_EQ(d.scan.bodies.size(), 1u);
  EXPECT_TRUE(d.scan.binaries.empty());
  EXPECT_GT(d.scan.sections[0].approxTextBytes, 0u);
}

TEST(MarkdownScan, TextBeforeFirstHeadingFormsUntitledOpeningChapter) {
  Doc d("Preface line.\n\n# Chapter\n\nBody.\n");
  ASSERT_EQ(d.scan.sections.size(), 2u);
  EXPECT_EQ(d.scan.sectionTitle(d.scan.sections[0]), "");
  EXPECT_EQ(d.title(0), "<none>");
  EXPECT_EQ(d.section(0), "<p>Preface line.</p>");
  EXPECT_EQ(d.title(1, 0), "<title0>Chapter</title>");
}

TEST(MarkdownScan, NoHeadingsAndEmptyDocumentsStillYieldAChapter) {
  Doc plain("Just a paragraph.\n");
  ASSERT_EQ(plain.scan.sections.size(), 1u);
  EXPECT_EQ(plain.section(0), "<p>Just a paragraph.</p>");
  Doc empty("");
  ASSERT_EQ(empty.scan.sections.size(), 1u);
  EXPECT_EQ(empty.section(0), "");
}

TEST(MarkdownScan, FrontMatterSuppliesMetadataAndIsNotRendered) {
  Doc d("---\ntitle: \"My Notes\"\nauthor: Jane Doe\nlang: en\ndate: 2024-05-01\n---\n\n# Heading\n\nText.\n");
  EXPECT_EQ(d.scan.metadata.title, "My Notes");  // front matter beats the first H1
  EXPECT_EQ(d.scan.metadata.author, "Jane Doe");
  EXPECT_EQ(d.scan.metadata.language, "en");
  EXPECT_EQ(d.scan.metadata.date, "2024-05-01");
  ASSERT_EQ(d.scan.sections.size(), 1u);
  EXPECT_EQ(d.section(0), "<p>Text.</p>");

  Doc unclosed("---\ntitle: x\n\n# Heading\n\nText.\n");  // not front matter: an hr and text
  EXPECT_EQ(unclosed.scan.metadata.title, "Heading");
}

TEST(MarkdownScan, SetextHeadingsAndHeadingsInsideCodeFences) {
  Doc d("Title Line\n==========\n\nText.\n\nSub\n---\n\n```\n# not a heading\n```\n");
  ASSERT_EQ(d.scan.sections.size(), 2u);
  EXPECT_EQ(d.scan.sectionTitle(d.scan.sections[0]), "Title Line");
  EXPECT_EQ(d.scan.sectionTitle(d.scan.sections[1]), "Sub");
  EXPECT_EQ(d.title(0), "<title0>Title Line</title>");
  EXPECT_EQ(d.section(1), "<cite><p># not a heading</p></cite>");
}

TEST(MarkdownScan, BomCrlfAndMissingFinalNewline) {
  Doc d(std::string("\xEF\xBB\xBF") + "# Title\r\n\r\nLine one\r\nline two\r\n\r\nLast without newline");
  ASSERT_EQ(d.scan.sections.size(), 1u);
  EXPECT_EQ(d.scan.sectionTitle(d.scan.sections[0]), "Title");
  EXPECT_EQ(d.section(0), "<p>Line one line two</p><p>Last without newline</p>");
}

TEST(MarkdownRender, ParagraphsJoinSoftBreaksAndStyleInline) {
  Doc d("# T\n\nSome *italic*, **bold**, ***both*** and ~~gone~~ text\nacross lines.\n\nSecond `code` para with snake_case_name.\n");
  EXPECT_EQ(d.section(0),
            "<p>Some {I:italic}, {B:bold}, {BI:both} and {S:gone} text across lines.</p>"
            "<p>Second code para with snake_case_name.</p>");
}

TEST(MarkdownRender, LinksImagesEscapesAndHtmlInline) {
  Doc d("# T\n\nSee [the **docs**](http://x.org) and ![logo](a.png) \\*literal\\* <http://auto.link> a<br>b <b>x</b>.\n");
  EXPECT_EQ(d.section(0), "<p>See the {B:docs} and [logo] *literal* http://auto.link a b x.</p>");
}

TEST(MarkdownRender, ListsKeepMarkersNestingAndTaskBoxes) {
  Doc d("# T\n\n- one\n- two\n  - nested\n    continued\n- [ ] todo\n- [x] done\n\n1. first\n2. second\n");
  EXPECT_EQ(d.section(0),
            "<p>\xE2\x80\xA2\xC2\xA0" "one</p>"
            "<p>\xE2\x80\xA2\xC2\xA0" "two</p>"
            "<p>\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xE2\x80\xA2\xC2\xA0" "nested continued</p>"
            "<p>\xE2\x98\x90\xC2\xA0" "todo</p>"
            "<p>\xE2\x98\x91\xC2\xA0" "done</p>"
            "<p>1.\xC2\xA0" "first</p><p>2.\xC2\xA0" "second</p>");
}

TEST(MarkdownRender, ListNestingFollowsActualIndentWidth) {
  Doc d("# T\n\n* a\n  * b (two-space indent)\n      * c\n* d\n");
  const std::string s = d.section(0);
  EXPECT_NE(s.find("\xE2\x80\xA2\xC2\xA0" "a"), std::string::npos);
  // b is level 1, c level 2, d back to level 0
  EXPECT_NE(s.find("<p>\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xE2\x80\xA2\xC2\xA0" "b"), std::string::npos);
  EXPECT_NE(s.find("<p>\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xE2\x80\xA2\xC2\xA0" "c"), std::string::npos);
  EXPECT_NE(s.find("<p>\xE2\x80\xA2\xC2\xA0" "d"), std::string::npos);
}

TEST(MarkdownRender, BlockquotesGroupParagraphsInOneCite) {
  Doc d("# T\n\n> first line\n> continues\n>\n> second para\n\nafter\n");
  EXPECT_EQ(d.section(0), "<cite><p>first line continues</p><p>second para</p></cite><p>after</p>");
}

TEST(MarkdownRender, CodeBlocksKeepLinesAndIndentation) {
  Doc d("# T\n\n```cpp\nint main() {\n    return 0;\n\n}\n```\ntail\n");
  EXPECT_EQ(d.section(0),
            "<cite><p>int main() {</p>"
            "<p>\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0return 0;</p><empty/><p>}</p></cite><p>tail</p>");
  Doc unterminated("# T\n\n~~~\nline\n");
  EXPECT_EQ(unterminated.section(0), "<cite><p>line</p></cite>");
}

TEST(MarkdownRender, TablesHaveHeaderAndAlignment) {
  Doc d("# T\n\n| Name | Qty |\n|:-----|----:|\n| **a** | 1 |\n| b | 2 |\n\ntext\n\n| x |\n|---|\n| y |\n");
  EXPECT_EQ(d.section(0),
            "<table><tr><th al=left:Name><th al=right:Qty></tr>"
            "<tr><td al=left:a><td al=right:1></tr><tr><td al=left:b><td al=right:2></tr></table>"
            "<p>text</p>"
            "<table><tr><th:x></tr><tr><td:y></tr></table>");  // second table gets its own header
}

TEST(MarkdownRender, RulesAndDeepHeadingsAndChapterBoundaries) {
  Doc d("# A\n\nbefore\n\n---\n\n#### Minor heading\n\nmid\n\n## B\n\nb text\n");
  ASSERT_EQ(d.scan.sections.size(), 2u);
  EXPECT_EQ(d.section(0), "<p>before</p><empty/><sub>{B:Minor heading}</sub><p>mid</p>");  // #### stays inside the chapter
  EXPECT_EQ(d.section(1), "<p>b text</p>");
  EXPECT_EQ(d.title(1, 1), "<title1>B</title>");
}

TEST(MarkdownRender, TitleKeepsInlineFormatting) {
  Doc d("# The *Great* **Gatsby**\n\nx\n");
  EXPECT_EQ(d.title(0, 0), "<title0>The {I:Great} {B:Gatsby}</title>");
  EXPECT_EQ(d.scan.sectionTitle(d.scan.sections[0]), "The Great Gatsby");
}

TEST(MarkdownRender, LongLinesAreSplitNotTruncated) {
  // 300 KiB on one line: far beyond the per-line and per-paragraph caps.
  std::string line;
  size_t words = 0;
  while (line.size() < 300 * 1024) {
    line += "w" + std::to_string(words++) + " ";
  }
  line.pop_back();
  Doc d("# T\n\n" + line + "\n\nend\n");
  const std::string s = d.section(0);

  // Strip the markup and compare the word sequence: no word lost, none doubled,
  // none glued together or split by the chunking.
  std::string flat;
  for (size_t i = 0; i < s.size();) {
    if (s.compare(i, 3, "<p>") == 0) { flat += ' '; i += 3; }
    else if (s.compare(i, 4, "</p>") == 0) { flat += ' '; i += 4; }
    else flat.push_back(s[i++]);
  }
  std::string expected = line + " end";
  std::string collapsed;
  bool prevSpace = false;
  for (char c : flat) {
    if (c == ' ') { if (!prevSpace && !collapsed.empty()) collapsed.push_back(' '); prevSpace = true; }
    else { collapsed.push_back(c); prevSpace = false; }
  }
  while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
  EXPECT_EQ(collapsed, expected);
}

TEST(MarkdownRender, StrayMarkersAreLiteral) {
  Doc d("# T\n\n2 * 3 = 6 and a_b_c and #hashtag and 5 > 3\n");
  EXPECT_EQ(d.section(0), "<p>2 * 3 = 6 and a_b_c and #hashtag and 5 > 3</p>");
}
