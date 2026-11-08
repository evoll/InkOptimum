#include <GlyphKey.h>
#include <CjkPunct.h>
#include <gtest/gtest.h>

#include <cstdio>

#include <GfxRenderer.h>

#include "Epub/ParsedText.h"
#include "Epub/WordSpacing.h"

namespace {

std::vector<std::string> layoutLines(ParsedText& text, const GfxRenderer& renderer, const uint16_t width) {
  std::vector<std::string> lines;
  text.layoutAndExtractLines(renderer, 0, width, [&](std::shared_ptr<TextBlock> block) {
    std::string line;
    for (const auto& word : block->getWords()) {
      if (!line.empty()) line += ' ';
      line += word;
    }
    lines.push_back(line);
  });
  return lines;
}

struct Line {
  std::vector<std::string> words;
  std::vector<int> x;
  std::string text() const {
    std::string joined;
    for (const auto& w : words) joined += w;
    return joined;
  }
};

std::vector<Line> layoutBlocks(ParsedText& text, const GfxRenderer& renderer, const uint16_t width) {
  std::vector<Line> lines;
  text.layoutAndExtractLines(renderer, 0, width, [&](std::shared_ptr<TextBlock> block) {
    Line line;
    line.words = block->getWords();
    for (const auto x : block->getWordXPositions()) line.x.push_back(x);
    lines.push_back(std::move(line));
  });
  return lines;
}

ParsedText leftAligned(const bool hyphenation) {
  ParsedText text(true, false, hyphenation);
  text.getBlockStyle().alignment = CssTextAlign::Left;
  return text;
}

}  // namespace

TEST(ParsedText, WrapsEnglishAtTheViewportWidth) {
  GfxRenderer renderer;
  ParsedText text(true, false, false);
  for (const char* word : {"aaaa", "bbbb", "cccc", "dddd"}) text.addWord(word, EpdFontFamily::REGULAR);
  // 4 letters = 40 px, a space 6 px: two words per 90 px line.
  const auto lines = layoutLines(text, renderer, 90);
  for (const auto& l : lines) std::printf("[%s]\n", l.c_str());
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0], "aaaa bbbb");
  EXPECT_EQ(lines[1], "cccc dddd");
}


TEST(ParsedText, CjkWrapsAtTheViewportWidthWithOrWithoutHyphenation) {
  for (const bool hyphenation : {false, true}) {
    GfxRenderer renderer;
    auto text = leftAligned(hyphenation);
    text.addWord("これは日本語の長い文章です", EpdFontFamily::REGULAR);  // 13 characters, 20 px each
    const auto lines = layoutBlocks(text, renderer, 100);
    ASSERT_EQ(lines.size(), 3u) << "hyphenation=" << hyphenation;
    EXPECT_EQ(lines[0].text(), "これは日本");
    EXPECT_EQ(lines[1].text(), "語の長い文");
    EXPECT_EQ(lines[2].text(), "章です");
  }
}

TEST(ParsedText, CjkClosingMarkBeforeOpeningMarkGivesUpHalfAnEm) {
  CjkPunctuation::setBookLanguage("ja");
  GfxRenderer renderer;
  auto text = leftAligned(false);
  text.addWord("日」「本", EpdFontFamily::REGULAR);
  const auto lines = layoutBlocks(text, renderer, 200);
  ASSERT_EQ(lines.size(), 1u);
  ASSERT_EQ(lines[0].x.size(), 4u);
  EXPECT_EQ(lines[0].x[1] - lines[0].x[0], 20);  // a plain character keeps its em
  EXPECT_EQ(lines[0].x[2] - lines[0].x[1], 10);  // the closing bracket loses its blank half
  EXPECT_EQ(lines[0].x[3] - lines[0].x[2], 20);  // the opening bracket keeps its width (drawn shifted)
  CjkPunctuation::setBookLanguage("");
}

