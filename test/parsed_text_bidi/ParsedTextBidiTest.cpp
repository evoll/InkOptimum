// ParsedText with the real bidi line pass (the plain ParsedTextTest stubs bidi out and runs left to right only).
#include <gtest/gtest.h>

#include <GfxRenderer.h>

#include "Epub/ParsedText.h"

namespace {
struct Out {
  std::vector<std::string> words, display;
  std::vector<int> x;
};
std::vector<Out> layout(ParsedText& text, const GfxRenderer& r, const uint16_t width) {
  std::vector<Out> lines;
  text.layoutAndExtractLines(r, 0, width, [&](std::shared_ptr<TextBlock> b) {
    Out o;
    o.words = b->getWords();
    o.display = b->getWordDisplay();
    for (const auto x : b->getWordXPositions()) o.x.push_back(x);
    lines.push_back(std::move(o));
  });
  return lines;
}
const char* const ALEF = "\xD7\x90";  // א
const char* const BET = "\xD7\x91";   // ב
}  // namespace

TEST(ParsedTextBidi, RtlLineDrawsWordsRightToLeftWithDisplayForms) {
  GfxRenderer r;
  ParsedText text(true, false, false);
  text.getBlockStyle().alignment = CssTextAlign::Left;
  text.getBlockStyle().isRtl = true;
  text.addWord(std::string(ALEF) + BET, EpdFontFamily::REGULAR);  // first in reading order
  text.addWord(std::string(BET) + ALEF, EpdFontFamily::REGULAR);
  const auto lines = layout(text, r, 200);
  ASSERT_EQ(lines.size(), 1u);
  ASSERT_EQ(lines[0].words.size(), 2u);
  ASSERT_EQ(lines[0].display.size(), 2u);
  // visual left to right: the second word of the text comes first, each with its letters reversed
  EXPECT_EQ(lines[0].words[0], std::string(BET) + ALEF);
  EXPECT_EQ(lines[0].display[0], std::string(ALEF) + BET);
  EXPECT_EQ(lines[0].words[1], std::string(ALEF) + BET);
  EXPECT_EQ(lines[0].display[1], std::string(BET) + ALEF);
  EXPECT_LT(lines[0].x[0], lines[0].x[1]);
}

TEST(ParsedTextBidi, LatinWordInsideHebrewKeepsItsLettersAndBracketsMirror) {
  GfxRenderer r;
  ParsedText text(true, false, false);
  text.getBlockStyle().alignment = CssTextAlign::Left;
  text.getBlockStyle().isRtl = true;
  text.addWord(std::string("(") + ALEF + BET + ")", EpdFontFamily::REGULAR);
  text.addWord("abc", EpdFontFamily::REGULAR);
  const auto lines = layout(text, r, 200);
  ASSERT_EQ(lines.size(), 1u);
  // reading order: (אב) then abc, in an RTL paragraph -> visual left to right: abc, then the Hebrew word with its letters
  // reversed and the brackets mirrored back into place: "(בא)"
  ASSERT_EQ(lines[0].display.size(), 2u);
  EXPECT_EQ(lines[0].display[0], "abc");
  EXPECT_EQ(lines[0].display[1], std::string("(") + BET + ALEF + ")");
  EXPECT_EQ(lines[0].words[1], std::string("(") + ALEF + BET + ")");
}

TEST(ParsedTextBidi, PureLatinLineStaysOnTheOldPath) {
  GfxRenderer r;
  ParsedText text(true, false, false);
  text.getBlockStyle().alignment = CssTextAlign::Left;
  text.addWord("aaaa", EpdFontFamily::REGULAR);
  text.addWord("bbbb", EpdFontFamily::REGULAR);
  const auto lines = layout(text, r, 200);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_TRUE(lines[0].display.empty());
}

// ---- Bidi markup: the parser's dir / <bdi> / <bdo> / unicode-bidi arrive as bidiOpen/bidiClose ----

TEST(ParsedTextBidi, BdoRtlReversesLatinWordsOfAnLtrParagraph) {
  GfxRenderer r;
  ParsedText text(true, false, false);
  text.getBlockStyle().alignment = CssTextAlign::Left;
  text.bidiOpen(0x202E);  // RLO
  text.addWord("abc", EpdFontFamily::REGULAR);
  text.addWord("def", EpdFontFamily::REGULAR);
  text.bidiClose(false);
  const auto lines = layout(text, r, 200);
  ASSERT_EQ(lines.size(), 1u);
  ASSERT_EQ(lines[0].display.size(), 2u);
  EXPECT_EQ(lines[0].display[0], "fed");
  EXPECT_EQ(lines[0].display[1], "cba");
  // the logical text is untouched and carries no control
  EXPECT_EQ(lines[0].words[0], "def");
  EXPECT_EQ(lines[0].words[1], "abc");
}

