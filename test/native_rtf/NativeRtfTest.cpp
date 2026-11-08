#include <gtest/gtest.h>

#include <string>

#include "BookTestSupport.h"
#include "HtmlParser.h"
#include "RtfConverter.h"

using booktest::LogSink;
using booktest::MemoryReader;

namespace {

std::string toHtml(const std::string& rtfText) {
  MemoryReader r(rtfText);
  std::string out;
  EXPECT_TRUE(rtf::convertToHtml(r, [&](const char* d, size_t n) {
    out.append(d, n);
    return true;
  }));
  return out;
}

std::string bodyOf(const std::string& html) {
  const size_t b = html.find("<body>");
  const size_t e = html.find("</body>");
  if (b == std::string::npos || e == std::string::npos) return html;
  std::string s = html.substr(b + 6, e - b - 6);
  while (!s.empty() && s.front() == '\n') s.erase(0, 1);
  while (!s.empty() && s.back() == '\n') s.pop_back();
  std::string flat;
  for (const char c : s) {
    if (c != '\n') flat.push_back(c);
  }
  return flat;
}

}  // namespace

TEST(Rtf, DetectsSignature) {
  const uint8_t good[] = "  {\\rtf1";
  EXPECT_TRUE(rtf::looksLikeRtf(good, sizeof(good) - 1));
  const uint8_t bad[] = "PK\x03\x04";
  EXPECT_FALSE(rtf::looksLikeRtf(bad, 4));
  MemoryReader r(std::string("hello"));
  EXPECT_FALSE(rtf::convertToHtml(r, [](const char*, size_t) { return true; }));
}

TEST(Rtf, ParagraphsAndInlineFormatting) {
  const std::string html = toHtml(
      "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Arial;}}\\pard Hello {\\b bold {\\i both}} \\ul under\\ulnone  plain\\par\\pard Second\\par}");
  EXPECT_EQ(bodyOf(html), "<p>Hello <b>bold </b><b><i>both</i></b> <u>under</u> plain</p><p>Second</p>");
}

TEST(Rtf, SpecialCharactersAndEscapes) {
  const std::string html = toHtml("{\\rtf1\\ansi a\\~b \\{x\\} \\\\ \\emdash  \\bullet  <&>\\par}");
  EXPECT_EQ(bodyOf(html), "<p>a\xC2\xA0" "b {x} \\ \xE2\x80\x94 \xE2\x80\xA2 &lt;&amp;&gt;</p>");
}

TEST(Rtf, Cp1251AndUnicode) {
  // "Привет" in cp1251 via \'hh, then "мир" as \u with ? fallback.
  const std::string html = toHtml(
      "{\\rtf1\\ansi\\ansicpg1251\\deff0{\\fonttbl{\\f0\\fcharset204 Times;}}\\f0 \\'cf\\'f0\\'e8\\'e2\\'e5\\'f2 "
      "\\u1084?\\u1080?\\u1088?\\par}");
  EXPECT_EQ(bodyOf(html), "<p>\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 \xD0\xBC\xD0\xB8\xD1\x80</p>");
}

TEST(Rtf, UnicodeFallbackWithHexAndNegative) {
  // \u-10179 = U+D83D? use a BMP negative: \u-3913 -> 0xF0B7 style private char; just ensure the fallback \'3f is dropped.
  const std::string html = toHtml("{\\rtf1\\ansi\\uc1 a\\u233\\'e9b\\uc2\\u1078\\'3f\\'3fc\\par}");
  EXPECT_EQ(bodyOf(html), "<p>a\xC3\xA9" "b\xD0\xB6" "c</p>");
}

TEST(Rtf, Cp1252AndLatin) {
  const std::string html = toHtml("{\\rtf1\\ansi\\ansicpg1252 caf\\'e9 \\'93q\\'94 \\'80\\par}");
  EXPECT_EQ(bodyOf(html), "<p>caf\xC3\xA9 \xE2\x80\x9Cq\xE2\x80\x9D \xE2\x82\xAC</p>");
}