TEST(ParsedText, CjkLatinGapIsAQuarterEmForChineseAndJapaneseOnly) {
  for (const char* lang : {"ja", "zh", "ko"}) {
    CjkPunctuation::setBookLanguage(lang);
    GfxRenderer renderer;
    auto text = leftAligned(false);
    text.addWord("日a", EpdFontFamily::REGULAR);
    const auto lines = layoutBlocks(text, renderer, 200);
    ASSERT_EQ(lines.size(), 1u);
    ASSERT_GE(lines[0].x.size(), 2u);
    const int gap = lines[0].x[1] - lines[0].x[0] - 20;
    EXPECT_EQ(gap, std::string(lang) == "ko" ? 0 : 5) << lang;
  }
  CjkPunctuation::setBookLanguage("");
}

TEST(ParsedText, CjkCharactersSitSideBySideWithoutAGap) {
  GfxRenderer renderer;
  auto text = leftAligned(false);
  text.addWord("日本語", EpdFontFamily::REGULAR);
  const auto lines = layoutBlocks(text, renderer, 200);
  ASSERT_EQ(lines.size(), 1u);
  ASSERT_EQ(lines[0].x.size(), 3u);
  EXPECT_EQ(lines[0].x[1] - lines[0].x[0], 20);
  EXPECT_EQ(lines[0].x[2] - lines[0].x[1], 20);
}

TEST(ParsedText, CjkTokensFromSeparateWordsAlsoBreakAndJoinWithoutAGap) {
  GfxRenderer renderer;
  auto text = leftAligned(false);
  text.addWord("これは日本語の", EpdFontFamily::REGULAR);
  text.addWord("漢字", EpdFontFamily::BOLD, false, true);  // attached, as after an inline <b>
  text.addWord("を読みます", EpdFontFamily::REGULAR, false, true);
  const auto lines = layoutBlocks(text, renderer, 100);
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0].text(), "これは日本");
  EXPECT_EQ(lines[1].text(), "語の漢字を");
  EXPECT_EQ(lines[2].text(), "読みます");
  EXPECT_EQ(lines[1].x.back(), 80);  // five characters, flush
}

TEST(ParsedText, KinsokuKeepsAStopOffTheStartOfALine) {
  GfxRenderer renderer;
  auto text = leftAligned(false);
  text.addWord("あいうえ。おかき", EpdFontFamily::REGULAR);
  const auto lines = layoutBlocks(text, renderer, 80);  // four characters per line
  ASSERT_GE(lines.size(), 2u);
  for (const auto& line : lines) {
    EXPECT_NE(line.text().rfind("。", 0), 0u) << "line starts with a full stop: " << line.text();
  }
  EXPECT_EQ(lines[0].text(), "あいう");
  EXPECT_EQ(lines[1].text().rfind("え。", 0), 0u);  // the stop travels with the character before it
}

TEST(ParsedText, KinsokuKeepsAnOpeningBracketOffTheEndOfALine) {
  GfxRenderer renderer;
  auto text = leftAligned(false);
  text.addWord("あいう「えお」", EpdFontFamily::REGULAR);
  const auto lines = layoutBlocks(text, renderer, 80);
  for (const auto& line : lines) {
    const auto t = line.text();
    ASSERT_GE(t.size(), 3u);
    EXPECT_NE(t.substr(t.size() - 3), "「") << "line ends with an opening bracket: " << t;
  }
}

TEST(ParsedText, CjkAfterLatinMayBreakWithoutASpace) {
  GfxRenderer renderer;
  auto text = leftAligned(false);
  text.addWord("Windows版です", EpdFontFamily::REGULAR);  // 7*10 = 70 px, then three CJK characters
  const auto lines = layoutBlocks(text, renderer, 100);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0].text(), "Windows版");  // 70 + 20 = 90
  EXPECT_EQ(lines[1].text(), "です");
}

TEST(ParsedText, JustifiedCjkLineIsStretchedToTheFullWidth) {
  GfxRenderer renderer;
  ParsedText text(true, false, false);
  text.getBlockStyle().alignment = CssTextAlign::Justify;
  text.addWord("これは日本語の長い文章です", EpdFontFamily::REGULAR);
  const auto lines = layoutBlocks(text, renderer, 110);  // five characters (100 px) plus 10 px spare
  ASSERT_GE(lines.size(), 2u);
  ASSERT_EQ(lines[0].x.size(), 5u);
  // 10 px spare over four gaps: 2 px each, and the line ends flush.
  EXPECT_EQ(lines[0].x[4] + 20, 110);
  EXPECT_EQ(lines[0].x[1] - lines[0].x[0], 22);
}

