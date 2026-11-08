#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "BookTestSupport.h"
#include "HtmlParser.h"
#include "HuffCdic.h"
#include "MobiBook.h"
#include "MobiFormat.h"
#include "MobiTestBuilder.h"
#include "PalmDoc.h"
#include <Codepage.h>

using booktest::LogSink;
using booktest::MemoryReader;
using namespace mobitest;

namespace {

const std::string kJpeg = std::string("\xFF\xD8\xFF\xE0", 4) + "JPEGDATA";

std::string toStr(const std::string& s) { return s; }

struct Converted {
  bool ok = false;
  std::string raw;
  std::string html;
  mobi::Error error = mobi::Error::None;
};

Converted convert(const std::string& file) {
  Converted c;
  MemoryReader reader(file);
  mobi::Book book;
  if (!book.open(reader)) {
    c.error = book.error();
    return c;
  }
  if (!book.extractText([&](const char* d, size_t n) {
        c.raw.append(d, n);
        return true;
      })) {
    c.error = book.error();
    return c;
  }
  MemoryReader raw(c.raw);
  c.ok = book.convertToHtml(raw, [&](const char* d, size_t n) {
    c.html.append(d, n);
    return true;
  });
  c.error = book.error();
  return c;
}

std::string bodyOf(const std::string& html) {
  const size_t b = html.find("<body>");
  const size_t e = html.rfind("</body>");
  if (b == std::string::npos || e == std::string::npos) return html;
  std::string flat;
  for (const char c : html.substr(b + 6, e - b - 6)) {
    if (c != '\n') flat.push_back(c);
  }
  return flat;
}

std::string num(uint64_t v) { return std::to_string(v); }

// Legacy (Mobipocket 6) book: rec0, text records, optional images.
Pdb legacyBook(const std::string& raw, const std::vector<std::string>& images = {}, Rec0 o = Rec0(),
               std::vector<std::string> extraRecords = {}, bool trailing = false) {
  std::vector<std::string> texts = makeTextRecords(raw, o.compression == 2, trailing);
  o.textLength = static_cast<uint32_t>(raw.size());
  o.textRecords = static_cast<uint32_t>(texts.size());
  std::vector<std::string> recs = {""};
  for (const auto& t : texts) recs.push_back(t);
  for (const auto& e : extraRecords) recs.push_back(e);
  o.firstImage = o.firstImage == 0xFFFFFFFFu ? static_cast<uint32_t>(recs.size()) : o.firstImage;
  for (const auto& i : images) recs.push_back(i);
  recs[0] = makeRecord0(o);
  return makePdb("Palm Name", recs);
}

}  // namespace

// ---- PalmDOC ---------------------------------------------------------------

TEST(Mobi, PalmDocDecompress) {
  auto run = [](const std::string& in, size_t maxOut = 1000) {
    std::string out;
    const bool ok = mobi::palmDocDecompress(reinterpret_cast<const uint8_t*>(in.data()), in.size(), out, maxOut);
    return std::make_pair(ok, out);
  };
  EXPECT_EQ(run(std::string("\x03" "abc")).second, "abc");
  EXPECT_EQ(run(std::string("\xC1")).second, " A");
  EXPECT_EQ(run(std::string("abc\x80\x18", 5)).second, "abcabc");     // dist 3, len 3
  EXPECT_EQ(run(std::string("a\x80\x0A", 3)).second, "aaaaaa");       // overlapping copy
  EXPECT_EQ(run(std::string("a\x80\x28", 3)).second, "a");            // distance beyond the record: stop
  EXPECT_FALSE(run("abcdef", 3).first);                                // output limit
  EXPECT_EQ(run(std::string("\x05" "ab")).second, "");                // truncated literal run
}

TEST(Mobi, CompressorRoundTrip) {
  std::string text;
  for (int i = 0; i < 300; ++i) text += "The quick brown fox \xE9 jumps over the lazy dog. ";
  text.push_back('\0');
  text += std::string("\x01\x02\x80\xFF", 4);
  const std::string packed = palmCompress(text);
  EXPECT_LT(packed.size(), text.size());
  std::string out;
  ASSERT_TRUE(mobi::palmDocDecompress(reinterpret_cast<const uint8_t*>(packed.data()), packed.size(), out, 1 << 20));
  EXPECT_EQ(out, text);
}