TEST(ParsedTextBidi, DirLtrInsideRtlParagraphKeepsItsRunTogetherAndOrdered) {
  GfxRenderer r;
  ParsedText text(true, false, false);
  text.getBlockStyle().alignment = CssTextAlign::Left;
  text.getBlockStyle().isRtl = true;
  text.addWord(ALEF, EpdFontFamily::REGULAR);
  text.bidiOpen(0x2066);  // LRI, as <span dir="ltr">
  text.addWord("one", EpdFontFamily::REGULAR);
  text.addWord("two", EpdFontFamily::REGULAR);
  text.bidiClose(true);
  text.addWord(BET, EpdFontFamily::REGULAR);
  const auto lines = layout(text, r, 300);
  ASSERT_EQ(lines.size(), 1u);
  ASSERT_EQ(lines[0].words.size(), 4u);
  // visual left to right: ב, "one two" (in reading order), א
  EXPECT_EQ(lines[0].words[0], BET);
  EXPECT_EQ(lines[0].words[1], "one");
  EXPECT_EQ(lines[0].words[2], "two");
  EXPECT_EQ(lines[0].words[3], ALEF);
}

TEST(ParsedTextBidi, BdiIsolatesNeutralsFromTheSurroundingRun) {
  GfxRenderer r;
  ParsedText text(true, false, false);
  text.getBlockStyle().alignment = CssTextAlign::Left;
  text.getBlockStyle().isRtl = true;
  text.addWord("a", EpdFontFamily::REGULAR);
  text.bidiOpen(0x2068);  // FSI, as <bdi>
  text.addWord(std::string(ALEF) + BET, EpdFontFamily::REGULAR);
  text.bidiClose(true);
  text.addWord("b", EpdFontFamily::REGULAR);
  const auto lines = layout(text, r, 200);
  ASSERT_EQ(lines.size(), 1u);
  ASSERT_EQ(lines[0].words.size(), 3u);
  // The isolate is neutral to its neighbours: "a <iso> b" stays one left-to-right run, so the line reads a, אב, b
  // across (without the isolate the Hebrew word would split the Latin ones and the line would run b, אב, a).
  EXPECT_EQ(lines[0].words[0], "a");
  EXPECT_EQ(lines[0].words[2], "b");
  ASSERT_EQ(lines[0].display.size(), 3u);
  EXPECT_EQ(lines[0].display[1], std::string(BET) + ALEF);
}

TEST(ParsedTextBidi, EmptyElementLeavesNoControlBehind) {
  GfxRenderer r;
  ParsedText text(true, false, false);
  text.getBlockStyle().alignment = CssTextAlign::Left;
  text.bidiOpen(0x202E);
  text.bidiClose(false);  // <bdo dir=rtl></bdo>
  text.addWord("abc", EpdFontFamily::REGULAR);
  const auto lines = layout(text, r, 200);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0].words[0], "abc");
}

TEST(ParsedTextBidi, OverrideCarriesAcrossLines) {
  GfxRenderer r;
  ParsedText text(true, false, false);
  text.getBlockStyle().alignment = CssTextAlign::Left;
  text.bidiOpen(0x202E);
  for (int i = 0; i < 12; ++i) text.addWord("abc", EpdFontFamily::REGULAR);
  text.bidiClose(false);
  const auto lines = layout(text, r, 60);
  ASSERT_GE(lines.size(), 2u);
  for (const auto& l : lines) {
    for (const auto& d : l.display) EXPECT_EQ(d, "cba");  // every line is still inside the override
  }
}

TEST(ParsedTextBidi, WithoutTheIsolateTheHebrewWordSplitsTheLatinOnes) {
  GfxRenderer r;
  ParsedText text(true, false, false);
  text.getBlockStyle().alignment = CssTextAlign::Left;
  text.getBlockStyle().isRtl = true;
  text.addWord("a", EpdFontFamily::REGULAR);
  text.addWord(std::string(ALEF) + BET, EpdFontFamily::REGULAR);
  text.addWord("b", EpdFontFamily::REGULAR);
  const auto lines = layout(text, r, 200);
  ASSERT_EQ(lines.size(), 1u);
  ASSERT_EQ(lines[0].words.size(), 3u);
  EXPECT_EQ(lines[0].words[0], "b");
  EXPECT_EQ(lines[0].words[2], "a");
}
