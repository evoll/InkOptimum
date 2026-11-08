#include <gtest/gtest.h>

#include "FontUploadRules.h"

using font_upload::Kind;

TEST(FontUploadRules, ClassifiesByExtension) {
  EXPECT_EQ(font_upload::classify("Bookerly_12.cpfont"), Kind::Cpfont);
  EXPECT_EQ(font_upload::classify("Literata-BoldItalic.ttf"), Kind::Vector);
  EXPECT_EQ(font_upload::classify("Noto Serif (Light).OTF"), Kind::Vector);
  EXPECT_EQ(font_upload::classify("Collection.ttc"), Kind::Vector);
  EXPECT_EQ(font_upload::classify("readme.txt"), Kind::None);
  EXPECT_EQ(font_upload::classify(".ttf"), Kind::None);
  EXPECT_EQ(font_upload::classify(""), Kind::None);
  EXPECT_EQ(font_upload::classify(nullptr), Kind::None);
}

TEST(FontUploadRules, RejectsTraversalAndOddCharacters) {
  EXPECT_EQ(font_upload::classify("../evil.ttf"), Kind::None);
  EXPECT_EQ(font_upload::classify("a/b.ttf"), Kind::None);
  EXPECT_EQ(font_upload::classify("a\\b.ttf"), Kind::None);
  EXPECT_EQ(font_upload::classify("a;b.ttf"), Kind::None);
  EXPECT_EQ(font_upload::classify("a\nb.ttf"), Kind::None);
}

TEST(FontUploadRules, MagicBytes) {
  const uint8_t ttf[8] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80};
  const uint8_t otf[8] = {'O', 'T', 'T', 'O', 0, 0, 0, 0};
  const uint8_t cp[8] = {'C', 'P', 'F', 'O', 'N', 'T', 0, 0};
  EXPECT_TRUE(font_upload::magicMatches(Kind::Vector, ttf, 8));
  EXPECT_TRUE(font_upload::magicMatches(Kind::Vector, otf, 8));
  EXPECT_TRUE(font_upload::magicMatches(Kind::Cpfont, cp, 8));
  EXPECT_FALSE(font_upload::magicMatches(Kind::Cpfont, ttf, 8));
  EXPECT_FALSE(font_upload::magicMatches(Kind::Vector, cp, 8));
  EXPECT_FALSE(font_upload::magicMatches(Kind::Vector, ttf, 3));
  EXPECT_FALSE(font_upload::magicMatches(Kind::None, ttf, 8));
}
