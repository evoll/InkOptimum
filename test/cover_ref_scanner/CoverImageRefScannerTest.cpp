#include <CoverImageRefScanner.h>
#include <gtest/gtest.h>

#include <string>

namespace {
std::string scan(const std::string& doc) {
  CoverImageRefScanner scanner;
  scanner.write(reinterpret_cast<const uint8_t*>(doc.data()), doc.size());
  return std::string(scanner.result());
}
}  // namespace

TEST(CoverImageRefScanner, SvgWrapperXlinkHref) {
  EXPECT_EQ(scan(R"(<svg viewBox="0 0 1135 1600"><image xlink:href="cover.jpg"/></svg>)"), "cover.jpg");
}

TEST(CoverImageRefScanner, ImgSrcAndSingleQuotes) {
  EXPECT_EQ(scan("<html><body><img src='images/Cover.PNG' alt=''/></body></html>"), "images/Cover.PNG");
}

TEST(CoverImageRefScanner, XlinkHrefBeatsSrc) {
  EXPECT_EQ(scan(R"(<img src="a.jpg"/><image xlink:href="b.png"/>)"), "b.png");
}

TEST(CoverImageRefScanner, PlainSvg2HrefIsTheLastResort) {
  EXPECT_EQ(scan(R"(<image href="c.jpg"/>)"), "c.jpg");
  EXPECT_EQ(scan(R"(<img src="a.jpg"/><image href="c.png"/>)"), "a.jpg");
}

TEST(CoverImageRefScanner, SkipsGifAndNonImages) {
  EXPECT_EQ(scan(R"(<link href="style.css"/><img src="orn.gif"/><img src="photo.jpeg"/>)"), "photo.jpeg");
}

TEST(CoverImageRefScanner, NothingFound) {
  EXPECT_EQ(scan("<svg><rect/></svg>"), "");
  EXPECT_EQ(scan(""), "");
}

TEST(CoverImageRefScanner, OverlongValueIsPassedOver) {
  const std::string longRef(300, 'a');
  EXPECT_EQ(scan("<img src=\"" + longRef + ".jpg\"/><img src=\"ok.jpg\"/>"), "ok.jpg");
}