TEST(ParsedText, JustifiedCjkLineEndsFlushWhateverTheRemainder) {
  GfxRenderer renderer;
  for (const uint16_t width : {103, 107, 109, 119}) {
    ParsedText text(true, false, false);
    text.getBlockStyle().alignment = CssTextAlign::Justify;
    text.addWord("これは日本語の長い文章です", EpdFontFamily::REGULAR);
    const auto lines = layoutBlocks(text, renderer, width);
    ASSERT_GE(lines.size(), 2u);
    const size_t n = lines[0].x.size();
    // Five characters fit up to width 119; the line must end exactly at the width, each gap 0..1 px apart.
    EXPECT_EQ(lines[0].x[n - 1] + 20, width) << "width " << width;
  }
}

TEST(ParsedText, IndicRunsBecomeGlyphKeysAndOtherTextIsLeftAlone) {
  GfxRenderer renderer;
  std::vector<std::vector<uint32_t>> seen;
  renderer.shaper = [&](const std::vector<uint32_t>& cps, const uint8_t script, std::string* out) {
    seen.push_back(cps);
    EXPECT_EQ(script, 0);  // Devanagari
    utf8AppendCodepoint(glyphkey::make(77, 0, 0), *out);
    utf8AppendCodepoint(glyphkey::make(78, 3, -2, true), *out);
    return true;
  };
  auto text = leftAligned(false);
  text.addWord("hello", EpdFontFamily::REGULAR);
  text.addWord("(हिन्दी),", EpdFontFamily::REGULAR);
  const auto lines = layoutBlocks(text, renderer, 400);
  ASSERT_EQ(lines.size(), 1u);
  ASSERT_EQ(lines[0].words.size(), 2u);
  EXPECT_EQ(lines[0].words[0], "hello");
  ASSERT_EQ(seen.size(), 1u);  // one run: the word's Indic letters, not its brackets
  EXPECT_EQ(seen[0].size(), 6u);
  std::string expected = "(";
  utf8AppendCodepoint(glyphkey::make(77, 0, 0), expected);
  utf8AppendCodepoint(glyphkey::make(78, 3, -2, true), expected);
  expected += "),";
  EXPECT_EQ(lines[0].words[1], expected);
}

TEST(ParsedText, KoreanKeepsItsSpacesBetweenWords) {
  GfxRenderer renderer;
  auto text = leftAligned(false);
  text.addWord("안녕", EpdFontFamily::REGULAR);
  text.addWord("하세요", EpdFontFamily::REGULAR);
  const auto lines = layoutBlocks(text, renderer, 300);
  ASSERT_EQ(lines.size(), 1u);
  ASSERT_EQ(lines[0].words.size(), 2u);
  EXPECT_EQ(lines[0].x[1] - lines[0].x[0], 2 * 20 + 6);  // two 20 px syllables plus a real space
}

namespace {
// Lays out and returns the TextBlocks themselves, for ruby and serialisation checks.
std::vector<std::shared_ptr<TextBlock>> layoutBlockObjects(ParsedText& text, const GfxRenderer& renderer,
                                                           const uint16_t width) {
  std::vector<std::shared_ptr<TextBlock>> blocks;
  text.layoutAndExtractLines(renderer, 0, width, [&](std::shared_ptr<TextBlock> b) { blocks.push_back(std::move(b)); });
  return blocks;
}
}  // namespace

