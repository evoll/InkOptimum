#include <gtest/gtest.h>

#include "Shelves.h"

using namespace shelves;

TEST(Shelves, CreateRejectsEmptyDuplicateAndOverflow) {
  ShelfStore s;
  EXPECT_EQ(s.create("  \t "), -1);
  EXPECT_EQ(s.create("Sci-Fi"), 0);
  EXPECT_EQ(s.create("sci-fi"), -1);
  for (size_t i = 1; i < ShelfStore::kMaxShelves; i++) EXPECT_GE(s.create("s" + std::to_string(i)), 0);
  EXPECT_EQ(s.create("one more"), -1);
}

TEST(Shelves, NameIsCleanedAndCutOnACharacterBoundary) {
  EXPECT_EQ(ShelfStore::cleanName("  a\tb\n "), "ab");
  const std::string cyr(60, 'x');
  EXPECT_EQ(ShelfStore::cleanName(cyr).size(), ShelfStore::kMaxNameBytes);
  std::string ru;
  for (int i = 0; i < 30; i++) ru += "\xD0\x96";  // 2-byte letters, 60 bytes
  const std::string cut = ShelfStore::cleanName(ru);
  EXPECT_LE(cut.size(), ShelfStore::kMaxNameBytes);
  EXPECT_EQ(cut.size() % 2, 0u);
}

TEST(Shelves, BooksAddRemoveContains) {
  ShelfStore s;
  s.create("A");
  EXPECT_TRUE(s.addBook(0, "/Books/x.epub"));
  EXPECT_FALSE(s.addBook(0, "/Books/x.epub"));
  EXPECT_TRUE(s.contains(0, "/Books/x.epub"));
  EXPECT_FALSE(s.addBook(3, "/y"));
  EXPECT_FALSE(s.addBook(0, "bad\npath"));
  EXPECT_TRUE(s.removeBook(0, "/Books/x.epub"));
  EXPECT_FALSE(s.removeBook(0, "/Books/x.epub"));
}

TEST(Shelves, ShelfSizeLimit) {
  ShelfStore s;
  s.create("A");
  for (size_t i = 0; i < ShelfStore::kMaxBooksPerShelf; i++) EXPECT_TRUE(s.addBook(0, "/b" + std::to_string(i)));
  EXPECT_FALSE(s.addBook(0, "/overflow"));
}

TEST(Shelves, RenameAndRemoveShelf) {
  ShelfStore s;
  s.create("A");
  s.create("B");
  EXPECT_FALSE(s.rename(1, "a"));
  EXPECT_TRUE(s.rename(1, "C"));
  EXPECT_EQ(s.shelves()[1].name, "C");
  EXPECT_TRUE(s.removeShelf(0));
  EXPECT_EQ(s.shelves().size(), 1u);
  EXPECT_FALSE(s.removeShelf(5));
}

TEST(Shelves, SerializeLoadRoundTripAndTolerance) {
  ShelfStore s;
  s.create("Читать потом");
  s.addBook(0, "/Books/Война и мир.epub");
  s.create("Work");
  s.addBook(1, "/Books/a.epub");
  s.addBook(1, "/Books/b.epub");
  ShelfStore t;
  t.load("B\torphan before any shelf\n" + s.serialize() + "junk\nX\tzzz\n");
  ASSERT_EQ(t.shelves().size(), 2u);
  EXPECT_EQ(t.shelves()[0].name, "Читать потом");
  EXPECT_EQ(t.shelves()[1].books.size(), 2u);
  EXPECT_EQ(t.serialize(), s.serialize());
}

TEST(Shelves, PathRenameAndForgetForFilesAndFolders) {
  ShelfStore s;
  s.create("A");
  s.addBook(0, "/Books/x.epub");
  s.addBook(0, "/Old/y.epub");
  s.renamePath("/Books/x.epub", "/Books/z.epub");
  s.renamePath("/Old/", "/New/");
  EXPECT_TRUE(s.contains(0, "/Books/z.epub"));
  EXPECT_TRUE(s.contains(0, "/New/y.epub"));
  s.forgetPath("/New/");
  EXPECT_FALSE(s.contains(0, "/New/y.epub"));
  s.forgetPath("/Books/z.epub");
  EXPECT_TRUE(s.shelves()[0].books.empty());
}
