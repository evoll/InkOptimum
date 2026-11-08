#include <DictWordForms.h>
#include <gtest/gtest.h>

using dict_word_forms::fallbackForms;
using dict_word_forms::lowerUtf8;
using dict_word_forms::stripElision;

TEST(DictWordForms, LowersLatinExtendedAndCyrillic) {
  EXPECT_EQ(lowerUtf8("Łódź"), "łódź");
  EXPECT_EQ(lowerUtf8("ÉCOLE"), "école");
  EXPECT_EQ(lowerUtf8("Привет"), "привет");
  EXPECT_EQ(lowerUtf8("ЃЂЋ"), "ѓђћ");
  EXPECT_EQ(lowerUtf8("Ґанок"), "ґанок");
  EXPECT_EQ(lowerUtf8("İstanbul"), "istanbul");
  EXPECT_EQ(lowerUtf8("Ț Ș"), "ț ș");
}

TEST(DictWordForms, LeavesOtherCharactersAlone) {
  EXPECT_EQ(lowerUtf8("×÷ß"), "×÷ß");
  EXPECT_EQ(lowerUtf8("東京"), "東京");
  EXPECT_EQ(lowerUtf8("a\xFFz"), "a\xFFz");  // malformed byte round-trips
}

TEST(DictWordForms, StripsFrenchElision) {
  EXPECT_EQ(stripElision("l'histoire"), "histoire");
  EXPECT_EQ(stripElision("qu\xE2\x80\x99" "après"), "après");
  EXPECT_EQ(stripElision("QU'Après"), "Après");
  EXPECT_EQ(stripElision("d\xCA\xBC" "avant-garde"), "avant-garde");
}

TEST(DictWordForms, IgnoresOtherPrefixes) {
  EXPECT_EQ(stripElision("O'Brien"), "");
  EXPECT_EQ(stripElision("rock'n'roll"), "");
  EXPECT_EQ(stripElision("l'"), "");
  EXPECT_EQ(stripElision("histoire"), "");
  EXPECT_EQ(stripElision("é'x"), "");
}

TEST(DictWordForms, FallbackOrderAndDuplicates) {
  const auto forms = fallbackForms("L'Histoire");
  ASSERT_EQ(forms.size(), 3u);
  EXPECT_EQ(forms[0], "l'histoire");
  EXPECT_EQ(forms[1], "Histoire");
  EXPECT_EQ(forms[2], "histoire");
  EXPECT_TRUE(fallbackForms("word").empty());
  EXPECT_EQ(fallbackForms("Łódź").size(), 1u);
}
