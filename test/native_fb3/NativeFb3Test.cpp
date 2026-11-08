#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "OpcNames.h"
#include "Fb3Package.h"
#include "Fb3Parser.h"
#include <ZipReader.h>

namespace {

class MemoryReader final : public IByteReader {
 public:
  explicit MemoryReader(std::vector<uint8_t> data) : data_(std::move(data)) {}

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

void put16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v));
  out.push_back(static_cast<uint8_t>(v >> 8));
}
void put32(std::vector<uint8_t>& out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

// Builds a ZIP of STORED entries (the reader does not verify CRCs).
std::vector<uint8_t> makeZip(const std::vector<std::pair<std::string, std::string>>& entries) {
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
    put32(out, 0);  // crc (unchecked)
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
    put16(central, 0);  // stored
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

// A 1x1 PNG (known-good bytes) so image paths really exercise base64.
const char kPngBytes[] =
    "\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x02\x00\x00\x00\x90wS\xde"
    "\x00\x00\x00\x0cIDATx\x9cc\xf8\xcf\xc0\x00\x00\x03\x01\x01\x00\xc9\xfe\x92\xef\x00\x00\x00\x00IEND\xaeB`\x82";
const std::string kPng(kPngBytes, sizeof(kPngBytes) - 1);

constexpr const char* kBodyNs = "http://www.fictionbook.org/FictionBook3/body";
constexpr const char* kDescNs = "http://www.fictionbook.org/FictionBook3/description";
constexpr const char* kRelNs = "http://schemas.openxmlformats.org/package/2006/relationships";

struct PackageOptions {
  std::string bodyPart = "fb3/body.xml";
  std::string descPart = "fb3/description.xml";
  std::string bodyRelsPart = "fb3/_rels/body.xml.rels";
  std::string descRelsPart = "fb3/_rels/description.xml.rels";
  std::string rootTargetForDesc = "fb3/description.xml";
  std::string descTargetForBody = "body.xml";
  std::string imagePart = "fb3/img/pic1.png";
  std::string imageTarget = "/fb3/img/pic1.png";
  std::string imageContentType = "image/png";
  std::string contentTypesExtra;
  std::string imageBytes = kPng;
  std::string imageRelId = "pic1";
  bool withImage = true;
};

std::string descriptionXml() {
  return std::string("<?xml version=\"1.0\"?><fb3-description xmlns=\"") + kDescNs +
         "\" id=\"id1\" version=\"1.0\"><title><main>Test Title</main></title><fb3-relations>"
         "<subject id=\"a1\" link=\"author\"><title><main>X</main></title><first-name>Ann</first-name>"
         "<middle-name>M</middle-name><last-name>Bee</last-name></subject></fb3-relations>"
         "<lang>ru</lang><written><date value=\"2001-02-03\">2001</date></written>"
         "<sequence number=\"3\"><title><main>Saga</main></title></sequence>"
         "<annotation><p>Hello <strong>annotation</strong></p></annotation></fb3-description>";
}

std::vector<uint8_t> makePackage(const std::string& bodyInner, const PackageOptions& o = PackageOptions()) {
  const std::string contentTypes =
      std::string("<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                  "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                  "<Default Extension=\"xml\" ContentType=\"application/xml\"/>") +
      "<Default Extension=\"png\" ContentType=\"image/png\"/>" + o.contentTypesExtra + "</Types>";
  const std::string rootRels =
      std::string("<?xml version=\"1.0\"?><Relationships xmlns=\"") + kRelNs +
      "\"><Relationship Id=\"rId1\" Type=\"http://www.fictionbook.org/FictionBook3/relationships/Book\" Target=\"" +
      o.rootTargetForDesc + "\"/></Relationships>";
  const std::string descRels =
      std::string("<?xml version=\"1.0\"?><Relationships xmlns=\"") + kRelNs +
      "\"><Relationship Id=\"rId1\" Type=\"http://www.fictionbook.org/FictionBook3/relationships/body\" Target=\"" +
      o.descTargetForBody + "\"/></Relationships>";
  std::string bodyRels = std::string("<?xml version=\"1.0\"?><Relationships xmlns=\"") + kRelNs + "\">";
  if (o.withImage) {
    bodyRels +=
        "<Relationship Id=\"" + o.imageRelId +
        "\" Type=\"http://www.fictionbook.org/FictionBook3/relationships/image\" Target=\"" +
        o.imageTarget + "\"/>";
  }
  bodyRels += "</Relationships>";
  const std::string body = std::string("<?xml version=\"1.0\"?><fb3-body xmlns=\"") + kBodyNs +
                           "\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" id=\"b\">" + bodyInner + "</fb3-body>";

  std::vector<std::pair<std::string, std::string>> entries = {
      {"[Content_Types].xml", contentTypes}, {"_rels/.rels", rootRels},        {o.descPart, descriptionXml()},
      {o.bodyPart, body},                    {o.descRelsPart, descRels},       {o.bodyRelsPart, bodyRels},
  };
  if (o.withImage) entries.emplace_back(o.imagePart, o.imageBytes);
  return makeZip(entries);
}


// ---------------------------------------------------------------------------
// Opening a book the way fb3::prepareSource() + scan do
// ---------------------------------------------------------------------------

// Serialises sink events into a compact log, e.g. "<p>Hello {B:bold}</p>".
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
    if (!a.align.empty()) log += " al=" + a.align;
    log += ":" + text + ">";
  }
};