// ---- headers ---------------------------------------------------------------

TEST(Mobi, DetectsFormat) {
  const Pdb p = legacyBook("<p>x</p>");
  EXPECT_TRUE(mobi::looksLikeMobi(reinterpret_cast<const uint8_t*>(p.bytes.data()), p.bytes.size()));
  EXPECT_FALSE(mobi::looksLikeMobi(reinterpret_cast<const uint8_t*>("short"), 5));
  EXPECT_TRUE(mobi::hasMobiExtension("a/b/Book.AZW3"));
  EXPECT_TRUE(mobi::hasMobiExtension("x.mobi"));
  EXPECT_FALSE(mobi::hasMobiExtension("x.epub"));
}

TEST(Mobi, RejectsBadFiles) {
  {
    MemoryReader r(std::string(200, 'x'));
    mobi::Book b;
    EXPECT_FALSE(b.open(r));
    EXPECT_EQ(b.error(), mobi::Error::NotMobi);
  }
  {
    MemoryReader r(std::string("tiny"));
    mobi::Book b;
    EXPECT_FALSE(b.open(r));
    EXPECT_EQ(b.error(), mobi::Error::Truncated);
  }
  {
    Rec0 o;
    o.crypto = 2;
    EXPECT_EQ(convert(legacyBook("<p>x</p>", {}, o).bytes).error, mobi::Error::Drm);
  }
  {
    Rec0 o;
    o.compression = 7;
    EXPECT_EQ(convert(legacyBook("<p>x</p>", {}, o).bytes).error, mobi::Error::Unsupported);
  }
  {
    Rec0 o;
    o.compression = 1;
    EXPECT_EQ(convert(legacyBook("%MOP print replica", {}, o).bytes).error, mobi::Error::Unsupported);
  }
}

TEST(Mobi, MetadataFromHeaderAndExth) {
  Rec0 o;
  o.title = "Header Title";
  o.exth = {{100, "Ann"}, {100, "Bob"}, {101, "Pub"}, {103, "About"}, {106, "2020-01-02"}, {524, "en-US"}};
  MemoryReader r(legacyBook("<p>x</p>", {}, o).bytes);
  mobi::Book b;
  ASSERT_TRUE(b.open(r));
  EXPECT_EQ(b.metadata().title, "Header Title");
  EXPECT_EQ(b.metadata().author, "Ann, Bob");
  EXPECT_EQ(b.metadata().publisher, "Pub");
  EXPECT_EQ(b.metadata().description, "About");
  EXPECT_EQ(b.metadata().published, "2020-01-02");
  EXPECT_EQ(b.metadata().language, "en-US");
  EXPECT_FALSE(b.isKf8());
}

TEST(Mobi, UpdatedTitleAndLanguageFromHeaderCode) {
  Rec0 o;
  o.title = "Old";
  o.lang = 25;
  o.exth = {{503, "New Title"}};
  MemoryReader r(legacyBook("<p>x</p>", {}, o).bytes);
  mobi::Book b;
  ASSERT_TRUE(b.open(r));
  EXPECT_EQ(b.metadata().title, "New Title");
  EXPECT_EQ(b.metadata().language, "ru");
}

TEST(Mobi, Cp1252AndUtf8Titles) {
  Rec0 o;
  o.title = "Caf\xE9";
  {
    MemoryReader r(legacyBook("<p>x</p>", {}, o).bytes);
    mobi::Book b;
    ASSERT_TRUE(b.open(r));
    EXPECT_EQ(b.metadata().title, "Caf\xC3\xA9");
  }
  o.codepage = 65001;
  o.title = "Caf\xC3\xA9";
  MemoryReader r(legacyBook("<p>x</p>", {}, o).bytes);
  mobi::Book b;
  ASSERT_TRUE(b.open(r));
  EXPECT_EQ(b.metadata().title, "Caf\xC3\xA9");
}

