#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "BookTestSupport.h"
#include "HtmlEntities.h"
#include "HtmlParser.h"
#include "HtmlTokenizer.h"

using booktest::LogSink;
using booktest::MemoryReader;

namespace {

struct Scanned {
  Fb2ScanResult result;
  bool ok = false;
};

Scanned scanOf(const std::string& html) {
  MemoryReader r(html);
  HtmlParser p;
  Scanned s;
  s.ok = p.scan(r, s.result);
  return s;
}

std::string renderSection(const std::string& html, size_t index) {
  MemoryReader r(html);
  HtmlParser p;
  Fb2ScanResult scan;
  EXPECT_TRUE(p.scan(r, scan));
  EXPECT_LT(index, scan.sections.size());
  LogSink sink;
  EXPECT_TRUE(p.renderSection(r, scan.sections[index], sink));
  return sink.log;
}

std::string titleOf(const std::string& html, size_t index) {
  MemoryReader r(html);
  HtmlParser p;
  Fb2ScanResult scan;
  EXPECT_TRUE(p.scan(r, scan));
  LogSink sink;
  if (!p.renderSectionTitle(r, scan.sections[index], sink, 0)) return "<none>";
  return sink.log;
}

}  // namespace

TEST(HtmlEntities, NamedNumericAndMalformed) {
  EXPECT_EQ(html::decodeEntities("a &amp; b &lt;c&gt; &quot;"), "a & b <c> \"");
  EXPECT_EQ(html::decodeEntities("&mdash;&hellip;&laquo;&raquo;"), "\xE2\x80\x94\xE2\x80\xA6\xC2\xAB\xC2\xBB");
  EXPECT_EQ(html::decodeEntities("&#1055;&#x44F;"), "\xD0\x9F\xD1\x8F");
  EXPECT_EQ(html::decodeEntities("&nbsp;"), "\xC2\xA0");
  EXPECT_EQ(html::decodeEntities("AT&T &unknown; & x"), "AT&T &unknown; & x");
  EXPECT_EQ(html::decodeEntities("&#0;&#xFFFFFFFF;"), "&#0;\xEF\xBF\xBD");
  EXPECT_EQ(html::lookupNamedEntity("alpha"), 945u);
  EXPECT_EQ(html::lookupNamedEntity("Omega"), 937u);
}

TEST(HtmlTokenizer, TagsAttributesAndOffsets) {
  const std::string src = "<P Class=\"a b\" id=x1>Hi &amp; <br/><img src='a.png' alt=z></P>";
  MemoryReader r(src);
  html::HtmlTokenizer t(r, 0);
  html::HtmlToken tok;
  ASSERT_TRUE(t.next(tok));
  EXPECT_EQ(tok.type, html::HtmlToken::Type::StartTag);
  EXPECT_EQ(tok.name, "p");
  EXPECT_EQ(*tok.attrValue("class"), "a b");
  EXPECT_EQ(*tok.attrValue("id"), "x1");
  EXPECT_EQ(tok.start, 0u);
  ASSERT_TRUE(t.next(tok));
  EXPECT_EQ(tok.text, "Hi & ");
  ASSERT_TRUE(t.next(tok));
  EXPECT_EQ(tok.name, "br");
  EXPECT_TRUE(tok.selfClosing);
  ASSERT_TRUE(t.next(tok));
  EXPECT_EQ(tok.name, "img");
  const html::HtmlAttr* a = tok.attr("src");
  ASSERT_TRUE(a);
  EXPECT_EQ(src.substr(a->valueStart, a->valueEnd - a->valueStart), "a.png");
  ASSERT_TRUE(t.next(tok));
  EXPECT_EQ(tok.type, html::HtmlToken::Type::EndTag);
  EXPECT_FALSE(t.next(tok));
}

