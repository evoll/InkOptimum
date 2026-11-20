#include <BookmarkAnchor.h>
#include <gtest/gtest.h>

static Bookmark make(uint16_t spine, float progress, uint16_t para = UINT16_MAX, const char* snip = "") {
  Bookmark b{};
  b.spineIndex = spine;
  b.progress = progress;
  b.paragraphIndex = para;
  std::snprintf(b.snippet, sizeof(b.snippet), "%s", snip);
  return b;
}

TEST(BookmarkAnchor, AnchorsTheMatchingLegacyBookmark) {
  std::vector<Bookmark> v{make(2, 0.25f), make(2, 0.5f), make(3, 0.5f)};
  EXPECT_TRUE(bookmark_anchor::apply(v, 2, 0.5f, 17, "some text"));
  EXPECT_EQ(v[0].paragraphIndex, UINT16_MAX);
  EXPECT_EQ(v[1].paragraphIndex, 17);
  EXPECT_STREQ(v[1].snippet, "some text");
  EXPECT_EQ(v[2].paragraphIndex, UINT16_MAX);
}

TEST(BookmarkAnchor, NeverOverwritesAnAnchorOrSnippet) {
  std::vector<Bookmark> v{make(1, 0.1f, 5, "kept")};
  EXPECT_FALSE(bookmark_anchor::apply(v, 1, 0.1f, 9, "new"));
  EXPECT_EQ(v[0].paragraphIndex, 5);
  std::vector<Bookmark> w{make(1, 0.1f, UINT16_MAX, "old snippet")};
  EXPECT_TRUE(bookmark_anchor::apply(w, 1, 0.1f, 9, "new"));
  EXPECT_STREQ(w[0].snippet, "old snippet");
  EXPECT_EQ(w[0].paragraphIndex, 9);
}

TEST(BookmarkAnchor, NoAnchorAvailableOrNoMatch) {
  std::vector<Bookmark> v{make(1, 0.1f)};
  EXPECT_FALSE(bookmark_anchor::apply(v, 1, 0.1f, UINT16_MAX, "x"));
  EXPECT_FALSE(bookmark_anchor::apply(v, 1, 0.2f, 4, "x"));
  EXPECT_FALSE(bookmark_anchor::apply(v, 2, 0.1f, 4, "x"));
  EXPECT_EQ(v[0].paragraphIndex, UINT16_MAX);
}
