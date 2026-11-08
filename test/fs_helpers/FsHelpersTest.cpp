#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "FsHelpers/FsHelpers.h"

TEST(FsHelpers, MatchesCommonExtensionsCaseInsensitively) {
  EXPECT_TRUE(FsHelpers::hasEpubExtension("BOOK.EPUB"));
  EXPECT_TRUE(FsHelpers::hasXtcExtension("title.XtCh"));
  EXPECT_TRUE(FsHelpers::hasJpgExtension("cover.JPEG"));
  EXPECT_TRUE(FsHelpers::hasPngExtension("cover.PnG"));
  EXPECT_TRUE(FsHelpers::hasMarkdownExtension("notes.MD"));
  EXPECT_FALSE(FsHelpers::hasEpubExtension("book.epub.bak"));
  EXPECT_FALSE(FsHelpers::hasTxtExtension("txt"));
}

TEST(FsHelpers, RecognisesConvertedBookExtensions) {
  for (const char* name : {"a.html", "A.HTM", "a.xhtml", "a.RTF", "a.mobi", "a.AZW", "a.azw3", "a.prc"}) {
    EXPECT_TRUE(FsHelpers::hasConvertedBookExtension(name)) << name;
  }
  for (const char* name : {"a.epub", "a.fb2", "a.docx", "a.txt", "html", "a.htmlx", "a.azw4"}) {
    EXPECT_FALSE(FsHelpers::hasConvertedBookExtension(name)) << name;
  }
}

TEST(FsHelpers, NaturalSortKeepsDirectoriesFirstAndOrdersNumbersNumerically) {
  std::vector<std::string> files = {
      "book10.epub", "Zoo/", "book2.epub", "alpha10.txt", "Folder/", "Alpha2.txt"};

  FsHelpers::sortFileList(files);

  const std::vector<std::string> expected = {
      "Folder/", "Zoo/", "Alpha2.txt", "alpha10.txt", "book2.epub", "book10.epub"};
  EXPECT_EQ(files, expected);
}

TEST(FsHelpers, DecodesUriEscapesWithoutTouchingPlainUtf8Bytes) {
  EXPECT_EQ(FsHelpers::decodeUriEscapes("Books%20and%20Notes"), "Books and Notes");
  EXPECT_EQ(FsHelpers::decodeUriEscapes("Книги/Тест.epub"), "Книги/Тест.epub");
  EXPECT_EQ(FsHelpers::decodeUriEscapes("bad%2Xpath"), "bad%2Xpath");
}

TEST(FsHelpers, NormalisePathPreservesLegacySemanticsWithoutComponentAllocations) {
  EXPECT_EQ(FsHelpers::normalisePath("/OEBPS//Text/chapter.xhtml"), "OEBPS/Text/chapter.xhtml");
  EXPECT_EQ(FsHelpers::normalisePath("OEBPS/Text/../Images/cover.jpg"), "OEBPS/Images/cover.jpg");
  EXPECT_EQ(FsHelpers::normalisePath("../OEBPS/Text/chapter.xhtml"), "OEBPS/Text/chapter.xhtml");
  EXPECT_EQ(FsHelpers::normalisePath("Книги/Раздел/../Глава.xhtml"), "Книги/Глава.xhtml");

  // Historical behavior: only a '..' terminated by '/' is resolved. Keep a
  // trailing '..' literal so optimized path handling cannot silently change
  // compatibility with existing callers/cache keys.
  EXPECT_EQ(FsHelpers::normalisePath("OEBPS/Text/.."), "OEBPS/Text/..");
  EXPECT_EQ(FsHelpers::normalisePath("a/b/../../c"), "c");
}

TEST(FsHelpers, ScratchOverloadsMatchTheReturningOnes) {
  const std::string_view inputs[] = {"OEBPS/Text/a%20b.xhtml", "OEBPS/../x/./y.html", "a//b/c", "%41%zz/%e4", "", "../up/file.html"};
  std::string decoded = "stale content that must be replaced";
  std::string normalised = "also stale";
  for (const auto in : inputs) {
    FsHelpers::decodeUriEscapes(in, decoded);
    EXPECT_EQ(decoded, FsHelpers::decodeUriEscapes(in)) << in;
    FsHelpers::normalisePath(in, normalised);
    EXPECT_EQ(normalised, FsHelpers::normalisePath(in)) << in;
  }
}

TEST(FsHelpers, ScratchOverloadsKeepTheirCapacityBetweenCalls) {
  std::string out;
  out.reserve(256);
  const size_t cap = out.capacity();
  FsHelpers::normalisePath("OEBPS/Text/chapter1.xhtml", out);
  FsHelpers::decodeUriEscapes("OEBPS/Text/chapter2.xhtml", out);
  EXPECT_EQ(out.capacity(), cap);  // no reallocation for a path that fits
}
