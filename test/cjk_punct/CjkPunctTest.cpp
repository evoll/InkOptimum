#include <CjkPunct.h>
#include <gtest/gtest.h>

using namespace CjkPunctuation;

TEST(CjkPunct, Classes) {
  EXPECT_EQ(classOf(0x300C), Class::Opening);   // 「
  EXPECT_EQ(classOf(0x300D), Class::Closing);   // 」
  EXPECT_EQ(classOf(0x3002), Class::FullStop);  // 。
  EXPECT_EQ(classOf(0x3001), Class::Comma);     // 、
  EXPECT_EQ(classOf(0xFF01), Class::Dividing);  // ！
  EXPECT_EQ(classOf(0x30FB), Class::MiddleDot);
  EXPECT_EQ(classOf('a'), Class::None);
  EXPECT_EQ(classOf(0x4E00), Class::None);
}

TEST(CjkPunct, LanguageProfiles) {
  EXPECT_EQ(profileForLanguage("ja").table, Table::Japanese);
  EXPECT_TRUE(profileForLanguage("ja-JP").cjkLatinGap);
  EXPECT_EQ(profileForLanguage("zh-Hant-TW").table, Table::TraditionalChinese);
  EXPECT_EQ(profileForLanguage("zh_TW").table, Table::TraditionalChinese);
  EXPECT_EQ(profileForLanguage("zh-CN").table, Table::SimplifiedChinese);
  EXPECT_EQ(profileForLanguage("zh-Hans-HK").table, Table::SimplifiedChinese);
  EXPECT_EQ(profileForLanguage("lzh").table, Table::TraditionalChinese);
  EXPECT_FALSE(profileForLanguage("ko").cjkLatinGap);
  EXPECT_FALSE(profileForLanguage("en-US").cjkLatinGap);
  EXPECT_FALSE(profileForLanguage("").cjkLatinGap);
  EXPECT_FALSE(profileForLanguage(nullptr).cjkLatinGap);
}

TEST(CjkPunct, JapaneseTable) {
  // 」followed by 「: half an em of blank stays, a quarter off each (6/8).
  EXPECT_EQ(widthOf(Table::Japanese, Class::Closing, Class::Opening, false).forced, 6);
  // 」」: the first sets solid, half width.
  EXPECT_EQ(widthOf(Table::Japanese, Class::Closing, Class::Closing, false).forced, 4);
  // 。 between text keeps its width.
  EXPECT_EQ(widthOf(Table::Japanese, Class::FullStop, Class::None, false).forced, 8);
  // 「 at a line start is half.
  EXPECT_EQ(widthOf(Table::Japanese, Class::Opening, Class::None, true).forced, 4);
}

TEST(CjkPunct, AdjustMovesOpeningInkLeft) {
  // 「 after 「 sets solid: half of a 20 px em goes, and the glyph moves left so its ink stays put.
  const Adjust a = adjustFor(Table::Japanese, Class::Opening, Class::Opening, 20);
  EXPECT_EQ(a.widthDelta, 10);
  EXPECT_EQ(a.drawShift, -10);
  // A closing mark keeps its ink on the left: width shrinks, glyph stays.
  const Adjust b = adjustFor(Table::Japanese, Class::Closing, Class::Closing, 20);
  EXPECT_EQ(b.widthDelta, 10);
  EXPECT_EQ(b.drawShift, 0);
  // Nothing to do mid-text.
  const Adjust c = adjustFor(Table::Japanese, Class::FullStop, Class::None, 20);
  EXPECT_EQ(c.widthDelta, 0);
  EXPECT_EQ(adjustFor(Table::Japanese, Class::Closing, Class::Closing, 0).widthDelta, 0);
}

TEST(CjkPunct, CjkLatinGapPairs) {
  EXPECT_TRUE(takesCjkLatinGap(0x4E00, 'A'));
  EXPECT_TRUE(takesCjkLatinGap('7', 0x3042));
  EXPECT_FALSE(takesCjkLatinGap(0x4E00, 0x3002));
  EXPECT_FALSE(takesCjkLatinGap('a', 'b'));
  EXPECT_FALSE(takesCjkLatinGap(0xFF21, 0x4E00));  // full-width A is not Latin here
}

TEST(CjkPunct, BookLanguageIsRemembered) {
  setBookLanguage("ja");
  EXPECT_EQ(bookProfile().table, Table::Japanese);
  setBookLanguage("");
  EXPECT_FALSE(bookProfile().cjkLatinGap);
}

TEST(CjkPunct, WholeEmGrid) {
  EXPECT_TRUE(profileForLanguage("zh-CN").grid);
  EXPECT_TRUE(profileForLanguage("ja").grid);
  EXPECT_FALSE(profileForLanguage("ko").grid);
  EXPECT_FALSE(profileForLanguage("en").grid);
  EXPECT_FALSE(profileForLanguage("").grid);
  EXPECT_EQ(wholeEmWidth(440, 20), 440);
  EXPECT_EQ(wholeEmWidth(447, 20), 440);
  EXPECT_EQ(wholeEmWidth(19, 20), 19);  // narrower than one em: left alone
  EXPECT_EQ(wholeEmWidth(447, 0), 447);
  EXPECT_EQ(wholeEmIndent(0, 20), 0);   // no indent stays none
  EXPECT_EQ(wholeEmIndent(-12, 20), -12);
  EXPECT_EQ(wholeEmIndent(7, 20), 20);   // at least one em
  EXPECT_EQ(wholeEmIndent(47, 20), 40);  // rounded down
  EXPECT_EQ(wholeEmIndent(40, 20), 40);
}