TEST(ParsedTextRuby, ARubyGroupIsNeverSplitAcrossLines) {
  GfxRenderer renderer;
  auto text = leftAligned(false);
  text.addWord("あいう", EpdFontFamily::REGULAR);  // 3 tokens
  text.addWord("漢字", EpdFontFamily::REGULAR, false, true);  // base of the group: 2 tokens
  text.addWord("えお", EpdFontFamily::REGULAR, false, true);
  text.setRubyGroupAt(3, 2, "かんじ");
  const auto lines = layoutBlocks(text, renderer, 90);  // four characters per line, the group needs 40 px after three
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0].text(), "あいう");  // the group does not fit after three characters and moves whole
  EXPECT_EQ(lines[1].text(), "漢字えお");
}

TEST(ParsedTextRuby, AnAnnotationWiderThanItsBaseWidensTheGroup) {
  GfxRenderer renderer;
  auto text = leftAligned(false);
  text.addWord("漢", EpdFontFamily::REGULAR);          // base 20 px
  text.addWord("字", EpdFontFamily::REGULAR, false, true);
  text.setRubyGroupAt(0, 2, "かんじかんじ");  // 6 characters, half size: 6 * 20 / 2 = 60 px; base is 40 px
  text.addWord("の", EpdFontFamily::REGULAR, false, true);
  const auto blocks = layoutBlockObjects(text, renderer, 300);
  ASSERT_EQ(blocks.size(), 1u);
  const auto& x = blocks[0]->getWordXPositions();
  ASSERT_EQ(x.size(), 3u);
  // 20 px of extra width: 10 on each side of the base.
  EXPECT_EQ(x[0], 0);
  EXPECT_EQ(x[1], 10 + 20);   // left pad + first base character
  EXPECT_EQ(x[2], 10 + 40 + 10);
  EXPECT_TRUE(blocks[0]->hasRuby());
  EXPECT_EQ(blocks[0]->getWordRuby()[0], "かんじかんじ");
  EXPECT_TRUE(blocks[0]->getWordRuby()[1].empty());
}

TEST(ParsedTextRuby, ALineWithoutRubyCarriesNone) {
  GfxRenderer renderer;
  auto text = leftAligned(false);
  text.addWord("日本語", EpdFontFamily::REGULAR);
  const auto blocks = layoutBlockObjects(text, renderer, 300);
  ASSERT_EQ(blocks.size(), 1u);
  EXPECT_FALSE(blocks[0]->hasRuby());
}

TEST(ParsedTextRuby, RubyStaysWithItsBaseWhenTheParagraphIsConsumedInWindows) {
  GfxRenderer renderer;
  auto text = leftAligned(false);
  text.addWord("あいうえお", EpdFontFamily::REGULAR);   // line one at width 100
  text.addWord("漢", EpdFontFamily::REGULAR, false, true);
  text.setRubyGroupAt(5, 1, "かん");
  text.addWord("字", EpdFontFamily::REGULAR, false, true);
  std::vector<std::shared_ptr<TextBlock>> blocks;
  text.layoutAndExtractLines(renderer, 0, 100, [&](std::shared_ptr<TextBlock> b) { blocks.push_back(b); }, false, 1, 1);
  ASSERT_EQ(blocks.size(), 1u);
  EXPECT_FALSE(blocks[0]->hasRuby());
  // The remainder keeps its annotation after the consumed prefix was erased.
  std::vector<std::shared_ptr<TextBlock>> rest;
  text.layoutAndExtractLines(renderer, 0, 100, [&](std::shared_ptr<TextBlock> b) { rest.push_back(b); });
  ASSERT_EQ(rest.size(), 1u);
  ASSERT_TRUE(rest[0]->hasRuby());
  EXPECT_EQ(rest[0]->getWordRuby()[0], "かん");
}

TEST(ParsedTextRuby, RubySurvivesSerialisation) {
  GfxRenderer renderer;
  auto text = leftAligned(false);
  text.addWord("漢字", EpdFontFamily::REGULAR);
  text.setRubyGroupAt(0, 2, "かんじ");
  const auto blocks = layoutBlockObjects(text, renderer, 300);
  ASSERT_EQ(blocks.size(), 1u);
  HalFile file;
  ASSERT_TRUE(blocks[0]->serialize(file));
  file.rewind();
  const auto copy = TextBlock::deserialize(file);
  ASSERT_NE(copy, nullptr);
  ASSERT_TRUE(copy->hasRuby());
  EXPECT_EQ(copy->getWordRuby()[0], "かんじ");
  EXPECT_EQ(copy->getWords(), blocks[0]->getWords());
  EXPECT_EQ(copy->getWordStyles()[1] & EpdFontFamily::RUBY_CONTINUE, EpdFontFamily::RUBY_CONTINUE);
}

