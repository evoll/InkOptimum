#include <gtest/gtest.h>

#include "BookmarkName/BookmarkName.h"

using bookmark_name::normalize;

TEST(BookmarkName, TrimsAndCollapsesWhitespace) {
  EXPECT_EQ(normalize("  Hello   world \t\n", 40), "Hello world");
}

TEST(BookmarkName, EmptyWhenOnlyWhitespaceOrControls) {
  EXPECT_EQ(normalize("   \t\n", 40), "");
  EXPECT_EQ(normalize("", 40), "");
  EXPECT_EQ(normalize("abc", 1), "");
}

TEST(BookmarkName, StripsControlCharacters) {
  EXPECT_EQ(normalize(std::string("a\x01" "b\x7f" "c"), 40), "a b c");
}

TEST(BookmarkName, TruncatesToCapacityMinusNul) {
  EXPECT_EQ(normalize("abcdefghij", 6), "abcde");
}

TEST(BookmarkName, TruncationDoesNotSplitUtf8) {
  // "Привет" = 6 Cyrillic letters, 2 bytes each (12 bytes). Capacity 8 -> 7 bytes -> 3 whole letters.
  EXPECT_EQ(normalize("Привет", 8), "При");
  EXPECT_EQ(normalize("Привет", 7), "При");
  EXPECT_EQ(normalize("Привет", 6), "Пр");
}

TEST(BookmarkName, TruncationDropsTrailingSpace) {
  EXPECT_EQ(normalize("abc def", 5), "abc");
}

TEST(BookmarkName, KeepsUtf8Intact) {
  EXPECT_EQ(normalize("  Любимая сцена ", 64), "Любимая сцена");
}
