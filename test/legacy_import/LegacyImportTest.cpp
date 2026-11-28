#include <LegacyImportRules.h>
#include <gtest/gtest.h>

using namespace legacy_import;

TEST(LegacyImportRules, RecognisesBookCacheDirectories) {
  EXPECT_TRUE(isBookCacheDirName("epub_1234567"));
  EXPECT_TRUE(isBookCacheDirName("txt_42"));
  EXPECT_TRUE(isBookCacheDirName("xtc_4294967295"));
  EXPECT_FALSE(isBookCacheDirName("epub_"));
  EXPECT_FALSE(isBookCacheDirName("epub_12ab"));
  EXPECT_FALSE(isBookCacheDirName("epub_12/../x"));
  EXPECT_FALSE(isBookCacheDirName("bookmarks"));
  EXPECT_FALSE(isBookCacheDirName("fb2_123"));
  EXPECT_FALSE(isBookCacheDirName(nullptr));
}

TEST(LegacyImportRules, OnlyPlainFilesAreCopiedFromDirectories) {
  EXPECT_TRUE(isPlainFileName("my_book.json"));
  EXPECT_TRUE(isPlainFileName("clip 1.txt"));
  EXPECT_FALSE(isPlainFileName(""));
  EXPECT_FALSE(isPlainFileName(".hidden"));
  EXPECT_FALSE(isPlainFileName("a/b"));
  EXPECT_FALSE(isPlainFileName("a\\b"));
  EXPECT_FALSE(isPlainFileName("book.json.tmp"));
  EXPECT_FALSE(isPlainFileName("book.json.bak"));
  EXPECT_FALSE(isPlainFileName(nullptr));
}

TEST(LegacyImportRules, TablesHoldOnlyFlatNames) {
  for (const auto& f : ROOT_FILES) {
    EXPECT_TRUE(isPlainFileName(f.name)) << f.name;
    if (f.alt) EXPECT_TRUE(isPlainFileName(f.alt)) << f.alt;
  }
  for (const char* d : ROOT_DIRS) EXPECT_TRUE(isPlainFileName(d)) << d;
  for (const char* b : BOOK_FILES) EXPECT_TRUE(isPlainFileName(b)) << b;
}

TEST(LegacyImportRules, SizeBounds) {
  EXPECT_EQ(maxSizeForBookFile("progress.bin"), 16u);
  EXPECT_GT(maxSizeForBookFile("stats.bin"), 16u);
  EXPECT_GE(maxSizeForRootFile(), maxSizeForDirFile());
}
