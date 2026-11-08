#include <gtest/gtest.h>

#include "Library.h"

using namespace library;

TEST(Library, ParseFileName) {
  std::string t, a;
  parseFileName("/books/Tolstoy - War and Peace.epub", t, a);
  EXPECT_EQ(a, "Tolstoy");
  EXPECT_EQ(t, "War and Peace");
  parseFileName("Dune (Frank Herbert).fb2", t, a);
  EXPECT_EQ(a, "Frank Herbert");
  EXPECT_EQ(t, "Dune");
  parseFileName("plain.txt", t, a);
  EXPECT_EQ(a, "");
  EXPECT_EQ(t, "plain");
  parseFileName("/x/ - broken.epub", t, a);
  EXPECT_EQ(t, "- broken");
}

TEST(Library, BookFileName) {
  EXPECT_TRUE(isBookFileName("a.EPUB"));
  EXPECT_TRUE(isBookFileName("a.fb2"));
  EXPECT_FALSE(isBookFileName("cover.png"));
  EXPECT_FALSE(isBookFileName("firmware.bin"));
  EXPECT_EQ(formatLabel("a.epub"), "EPUB");
}

TEST(Library, NewAndAuthors) {
  std::vector<Entry> all = {{"/a.epub", "B", "Zed", 10}, {"/b.epub", "A", "alpha", 20}, {"/c.epub", "C", "Alpha", 30},
                            {"/d.epub", "D", "", 40}};
  auto n = newBooks(all, {"/a.epub"});
  ASSERT_EQ(n.size(), 3u);
  auto g = groupByAuthor(all);
  ASSERT_EQ(g.size(), 3u);
  EXPECT_EQ(g[0].count, 2);  // alpha / Alpha merged
  EXPECT_EQ(g[1].author, "Zed");
  EXPECT_EQ(g[2].author, "");
  auto b = booksOfAuthor(all, "ALPHA");
  ASSERT_EQ(b.size(), 2u);
  EXPECT_EQ(b[0].title, "A");
}

TEST(Library, TabsCycle) {
  EXPECT_EQ(nextTab(Tab::Authors, 1), Tab::Books);
  EXPECT_EQ(nextTab(Tab::Books, -1), Tab::Authors);
  EXPECT_EQ(nextView(View::Covers), View::Names);
}

TEST(Library, IndexRoundTrip) {
  std::vector<Entry> all = {{"/a b.epub", "T\ti", "A", 1234}, {"/c.fb2", "X", "", 0}};
  const std::string s = serializeIndex(all, 777);
  std::vector<Entry> back;
  uint32_t sig = 0;
  ASSERT_TRUE(parseIndex(s, back, sig));
  EXPECT_EQ(sig, 777u);
  ASSERT_EQ(back.size(), 2u);
  EXPECT_EQ(back[0].title, "T i");
  EXPECT_EQ(back[0].size, 1234u);
  EXPECT_EQ(back[1].author, "");
  EXPECT_FALSE(parseIndex("garbage", back, sig));
}

TEST(Library, HumanSize) {
  EXPECT_EQ(humanSize(2048), "2 KB");
  EXPECT_EQ(humanSize(1572864), "1.5 MB");
}

TEST(Library, IndexLineStreaming) {
  Entry e;
  ASSERT_TRUE(parseIndexLine(indexLine({"/x.epub", "T", "A", 5}).substr(0, indexLine({"/x.epub", "T", "A", 5}).size() - 1), e));
  EXPECT_EQ(e.path, "/x.epub");
  uint32_t sig = 0;
  EXPECT_TRUE(parseIndexHeader("INKLIB1\t42", sig));
  EXPECT_EQ(sig, 42u);
  EXPECT_FALSE(parseIndexHeader("OTHER\t1", sig));
  EXPECT_FALSE(parseIndexLine("bad line", e));
}
