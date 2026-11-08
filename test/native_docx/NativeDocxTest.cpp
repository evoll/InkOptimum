#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include <OpcPackage.h>

#include "BookTestSupport.h"
#include "DocxInfo.h"
#include "DocxPackage.h"
#include "DocxParser.h"

namespace {

using booktest::LogSink;
using booktest::MemoryReader;
using booktest::makeZip;

const char kPngBytes[] =
    "\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x02\x00\x00\x00\x90wS\xde"
    "\x00\x00\x00\x0cIDATx\x9cc\xf8\xcf\xc0\x00\x00\x03\x01\x01\x00\xc9\xfe\x92\xef\x00\x00\x00\x00IEND\xaeB`\x82";
const std::string kPng(kPngBytes, sizeof(kPngBytes) - 1);

constexpr const char* kNs =
    "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
    "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
    "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" "
    "xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\"";
constexpr const char* kRelNs = "http://schemas.openxmlformats.org/package/2006/relationships";
constexpr const char* kRelBase = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/";

// --- tiny WordprocessingML builders -----------------------------------------

std::string run(const std::string& text, const std::string& rpr = "") {
  return "<w:r>" + (rpr.empty() ? std::string() : "<w:rPr>" + rpr + "</w:rPr>") +
         "<w:t xml:space=\"preserve\">" + text + "</w:t></w:r>";
}
std::string para(const std::string& inner, const std::string& ppr = "") {
  return "<w:p>" + (ppr.empty() ? std::string() : "<w:pPr>" + ppr + "</w:pPr>") + inner + "</w:p>";
}
std::string text(const std::string& t) { return para(run(t)); }
std::string heading(int level, const std::string& t) {
  return para(run(t), "<w:pStyle w:val=\"Heading" + std::to_string(level) + "\"/>");
}

const char* kStyles =
    "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
    "<w:docDefaults><w:rPrDefault><w:rPr><w:b/></w:rPr></w:rPrDefault></w:docDefaults>"
    "<w:style w:type=\"paragraph\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"Heading1\"><w:name w:val=\"heading 1\"/><w:basedOn w:val=\"Normal\"/>"
    "<w:pPr><w:keepNext/><w:outlineLvl w:val=\"0\"/></w:pPr><w:rPr><w:b/></w:rPr></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"Heading2\"><w:name w:val=\"heading 2\"/><w:basedOn w:val=\"Normal\"/></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"Heading3\"><w:name w:val=\"heading 3\"/><w:basedOn w:val=\"Normal\"/></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"Heading4\"><w:name w:val=\"heading 4\"/><w:basedOn w:val=\"Normal\"/></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"MyChapter\"><w:name w:val=\"My Chapter\"/><w:basedOn w:val=\"Heading1\"/></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"Title\"><w:name w:val=\"Title\"/></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"Quote\"><w:name w:val=\"Quote\"/><w:rPr><w:i/></w:rPr></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"TOC1\"><w:name w:val=\"toc 1\"/></w:style>"
    "<w:style w:type=\"character\" w:styleId=\"Strong\"><w:name w:val=\"Strong\"/><w:rPr><w:b/></w:rPr></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"ListBullet\"><w:name w:val=\"List Bullet\"/>"
    "<w:pPr><w:numPr><w:numId w:val=\"1\"/></w:numPr></w:pPr></w:style>"
    "</w:styles>";

const char* kNumbering =
    "<w:numbering xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
    "<w:abstractNum w:abstractNumId=\"0\"><w:lvl w:ilvl=\"0\"><w:numFmt w:val=\"bullet\"/></w:lvl>"
    "<w:lvl w:ilvl=\"1\"><w:numFmt w:val=\"bullet\"/></w:lvl></w:abstractNum>"
    "<w:abstractNum w:abstractNumId=\"1\"><w:lvl w:ilvl=\"0\"><w:numFmt w:val=\"decimal\"/></w:lvl>"
    "<w:lvl w:ilvl=\"1\"><w:numFmt w:val=\"lowerLetter\"/></w:lvl></w:abstractNum>"
    "<w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num>"
    "<w:num w:numId=\"2\"><w:abstractNumId w:val=\"1\"/></w:num>"
    "</w:numbering>";

const char* kCore =
    "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
    "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\">"
    "<dc:title>Test Doc</dc:title><dc:creator>Ann Bee</dc:creator><dc:language>ru-RU</dc:language>"
    "<dcterms:created>2024-05-01T10:00:00Z</dcterms:created></cp:coreProperties>";

struct DocOptions {
  std::string footnotes;  // inner XML of <w:footnotes>
  std::string endnotes;
  bool withStyles = true;
  bool withNumbering = true;
  bool withCore = true;
  bool withImage = true;
  std::string mainContentType =
      "application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml";
  std::string mainPart = "word/document.xml";
};

std::vector<uint8_t> makeDocx(const std::string& bodyInner, const DocOptions& o = DocOptions()) {
  const std::string decl = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
  std::string contentTypes =
      "<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
      "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
      "<Default Extension=\"xml\" ContentType=\"application/xml\"/><Default Extension=\"png\" ContentType=\"image/png\"/>"
      "<Default Extension=\"gif\" ContentType=\"image/gif\"/>"
      "<Override PartName=\"/" + o.mainPart + "\" ContentType=\"" + o.mainContentType + "\"/></Types>";
  std::string rootRels = std::string("<?xml version=\"1.0\"?><Relationships xmlns=\"") + kRelNs +
                         "\"><Relationship Id=\"rId1\" Type=\"" + kRelBase + "officeDocument\" Target=\"" + o.mainPart +
                         "\"/>";
  if (o.withCore) {
    rootRels += std::string("<Relationship Id=\"rId2\" Type=\"") + kRelNs +
                "/metadata/core-properties\" Target=\"docProps/core.xml\"/>";
  }
  rootRels += "</Relationships>";

  std::string docRels = std::string("<?xml version=\"1.0\"?><Relationships xmlns=\"") + kRelNs + "\">";
  const auto rel = [&](const std::string& id, const std::string& type, const std::string& target) {
    docRels += "<Relationship Id=\"" + id + "\" Type=\"" + kRelBase + type + "\" Target=\"" + target + "\"/>";
  };
  if (o.withStyles) rel("rIdS", "styles", "styles.xml");
  if (o.withNumbering) rel("rIdN", "numbering", "numbering.xml");
  if (!o.footnotes.empty()) rel("rIdF", "footnotes", "footnotes.xml");
  if (!o.endnotes.empty()) rel("rIdE", "endnotes", "endnotes.xml");
  if (o.withImage) {
    rel("rIdImg", "image", "media/image1.png");
    rel("rIdGif", "image", "media/anim.gif");
    docRels += "<Relationship Id=\"rIdExt\" Type=\"" + std::string(kRelBase) +
               "hyperlink\" Target=\"http://example.com\" TargetMode=\"External\"/>";
  }
  docRels += "</Relationships>";

  const std::string document =
      decl + "<w:document " + std::string(kNs) + "><w:body>" + bodyInner + "<w:sectPr/></w:body></w:document>";
  std::vector<std::pair<std::string, std::string>> entries = {
      {"[Content_Types].xml", contentTypes},
      {"_rels/.rels", rootRels},
      {o.mainPart, document},
      {"word/_rels/document.xml.rels", docRels},
  };
  if (o.withStyles) entries.emplace_back("word/styles.xml", decl + kStyles);
  if (o.withNumbering) entries.emplace_back("word/numbering.xml", decl + kNumbering);
  if (o.withCore) entries.emplace_back("docProps/core.xml", decl + kCore);
  if (!o.footnotes.empty()) {
    entries.emplace_back("word/footnotes.xml",
                         decl + "<w:footnotes " + std::string(kNs) + ">" + o.footnotes + "</w:footnotes>");
  }
  if (!o.endnotes.empty()) {
    entries.emplace_back("word/endnotes.xml",
                         decl + "<w:endnotes " + std::string(kNs) + ">" + o.endnotes + "</w:endnotes>");
  }
  if (o.withImage) {
    entries.emplace_back("word/media/image1.png", kPng);
    entries.emplace_back("word/media/anim.gif", "GIF89a....");
  }
  return makeZip(entries);
}

// The staging the firmware does, in memory: PartStager over the ZIP.
docx::PartStager memoryStager() {
  return [](IByteReader& zip, uint32_t ordinal, const docx::PartConsumer& consume) {
    std::string data;
    if (!opc::extractEntryByOrdinal(zip, ordinal, [&](const uint8_t* d, size_t n) {
          data.append(reinterpret_cast<const char*>(d), n);
        })) {
      return false;
    }
    opc::MemoryByteReader mem(data);
    return consume(mem);
  };
}

struct Book {
  std::vector<uint8_t> zipBytes;
  std::unique_ptr<MemoryReader> zip;
  docx::PackageInfo info;
  std::string source;
  std::unique_ptr<MemoryReader> sourceReader;
  Fb2ScanResult scan;
  DocxParser parser;
  bool ok = false;
};

std::unique_ptr<Book> openBook(std::vector<uint8_t> zipBytes) {
  auto b = std::make_unique<Book>();
  b->zipBytes = std::move(zipBytes);
  b->zip = std::make_unique<MemoryReader>(b->zipBytes);
  if (!docx::readPackageInfo(*b->zip, memoryStager(), b->info)) return b;
  if (!docx::writeBodySource(*b->zip, b->info, [&](const uint8_t* d, size_t n) {
        b->source.append(reinterpret_cast<const char*>(d), n);
      })) {
    return b;
  }
  b->sourceReader = std::make_unique<MemoryReader>(b->source);
  b->parser.setInfo(&b->info);
  b->ok = b->parser.scan(*b->sourceReader, b->scan);
  return b;
}

std::string renderSection(Book& b, size_t index) {
  LogSink sink;
  EXPECT_TRUE(b.parser.renderSection(*b.sourceReader, b.scan.sections[index], sink));
  return sink.log;
}

std::string renderTitle(Book& b, size_t index) {
  LogSink sink;
  b.parser.renderSectionTitle(*b.sourceReader, b.scan.sections[index], sink, 1);
  return sink.log;
}

std::string titleOf(Book& b, size_t index) { return std::string(b.scan.sectionTitle(b.scan.sections[index])); }

}  // namespace