TEST(HtmlTokenizer, SkipsCommentsScriptsStyles) {
  const std::string src = "A<!-- c > -->B<script>if (a<b) x='</p>';</script>C<style>p{}</style>D<![CDATA[x]]>E<!doctype html>";
  MemoryReader r(src);
  html::HtmlTokenizer t(r, 0);
  html::HtmlToken tok;
  std::string text;
  while (t.next(tok)) {
    if (tok.type == html::HtmlToken::Type::Text) text += tok.text;
  }
  EXPECT_EQ(text, "ABCDE");
}

TEST(HtmlTokenizer, LiteralLessThanAndBom) {
  const std::string src = "\xEF\xBB\xBF" "a < b and c<3";
  MemoryReader r(src);
  html::HtmlTokenizer t(r, 0);
  html::HtmlToken tok;
  std::string text;
  while (t.next(tok)) text += tok.text;
  EXPECT_EQ(text, "a < b and c<3");
}

TEST(HtmlParser, ChaptersTitlesAndMetadata) {
  const std::string doc =
      "<html lang=\"ru\"><head><title>Моя книга</title><meta name=\"author\" content=\"И. Иванов\">"
      "<meta name=\"description\" content=\"Описание\"></head><body>"
      "<p>Пролог</p><h1 id=\"c1\">Глава 1</h1><p>Текст 1</p>"
      "<h2>Раздел</h2><p>Текст 2</p><h1>Глава 2</h1><p>Конец</p></body></html>";
  Scanned s = scanOf(doc);
  ASSERT_TRUE(s.ok);
  EXPECT_EQ(s.result.metadata.title, "Моя книга");
  EXPECT_EQ(s.result.metadata.author, "И. Иванов");
  EXPECT_EQ(s.result.metadata.language, "ru");
  EXPECT_EQ(s.result.metadata.annotationText, "Описание");
  ASSERT_EQ(s.result.sections.size(), 4u);  // untitled + 3 headings
  EXPECT_EQ(s.result.sections[0].level, 0);
  EXPECT_EQ(s.result.sectionTitle(s.result.sections[1]), "Глава 1");
  EXPECT_EQ(s.result.sectionId(s.result.sections[1]), "c1");
  EXPECT_EQ(s.result.sections[2].level, 1);
  EXPECT_EQ(s.result.sectionTitle(s.result.sections[2]), "Раздел");
  EXPECT_EQ(s.result.sections[3].level, 0);
  EXPECT_EQ(titleOf(doc, 0), "<none>");
  EXPECT_EQ(titleOf(doc, 1), "<title0>Глава 1</title>");
  EXPECT_EQ(renderSection(doc, 0), "<p>Пролог</p>");
  EXPECT_EQ(renderSection(doc, 1), "<p>Текст 1</p>");
  EXPECT_EQ(renderSection(doc, 2), "<p>Текст 2</p>");
  EXPECT_EQ(renderSection(doc, 3), "<p>Конец</p>");
}

TEST(HtmlParser, RepeatedTitleParagraphsAreDropped) {
  // The chapter title repeated as the first paragraph(s), also split over several paragraphs.
  const std::string doc =
      "<h1>ПРОЛОГ</h1><div><p>ПРОЛОГ</p></div><p>Текст</p>"
      "<h1>Часть первая Я НЕ КОРОЛЬ!</h1><div><p>Часть первая</p><p>Я НЕ КОРОЛЬ!</p></div><p>Дальше</p>"
      "<h1>Три</h1><p>Совсем другой</p><p>Три</p>"
      "<h1>Четыре</h1><p>Четыре</p>";
  EXPECT_EQ(renderSection(doc, 0), "<p>Текст</p>");
  EXPECT_EQ(renderSection(doc, 1), "<p>Дальше</p>");
  EXPECT_EQ(renderSection(doc, 2), "<p>Совсем другой</p><p>Три</p>");  // only a leading repeat is dropped
  EXPECT_EQ(renderSection(doc, 3), "");                                 // a section that is only its title
}

TEST(HtmlParser, FirstH1BecomesTitleWithoutTitleTag) {
  Scanned s = scanOf("<h1>Only Heading</h1><p>x</p>");
  EXPECT_EQ(s.result.metadata.title, "Only Heading");
  ASSERT_EQ(s.result.sections.size(), 1u);
}