// ---- text extraction -------------------------------------------------------

TEST(Mobi, ExtractsCompressedMultiRecordText) {
  std::string raw = "<html><body>";
  for (int i = 0; i < 2000; ++i) raw += "<p>Paragraph number " + std::to_string(i) + " of the test.</p>";
  raw += "</body></html>";
  const Converted c = convert(legacyBook(raw).bytes);
  ASSERT_TRUE(c.ok);
  EXPECT_EQ(c.raw, raw);
}

TEST(Mobi, UncompressedText) {
  Rec0 o;
  o.compression = 1;
  const Converted c = convert(legacyBook("<p>plain</p>", {}, o).bytes);
  ASSERT_TRUE(c.ok);
  EXPECT_EQ(c.raw, "<p>plain</p>");
}

TEST(Mobi, StripsTrailingEntries) {
  Rec0 o;
  o.trailFlags = 3;  // multibyte overlap + one trailing entry
  std::string raw;
  for (int i = 0; i < 700; ++i) raw += "line " + std::to_string(i) + "<br/>";
  const Converted c = convert(legacyBook(raw, {}, o, {}, true).bytes);
  ASSERT_TRUE(c.ok);
  EXPECT_EQ(c.raw, raw);
}

// ---- HUFF/CDIC -------------------------------------------------------------

namespace {

// A toy Huffman set: every code is 8 bits, byte b selects phrase 255-b.
std::string makeHuffRecord() {
  std::string h = "HUFF";
  be32(h, 0x18);
  be32(h, 24);
  be32(h, 24 + 1024);
  h.resize(24, '\0');
  for (int i = 0; i < 256; ++i) be32(h, (255u << 8) | 0x80 | 8);
  h.append(256, '\0');
  return h;
}

std::string makeCdicRecord() {
  std::vector<std::string> phrases(256);
  std::vector<bool> literal(256, true);
  for (int i = 0; i < 256; ++i) phrases[i] = std::string(1, static_cast<char>(i));
  phrases[200] = "hello ";
  phrases[201] = std::string(1, static_cast<char>(255 - 'a')) + static_cast<char>(255 - 'b');
  literal[201] = false;  // compressed phrase, decoded recursively
  std::string c = "CDIC";
  be32(c, 0x10);
  be32(c, 256);
  be32(c, 8);
  std::string data;
  for (int i = 0; i < 256; ++i) {
    be16(c, static_cast<uint32_t>(512 + data.size()));
    be16(data, static_cast<uint32_t>(phrases[i].size()) | (literal[i] ? 0x8000u : 0u));
    data += phrases[i];
  }
  return c + data;
}

std::string huffEncode(const std::string& text) {
  std::string out;
  for (const char c : text) out.push_back(static_cast<char>(255 - static_cast<uint8_t>(c)));
  return out;
}

}  // namespace

TEST(Mobi, HuffCdicDecompression) {
  std::string packed = huffEncode("Say ");
  packed.push_back(static_cast<char>(255 - 200));  // "hello "
  packed.push_back(static_cast<char>(255 - 201));  // nested phrase "ab"
  packed += huffEncode("!");

  Rec0 o;
  o.compression = 0x4448;
  o.textLength = 13;
  o.textRecords = 1;
  o.huff = 2;
  o.huffCount = 2;
  o.firstImage = 4;
  const std::vector<std::string> recs = {makeRecord0(o), packed, makeHuffRecord(), makeCdicRecord()};
  const Converted c = convert(makePdb("huff", recs).bytes);
  ASSERT_TRUE(c.ok);
  EXPECT_EQ(c.raw, "Say hello ab!");
}

TEST(Mobi, HuffCdicRejectsGarbage) {
  Rec0 o;
  o.compression = 0x4448;
  o.textLength = 4;
  o.textRecords = 1;
  o.huff = 2;
  o.huffCount = 2;
  const std::vector<std::string> recs = {makeRecord0(o), "abcd", std::string(100, 'x'), std::string(100, 'y')};
  EXPECT_FALSE(convert(makePdb("huff", recs).bytes).ok);
}