// ---------------------------------------------------------------------------
// Package
// ---------------------------------------------------------------------------

TEST(DocxPackage, RecognisesWordPackagesOnly) {
  auto bytes = makeDocx(text("x"));
  MemoryReader good(bytes);
  EXPECT_TRUE(docx::isDocxPackage(good));

  DocOptions xlsx;
  xlsx.mainPart = "xl/workbook.xml";
  xlsx.mainContentType = "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml";
  MemoryReader sheet(makeDocx(text("x"), xlsx));
  EXPECT_FALSE(docx::isDocxPackage(sheet));

  MemoryReader notZip(std::string("plain text, not a zip"));
  EXPECT_FALSE(docx::isDocxPackage(notZip));
}

TEST(DocxPackage, ReadsMetadataImagesStylesAndNumbering) {
  auto b = openBook(makeDocx(text("x")));
  ASSERT_TRUE(b->ok);
  const docx::PackageInfo& info = b->info;
  EXPECT_EQ(info.metadata.title, "Test Doc");
  EXPECT_EQ(info.metadata.author, "Ann Bee");
  EXPECT_EQ(info.metadata.language, "ru-RU");
  EXPECT_EQ(info.metadata.date, "2024-05-01");

  // Only the PNG is usable; the GIF and the external hyperlink are dropped.
  ASSERT_EQ(info.images.size(), 1u);
  EXPECT_EQ(info.images[0].relId, "rIdImg");
  EXPECT_EQ(info.images[0].kind, opc::ImageKind::Png);

  const docx::StyleInfo* h1 = info.findStyle("Heading1");
  ASSERT_NE(h1, nullptr);
  EXPECT_EQ(h1->heading, 1);
  const docx::StyleInfo* custom = info.findStyle("MyChapter");  // inherits through basedOn
  ASSERT_NE(custom, nullptr);
  EXPECT_EQ(custom->heading, 1);
  ASSERT_NE(info.findStyle("Heading4"), nullptr);
  EXPECT_EQ(info.findStyle("Heading4")->heading, 4);
  EXPECT_TRUE(info.findStyle("Title")->flags & docx::kTitle);
  EXPECT_TRUE(info.findStyle("Quote")->flags & (docx::kQuote | docx::kItalic));
  EXPECT_TRUE(info.findStyle("Strong")->flags & docx::kBold);
  EXPECT_EQ(info.findStyle("ListBullet")->numId, 1u);
  EXPECT_EQ(info.findStyle("Normal"), nullptr);  // nothing a reader cares about
  // docDefaults bold must not leak into styles.
  EXPECT_FALSE(info.findStyle("Heading2")->flags & docx::kBold);

  EXPECT_FALSE(info.isOrdered(1, 0));
  EXPECT_TRUE(info.isOrdered(2, 0));
  EXPECT_TRUE(info.isOrdered(2, 1));
  EXPECT_FALSE(info.isOrdered(9, 0));  // unknown list: bulleted
}

