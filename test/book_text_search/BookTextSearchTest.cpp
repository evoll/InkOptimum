#include <Epub/BookTextSearch.h>
#include <gtest/gtest.h>

using book_search::Matcher;

static std::vector<book_search::Hit> run(const std::string& html, const std::string& q, size_t chunk = 0) {
  Matcher m(q, 100);
  if (chunk == 0) chunk = html.size();
  for (size_t i = 0; i < html.size(); i += chunk) m.feed(html.data() + i, std::min(chunk, html.size() - i));
  m.finish();
  return m.hits();
}

TEST(BookTextSearch, FindsTextAndCountsParagraphs) {
  const auto h = run("<html><body><h1>Title</h1><p>First one.</p><p>The <b>quick</b> fox</p></body></html>", "quick fox");
  ASSERT_EQ(h.size(), 1u);
  EXPECT_EQ(h[0].paragraphIndex, 2);
  EXPECT_EQ(h[0].snippet, "The quick fox");
}

TEST(BookTextSearch, TagInsideWordDoesNotSplit) {
  EXPECT_EQ(run("<p>wo<i>rd</i>s</p>", "words").size(), 1u);
}

TEST(BookTextSearch, BlockTagsSeparate) {
  EXPECT_EQ(run("<p>foo</p><p>bar</p>", "foobar").size(), 0u);
  EXPECT_EQ(run("<p>foo<br/>bar</p>", "foobar").size(), 0u);
}

TEST(BookTextSearch, CaseFoldingAndYo) {
  EXPECT_EQ(run("<p>ЁЛКА и Ёж</p>", "елка").size(), 1u);
  EXPECT_EQ(run("<p>ÉCOLE</p>", "école").size(), 1u);
  EXPECT_EQ(run("<p>Łódź</p>", "ŁÓDŹ").size(), 1u);
}

TEST(BookTextSearch, IgnoresStressAndSoftHyphen) {
  EXPECT_EQ(run("<p>сло\xCC\x81во</p>", "слово").size(), 1u);
  EXPECT_EQ(run("<p>hy&shy;phen</p>", "hyphen").size(), 1u);
}

TEST(BookTextSearch, DecodesEntities) {
  EXPECT_EQ(run("<p>Tom &amp; Jerry</p>", "tom & jerry").size(), 1u);
  EXPECT_EQ(run("<p>a&#8212;b &#x41;BC&nbsp;d</p>", "abc d").size(), 1u);
  EXPECT_EQ(run("<p>AT&T rocks</p>", "at&t").size(), 1u);
}

TEST(BookTextSearch, SkipsScriptStyleHeadAndTags) {
  const auto h = run("<head><title>needle</title><style>p{needle:1}</style></head><body><p class=\"needle\" title='x>needle'>hay</p></body>",
                     "needle");
  EXPECT_EQ(h.size(), 0u);
  EXPECT_EQ(run("<p>a<script>needle</script>b</p>", "needle").size(), 0u);
}

TEST(BookTextSearch, WhitespaceCollapses) {
  EXPECT_EQ(run("<p>one \n\t  two</p>", "one two").size(), 1u);
  EXPECT_EQ(run("<p>one two</p>", "one   two").size(), 1u);
}

TEST(BookTextSearch, ChunkBoundariesAreInvisible) {
  const std::string html = "<p>Привет, мир &amp; <em>друзья</em>!</p><p>ещё привет</p>";
  const auto whole = run(html, "привет");
  for (size_t c = 1; c < 12; ++c) {
    const auto parts = run(html, "привет", c);
    ASSERT_EQ(parts.size(), whole.size()) << "chunk " << c;
    for (size_t i = 0; i < whole.size(); ++i) {
      EXPECT_EQ(parts[i].snippet, whole[i].snippet) << "chunk " << c;
      EXPECT_EQ(parts[i].paragraphIndex, whole[i].paragraphIndex);
    }
  }
  EXPECT_EQ(whole.size(), 2u);
}

TEST(BookTextSearch, MultipleHitsInOneBlockAndCap) {
  EXPECT_EQ(run("<p>ab ab ab</p>", "ab").size(), 3u);
  Matcher m("ab", 2);
  const std::string s = "<p>ab ab ab ab</p>";
  m.feed(s.data(), s.size());
  m.finish();
  EXPECT_EQ(m.hits().size(), 2u);
  EXPECT_TRUE(m.full());
}

TEST(BookTextSearch, SnippetIsUtf8SafeAndEllipsized) {
  std::string html = "<p>";
  for (int i = 0; i < 40; ++i) html += "слово ";
  html += "ИСКОМОЕ";
  for (int i = 0; i < 40; ++i) html += " слово";
  html += "</p>";
  const auto h = run(html, "искомое");
  ASSERT_EQ(h.size(), 1u);
  EXPECT_EQ(h[0].snippet.compare(0, 3, "\xE2\x80\xA6"), 0);
  EXPECT_NE(h[0].snippet.find("ИСКОМОЕ"), std::string::npos);
  EXPECT_LT(h[0].snippet.size(), 260u);
  EXPECT_EQ(h[0].snippet.compare(h[0].snippet.size() - 3, 3, "\xE2\x80\xA6"), 0);
}

TEST(BookTextSearch, TooShortQueryIsInvalid) {
  Matcher m("a", 10);
  EXPECT_FALSE(m.valid());
  Matcher e("   ", 10);
  EXPECT_FALSE(e.valid());
}

TEST(BookTextSearch, ResetStartsNewChapter) {
  Matcher m("zz", 10);
  std::string a = "<p>x</p><p>zz</p>";
  m.feed(a.data(), a.size());
  m.finish();
  ASSERT_EQ(m.hits().size(), 1u);
  EXPECT_EQ(m.hits()[0].paragraphIndex, 2);
  m.reset();
  std::string b = "<p>zz</p>";
  m.feed(b.data(), b.size());
  m.finish();
  ASSERT_EQ(m.hits().size(), 1u);
  EXPECT_EQ(m.hits()[0].paragraphIndex, 1);
}