// ---- legacy conversion -----------------------------------------------------

TEST(Mobi, LegacyLinksImagesAndEncoding) {
  std::string raw =
      "<html><head><guide><reference type=\"toc\" filepos=0000000010 /></guide></head><body>\n"
      "<p>Caf\xE9 &amp; <a filepos=FILEPOS000>link</a></p>\n<mbp:pagebreak/>\n";
  const size_t target = raw.size();
  raw += "<p id=\"t\">Target <img recindex=\"00001\" /> end</p>\n<img recindex=\"2\"/></body></html>";
  char pos[16];
  snprintf(pos, sizeof(pos), "%010zu", target);
  raw.replace(raw.find("FILEPOS000"), 10, pos);

  Rec0 o;
  o.exth = {{100, "Ann"}};
  const Pdb p = legacyBook(raw, {kJpeg, std::string(40, 'x')}, o);
  const Converted c = convert(p.bytes);
  ASSERT_TRUE(c.ok);

  const uint32_t imgStart = p.offsets[2];  // rec0, text, image
  const std::string range = "x-raw:" + num(imgStart) + "-" + num(imgStart + kJpeg.size()) + ":image/jpeg";
  const std::string t = num(target);
  EXPECT_EQ(bodyOf(c.html), "<p>Caf\xC3\xA9 &amp; <a href=\"#p" + t + "\">link</a></p><a id=\"p" + t +
                                "\"></a><p id=\"t\">Target <img src=\"" + range + "\"/> end</p>");
  EXPECT_NE(c.html.find("<meta name=\"author\" content=\"Ann\">"), std::string::npos);
  EXPECT_EQ(c.html.find("guide"), std::string::npos);
  EXPECT_EQ(c.html.find("mbp:"), std::string::npos);
}

TEST(Mobi, LegacyBookThroughHtmlParser) {
  std::string raw = "<html><body><h2>One</h2><p>First <a filepos=FILEPOS000>go</a></p>";
  const size_t target = raw.size();
  raw += "<h2>Two</h2><p>Second<img recindex=\"1\"/></p></body></html>";
  char pos[16];
  snprintf(pos, sizeof(pos), "%010zu", target);
  raw.replace(raw.find("FILEPOS000"), 10, pos);

  Rec0 o;
  o.title = "Parsed Book";
  o.exth = {{100, "Ann"}, {201, std::string("\x00\x00\x00\x00", 4)}};
  const Pdb p = legacyBook(raw, {kJpeg}, o);
  const Converted c = convert(p.bytes);
  ASSERT_TRUE(c.ok);

  MemoryReader hr(c.html);
  HtmlParser parser;
  Fb2ScanResult scan;
  ASSERT_TRUE(parser.scan(hr, scan));
  EXPECT_EQ(scan.metadata.title, "Parsed Book");
  EXPECT_EQ(scan.metadata.author, "Ann");
  ASSERT_EQ(scan.sections.size(), 2u);
  EXPECT_EQ(scan.sectionTitle(scan.sections[0]), "One");
  EXPECT_EQ(scan.sectionTitle(scan.sections[1]), "Two");
  EXPECT_FALSE(scan.metadata.coverBinaryId.empty());

  // The cover and the inline image decode from the ORIGINAL book file.
  ASSERT_GE(scan.binaries.size(), 1u);
  MemoryReader original(p.bytes);
  for (const auto& bin : scan.binaries) {
    EXPECT_EQ(bin.id[0], 'r');
    std::string bytes;
    ASSERT_TRUE(parser.decodeBinary(original, bin, [&](const uint8_t* d, size_t n) { bytes.append((const char*)d, n); }));
    EXPECT_EQ(bytes, kJpeg);
  }
  LogSink s1;
  ASSERT_TRUE(parser.renderSection(hr, scan.sections[1], s1));
  EXPECT_EQ(s1.imageCalls, 1);
}

// ---- NCX headings ----------------------------------------------------------

