#pragma once

#include <EpdFontFamily.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "blocks/BlockStyle.h"
#include "blocks/TextBlock.h"

class GfxRenderer;

class ParsedText {
  // Bidi markup (dir, <bdi>, <bdo>, unicode-bidi): the controls an element opens before a word and closes after it,
  // kept beside the words, never inside their text (offsets, dictionary and widths stay clean).
  struct BidiMark {
    uint32_t word;      // index into `words`
    uint32_t opens[4];  // LRE/RLE/LRO/RLO/LRI/RLI/FSI code points, outermost first
    uint8_t nOpen;
    char closes[4];  // 'I' = PDI, 'F' = PDF, in order
    uint8_t nClose;
  };
  std::vector<BidiMark> bidiMarks;
  std::vector<uint32_t> bidiPendingOpens;  // opened, no word yet
  uint32_t bidiCarry_[8] = {};  // the controls open at the start of the next line (BidiLine::OpenControls)
  uint8_t bidiCarryN_ = 0;
  uint8_t bidiCarryOver_ = 0;
  BidiMark& bidiMarkAt(uint32_t word);
  void bidiShiftAfterErase(size_t consumed);
  void bidiSplitWord(size_t wordIndex);
  std::vector<std::string> words;
  std::vector<EpdFontFamily::Style> wordStyles;
  std::vector<bool> wordContinues;       // true = word attaches to previous (no space before it)
  std::vector<bool> wordIsBionicSuffix;  // true = token is the regular tail of a bionic bold-prefix split
  std::vector<bool> wordIsGuideDot;      // true = token is a guide dot (U+00B7) inserted between words
  std::vector<uint8_t> wordBackgroundBlack;
  // Ruby annotation text per word (the first base word of a group carries it). May be shorter than
  // `words`: a missing entry means no annotation, so addWord() never has to keep it in step.
  std::vector<std::string> rubyTexts;
  // Draw shift per word for CJK marks narrowed by neighbouring punctuation (an opening mark's ink sits on the
  // right of its em); empty when no mark was adjusted. Kept in step with `words` like rubyTexts.
  std::vector<int16_t> wordDrawShift;
  int letterSpacingPx_ = 0;  // extra pixels after every glyph while this block is measured (0 in vertical text)
  bool extraParagraphSpacing;
  bool forceParagraphIndents;
  bool hyphenationEnabled;
  bool bionicReadingEnabled;
  bool guideReadingEnabled;
  bool preserveNaturalWordSpaces;  // FB2: never compress justified spaces below their natural width
  BlockStyle blockStyle;
  bool hasRtlWord;
  // Once a paragraph has been drained in RAM windows, keep using a local/greedy
  // line breaker for the rest of that *same* paragraph. Unlike the legacy DP
  // breaker, greedy breaks are prefix-stable: a line already known to be full
  // can never move just because more words arrive in the next RAM chunk.
  // This makes internal streaming boundaries completely invisible to pagination.
  bool streamingParagraphMode = false;
  std::vector<std::string> reorderedWordsScratch;
  std::vector<uint8_t> bidiGapScratch;  // per piece of a bidi line, in visual order: 1 when glued to the previous one
  std::vector<EpdFontFamily::Style> reorderedStylesScratch;
  std::vector<uint16_t> reorderedWidthsScratch;
  std::vector<bool> reorderedContinuesScratch;
  std::vector<bool> reorderedBionicSuffixScratch;
  std::vector<bool> reorderedGuideDotScratch;
  std::vector<uint8_t> reorderedBackgroundBlackScratch;
  std::vector<uint16_t> visualOrderScratch;

