#include <CoverNameScan.h>
#include <NavCoverLandmark.h>
#include <gtest/gtest.h>

#include <string>

namespace {
std::string scanNav(const std::string& doc, bool* stopped = nullptr) {
  NavCoverLandmark s;
  s.write(reinterpret_cast<const uint8_t*>(doc.data()), doc.size());
  if (stopped) *stopped = *s.stopFlag();
  return s.result();
}
}  // namespace

TEST(NavCoverLandmark, FindsTheCoverLink) {
  bool stopped = false;
  const std::string href = scanNav(
      "<html><body><nav epub:type=\"toc\"><ol><li><a href=\"c1.xhtml\">One</a></li></ol></nav>"
      "<nav epub:type=\"landmarks\"><ol>"
      "<li><a epub:type=\"cover\" href=\"text/cover.xhtml\">Cover</a></li>"
      "<li><a epub:type=\"bodymatter\" href=\"c1.xhtml\">Start</a></li></ol></nav></body></html>",
      &stopped);
  EXPECT_EQ(href, "text/cover.xhtml");
  EXPECT_TRUE(stopped);
}

TEST(NavCoverLandmark, AttributeOrderAndExtraTypesDoNotMatter) {
  EXPECT_EQ(scanNav("<nav epub:type='landmarks'><a href='cv.xhtml#top' class=\"x\" epub:type=\"frontmatter cover\">c</a></nav>"),
            "cv.xhtml#top");
  EXPECT_EQ(scanNav("<nav epub:type=\"landmarks\"><a role=\"doc-cover\" href=\"c.xhtml\">c</a></nav>"), "c.xhtml");
}

TEST(NavCoverLandmark, IgnoresACoverTypeOutsideLandmarks) {
  // A toc entry that happens to carry the type is not a landmark.
  EXPECT_EQ(scanNav("<nav epub:type=\"toc\"><a epub:type=\"cover\" href=\"c.xhtml\">c</a></nav>"), "");
  // A nav that ended before the link.
  EXPECT_EQ(scanNav("<nav epub:type=\"landmarks\"></nav><a epub:type=\"cover\" href=\"c.xhtml\">c</a>"), "");
}

TEST(NavCoverLandmark, NoCoverLinkGivesNothing) {
  bool stopped = true;
  EXPECT_EQ(scanNav("<nav epub:type=\"landmarks\"><a epub:type=\"bodymatter\" href=\"c1.xhtml\">s</a></nav>", &stopped), "");
  EXPECT_FALSE(stopped);
}

TEST(NavCoverLandmark, WholeAttributeNamesOnly) {
  using S = NavCoverLandmark;
  EXPECT_EQ(S::attribute("a data-href=\"no\" href=\"yes\"", "href"), "yes");
  EXPECT_EQ(S::attribute("a xlink:href=\"no\"", "href"), "");
  EXPECT_EQ(S::attribute("a epub:type=\"cover\"", "type"), "");
  EXPECT_TRUE(S::hasToken("a cover b", "cover"));
  EXPECT_FALSE(S::hasToken("discover coverage", "cover"));
}

TEST(NavCoverLandmark, ABytewiseStreamGivesTheSameAnswer) {
  const std::string doc = "<nav epub:type=\"landmarks\"><a epub:type=\"cover\" href=\"c.xhtml\">c</a></nav>";
  NavCoverLandmark s;
  for (const char c : doc) s.write(static_cast<uint8_t>(c));
  EXPECT_EQ(s.result(), "c.xhtml");
}

TEST(CoverNameScan, PicksTheCoverPictureByName) {
  CoverNameScan s;
  for (const char* p : {"OEBPS/text/ch1.xhtml", "OEBPS/images/fig1.jpg", "OEBPS/images/Cover.JPG", "OEBPS/images/ornament.png"}) {
    s.feed(p);
  }
  EXPECT_EQ(s.result(), "OEBPS/images/Cover.JPG");
}

TEST(CoverNameScan, ExactNameBeatsAVariantAndShallowBeatsDeep) {
  CoverNameScan s;
  s.feed("OEBPS/images/cover-image.png");
  s.feed("OEBPS/images/sub/dir/cover.jpg");
  s.feed("OEBPS/cover.png");
  EXPECT_EQ(s.result(), "OEBPS/cover.png");
}

TEST(CoverNameScan, NamesThatOnlyContainCoverDoNotCount) {
  EXPECT_EQ(CoverNameScan::nameScore("a/discover.jpg"), 0);
  EXPECT_EQ(CoverNameScan::nameScore("a/covers/page1.jpg"), 0);
  EXPECT_EQ(CoverNameScan::nameScore("a/cover.xhtml"), 0);
  EXPECT_EQ(CoverNameScan::nameScore("a/cover.gif"), 0);  // cover generation decodes JPEG and PNG only
  EXPECT_EQ(CoverNameScan::nameScore("cover.jpeg"), 3);
  CoverNameScan s;
  s.feed("a/discover.jpg");
  EXPECT_EQ(s.result(), "");
}
