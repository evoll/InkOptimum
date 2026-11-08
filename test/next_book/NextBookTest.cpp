#include <gtest/gtest.h>

#include "util/NextBook.h"

TEST(NextBook, PicksFollowingBookInNaturalOrder) {
  EXPECT_EQ(next_book::findNext({"Book 10.epub", "Book 2.epub", "Book 1.epub"}, "Book 2.epub"), "Book 10.epub");
  EXPECT_EQ(next_book::findNext({"b.fb2", "a.epub", "c.mobi"}, "a.epub"), "b.fb2");
}

TEST(NextBook, LastOrMissingGivesEmpty) {
  EXPECT_EQ(next_book::findNext({"a.epub", "b.epub"}, "b.epub"), "");
  EXPECT_EQ(next_book::findNext({"a.epub", "b.epub"}, "zzz.epub"), "");
  EXPECT_EQ(next_book::findNext({"a.epub"}, "a.epub"), "");
  EXPECT_EQ(next_book::findNext({}, "a.epub"), "");
}

TEST(NextBook, PathHelpers) {
  EXPECT_EQ(next_book::folderOf("/Books/a.epub"), "/Books/");
  EXPECT_EQ(next_book::folderOf("/a.epub"), "/");
  EXPECT_EQ(next_book::fileNameOf("/Books/a.epub"), "a.epub");
}