namespace {

std::vector<std::string> makeNcxRecords(const std::vector<std::tuple<uint32_t, uint32_t, std::string>>& items,
                                        bool kf8) {
  std::string ctoc;
  std::vector<IndexEntryB> entries;
  int n = 0;
  for (const auto& [pos, level, label] : items) {
    uint32_t off = 0;
    ctoc = ctocEntry(label, &off, ctoc);
    IndexEntryB e;
    char name[8];
    snprintf(name, sizeof(name), "%04d", n++);
    e.text = name;
    if (kf8) {
      e.values = {{3, {off}}, {4, {level}}, {6, {pos, 0}}};  // pos = fragment id
    } else {
      e.values = {{1, {pos}}, {3, {off}}, {4, {level}}};
    }
    entries.push_back(e);
  }
  const std::vector<TagDef> defs = {{1, 1, 0x01, 0}, {3, 1, 0x02, 0}, {4, 1, 0x04, 0}, {6, 2, 0x08, 0}, {0, 0, 0, 1}};
  return makeIndex(defs, entries, ctoc);
}

}  // namespace

TEST(Mobi, NcxProvidesHeadingsWhenTextHasNone) {
  const std::string raw = "<html><body><p>Intro text</p><p>First chapter text</p><p>Second chapter</p></body></html>";
  const uint32_t first = static_cast<uint32_t>(raw.find("<p>First"));
  const uint32_t second = static_cast<uint32_t>(raw.find("<p>Second"));
  auto ncx = makeNcxRecords({{first, 0, "Chapter One"}, {second, 1, "Chapter & Two"}}, false);

  Rec0 o;
  o.ncx = 2;  // rec0, text, then the index
  const Converted c = convert(legacyBook(raw, {}, o, ncx).bytes);
  ASSERT_TRUE(c.ok);
  EXPECT_EQ(bodyOf(c.html),
            "<p>Intro text</p><h1>Chapter One</h1><p>First chapter text</p><h2>Chapter &amp; Two</h2><p>Second chapter</p>");
}

TEST(Mobi, NcxIgnoredWhenTextHasHeadings) {
  const std::string raw = "<html><body><h2>Own</h2><p>First chapter text</p></body></html>";
  auto ncx = makeNcxRecords({{static_cast<uint32_t>(raw.find("<p>First")), 0, "Chapter One"}}, false);
  Rec0 o;
  o.ncx = 2;
  const Converted c = convert(legacyBook(raw, {}, o, ncx).bytes);
  ASSERT_TRUE(c.ok);
  EXPECT_EQ(bodyOf(c.html), "<h2>Own</h2><p>First chapter text</p>");
}

// ---- KF8 -------------------------------------------------------------------