TEST(HtmlParser, HeadContentIsNotRendered) {
  const std::string doc = "<html><head><title>T</title><style>p{color:red}</style></head><body><p>Body</p></body></html>";
  EXPECT_EQ(renderSection(doc, 0), "<p>Body</p>");
}

TEST(HtmlParser, InlineStylesAndSmallCaps) {
  const std::string doc =
      "<p>a <b>bold <i>both</i></b> <u>u</u> <s>s</s> x<sup>2</sup> <span class=\"smallcaps\">sc</span> "
      "<span style=\"font-weight:bold; font-style:italic\">bi</span> <span style=\"font-variant: small-caps\">v</span></p>";
  EXPECT_EQ(renderSection(doc, 0),
            "<p>a {B:bold }{BI:both} {U:u} {S:s} x{^:2} {C:sc} {BI:bi} {C:v}</p>");
}

TEST(HtmlParser, ParagraphsBreaksAndWhitespace) {
  const std::string doc = "<div>  one   two\n three <br>four<br><br>five</div><p>   </p><p>six</p>";
  EXPECT_EQ(renderSection(doc, 0), "<p>one two three</p><p>four</p><empty/><p>five</p><p>six</p>");
}

TEST(HtmlParser, ListsNestedAndOrdered) {
  const std::string doc = "<ul><li>a</li><li>b<ol><li>x</li><li>y</li></ol></li></ul><ol start=3><li>c</li></ol>";
  EXPECT_EQ(renderSection(doc, 0),
            "<p>\xE2\x80\xA2\xC2\xA0" "a</p><p>\xE2\x80\xA2\xC2\xA0" "b</p>"
            "<p>\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0" "1.\xC2\xA0" "x</p><p>\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0" "2.\xC2\xA0" "y</p>"
            "<p>3.\xC2\xA0" "c</p>");
}

TEST(HtmlParser, ListItemWithParagraphGetsOneMarker) {
  EXPECT_EQ(renderSection("<ul><li><p>only</p></li></ul>", 0), "<p>\xE2\x80\xA2\xC2\xA0" "only</p>");
}

TEST(HtmlParser, BlockquoteAndPre) {
  const std::string doc = "<blockquote><p>q1</p><p>q2</p></blockquote><pre>  code\n\n\tx</pre>";
  EXPECT_EQ(renderSection(doc, 0),
            "<cite><p>q1</p><p>q2</p></cite><cite><p>\xC2\xA0\xC2\xA0" "code</p><empty/>"
            "<p>\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0" "x</p></cite>");
}

TEST(HtmlParser, SubtitlesForDeepHeadings) {
  EXPECT_EQ(renderSection("<h4>Small</h4><p>t</p>", 0), "<sub>{B:Small}</sub><p>t</p>");
}

TEST(HtmlParser, Tables) {
  const std::string doc =
      "<table><tr><th>H1</th><th colspan=2>H2</th></tr><tr><td>a <b>b</b></td><td>c</td><td valign=top>d</td></tr></table><p>after</p>";
  EXPECT_EQ(renderSection(doc, 0),
            "<table><tr><th:H1><th cs=2:H2></tr><tr><td:a b><td:c><td va=top:d></tr></table><p>after</p>");
}

TEST(HtmlParser, TableWithoutTrAndNested) {
  EXPECT_EQ(renderSection("<table><td>x</td><td><table><tr><td>in</td></tr></table></td></table>", 0),
            "<table><tr><td:x><td:in></tr></table>");
}

TEST(HtmlParser, LinksOnlyForFragments) {
  const std::string doc = "<p><a href=\"#n1\">note</a> and <a href=\"http://x.y/\">web</a></p>";
  EXPECT_EQ(renderSection(doc, 0), "<p><a:n1>note</a> and web</p>");
}

TEST(HtmlParser, HeadingTitleWithBreakAndMarkup) {
  const std::string doc = "<h2>Part <i>One</i><br/>Sub</h2><p>x</p>";
  EXPECT_EQ(titleOf(doc, 0), "<title0>Part {I:One}<br/>Sub</title>");
}