  int resolveFirstLineIndent(bool isFirstLine, const GfxRenderer& renderer, int fontId) const;
  int naturalFirstLineIndent(bool isFirstLine, const GfxRenderer& renderer, int fontId) const;
  // The whole-em grid of the paragraph being laid out (spec 5.5): the em, and the left shift of its narrowed
  // text box. 0 when the grid is off.
  int gridEm_ = 0;
  int gridShift_ = 0;
  std::vector<size_t> computeLineBreaks(const GfxRenderer& renderer, int fontId, int pageWidth,
                                        std::vector<uint16_t>& wordWidths, std::vector<bool>& continuesVec,
                                        bool paragraphContinuation = false);
  std::vector<size_t> computeGreedyLineBreaks(const GfxRenderer& renderer, int fontId, int pageWidth,
                                             std::vector<uint16_t>& wordWidths, std::vector<bool>& continuesVec,
                                             bool paragraphContinuation = false);
  std::vector<size_t> computeHyphenatedLineBreaks(const GfxRenderer& renderer, int fontId, int pageWidth,
                                                  std::vector<uint16_t>& wordWidths, std::vector<bool>& continuesVec,
                                                  bool paragraphContinuation = false);
  bool hyphenateWordAtIndex(size_t wordIndex, int availableWidth, const GfxRenderer& renderer, int fontId,
                            std::vector<uint16_t>& wordWidths, bool allowFallbackBreaks);
  bool splitPathologicalTokenAtIndex(size_t wordIndex, int availableWidth, const GfxRenderer& renderer, int fontId,
                                     std::vector<uint16_t>& wordWidths);
  void extractLine(size_t breakIndex, int pageWidth, const std::vector<uint16_t>& wordWidths,
                   const std::vector<bool>& continuesVec, const std::vector<size_t>& lineBreakIndices,
                   const std::function<void(std::shared_ptr<TextBlock>)>& processLine, const GfxRenderer& renderer,
                   int fontId, bool paragraphContinuation = false);
  // Replaces the Indic runs of words with the glyph keys the font's OpenType shaper makes of them (when it has
  // one); everything else is left as it is.
  void shapeIndicWords(const GfxRenderer& renderer, int fontId);
  std::vector<uint16_t> calculateWordWidths(const GfxRenderer& renderer, int fontId);
  void applyCjkPunctuation(const GfxRenderer& renderer, int fontId, std::vector<uint16_t>& wordWidths);
  void widenRubyGroups(const GfxRenderer& renderer, int fontId, std::vector<uint16_t>& wordWidths) const;
  // Han/kana text has no spaces: split it into tokens a line may break between (kinsoku applied).
  void addSpacelessCjkWord(const std::string& word, EpdFontFamily::Style style, bool attachToPrevious,
                           bool backgroundBlack);

 public:
  explicit ParsedText(const bool extraParagraphSpacing, const bool forceParagraphIndents = false,
                      const bool hyphenationEnabled = false, const bool bionicReadingEnabled = false,
                      const bool guideReadingEnabled = false, const BlockStyle& blockStyle = BlockStyle(),
                      const bool preserveNaturalWordSpaces = false)
      : extraParagraphSpacing(extraParagraphSpacing),
        forceParagraphIndents(forceParagraphIndents),
        hyphenationEnabled(hyphenationEnabled),
        bionicReadingEnabled(bionicReadingEnabled),
        guideReadingEnabled(guideReadingEnabled),
        preserveNaturalWordSpaces(preserveNaturalWordSpaces),
        blockStyle(blockStyle),
        hasRtlWord(false) {}
  ~ParsedText() = default;

  void addWord(std::string word, EpdFontFamily::Style fontStyle, bool underline = false, bool attachToPrevious = false,
               bool backgroundBlack = false);
  // Annotates the `count` base words starting at `startIndex` with `ruby` (furigana/pinyin). The words
  // after the first are marked RUBY_CONTINUE and attached, so the group is never split across lines.
  void setRubyGroupAt(size_t startIndex, size_t count, const std::string& ruby);
  // Bidi markup. bidiOpen: an element with `cp` (U+202A-202E, U+2066-2068) starts; it attaches to the next word added.
  // bidiClose: the element ends (isolate: PDI, else PDF); it attaches to the last word, or cancels an empty element.
  void setLetterSpacing(const int px) { letterSpacingPx_ = px; }
  void bidiOpen(uint32_t cp);
  void bidiClose(bool isolate);
  void setBlockStyle(const BlockStyle& blockStyle) { this->blockStyle = blockStyle; }
  void setHyphenationEnabled(const bool enabled) { hyphenationEnabled = enabled; }
  BlockStyle& getBlockStyle() { return blockStyle; }
  size_t size() const { return words.size(); }
  bool isEmpty() const { return words.empty(); }
  void enableStreamingParagraphMode() { streamingParagraphMode = true; }
  bool isStreamingParagraphMode() const { return streamingParagraphMode; }
  void layoutAndExtractLines(const GfxRenderer& renderer, int fontId, uint16_t viewportWidth,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                             bool includeLastLine = true, size_t maxLines = SIZE_MAX,
                             size_t trailingLinesToKeep = 1, bool paragraphContinuation = false);
};