namespace {

struct Kf8Parts {
  std::string raw;
  uint32_t p0 = 0;       // insertion point inside skeleton 0
  uint32_t linkPos = 0;  // position of fragment 1
  std::vector<std::string> indexRecords;
  std::vector<std::string> skel, frag;
};

Kf8Parts makeKf8Parts(const std::string& f0) {
  Kf8Parts k;
  const std::string skel0 = "<html><head><title>x</title></head><body aid=\"0\"></body></html>";
  const std::string f1 = "<p>Beta <img src=\"kindle:embed:0001?mime=image/jpeg\" alt=\"pic\"/></p>";
  const std::string skel1 =
      "<html><head></head><body><svg><image xlink:href=\"kindle:embed:0001?mime=image/jpeg\"/></svg></body></html>";
  const std::string f2 = "<p>Gamma</p>";
  k.raw = skel0 + f0 + f1 + skel1 + f2;
  const uint32_t skelPos1 = static_cast<uint32_t>(skel0.size() + f0.size() + f1.size());
  k.p0 = static_cast<uint32_t>(skel0.find("</body>"));
  const uint32_t p1 = static_cast<uint32_t>(skel1.find("</body>"));
  k.linkPos = k.p0 + static_cast<uint32_t>(f0.size());

  const std::vector<TagDef> skelDefs = {{1, 1, 0x01, 0}, {6, 2, 0x02, 0}, {0, 0, 0, 1}};
  std::vector<IndexEntryB> skelEntries(2);
  skelEntries[0].text = "SKEL0000";
  skelEntries[0].values = {{1, {2}}, {6, {0, static_cast<uint32_t>(skel0.size())}}};
  skelEntries[1].text = "SKEL0001";
  skelEntries[1].values = {{1, {1}}, {6, {skelPos1, static_cast<uint32_t>(skel1.size())}}};
  k.skel = makeIndex(skelDefs, skelEntries, "");

  const std::vector<TagDef> fragDefs = {{6, 2, 0x01, 0}, {0, 0, 0, 1}};
  std::vector<IndexEntryB> fragEntries(3);
  fragEntries[0].text = std::to_string(k.p0);
  fragEntries[0].values = {{6, {static_cast<uint32_t>(skel0.size()), static_cast<uint32_t>(f0.size())}}};
  fragEntries[1].text = std::to_string(k.linkPos);
  fragEntries[1].values = {{6, {static_cast<uint32_t>(skel0.size() + f0.size()), static_cast<uint32_t>(f1.size())}}};
  fragEntries[2].text = std::to_string(skelPos1 + p1);
  fragEntries[2].values = {{6, {skelPos1 + static_cast<uint32_t>(skel1.size()), static_cast<uint32_t>(f2.size())}}};
  k.frag = makeIndex(fragDefs, fragEntries, "");
  return k;
}

// rec0, text, skeleton index, fragment index, [ncx], image.
Pdb kf8Book(const Kf8Parts& k, const std::vector<std::string>& ncx = {}, Rec0 o = Rec0()) {
  std::vector<std::string> recs = {"", palmCompress(k.raw)};
  o.version = 8;
  o.minVersion = 8;
  o.codepage = 65001;
  o.textLength = static_cast<uint32_t>(k.raw.size());
  o.textRecords = 1;
  o.skel = static_cast<uint32_t>(recs.size());
  for (const auto& r : k.skel) recs.push_back(r);
  o.frag = static_cast<uint32_t>(recs.size());
  for (const auto& r : k.frag) recs.push_back(r);
  if (!ncx.empty()) {
    o.ncx = static_cast<uint32_t>(recs.size());
    for (const auto& r : ncx) recs.push_back(r);
  }
  o.firstImage = static_cast<uint32_t>(recs.size());
  recs.push_back(kJpeg);
  recs[0] = makeRecord0(o);
  return makePdb("kf8", recs);
}

}  // namespace

TEST(Mobi, Kf8SkeletonFragmentAssembly) {
  const std::string f0 =
      "<h1>One</h1><p id=\"a\">Alpha <a href=\"kindle:pos:fid:0001:off:0000000000\">go</a></p>";
  const Kf8Parts k = makeKf8Parts(f0);
  const Pdb p = kf8Book(k);
  const Converted c = convert(p.bytes);
  ASSERT_TRUE(c.ok);
  EXPECT_EQ(c.raw, k.raw);

  const uint32_t imgStart = p.offsets.back();
  const std::string range = "x-raw:" + num(imgStart) + "-" + num(imgStart + kJpeg.size()) + ":image/jpeg";
  const std::string pos = num(k.linkPos);
  EXPECT_EQ(bodyOf(c.html), "<h1>One</h1><p id=\"a\">Alpha <a href=\"#p" + pos + "\">go</a></p><a id=\"p" + pos +
                                "\"></a><p>Beta <img src=\"" + range + "\" alt=\"pic\"/></p><img src=\"" + range +
                                "\"/><p>Gamma</p>");
  EXPECT_EQ(c.html.find("<svg"), std::string::npos);
  EXPECT_EQ(c.html.find("<title>x"), std::string::npos);  // skeleton <head> dropped
}