TEST(ParsedTextRuby, ALineWithoutRubySerialisesWithoutAnnotations) {
  GfxRenderer renderer;
  auto text = leftAligned(false);
  text.addWord("日本語", EpdFontFamily::REGULAR);
  const auto blocks = layoutBlockObjects(text, renderer, 300);
  HalFile file;
  ASSERT_TRUE(blocks[0]->serialize(file));
  file.rewind();
  const auto copy = TextBlock::deserialize(file);
  ASSERT_NE(copy, nullptr);
  EXPECT_FALSE(copy->hasRuby());
  EXPECT_EQ(file.available(), 0);
}

TEST(ParsedTextRuby, RenderDrawsTheAnnotationAboveTheBaseAndShiftsNarrowBases) {
  GfxRenderer renderer;
  auto text = leftAligned(false);
  text.addWord("漢", EpdFontFamily::REGULAR);          // 20 px base
  text.setRubyGroupAt(0, 1, "かんじ");                  // 3 chars * 20 / 2 = 30 px: 10 px wider
  const auto blocks = layoutBlockObjects(text, renderer, 300);
  ASSERT_EQ(blocks.size(), 1u);
  blocks[0]->render(renderer, 0, 100, 50);
  ASSERT_EQ(renderer.drawn.size(), 2u);
  EXPECT_EQ(renderer.drawn[0].text, "かんじ");
  EXPECT_NE(renderer.drawn[0].style & EpdFontFamily::SUP, 0);
  EXPECT_EQ(renderer.drawn[0].x, 100);     // the group's left edge: the annotation is the wider one
  EXPECT_EQ(renderer.drawn[0].y, 50 - 8);  // half the 16 px ascender above the line
  EXPECT_EQ(renderer.drawn[1].text, "漢");
  EXPECT_EQ(renderer.drawn[1].x, 105);     // shifted by half the difference
  EXPECT_EQ(renderer.drawn[1].style & EpdFontFamily::RUBY_CONTINUE, 0);
}

TEST(ParsedText, WordSpacingWidensAndNarrowsTheGapBetweenWords) {
  GfxRenderer renderer;
  const auto secondWordX = [&](const uint8_t spacing) {
    WordSpacing::current() = spacing;
    ParsedText text = leftAligned(false);
    text.addWord("aaaa", EpdFontFamily::REGULAR);
    text.addWord("bbbb", EpdFontFamily::REGULAR);
    const auto lines = layoutBlocks(text, renderer, 200);
    WordSpacing::current() = 0;
    return lines.empty() || lines[0].x.size() < 2 ? -1 : lines[0].x[1];
  };
  const int base = secondWordX(0);
  ASSERT_GT(base, 0);
  EXPECT_EQ(secondWordX(1), base + 10);  // one step wider
  EXPECT_EQ(secondWordX(2), base + 20);
  EXPECT_LT(secondWordX(5), base);       // level -1 narrows the 6 px gap
  EXPECT_GE(secondWordX(8), base - 5);   // never removes the whole gap
}

TEST(WordSpacing, LevelsRoundTripAndNeverEatTheWholeGap) {
  for (int level = WordSpacing::MIN_LEVEL; level <= WordSpacing::MAX_LEVEL; ++level)
    EXPECT_EQ(WordSpacing::level(WordSpacing::fromLevel(level)), level);
  EXPECT_EQ(WordSpacing::extra(6, 0), 0);
  EXPECT_GE(6 + WordSpacing::extra(6, WordSpacing::fromLevel(-4)), 1);
  EXPECT_EQ(WordSpacing::extra(0, WordSpacing::fromLevel(3)), 0);
}