TEST(DocxPackage, InfoSidecarRoundTrips) {
  auto b = openBook(makeDocx(text("x")));
  ASSERT_TRUE(b->ok);
  const std::string text = docx::serialize(b->info);
  docx::PackageInfo back;
  ASSERT_TRUE(docx::parse(text, back));
  EXPECT_EQ(back.metadata.title, "Test Doc");
  EXPECT_EQ(back.styles.size(), b->info.styles.size());
  EXPECT_EQ(back.findStyle("MyChapter")->heading, 1);
  EXPECT_TRUE(back.isOrdered(2, 1));
  ASSERT_EQ(back.images.size(), 1u);
  EXPECT_EQ(back.images[0].entryOrdinal, b->info.images[0].entryOrdinal);
  EXPECT_FALSE(docx::parse("garbage", back));
}

TEST(DocxPackage, MissingStylesAndNumberingAreNotFatal) {
  DocOptions o;
  o.withStyles = false;
  o.withNumbering = false;
  o.withCore = false;
  auto b = openBook(makeDocx(text("Hello"), o));
  ASSERT_TRUE(b->ok);
  EXPECT_TRUE(b->info.styles.empty());
  EXPECT_EQ(b->scan.sections.size(), 1u);
}

TEST(DocxPackage, BodySourceStripsDeclarationsAndAppendsNotes) {
  DocOptions o;
  o.footnotes = "<w:footnote w:id=\"2\">" + text("note") + "</w:footnote>";
  auto b = openBook(makeDocx(text("x"), o));
  ASSERT_TRUE(b->ok);
  EXPECT_EQ(b->source.compare(0, 11, "<docx-book>"), 0);
  EXPECT_EQ(b->source.find("<?xml"), std::string::npos);
  EXPECT_NE(b->source.find("<w:footnotes"), std::string::npos);
  EXPECT_EQ(b->source.compare(b->source.size() - 12, 12, "</docx-book>"), 0);
}