TEST(HtmlParser, AnchorBeforeHeadingProvidesId) {
  Scanned s = scanOf("<p>a</p><a id=\"fp7\"></a><h2>Chapter</h2><p>b</p><h2><a name=\"in\"/>Two</h2>");
  ASSERT_EQ(s.result.sections.size(), 3u);
  EXPECT_EQ(s.result.sectionId(s.result.sections[1]), "fp7");
  EXPECT_EQ(s.result.sectionId(s.result.sections[2]), "in");
}

TEST(HtmlParser, ScriptSvgNoscriptSkipped) {
  const std::string doc = "<p>a</p><svg><g><h1>no</h1></g></svg><noscript><p>nope</p></noscript><script>var h='<h1>x</h1>';</script><p>b</p>";
  Scanned s = scanOf(doc);
  EXPECT_EQ(s.result.sections.size(), 1u);
  EXPECT_EQ(renderSection(doc, 0), "<p>a</p><p>b</p>");
}

TEST(HtmlParser, ImagesDataFileAndRange) {
  const std::string doc =
      "<p>x</p><img src=\"data:image/png;base64,iVBORw0KGgo=\"><img src=\"pics/a%20b.JPG?x=1\">"
      "<img src=\"http://e/x.png\"><img src=\"x-raw:100-250:image/jpeg\"><img src=\"x-hex:5-9:image/png\">"
      "<img src=\"v.svg\">";
  Scanned s = scanOf(doc);
  ASSERT_EQ(s.result.binaries.size(), 4u);
  EXPECT_EQ(s.result.binaries[0].id[0], 'd');
  EXPECT_EQ(s.result.binaries[0].contentType, "image/png");
  EXPECT_EQ(doc.substr(s.result.binaries[0].payloadStartOffset,
                       s.result.binaries[0].payloadEndOffset - s.result.binaries[0].payloadStartOffset),
            "iVBORw0KGgo=");
  EXPECT_EQ(s.result.binaries[1].id[0], 'f');
  EXPECT_EQ(s.result.binaries[1].contentType, "image/jpeg");
  EXPECT_EQ(s.result.binaries[2].id[0], 'r');
  EXPECT_EQ(s.result.binaries[2].payloadStartOffset, 100u);
  EXPECT_EQ(s.result.binaries[2].payloadEndOffset, 250u);
  EXPECT_EQ(s.result.binaries[3].id[0], 'h');
  EXPECT_EQ(s.result.sections[0].imageRefCount, 4u);

  MemoryReader r(doc);
  HtmlParser p;
  LogSink sink;
  ASSERT_TRUE(p.renderSection(r, s.result.sections[0], sink));
  EXPECT_EQ(sink.imageCalls, 4);
  EXPECT_NE(sink.log.find("<img:" + s.result.binaries[0].id + ">"), std::string::npos);
}

TEST(HtmlParser, DecodeBinaryKinds) {
  // data: base64 -> "Hello"
  const std::string doc = "<img src=\"data:image/png;base64,SGVsbG8=\">";
  Scanned s = scanOf(doc);
  ASSERT_EQ(s.result.binaries.size(), 1u);
  MemoryReader r(doc);
  HtmlParser p;
  std::string got;
  ASSERT_TRUE(p.decodeBinary(r, s.result.binaries[0], [&](const uint8_t* d, size_t n) { got.append((const char*)d, n); }));
  EXPECT_EQ(got, "Hello");

  // raw + hex ranges are relative to the reader handed to decodeBinary
  MemoryReader blob(std::string("xxABCDEFyy 48 65 6c\n6C 6f zz"));
  Fb2BinaryIndexEntry raw;
  raw.id = "r1";
  raw.payloadStartOffset = 2;
  raw.payloadEndOffset = 8;
  got.clear();
  ASSERT_TRUE(p.decodeBinary(blob, raw, [&](const uint8_t* d, size_t n) { got.append((const char*)d, n); }));
  EXPECT_EQ(got, "ABCDEF");
  Fb2BinaryIndexEntry hex;
  hex.id = "h1";
  hex.payloadStartOffset = 11;
  hex.payloadEndOffset = 25;
  got.clear();
  ASSERT_TRUE(p.decodeBinary(blob, hex, [&](const uint8_t* d, size_t n) { got.append((const char*)d, n); }));
  EXPECT_EQ(got, "Hello");
}