TEST(Mobi, Kf8NcxHeadings) {
  const std::string f0 = "<p>One</p>";
  const Kf8Parts k = makeKf8Parts(f0);
  const auto ncx = makeNcxRecords({{0, 0, "Chapter"}, {2, 1, "Last"}}, true);  // fragments 0 and 2
  const Converted c = convert(kf8Book(k, ncx).bytes);
  ASSERT_TRUE(c.ok);
  const std::string body = bodyOf(c.html);
  EXPECT_EQ(body.compare(0, 31, "<h1>Chapter</h1><p>One</p><p>Be"), 0) << body;
  EXPECT_NE(body.find("<h2>Last</h2><p>Gamma</p>"), std::string::npos) << body;
}

TEST(Mobi, Kf8ThroughHtmlParser) {
  const std::string f0 = "<h1>One</h1><p>Alpha</p>";
  const Kf8Parts k = makeKf8Parts(f0);
  const Pdb p = kf8Book(k);
  const Converted c = convert(p.bytes);
  ASSERT_TRUE(c.ok);
  MemoryReader hr(c.html);
  HtmlParser parser;
  Fb2ScanResult scan;
  ASSERT_TRUE(parser.scan(hr, scan));
  ASSERT_EQ(scan.sections.size(), 1u);
  EXPECT_EQ(scan.sectionTitle(scan.sections[0]), "One");
  ASSERT_EQ(scan.binaries.size(), 2u);
  MemoryReader original(p.bytes);
  for (const auto& bin : scan.binaries) {
    std::string bytes;
    ASSERT_TRUE(parser.decodeBinary(original, bin, [&](const uint8_t* d, size_t n) { bytes.append((const char*)d, n); }));
    EXPECT_EQ(bytes, kJpeg);
  }
}

TEST(Mobi, CombinedMobi7Kf8UsesKf8Half) {
  // Real combined files (Calibre/KindleGen) keep the shared resources in the MOBI7 half, before the
  // BOUNDARY record, and the KF8 header addresses them absolutely.
  Rec0 old;
  old.title = "Old half";
  const std::string oldRaw = "<html><body><p>OLD CONTENT</p></body></html>";
  old.textLength = static_cast<uint32_t>(oldRaw.size());
  old.textRecords = 1;
  old.firstImage = 2;
  old.exth = {{201, std::string("\x00\x00\x00\x00", 4)}};
  // KF8 half: flat text (no skeleton index).
  const std::string newRaw =
      "<html><body><p>Modern content<img src=\"kindle:embed:0001?mime=image/jpeg\"/></p></body></html>";
  Rec0 modern;
  modern.version = 8;
  modern.minVersion = 8;
  modern.codepage = 65001;
  modern.title = "Modern half";
  modern.textLength = static_cast<uint32_t>(newRaw.size());
  modern.textRecords = 1;
  modern.firstImage = 2;  // absolute: below the KF8 header record, so it cannot be relative
  modern.exth = {{201, std::string("\x00\x00\x00\x00", 4)}};
  const std::vector<std::string> recs = {makeRecord0(old),   palmCompress(oldRaw), kJpeg, std::string("BOUNDARY"),
                                         makeRecord0(modern), palmCompress(newRaw)};
  const Converted c = convert(makePdb("combo", recs).bytes);
  ASSERT_TRUE(c.ok);
  EXPECT_NE(c.html.find("<p>Modern content<img src=\"x-raw:"), std::string::npos);
  EXPECT_NE(c.html.find("<meta name=\"cover\" content=\"x-raw:"), std::string::npos);
  EXPECT_NE(c.html.find("<title>Modern half</title>"), std::string::npos);
  EXPECT_EQ(c.html.find("OLD CONTENT"), std::string::npos);
}

// ---- robustness ------------------------------------------------------------

TEST(Mobi, AbortsWhenSinkFailsOrCancelled) {
  const Pdb p = legacyBook("<html><body><p>" + std::string(20000, 'x') + "</p></body></html>");
  {
    MemoryReader r(p.bytes);
    mobi::Book b;
    ASSERT_TRUE(b.open(r));
    EXPECT_FALSE(b.extractText([](const char*, size_t) { return false; }));
  }
  const Converted c = convert(p.bytes);
  ASSERT_TRUE(c.ok);
  {
    MemoryReader r(p.bytes), raw(c.raw);
    mobi::Book b;
    ASSERT_TRUE(b.open(r));
    EXPECT_FALSE(b.convertToHtml(raw, [](const char*, size_t) { return false; }));
    EXPECT_EQ(b.error(), mobi::Error::Io);
  }
  {
    MemoryReader r(p.bytes), raw(c.raw);
    mobi::Book b;
    ASSERT_TRUE(b.open(r));
    EXPECT_FALSE(b.convertToHtml(raw, [](const char*, size_t) { return true; }, [] { return true; }));
    EXPECT_EQ(b.error(), mobi::Error::Cancelled);
  }
}