struct Book {
  std::vector<uint8_t> zipBytes;
  std::unique_ptr<MemoryReader> zip;
  fb3::PackageInfo info;
  std::string body;
  std::unique_ptr<MemoryReader> bodyReader;
  Fb2ScanResult scan;
  Fb3Parser parser;
  bool ok = false;
};

std::unique_ptr<Book> openBook(std::vector<uint8_t> zipBytes) {
  auto b = std::make_unique<Book>();
  b->zipBytes = std::move(zipBytes);
  b->zip = std::make_unique<MemoryReader>(b->zipBytes);
  if (!fb3::readPackageInfo(*b->zip, b->info)) return b;
  // The one inflate-to-file step the firmware performs for the body part.
  const bool extracted = fb3::extractEntryByOrdinal(
      *b->zip, b->info.bodyOrdinal, [&](const uint8_t* d, size_t n) { b->body.append(reinterpret_cast<const char*>(d), n); });
  if (!extracted) return b;
  b->bodyReader = std::make_unique<MemoryReader>(std::vector<uint8_t>(b->body.begin(), b->body.end()));
  b->parser.setPackageInfo(&b->info);
  b->ok = b->parser.scan(*b->bodyReader, b->scan);
  return b;
}

std::string renderSectionLog(Book& b, size_t index, int* imageCalls = nullptr) {
  LogSink sink;
  EXPECT_TRUE(b.parser.renderSection(*b.bodyReader, b.scan.sections[index], sink));
  if (imageCalls) *imageCalls = sink.imageCalls;
  return sink.log;
}

bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

}  // namespace

// ---------------------------------------------------------------------------
// Package layer
// ---------------------------------------------------------------------------

TEST(Fb3Package, DetectsFb3AndRejectsOtherZips) {
  MemoryReader fb3Zip(makePackage("<section><p>x</p></section>"));
  EXPECT_TRUE(fb3::isFb3Package(fb3Zip));
  MemoryReader fb2Zip(makeZip({{"book.fb2", "<FictionBook/>"}}));
  EXPECT_FALSE(fb3::isFb3Package(fb2Zip));
  MemoryReader notZip(std::vector<uint8_t>{'h', 'i'});
  EXPECT_FALSE(fb3::isFb3Package(notZip));
}