TEST(HtmlParser, ExternalImageLoaderGetsDecodedPath) {
  const std::string doc = "<img src=\"pics/a%20b.png?x#y\">";
  Scanned s = scanOf(doc);
  ASSERT_EQ(s.result.binaries.size(), 1u);
  HtmlParser p;
  std::string seen;
  p.setExternalImageLoader([&](const std::string& src, const FictionBookParser::BinaryOutputFn& out) {
    seen = src;
    out(reinterpret_cast<const uint8_t*>("ok"), 2);
    return true;
  });
  MemoryReader r(doc);
  std::string got;
  ASSERT_TRUE(p.decodeBinary(r, s.result.binaries[0], [&](const uint8_t* d, size_t n) { got.append((const char*)d, n); }));
  EXPECT_EQ(seen, "pics/a b.png");
  EXPECT_EQ(got, "ok");
  HtmlParser noLoader;
  EXPECT_FALSE(noLoader.decodeBinary(r, s.result.binaries[0], [](const uint8_t*, size_t) {}));
}

TEST(HtmlParser, MetaCoverUsesRangeSource) {
  Scanned s = scanOf("<html><head><meta name=\"cover\" content=\"x-raw:10-20:image/jpeg\"><title>T</title></head><body><p>x</p></body></html>");
  EXPECT_FALSE(s.result.metadata.coverBinaryId.empty());
  ASSERT_EQ(s.result.binaries.size(), 1u);
  EXPECT_EQ(s.result.binaries[0].id, s.result.metadata.coverBinaryId);
}

TEST(HtmlParser, EmptyAndGarbageDocuments) {
  Scanned e = scanOf("");
  EXPECT_TRUE(e.ok);
  EXPECT_EQ(e.result.sections.size(), 1u);
  Scanned g = scanOf("<<<>>> & </ <h1 <p");
  EXPECT_TRUE(g.ok);
  std::string noisy;
  uint32_t s = 99;
  for (int i = 0; i < 4000; ++i) {
    s = s * 1664525u + 1013904223u;
    static const char* const frag[] = {"<p>", "</p>", "<h1>", "</h2>", "<b>", "</i>", "<table>", "<td>", "</tr>", "<ul>", "<li>",
                                       "<a href=\"#x\">", "</a>", "text ", "&amp;", "<img src=\"a.png\">", "<br>", "<pre>", "\n", "<svg>", "</svg>"};
    noisy += frag[(s >> 12) % (sizeof(frag) / sizeof(frag[0]))];
  }
  Scanned n = scanOf(noisy);
  ASSERT_TRUE(n.ok);
  MemoryReader r(noisy);
  HtmlParser p;
  for (const auto& sec : n.result.sections) {
    LogSink sink;
    EXPECT_TRUE(p.renderSection(r, sec, sink));
    LogSink t;
    p.renderSectionTitle(r, sec, t, 0);
  }
}

TEST(HtmlParser, LargeDocumentManyChapters) {
  std::string doc = "<html><body>";
  for (int i = 0; i < 300; ++i) {
    doc += "<h2>Chapter " + std::to_string(i) + "</h2><p>" + std::string(200, 'x') + "</p>";
  }
  doc += "</body></html>";
  Scanned s = scanOf(doc);
  ASSERT_EQ(s.result.sections.size(), 300u);
  EXPECT_EQ(s.result.sectionTitle(s.result.sections[299]), "Chapter 299");
  EXPECT_EQ(renderSection(doc, 150), "<p>" + std::string(200, 'x') + "</p>");
}