TEST(Mobi, CorruptedFilesNeverCrash) {
  const Kf8Parts k = makeKf8Parts("<h1>One</h1><p>Alpha <a href=\"kindle:pos:fid:0001:off:0000000000\">go</a></p>");
  const std::string kf8 = kf8Book(k, makeNcxRecords({{0, 0, "Chapter"}}, true)).bytes;
  const std::string legacy = legacyBook("<html><body><p>Hello <a filepos=0000000012>x</a><img recindex=1/></p></body></html>",
                                        {kJpeg}).bytes;
  uint32_t seed = 12345;
  auto next = [&] {
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
  };
  for (const std::string* base : {&kf8, &legacy}) {
    for (int iter = 0; iter < 400; ++iter) {
      std::string f = *base;
      const int flips = 1 + static_cast<int>(next() % 6);
      for (int i = 0; i < flips; ++i) f[next() % f.size()] = static_cast<char>(next());
      if (iter % 7 == 0) f.resize(next() % f.size());
      convert(f);  // outcome irrelevant; must not crash (ASan/UBSan run)
    }
  }
  // Random raw markup against a valid book.
  for (int iter = 0; iter < 200; ++iter) {
    std::string raw;
    static const char* const frag[] = {"<", ">", "<p>", "</p>", "<a filepos=5>", "<img recindex=1>", "<!--", "-->",
                                       "<svg>", "</svg>", "<head>", "</head>", "<script>", "</script>", "\xE9", "text",
                                       "<a href=\"kindle:pos:fid:0000:off:0\">", "\"", "'", "="};
    for (int i = 0; i < 300; ++i) raw += frag[next() % (sizeof(frag) / sizeof(frag[0]))];
    MemoryReader r(legacy), rr(raw);
    mobi::Book b;
    ASSERT_TRUE(b.open(r));
    b.convertToHtml(rr, [](const char*, size_t) { return true; });
    MemoryReader r2(kf8), rr2(raw);
    mobi::Book b2;
    ASSERT_TRUE(b2.open(r2));
    b2.convertToHtml(rr2, [](const char*, size_t) { return true; });
  }
}

// ---- shared code pages -----------------------------------------------------

TEST(Codepage, NamesAndKoi8r) {
  EXPECT_EQ(fictionbook::codepageFromName("windows-1251"), 1251);
  EXPECT_EQ(fictionbook::codepageFromName("Windows_1252"), 1252);
  EXPECT_EQ(fictionbook::codepageFromName("CP866"), 866);
  EXPECT_EQ(fictionbook::codepageFromName("IBM866"), 866);
  EXPECT_EQ(fictionbook::codepageFromName("koi8-r"), 20866);
  EXPECT_EQ(fictionbook::codepageFromName("ISO-8859-1"), 1252);
  EXPECT_EQ(fictionbook::codepageFromName("utf-8"), 0);
  EXPECT_EQ(fictionbook::codepageFromName("shift_jis"), 0);
  EXPECT_EQ(fictionbook::codepageFromName(""), 0);
  // KOI8-R: 0xC1 = 'а' (U+0430), 0xE1 = 'А' (U+0410), 0xA3 = 'ё' (U+0451).
  EXPECT_EQ(fictionbook::codepageToUnicode(20866, 0xC1), 0x0430u);
  EXPECT_EQ(fictionbook::codepageToUnicode(20866, 0xE1), 0x0410u);
  EXPECT_EQ(fictionbook::codepageToUnicode(20866, 0xA3), 0x0451u);
}