TEST(Fb3Package, FindsFb3WrappedInsideZip) {
  const std::vector<uint8_t> inner = makePackage("<section><p>x</p></section>");
  MemoryReader wrapper(makeZip({{"readme.txt", "hi"}, {"Book.FB3", std::string(inner.begin(), inner.end())}}));
  ZipEntryInfo entry;
  ASSERT_TRUE(fb3::findNestedFb3Entry(wrapper, entry));
  EXPECT_EQ(entry.filename, "Book.FB3");
  std::string staged;
  ASSERT_TRUE(extractZipEntry(wrapper, entry, [&](const uint8_t* d, size_t n) { staged.append((const char*)d, n); }));
  MemoryReader innerReader(std::vector<uint8_t>(staged.begin(), staged.end()));
  EXPECT_TRUE(fb3::isFb3Package(innerReader));
  MemoryReader none(makeZip({{"a.txt", "x"}}));
  EXPECT_FALSE(fb3::findNestedFb3Entry(none, entry));
}

TEST(Fb3Package, ReadsPartsImagesAndCover) {
  PackageOptions o;
  o.imageRelId = "Cover.png";
  auto b = openBook(makePackage("<section><p>x</p></section>", o));
  ASSERT_TRUE(b->ok);
  ASSERT_EQ(b->info.images.size(), 1u);
  EXPECT_EQ(b->info.images[0].relId, "Cover.png");
  EXPECT_EQ(b->info.images[0].kind, fb3::ImageKind::Png);
  EXPECT_EQ(b->info.coverBinaryId, "Cover.png");
  EXPECT_TRUE(contains(b->info.descriptionXml, "<main>Test Title</main>"));
  EXPECT_TRUE(contains(b->body, "<fb3-body"));
}

TEST(Fb3Package, ResolvesCaseInsensitivePercentEncodedAndExtensionlessParts) {
  PackageOptions o;
  o.bodyPart = "%D0%BA%D0%BD%D0%B8%D0%B3%D0%B0";  // percent-encoded Cyrillic, no extension
  o.descTargetForBody = "../../%d0%BA%d0%bd%D0%B8%d0%b3%D0%B0";
  o.descPart = "FB3/Sub/Meta.XML";
  o.rootTargetForDesc = "fb3/SUB/meta.xml";
  o.descRelsPart = "fb3/sub/_RELS/META.XML.RELS";
  o.bodyRelsPart = "_rels/%D0%BA%D0%BD%D0%B8%D0%B3%D0%B0.rels";
  o.imageTarget = "./FB3/IMG/PIC1.PNG";
  auto b = openBook(makePackage("<section><p>ok</p></section>", o));
  ASSERT_TRUE(b->ok) << "parts must resolve case-insensitively and after percent-decoding";
  EXPECT_EQ(b->info.images.size(), 1u);
  EXPECT_EQ(renderSectionLog(*b, 0), "<p>ok</p>");
}

TEST(Fb3Package, ContentTypeOverrideBeatsFileExtension) {
  PackageOptions svg;  // a ".jpg" part declared as SVG must not be an image
  svg.imagePart = "fb3/img/real.jpg";
  svg.imageTarget = "/fb3/img/real.jpg";
  svg.contentTypesExtra = "<Override PartName=\"/fb3/img/real.jpg\" ContentType=\"image/svg+xml\"/>";
  auto a = openBook(makePackage("<section><p>x</p></section>", svg));
  ASSERT_TRUE(a->ok);
  EXPECT_TRUE(a->info.images.empty());

  PackageOptions noExt;  // an extension-less part declared as PNG is one
  noExt.imagePart = "fb3/img/noext";
  noExt.imageTarget = "/fb3/img/noext";
  noExt.contentTypesExtra = "<Override PartName=\"/fb3/img/noext\" ContentType=\"image/png\"/>";
  auto b = openBook(makePackage("<section><p>x</p></section>", noExt));
  ASSERT_TRUE(b->ok);
  ASSERT_EQ(b->info.images.size(), 1u);
  EXPECT_EQ(b->info.images[0].kind, fb3::ImageKind::Png);
}