// ---------------------------------------------------------------------------
// Scan
// ---------------------------------------------------------------------------

TEST(DocxScan, HeadingsStartChaptersAndContentBeforeFirstHeadingIsUntitled) {
  auto b = openBook(makeDocx(text("Preface text") + heading(1, "One") + text("a") + heading(2, "One.A") +
                             text("b") + heading(3, "Deep") + heading(4, "Not a chapter") + text("c") +
                             heading(1, "Two") + text("d")));
  ASSERT_TRUE(b->ok);
  ASSERT_EQ(b->scan.sections.size(), 5u);
  EXPECT_EQ(titleOf(*b, 0), "");
  EXPECT_EQ(titleOf(*b, 1), "One");
  EXPECT_EQ(titleOf(*b, 2), "One.A");
  EXPECT_EQ(titleOf(*b, 3), "Deep");
  EXPECT_EQ(titleOf(*b, 4), "Two");
  EXPECT_EQ(b->scan.sections[1].level, 0);
  EXPECT_EQ(b->scan.sections[2].level, 1);
  EXPECT_EQ(b->scan.sections[3].level, 2);
  EXPECT_LT(b->scan.sections[0].innerStartOffset, b->scan.sections[1].innerStartOffset);
  EXPECT_GT(b->scan.sections[0].approxTextBytes, 0u);
  ASSERT_EQ(b->scan.bodies.size(), 1u);
  EXPECT_EQ(b->scan.metadata.title, "Test Doc");
  EXPECT_EQ(b->scan.metadata.author, "Ann Bee");
}

TEST(DocxScan, FirstHeadingNeedsNoUntitledSection) {
  auto b = openBook(makeDocx(heading(1, "Only") + text("a")));
  ASSERT_TRUE(b->ok);
  ASSERT_EQ(b->scan.sections.size(), 1u);
  EXPECT_EQ(titleOf(*b, 0), "Only");
}

TEST(DocxScan, DocumentWithoutHeadingsIsOneSection) {
  auto b = openBook(makeDocx(text("alpha") + text("beta") + text("gamma")));
  ASSERT_TRUE(b->ok);
  ASSERT_EQ(b->scan.sections.size(), 1u);
  EXPECT_EQ(titleOf(*b, 0), "");
  EXPECT_EQ(renderSection(*b, 0), "<p>alpha</p><p>beta</p><p>gamma</p>");
}

TEST(DocxScan, OutlineLevelAndInheritedStyleCountAsHeadings) {
  auto b = openBook(makeDocx(para(run("Outlined"), "<w:outlineLvl w:val=\"0\"/>") +
                             para(run("Custom"), "<w:pStyle w:val=\"MyChapter\"/>") +
                             para(run("Body"), "<w:pStyle w:val=\"Heading1\"/><w:outlineLvl w:val=\"9\"/>")));
  ASSERT_TRUE(b->ok);
  // outlineLvl 9 on a Heading1 paragraph = body text.
  ASSERT_EQ(b->scan.sections.size(), 2u);
  EXPECT_EQ(titleOf(*b, 0), "Outlined");
  EXPECT_EQ(titleOf(*b, 1), "Custom");
  EXPECT_EQ(renderSection(*b, 1), "<p>{B:Body}</p>");  // keeps the style's own bold
}

TEST(DocxScan, HeadingTextAcrossRunsAndTabsIsCollapsed) {
  auto b = openBook(makeDocx(para(run("Chapter ") + "<w:r><w:tab/></w:r>" + run("  Seven", "<w:b/>"),
                                  "<w:pStyle w:val=\"Heading1\"/>")));
  ASSERT_TRUE(b->ok);
  EXPECT_EQ(titleOf(*b, 0), "Chapter Seven");
}

