#include <TocFallbackPolicy.h>
#include <gtest/gtest.h>

using namespace toc_fallback;

TEST(TocFallbackPolicy, SparseWhenMappedEntriesCoverLessThanHalfTheSpine) {
  EXPECT_TRUE(navIsSparse(true, 15, 125));
  EXPECT_TRUE(navIsSparse(true, 0, 10));  // parsed but empty
  EXPECT_FALSE(navIsSparse(true, 5, 10));
  EXPECT_FALSE(navIsSparse(true, 63, 125));
  EXPECT_FALSE(navIsSparse(false, 0, 10));  // a nav that did not parse is the normal fallback
  EXPECT_FALSE(navIsSparse(true, 0, 0));
}

TEST(TocFallbackPolicy, NcxMustBeStrictlyRicherInMappedEntries) {
  EXPECT_TRUE(shouldAdoptNcx(true, 15, true, 125));
  EXPECT_FALSE(shouldAdoptNcx(true, 15, true, 15));   // tie keeps the nav
  EXPECT_FALSE(shouldAdoptNcx(true, 15, true, 3));    // stale NCX with dead links
  EXPECT_FALSE(shouldAdoptNcx(true, 15, false, 200)); // NCX did not parse
  EXPECT_TRUE(shouldAdoptNcx(false, 0, true, 1));     // no nav: any parsed NCX wins
}

#include <SpineHrefMatch.h>

TEST(SpineHrefMatch, ExactFilenameAndNone) {
  using spine_href::match;
  EXPECT_EQ(match("OEBPS/text/c1.xhtml", "OEBPS/text/c1.xhtml"), SpineHrefMatch::Exact);
  EXPECT_EQ(match("c1.xhtml", "OEBPS/text/c1.xhtml"), SpineHrefMatch::Filename);
  EXPECT_EQ(match("c1.xhtml", "OEBPS/text/c2.xhtml"), SpineHrefMatch::None);
}

TEST(SpineHrefMatch, EmptyFilenameNeverMatchesByName) {
  EXPECT_EQ(spine_href::match("a/", "b/"), SpineHrefMatch::None);
}