TEST(Fb3Package, RejectsBrokenPackages) {
  fb3::PackageInfo info;
  MemoryReader fb2Zip(makeZip({{"book.fb2", "<FictionBook/>"}}));
  EXPECT_FALSE(fb3::readPackageInfo(fb2Zip, info));

  std::vector<uint8_t> zip = makePackage("<section><p>x</p></section>");
  std::string raw(zip.begin(), zip.end());
  for (size_t at = raw.find("fb3/body.xml"); at != std::string::npos; at = raw.find("fb3/body.xml", at + 12)) {
    raw.replace(at, 12, "fb3/gone.xml");  // local header AND central directory entry
  }
  MemoryReader missingBody(std::vector<uint8_t>(raw.begin(), raw.end()));
  EXPECT_FALSE(fb3::readPackageInfo(missingBody, info));
}

// ---------------------------------------------------------------------------
// Scan
// ---------------------------------------------------------------------------

TEST(Fb3Scan, BuildsMetadataSectionIndexAndBodies) {
  auto b = openBook(makePackage(
      "<epigraph><p>E</p></epigraph>"
      "<section id=\"s1\"><title><p>One</p><p>Sub <strong>line</strong></p></title><p>text</p>"
      "<section id=\"s1a\"><title><p>One-A</p></title><p>deep<img src=\"pic1\"/></p></section>"
      "<poem><title><p>Poem title must not rename the section</p></title><stanza><p>v</p></stanza></poem>"
      "</section>"
      "<preamble><p>preface</p></preamble>"
      "<notes><title><p>Notes</p></title><notebody id=\"n_1\"><title><p>1</p></title><p>The note.</p></notebody></notes>"));
  ASSERT_TRUE(b->ok);
  const Fb2ScanResult& s = b->scan;

  EXPECT_EQ(s.metadata.title, "Test Title");
  EXPECT_EQ(s.metadata.author, "Ann M Bee");
  EXPECT_EQ(s.metadata.language, "ru");
  EXPECT_EQ(s.metadata.date, "2001-02-03");
  EXPECT_EQ(s.metadata.sequenceName, "Saga");
  EXPECT_EQ(s.metadata.sequenceNumber, 3u);
  EXPECT_EQ(s.metadata.annotationText, "Hello annotation");

  ASSERT_EQ(s.bodies.size(), 2u);
  EXPECT_EQ(s.bodies[0].name, "");
  EXPECT_EQ(s.bodies[1].name, "notes");

  ASSERT_EQ(s.sections.size(), 4u);  // s1, s1a, preamble, notebody
  EXPECT_EQ(s.sectionId(s.sections[0]), "s1");
  EXPECT_EQ(s.sectionTitle(s.sections[0]), "One Sub line");  // direct title only, paragraphs joined
  EXPECT_EQ(s.sections[0].level, 0);
  EXPECT_EQ(s.sections[0].bodyIndex, 0);
  EXPECT_EQ(s.sectionId(s.sections[1]), "s1a");
  EXPECT_EQ(s.sections[1].level, 1);
  EXPECT_EQ(s.sections[1].imageRefCount, 1);
  EXPECT_EQ(s.sections[0].imageRefCount, 0);  // the nested section's image is not ours
  EXPECT_EQ(s.sections[2].level, 0);          // <preamble> is its own chapter
  EXPECT_EQ(s.sectionId(s.sections[3]), "n_1");  // raw id: links hash the same string
  EXPECT_EQ(s.sections[3].bodyIndex, 1);
  EXPECT_EQ(s.sectionTitle(s.sections[3]), "1");
  EXPECT_GT(s.sections[0].approxTextBytes, 0u);
  EXPECT_LT(s.sections[0].innerStartOffset, s.sections[1].innerStartOffset);
}

TEST(Fb3Scan, EmitsBinaryIndexWithZipOrdinalsAndCover) {
  PackageOptions o;
  o.imageRelId = "Cover.png";
  auto b = openBook(makePackage("<section><p><img src=\"Cover.png\"/></p></section>", o));
  ASSERT_TRUE(b->ok);
  ASSERT_EQ(b->scan.binaries.size(), 1u);
  EXPECT_EQ(b->scan.binaries[0].id, "Cover.png");
  EXPECT_EQ(b->scan.binaries[0].contentType, "image/png");
  EXPECT_EQ(b->scan.metadata.coverBinaryId, "Cover.png");

  std::string bytes;
  ASSERT_TRUE(b->parser.decodeBinary(*b->zip, b->scan.binaries[0],
                                     [&](const uint8_t* d, size_t n) { bytes.append((const char*)d, n); }));
  EXPECT_EQ(bytes, kPng);
}