TEST(Rtf, HeadingsFromStylesheetAndOutline) {
  const std::string html = toHtml(
      "{\\rtf1\\ansi{\\stylesheet{\\s0 Normal;}{\\s1\\sbasedon0 heading 1;}{\\s2 heading 2;}{\\s3 My Custom;}}"
      "\\pard\\s1 Chapter\\par\\pard\\s0 Body\\par\\pard\\s2 Section\\par\\pard\\outlinelevel0 Outline\\par\\pard\\s3 Plain\\par}");
  EXPECT_EQ(bodyOf(html), "<h1>Chapter</h1><p>Body</p><h2>Section</h2><h1>Outline</h1><p>Plain</p>");
}

TEST(Rtf, InfoTitleAuthorGoToHead) {
  const std::string html = toHtml("{\\rtf1\\ansi{\\info{\\title My Book}{\\author A. Writer}{\\subject skip}}\\pard Text\\par}");
  EXPECT_NE(html.find("<title>My Book</title>"), std::string::npos);
  EXPECT_NE(html.find("<meta name=\"author\" content=\"A. Writer\">"), std::string::npos);
  EXPECT_EQ(bodyOf(html), "<p>Text</p>");
}

TEST(Rtf, SkipsIgnorableDestinationsAndHeaders) {
  const std::string html = toHtml(
      "{\\rtf1\\ansi{\\colortbl;\\red0\\green0\\blue0;}{\\*\\generator Word}{\\header Head text}{\\footer foot}"
      "{\\*\\unknown skipped}{\\footnote note}Visible\\par}");
  EXPECT_EQ(bodyOf(html), "<p>Visible</p>");
}

TEST(Rtf, Tables) {
  const std::string html = toHtml(
      "{\\rtf1\\ansi\\trowd\\cellx1000\\cellx2000\\pard\\intbl A\\cell B\\cell\\row\\trowd\\cellx1000\\cellx2000"
      "\\pard\\intbl C\\cell D\\cell\\row\\pard After\\par}");
  EXPECT_EQ(bodyOf(html),
            "<table><tr><td><p>A</p></td><td><p>B</p></td></tr><tr><td><p>C</p></td><td><p>D</p></td></tr></table><p>After</p>");
}

TEST(Rtf, LineBreakTabAndSmallCaps) {
  const std::string html = toHtml("{\\rtf1\\ansi a\\line b\\tab c \\scaps sc\\par}");
  EXPECT_EQ(bodyOf(html), "<p>a<br/>b\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0" "c <span style=\"font-variant:small-caps\">sc</span></p>");
}

TEST(Rtf, FieldInternalLinkAndBookmark) {
  const std::string html = toHtml(
      "{\\rtf1\\ansi {\\*\\bkmkstart target}Anchor{\\*\\bkmkend target}\\par "
      "{\\field{\\*\\fldinst HYPERLINK \\\\l \"target\"}{\\fldrslt Go there}} "
      "{\\field{\\*\\fldinst HYPERLINK \"http://x.y\"}{\\fldrslt Web}}\\par}");
  EXPECT_EQ(bodyOf(html),
            "<a id=\"target\"></a><p>Anchor</p><p><a href=\"#target\">Go there</a> Web</p>");
}

TEST(Rtf, PictureRangesPointIntoSource) {
  const std::string src =
      "{\\rtf1\\ansi Before\\par{\\pict\\pngblip\\picw10\\pich10 89504e47\r\n0d0a1a0a}\\par After\\par"
      "{\\pict\\wmetafile8 0102}{\\*\\shppict{\\pict\\jpegblip ffd8ffe0}}{\\nonshppict{\\pict\\wmetafile8 aa}}}";
  const std::string html = toHtml(src);
  const size_t p = html.find("<img src=\"x-hex:");
  ASSERT_NE(p, std::string::npos);
  size_t a = 0, b = 0;
  char mime[32];
  ASSERT_EQ(sscanf(html.c_str() + p, "<img src=\"x-hex:%zu-%zu:%31[^\"]", &a, &b, mime), 3);
  EXPECT_EQ(std::string(mime), "image/png");
  EXPECT_EQ(src.substr(a, b - a), "89504e47\r\n0d0a1a0a");
  // exactly two images (png + jpeg); wmetafile ones are dropped
  size_t count = 0;
  for (size_t i = html.find("<img"); i != std::string::npos; i = html.find("<img", i + 1)) ++count;
  EXPECT_EQ(count, 2u);
}