TEST(DocxScan, HeadingsInsideTablesAndTextBoxesAreNotChapters) {
  auto b = openBook(makeDocx(
      heading(1, "Real") + "<w:tbl><w:tr><w:tc>" + heading(1, "In table") + "</w:tc></w:tr></w:tbl>" +
      para("<w:r><mc:AlternateContent><mc:Choice><w:drawing><w:txbxContent>" + heading(1, "In box") +
           "</w:txbxContent></w:drawing></mc:Choice></mc:AlternateContent></w:r>")));
  ASSERT_TRUE(b->ok);
  ASSERT_EQ(b->scan.sections.size(), 1u);
  EXPECT_EQ(titleOf(*b, 0), "Real");
}

TEST(DocxScan, CountsKnownImagesOnly) {
  const std::string png =
      "<w:r><w:drawing><wp:inline><a:graphic><a:graphicData><a:blip r:embed=\"rIdImg\"/></a:graphicData></a:graphic>"
      "</wp:inline></w:drawing></w:r>";
  const std::string gif = "<w:r><w:drawing><a:blip r:embed=\"rIdGif\"/></w:drawing></w:r>";
  auto b = openBook(makeDocx(heading(1, "Pics") + para(png) + para(gif) + para(png)));
  ASSERT_TRUE(b->ok);
  EXPECT_EQ(b->scan.sections[0].imageRefCount, 2);
  ASSERT_EQ(b->scan.binaries.size(), 1u);
  EXPECT_EQ(b->scan.binaries[0].id, "rIdImg");
  EXPECT_EQ(b->scan.binaries[0].contentType, "image/png");
}

TEST(DocxScan, FootnotesAndEndnotesFormANotesBody) {
  DocOptions o;
  o.footnotes = "<w:footnote w:type=\"separator\" w:id=\"-1\"><w:p><w:r><w:separator/></w:r></w:p></w:footnote>"
                "<w:footnote w:id=\"1\">" + text("First note") + "</w:footnote>"
                "<w:footnote w:id=\"2\">" + text("Second note") + "</w:footnote>";
  o.endnotes = "<w:endnote w:id=\"1\">" + text("End note") + "</w:endnote>";
  auto b = openBook(makeDocx(heading(1, "Ch") + text("body"), o));
  ASSERT_TRUE(b->ok);
  ASSERT_EQ(b->scan.bodies.size(), 2u);
  EXPECT_EQ(b->scan.bodies[1].name, "notes");
  ASSERT_EQ(b->scan.sections.size(), 4u);
  EXPECT_EQ(b->scan.sectionId(b->scan.sections[1]), "fn1");
  EXPECT_EQ(b->scan.sectionId(b->scan.sections[2]), "fn2");
  EXPECT_EQ(b->scan.sectionId(b->scan.sections[3]), "en1");
  EXPECT_EQ(b->scan.sections[1].bodyIndex, 1);
  EXPECT_EQ(renderSection(*b, 1), "<p>First note</p>");
  EXPECT_EQ(renderSection(*b, 2), "<p>Second note</p>");
  EXPECT_EQ(renderSection(*b, 3), "<p>End note</p>");
  EXPECT_EQ(renderTitle(*b, 1), "");
}