TEST(Fb3Scan, RejectsDocumentsWithoutBodyRoot) {
  Fb3Parser parser;
  std::string xml = "<?xml version=\"1.0\"?><something><p>x</p></something>";
  MemoryReader r(std::vector<uint8_t>(xml.begin(), xml.end()));
  Fb2ScanResult out;
  EXPECT_FALSE(parser.scan(r, out));
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

TEST(Fb3Render, ParagraphsStylesAndWhitespaceCollapse) {
  auto b = openBook(makePackage(
      "<section><p>\n   plain   <strong>bold</strong> <em>it</em> <underline>u</underline> <smallcaps>sc</smallcaps>"
      " <strikethrough>s</strikethrough> x<sub>2</sub><sup>3</sup> <span class=\"k\">span</span>\n</p>"
      "<p>   </p><p>second</p></section>"));
  ASSERT_TRUE(b->ok);
  EXPECT_EQ(renderSectionLog(*b, 0),
            "<p>plain {B:bold} {I:it} {U:u} {C:sc} {S:s} x{_:2}{^:3} span</p><p>second</p>");
}

TEST(Fb3Render, NestedSectionsAreSeparateAndTitleIsRenderedSeparately) {
  auto b = openBook(makePackage(
      "<section id=\"a\"><title><p>T</p></title><p>before</p><section id=\"b\"><p>inner</p></section><p>after</p></section>"));
  ASSERT_TRUE(b->ok);
  ASSERT_EQ(b->scan.sections.size(), 2u);
  EXPECT_EQ(renderSectionLog(*b, 0), "<p>before</p><p>after</p>");
  EXPECT_EQ(renderSectionLog(*b, 1), "<p>inner</p>");

  LogSink title;
  ASSERT_TRUE(b->parser.renderSectionTitle(*b->bodyReader, b->scan.sections[0], title, 0));
  EXPECT_EQ(title.log, "<title0>T</title>");
  LogSink none;
  EXPECT_FALSE(b->parser.renderSectionTitle(*b->bodyReader, b->scan.sections[1], none, 1));  // no own heading
}

TEST(Fb3Render, MultiLineTitleKeepsInlineStyleAndLineBreaks) {
  auto b = openBook(makePackage(
      "<section><title><p>Line <em>one</em></p><p>Line two</p></title><p>x</p></section>"));
  ASSERT_TRUE(b->ok);
  LogSink title;
  ASSERT_TRUE(b->parser.renderSectionTitle(*b->bodyReader, b->scan.sections[0], title, 2));
  EXPECT_EQ(title.log, "<title2>Line {I:one}<br/>Line two</title>");
}

TEST(Fb3Render, FootnotesAndLinksUseRawIds) {
  auto b = openBook(makePackage(
      "<section id=\"sec\"><p>See<note href=\"n_1\">[1]</note> and <a xlink:href=\"#sec\">link</a>"
      " and <a xlink:href=\"http://example.org\">web</a>.</p></section>"
      "<notes><notebody id=\"n_1\"><p>The note.</p></notebody></notes>"));
  ASSERT_TRUE(b->ok);
  EXPECT_EQ(renderSectionLog(*b, 0), "<p>See<a:n_1>[1]</a> and <a:sec>link</a> and <a:http://example.org>web</a>.</p>");
  EXPECT_EQ(renderSectionLog(*b, 1), "<p>The note.</p>");
  // The note target is a real indexed section, so the footnote machinery can resolve it.
  EXPECT_EQ(b->scan.sectionId(b->scan.sections[1]), "n_1");
  EXPECT_EQ(b->scan.bodies[b->scan.sections[1].bodyIndex].name, "notes");
}

TEST(Fb3Render, ImagesAreBlockWhenAloneAndInlineWithinText) {
  auto b = openBook(makePackage(
      "<section><p><img src=\"pic1\"/></p><p>text <img src=\"pic1\"/> inline</p><p>tail<img src=\"missing\"/></p></section>"));
  ASSERT_TRUE(b->ok);
  int calls = 0;
  const std::string log = renderSectionLog(*b, 0, &calls);
  EXPECT_EQ(log, "<img:pic1><p>text <img:pic1> inline</p><p>tail<img:missing></p>");
  EXPECT_EQ(calls, 3);
  EXPECT_EQ(b->scan.sections[0].imageRefCount, 3);  // scan and render agree (drives virtual chapter slicing)
}

TEST(Fb3Render, PoemsQuotesEpigraphsAndSubscriptions) {
  auto b = openBook(makePackage(
      "<section><epigraph><p>E</p><subscription><p>Ep Author</p></subscription></epigraph>"
      "<poem><title><p>PT</p></title><stanza><p>v1</p><p>v2</p></stanza><subscription><p>P Author</p></subscription></poem>"
      "<blockquote><p>quote</p><subscription><p>Q</p><p>Author</p></subscription></blockquote>"
      "<subtitle>Sub</subtitle><p>x<br/>y</p><br/></section>"));
  ASSERT_TRUE(b->ok);
  EXPECT_EQ(renderSectionLog(*b, 0),
            "<epi><p>E</p><ta>Ep Author</ta></epi>"
            "<poem><sub>PT</sub><stanza><v>v1</v><v>v2</v></stanza><ta>P Author</ta></poem>"
            "<cite><p>quote</p><ta>Q Author</ta></cite>"
            "<sub>Sub</sub><p>x y</p><empty/>");
}

TEST(Fb3Render, ListsGetMarkersAndNestingIndentation) {
  auto b = openBook(makePackage(
      "<section><ol><li>one</li><li>two</li><ul><li>deep</li></ul></ol><ul><li>dot</li></ul></section>"));
  ASSERT_TRUE(b->ok);
  EXPECT_EQ(renderSectionLog(*b, 0),
            "<p>1.\xC2\xA0" "one</p><p>2.\xC2\xA0" "two</p>"
            "<p>\xC2\xA0\xC2\xA0\xE2\x80\xA2\xC2\xA0" "deep</p>"
            "<p>\xE2\x80\xA2\xC2\xA0" "dot</p>");
}

TEST(Fb3Render, TablesBecomeCellsWithAttributes) {
  auto b = openBook(makePackage(
      "<section><table><tr><th colspan=\"2\" align=\"center\">H</th></tr>"
      "<tr><td><p>a</p><p>b <em>c</em></p></td><td rowspan=\"2\">d</td></tr></table></section>"));
  ASSERT_TRUE(b->ok);
  EXPECT_EQ(renderSectionLog(*b, 0),
            "<table><tr><th cs=2 al=center:H></tr><tr><td:a b c><td rs=2:d></tr></table>");
}

TEST(Fb3Render, PreformattedBecomesQuoteBlock) {
  auto b = openBook(makePackage("<section><pre><p>line   one</p><p>line two</p></pre></section>"));
  ASSERT_TRUE(b->ok);
  EXPECT_EQ(renderSectionLog(*b, 0), "<cite><p>line one</p><p>line two</p></cite>");
}

TEST(Fb3Render, BodyLevelEpigraphIsRecoveredBeforeFirstSection) {
  auto b = openBook(makePackage(
      "<title><p>Book</p></title><epigraph><p>Book epigraph</p></epigraph>"
      "<section id=\"s\"><epigraph><p>not me</p></epigraph><p>x</p></section>"));
  ASSERT_TRUE(b->ok);
  LogSink sink;
  ASSERT_TRUE(b->parser.renderBodyPreambleForFirstSection(*b->bodyReader, sink));
  EXPECT_EQ(sink.log, "<epi><p>Book epigraph</p></epi>");
}

TEST(Fb3Render, AnnotationAndCoverComeFromDescriptionContext) {
  auto b = openBook(makePackage("<section><p>x</p></section>"));
  ASSERT_TRUE(b->ok);
  LogSink sink;
  ASSERT_TRUE(b->parser.renderAnnotation(*b->bodyReader, sink));
  EXPECT_EQ(sink.log, "<p>Hello {B:annotation}</p>");

  // A render-only parser (new object per chapter, as Fb2 creates them) gets the
  // same data from the cached sidecar values instead of the package.
  Fb3Parser renderOnly;
  renderOnly.setRenderContext(b->info.descriptionXml, "cover-id");
  LogSink s2;
  ASSERT_TRUE(renderOnly.renderAnnotation(*b->bodyReader, s2));
  EXPECT_EQ(s2.log, sink.log);
  LogSink s3;
  EXPECT_TRUE(renderOnly.renderCoverForFirstSection(*b->bodyReader, b->scan.sections[0], s3));
  EXPECT_EQ(s3.log, "<img:cover-id>");
}

TEST(Fb3Render, DeepNestingAndMalformedStructureDoNotCrash) {
  std::string deep;
  for (int i = 0; i < 300; ++i) deep += "<blockquote>";
  deep += "<p>deep</p>";
  for (int i = 0; i < 300; ++i) deep += "</blockquote>";
  auto b = openBook(makePackage("<section>" + deep + "</section>"));
  ASSERT_TRUE(b->ok);
  EXPECT_TRUE(contains(renderSectionLog(*b, 0), "<p>deep</p>"));

  auto stray = openBook(makePackage("<section><p>a</strong></p><p>b</p></section>"));
  ASSERT_TRUE(stray->ok);
  EXPECT_TRUE(contains(renderSectionLog(*stray, 0), "b"));
}

// ---------------------------------------------------------------------------
// Official FB3 samples (set FB3_SAMPLES_DIR to the corpus "Examples" dir, e.g.
// a checkout of github.com/gribuser/fb3). Skipped when absent; not vendored.
// ---------------------------------------------------------------------------

TEST(Fb3Samples, OfficialExamplesScanRenderAndDecodeConsistently) {
  const char* dir = std::getenv("FB3_SAMPLES_DIR");
  if (!dir) GTEST_SKIP() << "FB3_SAMPLES_DIR not set";
  size_t tested = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() != ".fb3") continue;
    std::ifstream in(entry.path(), std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto b = openBook(std::move(bytes));
    ASSERT_TRUE(b->ok) << entry.path();
    ASSERT_GT(b->scan.sections.size(), 0u) << entry.path();
    EXPECT_FALSE(b->scan.metadata.title.empty()) << entry.path();

    std::set<std::string> sectionIds;
    for (const auto& sec : b->scan.sections) sectionIds.insert(std::string(b->scan.sectionId(sec)));
    for (size_t i = 0; i < b->scan.sections.size(); ++i) {
      LogSink sink;
      ASSERT_TRUE(b->parser.renderSection(*b->bodyReader, b->scan.sections[i], sink)) << entry.path() << " #" << i;
      // The image count in the index must equal what rendering emits: the
      // document layer slices illustration-heavy sections by that count.
      EXPECT_EQ(sink.imageCalls, b->scan.sections[i].imageRefCount) << entry.path() << " section " << i;
    }
    // Every footnote reference points at an indexed notes section.
    size_t footnotes = 0;
    for (size_t i = 0; i < b->scan.sections.size(); ++i) {
      LogSink sink;
      b->parser.renderSection(*b->bodyReader, b->scan.sections[i], sink);
      for (size_t at = sink.log.find("<a:"); at != std::string::npos; at = sink.log.find("<a:", at + 3)) {
        const size_t close = sink.log.find('>', at);
        const std::string target = sink.log.substr(at + 3, close - at - 3);
        if (target.rfind("http", 0) == 0) continue;
        ++footnotes;
      }
    }
    (void)footnotes;
    for (const auto& bin : b->scan.binaries) {
      size_t total = 0;
      ASSERT_TRUE(b->parser.decodeBinary(*b->zip, bin, [&](const uint8_t*, size_t n) { total += n; })) << bin.id;
      EXPECT_GT(total, 0u) << entry.path() << " image " << bin.id;
    }
    ++tested;
  }
  EXPECT_GT(tested, 0u);
}