TEST(Rtf, FullPipelineThroughHtmlParser) {
  const std::string src =
      "{\\rtf1\\ansi\\ansicpg1251{\\fonttbl{\\f0\\fcharset204 Arial;}}"
      "{\\stylesheet{\\s0 Normal;}{\\s1 heading 1;}}{\\info{\\title \\'cf\\'f0\\'e8\\'e2\\'e5\\'f2}{\\author Ann}}"
      "\\f0\\pard\\s1 \\'c3\\'eb\\'e0\\'e2\\'e0 1\\par\\pard First {\\b bold}\\par"
      "\\pard\\s1 Two\\par\\pard Second\\par"
      "{\\pict\\pngblip 4142 4344}\\par}";
  const std::string html = toHtml(src);

  MemoryReader hr(html);
  HtmlParser parser;
  Fb2ScanResult scan;
  ASSERT_TRUE(parser.scan(hr, scan));
  EXPECT_EQ(scan.metadata.title, "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
  EXPECT_EQ(scan.metadata.author, "Ann");
  ASSERT_EQ(scan.sections.size(), 2u);
  EXPECT_EQ(scan.sectionTitle(scan.sections[0]), "\xD0\x93\xD0\xBB\xD0\xB0\xD0\xB2\xD0\xB0 1");
  LogSink s0;
  ASSERT_TRUE(parser.renderSection(hr, scan.sections[0], s0));
  EXPECT_EQ(s0.log, "<p>First {B:bold}</p>");
  LogSink s1;
  ASSERT_TRUE(parser.renderSection(hr, scan.sections[1], s1));
  EXPECT_EQ(s1.imageCalls, 1);

  // The picture decodes from the ORIGINAL rtf via the h-range.
  ASSERT_EQ(scan.binaries.size(), 1u);
  EXPECT_EQ(scan.binaries[0].id[0], 'h');
  MemoryReader original(src);
  std::string bytes;
  ASSERT_TRUE(parser.decodeBinary(original, scan.binaries[0], [&](const uint8_t* d, size_t n) { bytes.append((const char*)d, n); }));
  EXPECT_EQ(bytes, "ABCD");
}

TEST(Rtf, MalformedInputNeverCrashes) {
  const char* const samples[] = {
      "{\\rtf1", "{\\rtf1{{{{", "{\\rtf1}}}}", "{\\rtf1\\u", "{\\rtf1\\'", "{\\rtf1\\'z", "{\\rtf1\\bin5 ab}",
      "{\\rtf1{\\pict\\pngblip ZZ}}", "{\\rtf1{\\*\\fldinst HYPERLINK \\l \"}}", "{\\rtf1\\cell\\row\\cell",
  };
  for (const char* s : samples) toHtml(s);
  std::string noisy = "{\\rtf1\\ansi ";
  uint32_t seed = 5;
  static const char* const frag[] = {"{", "}", "\\par ", "\\b ", "\\b0 ", "text ", "\\'e9", "\\u123?", "\\pict\\pngblip ", "\\*\\x ",
                                     "\\cell ", "\\row ", "\\intbl ", "\\fonttbl", "\\stylesheet", "\\s1 ", ";", "\\field", "\\fldrslt ", "\\fldinst "};
  for (int i = 0; i < 6000; ++i) {
    seed = seed * 1664525u + 1013904223u;
    noisy += frag[(seed >> 12) % (sizeof(frag) / sizeof(frag[0]))];
  }
  const std::string html = toHtml(noisy);
  MemoryReader hr(html);
  HtmlParser parser;
  Fb2ScanResult scan;
  ASSERT_TRUE(parser.scan(hr, scan));
  for (const auto& sec : scan.sections) {
    LogSink sink;
    EXPECT_TRUE(parser.renderSection(hr, sec, sink));
  }
}

TEST(Rtf, AbortsWhenSinkFails) {
  MemoryReader r(std::string("{\\rtf1\\ansi ") + std::string(20000, 'x') + "\\par}");
  EXPECT_FALSE(rtf::convertToHtml(r, [](const char*, size_t) { return false; }));
}

TEST(Rtf, DeepNestingIsBounded) {
  std::string s = "{\\rtf1\\ansi ";
  for (int i = 0; i < 5000; ++i) s += "{";
  s += "deep";
  for (int i = 0; i < 5000; ++i) s += "}";
  s += "\\par}";
  const std::string html = toHtml(s);
  EXPECT_NE(html.find("deep"), std::string::npos);
}