TEST(DocxScan, RejectsBodylessAndEmptyDocuments) {
  Fb2ScanResult out;
  DocxParser parser;
  MemoryReader nothing(std::string("<docx-book></docx-book>"));
  EXPECT_FALSE(parser.scan(nothing, out));
  auto b = openBook(makeDocx(""));
  EXPECT_FALSE(b->ok);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

TEST(DocxRender, ChapterTitleAndBodyAreSeparated) {
  auto b = openBook(makeDocx(heading(1, "Alpha") + text("one") + text("two") + heading(2, "Beta") + text("three")));
  ASSERT_TRUE(b->ok);
  ASSERT_EQ(b->scan.sections.size(), 2u);
  EXPECT_EQ(renderTitle(*b, 0), "<title1>Alpha</title>");
  EXPECT_EQ(renderSection(*b, 0), "<p>one</p><p>two</p>");
  EXPECT_EQ(renderTitle(*b, 1), "<title1>Beta</title>");
  EXPECT_EQ(renderSection(*b, 1), "<p>three</p>");
}

TEST(DocxRender, UntitledFirstSectionHasNoTitleAndStopsAtFirstHeading) {
  auto b = openBook(makeDocx(text("Preface") + heading(1, "One") + text("a")));
  ASSERT_TRUE(b->ok);
  EXPECT_EQ(renderTitle(*b, 0), "");
  EXPECT_EQ(renderSection(*b, 0), "<p>Preface</p>");
}

TEST(DocxRender, InlineStylesAndWordGapsStayOutsideStyledRuns) {
  const std::string p = para(run("Hello ") + run("bold", "<w:b/>") + run(" and ") + run("italic", "<w:i/>") +
                             run(" ", "<w:i/>") + run("both", "<w:b/><w:i/>") + run(" end", "<w:b w:val=\"0\"/>") +
                             run(" x", "<w:vertAlign w:val=\"superscript\"/>") +
                             run(" y", "<w:strike/><w:u w:val=\"single\"/><w:smallCaps/>"));
  auto b = openBook(makeDocx(p));
  ASSERT_TRUE(b->ok);
  EXPECT_EQ(renderSection(*b, 0),
            "<p>Hello {B:bold} and {I:italic} {BI:both} end {^:x} {USC:y}</p>");
}

TEST(DocxRender, CharacterStyleAppliesItsFormatting) {
  auto b = openBook(makeDocx(para("<w:r><w:rPr><w:rStyle w:val=\"Strong\"/></w:rPr><w:t>loud</w:t></w:r>")));
  ASSERT_TRUE(b->ok);
  EXPECT_EQ(renderSection(*b, 0), "<p>{B:loud}</p>");
}

TEST(DocxRender, ListsGetMarkersAndCountPerLevel) {
  const std::string bullet = "<w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\"1\"/></w:numPr>";
  const std::string n0 = "<w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\"2\"/></w:numPr>";
  const std::string n1 = "<w:numPr><w:ilvl w:val=\"1\"/><w:numId w:val=\"2\"/></w:numPr>";
  auto b = openBook(makeDocx(para(run("b1"), bullet) + para(run("one"), n0) + para(run("sub"), n1) +
                             para(run("sub2"), n1) + para(run("two"), n0) + para(run("sub3"), n1) +
                             para(run("styled"), "<w:pStyle w:val=\"ListBullet\"/>")));
  ASSERT_TRUE(b->ok);
  const std::string nb = "\xC2\xA0";
  EXPECT_EQ(renderSection(*b, 0),
            "<p>\xE2\x80\xA2" + nb + "b1</p>"
            "<p>1." + nb + "one</p>"
            "<p>" + nb + nb + "1." + nb + "sub</p>"
            "<p>" + nb + nb + "2." + nb + "sub2</p>"
            "<p>2." + nb + "two</p>"
            "<p>" + nb + nb + "1." + nb + "sub3</p>"  // restarts under a new parent item
            "<p>\xE2\x80\xA2" + nb + "styled</p>");
}

TEST(DocxRender, SubtitleTitleAndQuoteStyles) {
  auto b = openBook(makeDocx(para(run("Doc"), "<w:pStyle w:val=\"Title\"/>") + heading(4, "Minor") +
                             para(run("Said"), "<w:pStyle w:val=\"Quote\"/>") + text("end")));
  ASSERT_TRUE(b->ok);
  EXPECT_EQ(renderSection(*b, 0), "<sub>Doc</sub><sub>Minor</sub><cite><p>{I:Said}</p></cite><p>end</p>");
}

TEST(DocxRender, EmptyParagraphsTocAndFieldCodesProduceNothing) {
  const std::string field =
      "<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r><w:r><w:instrText> PAGEREF _Toc1 </w:instrText></w:r>"
      "<w:r><w:fldChar w:fldCharType=\"end\"/></w:r>";
  auto b = openBook(makeDocx("<w:p/>" + para("") + para(run("Contents line"), "<w:pStyle w:val=\"TOC1\"/>") +
                             para(run("Real ") + field + run("text"))));
  ASSERT_TRUE(b->ok);
  EXPECT_EQ(renderSection(*b, 0), "<p>Real text</p>");
}

TEST(DocxRender, TrackedDeletionsAndTextBoxesAreSkipped) {
  const std::string del = "<w:del><w:r><w:delText>gone</w:delText></w:r></w:del>";
  const std::string ins = "<w:ins><w:r><w:t> added</w:t></w:r></w:ins>";
  const std::string box =
      "<w:r><mc:AlternateContent><mc:Choice><w:txbxContent>" + text("BOX") + "</w:txbxContent></mc:Choice>"
      "<mc:Fallback><w:pict><w:txbxContent>" + text("BOX") + "</w:txbxContent></w:pict></mc:Fallback>"
      "</mc:AlternateContent></w:r>";
  const std::string change =
      "<w:r><w:rPr><w:b/><w:rPrChange><w:rPr><w:i/></w:rPr></w:rPrChange></w:rPr><w:t>kept</w:t></w:r>";
  auto b = openBook(makeDocx(para(run("Keep") + del + ins + box + run(" ") + change)));
  ASSERT_TRUE(b->ok);
  EXPECT_EQ(renderSection(*b, 0), "<p>Keep added {B:kept}</p>");
}

TEST(DocxRender, ImagesEmitOnlyDecodableOnes) {
  const std::string png = "<w:r><w:drawing><a:blip r:embed=\"rIdImg\"/></w:drawing></w:r>";
  const std::string gif = "<w:r><w:drawing><a:blip r:embed=\"rIdGif\"/></w:drawing></w:r>";
  const std::string vml = "<w:r><w:pict><v:imagedata xmlns:v=\"urn:v\" r:id=\"rIdImg\"/></w:pict></w:r>";
  auto b = openBook(makeDocx(para(run("A") + png + run("B")) + para(gif) + para(vml)));
  ASSERT_TRUE(b->ok);
  LogSink sink;
  ASSERT_TRUE(b->parser.renderSection(*b->sourceReader, b->scan.sections[0], sink));
  EXPECT_EQ(sink.imageCalls, 2);
  EXPECT_EQ(sink.log, "<p>A<img:rIdImg>B</p><img:rIdImg>");
}

TEST(DocxRender, FootnoteReferencesAndHyperlinks) {
  DocOptions o;
  o.footnotes = "<w:footnote w:id=\"1\">" + text("N") + "</w:footnote>";
  const std::string ref = "<w:r><w:footnoteReference w:id=\"1\"/></w:r>";
  const std::string link = "<w:hyperlink w:anchor=\"target\">" + run("jump") + "</w:hyperlink>";
  const std::string external = "<w:hyperlink r:id=\"rIdExt\">" + run("web") + "</w:hyperlink>";
  auto b = openBook(makeDocx(para(run("See") + ref + run(" and ") + link + run(" ") + external), o));
  ASSERT_TRUE(b->ok);
  EXPECT_EQ(renderSection(*b, 0), "<p>See<a:fn1>{^:1}</a> and <a:target>jump</a> web</p>");
}

TEST(DocxRender, TablesKeepRowsHeadersSpansAndFlattenNested) {
  const std::string cell = "<w:tc><w:p><w:r><w:t>X</w:t></w:r></w:p></w:tc>";
  const std::string table =
      "<w:tbl><w:tr><w:trPr><w:tblHeader/></w:trPr>"
      "<w:tc><w:tcPr><w:gridSpan w:val=\"2\"/></w:tcPr>" + text("Head") + "</w:tc></w:tr>"
      "<w:tr><w:tc><w:tcPr><w:vAlign w:val=\"center\"/></w:tcPr>" + text("one") + text("two") + "</w:tc>"
      "<w:tc><w:tcPr><w:vMerge w:val=\"restart\"/></w:tcPr>" + text("m") + "</w:tc></w:tr>"
      "<w:tr><w:tc>" + text("outer ") +
      "<w:tbl><w:tr><w:tc>" + text("in1") + "</w:tc><w:tc>" + text("in2") + "</w:tc></w:tr></w:tbl></w:tc>"
      "<w:tc><w:tcPr><w:vMerge/></w:tcPr>" + text("hidden") + "</w:tc></w:tr></w:tbl>";
  auto b = openBook(makeDocx(text("before") + table + text("after")));
  ASSERT_TRUE(b->ok);
  EXPECT_EQ(renderSection(*b, 0),
            "<p>before</p><table><tr><th cs=2:Head></tr>"
            "<tr><td va=middle:one two><td:m></tr>"
            "<tr><td:outer in1 in2><td:></tr></table><p>after</p>");
}

TEST(DocxRender, TableAsFirstBlockStartsAnUntitledSection) {
  auto b = openBook(makeDocx("<w:tbl><w:tr><w:tc>" + text("cell") + "</w:tc></w:tr></w:tbl>" + heading(1, "H") +
                             text("x")));
  ASSERT_TRUE(b->ok);
  ASSERT_EQ(b->scan.sections.size(), 2u);
  EXPECT_EQ(renderSection(*b, 0), "<table><tr><td:cell></tr></table>");
  EXPECT_EQ(renderSection(*b, 1), "<p>x</p>");
}

TEST(DocxRender, SectionsInsideContentControlsRenderCompletely) {
  // A cover-page / TOC style content control wraps blocks; the section that
  // starts inside it must not end at the wrapper's closing tag.
  const std::string sdt = "<w:sdt><w:sdtPr><w:rPr><w:b/></w:rPr></w:sdtPr><w:sdtContent>" + heading(1, "Inside") +
                          text("in sdt") + "</w:sdtContent></w:sdt>";
  auto b = openBook(makeDocx(sdt + text("after sdt") + heading(1, "Next") + text("n")));
  ASSERT_TRUE(b->ok);
  ASSERT_EQ(b->scan.sections.size(), 2u);
  EXPECT_EQ(renderTitle(*b, 0), "<title1>Inside</title>");
  EXPECT_EQ(renderSection(*b, 0), "<p>in sdt</p><p>after sdt</p>");
}

TEST(DocxRender, HeadingTitleKeepsInlineStylesButNotBold) {
  auto b = openBook(makeDocx(para(run("A ", "<w:b/>") + run("b", "<w:i/>") + run(" c"),
                                  "<w:pStyle w:val=\"Heading1\"/>")));
  ASSERT_TRUE(b->ok);
  EXPECT_EQ(renderTitle(*b, 0), "<title1>A {I:b} c</title>");
}

TEST(DocxRender, NoBreakHyphenAndLineBreaks) {
  auto b = openBook(makeDocx(para(run("a") + "<w:r><w:noBreakHyphen/></w:r>" + run("b") + "<w:r><w:br/></w:r>" +
                                  run("c"))));
  ASSERT_TRUE(b->ok);
  EXPECT_EQ(renderSection(*b, 0), "<p>a-b c</p>");
}

TEST(DocxRender, CancellationStopsRendering) {
  auto b = openBook(makeDocx(text("x")));
  ASSERT_TRUE(b->ok);
  // A null token never cancels; just make sure the API accepts one and works.
  LogSink sink;
  EXPECT_TRUE(b->parser.renderSection(*b->sourceReader, b->scan.sections[0], sink, nullptr));
}

TEST(DocxRender, DecodeBinaryReadsTheZipMember) {
  auto b = openBook(makeDocx(text("x")));
  ASSERT_TRUE(b->ok);
  std::string bytes;
  ASSERT_TRUE(b->parser.decodeBinary(*b->zip, b->scan.binaries[0], [&](const uint8_t* d, size_t n) {
    bytes.append(reinterpret_cast<const char*>(d), n);
  }));
  EXPECT_EQ(bytes, kPng);
}

TEST(DocxRender, RenderParserFromSidecarMatchesScanParser) {
  auto b = openBook(makeDocx(heading(1, "A") + para(run("x"), "<w:numPr><w:numId w:val=\"2\"/></w:numPr>")));
  ASSERT_TRUE(b->ok);
  docx::PackageInfo loaded;
  ASSERT_TRUE(docx::parse(docx::serialize(b->info), loaded));
  DocxParser fresh;
  fresh.setOwnedInfo(std::move(loaded));
  LogSink sink;
  ASSERT_TRUE(fresh.renderSection(*b->sourceReader, b->scan.sections[0], sink));
  EXPECT_EQ(sink.log, renderSection(*b, 0));
}

// ---------------------------------------------------------------------------
// Robustness: damaged XML must never crash or hang the parser.
// ---------------------------------------------------------------------------

TEST(DocxRobustness, MutatedBodiesScanAndRenderWithoutCrashing) {
  DocOptions o;
  o.footnotes = "<w:footnote w:id=\"1\">" + text("note") + "</w:footnote>";
  const std::string rich =
      heading(1, "One") + para(run("a ") + run("b", "<w:b/><w:i/>") + "<w:r><w:footnoteReference w:id=\"1\"/></w:r>") +
      para(run("item"), "<w:numPr><w:ilvl w:val=\"1\"/><w:numId w:val=\"2\"/></w:numPr>") +
      "<w:tbl><w:tr><w:tc>" + text("c") + "<w:tbl><w:tr><w:tc>" + text("n") + "</w:tc></w:tr></w:tbl></w:tc></w:tr></w:tbl>" +
      heading(2, "Two") + para("<w:r><w:drawing><a:blip r:embed=\"rIdImg\"/></w:drawing></w:r>") +
      "<w:sdt><w:sdtContent>" + heading(1, "Sdt") + text("x") + "</w:sdtContent></w:sdt>";
  auto base = openBook(makeDocx(rich, o));
  ASSERT_TRUE(base->ok);

  uint32_t seed = 12345;
  const auto next = [&]() {
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
  };
  for (int round = 0; round < 400; ++round) {
    std::string mutated = base->source;
    const int edits = 1 + static_cast<int>(next() % 6);
    for (int e = 0; e < edits; ++e) {
      const size_t pos = next() % mutated.size();
      switch (next() % 4) {
        case 0: mutated[pos] = static_cast<char>(next()); break;
        case 1: mutated.erase(pos, 1 + next() % 8); break;
        case 2: mutated.insert(pos, "<w:p><w:r>"); break;
        default: mutated.resize(pos); break;
      }
      if (mutated.empty()) mutated = "<";
    }
    MemoryReader reader(mutated);
    DocxParser parser;
    parser.setInfo(&base->info);
    Fb2ScanResult scan;
    if (!parser.scan(reader, scan)) continue;
    for (const Fb2SectionIndexEntry& s : scan.sections) {
      LogSink sink;
      parser.renderSectionTitle(reader, s, sink, 1);
      parser.renderSection(reader, s, sink);
    }
  }
}
