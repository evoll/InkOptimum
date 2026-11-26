#include "BookFont.h"
#include "HeadingFont.h"
#include "ParsedText.h"
#include "LetterSpacing.h"
#include "WordSpacing.h"

#include <BidiUtils.h>

#include <Epub/BidiLine.h>
#include <CjkBreak.h>
#include <CjkPunct.h>
#include <GfxRenderer.h>
#include <IndicScripts.h>
#include <Utf8.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

#include "hyphenation/Hyphenator.h"

constexpr int MAX_COST = std::numeric_limits<int>::max();

namespace {

// Soft hyphen byte pattern used throughout EPUBs (UTF-8 for U+00AD).
constexpr char SOFT_HYPHEN_UTF8[] = "\xC2\xAD";
constexpr size_t SOFT_HYPHEN_BYTES = 2;
constexpr size_t PATHOLOGICAL_TOKEN_MIN_BYTES = 128;
constexpr size_t PATHOLOGICAL_TOKEN_SCAN_BYTES = 256;
constexpr size_t RTL_PARAGRAPH_PROBE_WORDS = 3;
constexpr int RTL_PER_WORD_PROBE_DEPTH = 64;

bool mayContainRtlBytes(const char* str) {
  for (const auto* p = reinterpret_cast<const unsigned char*>(str); *p; ++p) {
    if (*p >= 0xD6 && *p <= 0xDB) return true;
  }
  return false;
}

// Returns the first rendered codepoint of a word (skipping leading soft hyphens).
uint32_t firstCodepoint(const std::string& word) {
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.c_str());
  while (true) {
    const uint32_t cp = utf8NextCodepoint(&ptr);
    if (cp == 0) return 0;
    if (cp != 0x00AD) return cp;  // skip soft hyphens
  }
}

// Returns the last codepoint of a word by scanning backward for the start of the last UTF-8 sequence.
uint32_t lastCodepoint(const std::string& word) {
  if (word.empty()) return 0;
  // UTF-8 continuation bytes start with 10xxxxxx; scan backward to find the leading byte.
  size_t i = word.size() - 1;
  while (i > 0 && (static_cast<uint8_t>(word[i]) & 0xC0) == 0x80) {
    --i;
  }
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.c_str() + i);
  return utf8NextCodepoint(&ptr);
}

// Han, kana and CJK punctuation are written without spaces, so a line may break between any two of
// them and no gap is drawn there. Hangul is excluded: Korean separates its words with spaces.
bool isSpacelessCjk(const uint32_t cp) {
  if (!utf8IsCjkBreakable(cp)) return false;
  return !((cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0xFFA0 && cp <= 0xFFDC));
}

// True when the boundary between two characters is a spaceless CJK boundary.
bool isSpacelessCjkBoundary(const uint32_t left, const uint32_t right) {
  return isSpacelessCjk(left) || isSpacelessCjk(right);
}

// The gap the layout puts between two words that are not marked as continuing: the font's space,
// except at a spaceless CJK boundary where nothing is drawn.
// A codepoint as UTF-8, for measuring one character.
std::string utf8Of(const uint32_t cp) {
  std::string out;
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
  return out;
}

int spaceAdvance(const GfxRenderer& renderer, const int fontId, const uint32_t left, const uint32_t right,
                 const EpdFontFamily::Style style) {
  if (left != 0 && right != 0 && isSpacelessCjkBoundary(left, right)) {
    // Chinese and Japanese set a quarter em between Han/kana and a Latin letter or digit (spec 5.4).
    if (CjkPunctuation::bookProfile().cjkLatinGap && CjkPunctuation::takesCjkLatinGap(left, right)) {
      const uint32_t cjk = CjkPunctuation::isHanOrKana(left) ? left : right;
      return renderer.getTextAdvanceX(fontId, utf8Of(cjk).c_str(), style) * CjkPunctuation::kCjkLatinGap / 8;
    }
    return 0;
  }
  const int natural = renderer.getSpaceAdvance(fontId, left, right, style);
  // Reader > Word Spacing: widen (+10 px steps) or narrow (-20 % of the gap per step) every inter-word space.
  return natural + WordSpacing::extra(natural, WordSpacing::current());
}

// CJK marks whose width depends on their neighbour; each is laid out as a word of its own.
bool isAdjustedMark(const uint32_t cp) {
  using CjkPunctuation::Class;
  const Class c = CjkPunctuation::classOf(cp);
  return c != Class::None && c != Class::IdeographicSpace && c != Class::AmbiguousQuote;
}

bool containsSpacelessCjk(const std::string& word) {
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.c_str());
  while (*ptr) {
    if (isSpacelessCjk(utf8NextCodepoint(&ptr))) return true;
  }
  return false;
}

bool containsSoftHyphen(const std::string& word) { return word.find(SOFT_HYPHEN_UTF8) != std::string::npos; }

// FB2 dialogue punctuation may be emitted as a separate token when XML or
// styling splits the original dash+NBSP+word sequence. Never justify the gap
// directly after a standalone dash, even if the parser lost NBSP continuity.
bool isDialogueDashToken(const std::string& word) {
  return word == "-" || word == "\xE2\x80\x93" || word == "\xE2\x80\x94";
}

bool isFixedDialogueGap(const bool preserveNaturalWordSpaces, const std::string& previousWord) {
  return preserveNaturalWordSpaces && isDialogueDashToken(previousWord);
}


bool isBase64LikeChar(const char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
}

bool isPathologicalUnbrokenToken(const std::string& word) {
  if (word.size() < PATHOLOGICAL_TOKEN_MIN_BYTES) {
    return false;
  }

  const size_t scanBytes = std::min(word.size(), PATHOLOGICAL_TOKEN_SCAN_BYTES);
  size_t base64LikeCount = 0;
  for (size_t i = 0; i < scanBytes; ++i) {
    const char c = word[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      return false;
    }
    if (isBase64LikeChar(c)) {
      ++base64LikeCount;
    }
  }

  return base64LikeCount * 100 >= scanBytes * 95;
}

// Removes every soft hyphen in-place so rendered glyphs match measured widths.
void stripSoftHyphensInPlace(std::string& word) {
  size_t pos = 0;
  while ((pos = word.find(SOFT_HYPHEN_UTF8, pos)) != std::string::npos) {
    word.erase(pos, SOFT_HYPHEN_BYTES);
  }
}

// Returns the advance width for a word while ignoring soft hyphen glyphs and optionally appending a visible hyphen.
// Uses advance width (sum of glyph advances + kerning) rather than bounding box width so that italic glyph overhangs
// don't inflate inter-word spacing.
uint16_t measureWordWidth(const GfxRenderer& renderer, const int fontId, const std::string& word,
                          const EpdFontFamily::Style style, const bool appendHyphen = false) {
  if (word.size() == 1 && word[0] == ' ' && !appendHyphen) {
    return renderer.getSpaceWidth(fontId, style);
  }
  const bool hasSoftHyphen = containsSoftHyphen(word);
  if (!hasSoftHyphen && !appendHyphen) {
    return renderer.getTextAdvanceX(fontId, word.c_str(), style);
  }

  std::string sanitized = word;
  if (hasSoftHyphen) {
    stripSoftHyphensInPlace(sanitized);
  }
  if (appendHyphen) {
    sanitized.push_back('-');
  }
  return renderer.getTextAdvanceX(fontId, sanitized.c_str(), style);
}

bool isRightHangingPunctuation(const uint32_t cp) {
  switch (cp) {
    case '.':
    case ',':
    case ';':
    case ':':
    case '!':
    case '?':
    case '-':
    case 0x2010:  // hyphen
    case 0x2011:  // non-breaking hyphen
    case 0x2013:  // en dash
    case 0x2014:  // em dash
    case 0x2019:  // right single quote
    case 0x201D:  // right double quote
    case 0x00BB:  // »
    case ')':
    case ']':
      return true;
    default:
      return false;
  }
}

int trailingHangingWidth(const GfxRenderer& renderer, const int fontId, const std::string& word,
                         const EpdFontFamily::Style style) {
  if (word.empty()) return 0;
  const uint32_t cp = lastCodepoint(word);
  if (!isRightHangingPunctuation(cp)) return 0;
  size_t start = word.size() - 1;
  while (start > 0 && (static_cast<uint8_t>(word[start]) & 0xC0) == 0x80) --start;
  return renderer.getTextAdvanceX(fontId, word.c_str() + start, style);
}

// Checks if a UTF-8 codepoint should be counted as part of a word for Focus Reading
bool isWordCharacter(uint32_t cp) {
  // ASCII range (Catches 95%+ of characters immediately)
  if (cp < 128) {
    // Bitwise trick: (cp | 0x20) converts uppercase ASCII to lowercase.
    // This checks for A-Z and a-z mathematically, avoiding memory lookups and <cctype>
    return ((cp | 0x20) >= 'a' && (cp | 0x20) <= 'z') || cp == '\'';
  }

  // General Punctuation Block, Currency, Math, Arrows, & Symbols (0x2000 - 0x2BFF)
  if (cp >= 0x2000 && cp <= 0x2BFF) {
    // Explicitly allow smart quotes, reject all other general punctuation (em-dashes, etc.)
    return cp == 0x2018 || cp == 0x2019;
  }

  // Latin-1 Punctuation Block (0x00A1 - 0x00BF)
  if (cp >= 0x00A1 && cp <= 0x00BF) {
    // Allow ordinal indicators and micro sign, reject the rest (¡, ¿, «, », etc.)
    return cp == 0x00AA || cp == 0x00B5 || cp == 0x00BA;
  }

  // Rejects Two-em dash, Three-em dash, Double oblique hyphen, etc.
  if (cp >= 0x2E00 && cp <= 0x2E7F) return false;

  // Rejects Modifier Minus (0x02D7), Small Hyphen (0xFE63), and Fullwidth Hyphen (0xFF0D)
  if (cp == 0x02D7 || cp == 0xFE63 || cp == 0xFF0D) return false;
  // Assume all other Unicode ranges (accented letters, Cyrillic, Greek, etc.) are valid

  return true;
}

}  // namespace

ParsedText::BidiMark& ParsedText::bidiMarkAt(const uint32_t word) {
  for (auto& m : bidiMarks) {
    if (m.word == word) return m;
  }
  bidiMarks.push_back(BidiMark{word, {0, 0, 0, 0}, 0, {0, 0, 0, 0}, 0});
  return bidiMarks.back();
}

void ParsedText::bidiOpen(const uint32_t cp) {
  if (bidiPendingOpens.size() < 4) bidiPendingOpens.push_back(cp);
}

void ParsedText::bidiClose(const bool isolate) {
  if (!bidiPendingOpens.empty()) {  // the element held no word: drop its opener
    bidiPendingOpens.pop_back();
    return;
  }
  if (words.empty()) return;
  BidiMark& m = bidiMarkAt(static_cast<uint32_t>(words.size() - 1));
  if (m.nClose < 4) m.closes[m.nClose++] = isolate ? 'I' : 'F';
}

// The consumed words are gone: their marks go too, the rest move down.
void ParsedText::bidiShiftAfterErase(const size_t consumed) {
  size_t out = 0;
  for (size_t i = 0; i < bidiMarks.size(); ++i) {
    if (bidiMarks[i].word < consumed) continue;
    bidiMarks[out] = bidiMarks[i];
    bidiMarks[out].word -= static_cast<uint32_t>(consumed);
    ++out;
  }
  bidiMarks.resize(out);
}

// Word `wordIndex` was split in two: its closers go to the second part, later marks move up by one.
void ParsedText::bidiSplitWord(const size_t wordIndex) {
  if (bidiMarks.empty()) return;
  BidiMark* own = nullptr;
  for (auto& m : bidiMarks) {
    if (m.word == wordIndex) {
      own = &m;
    } else if (m.word > wordIndex) {
      ++m.word;
    }
  }
  if (own != nullptr && own->nClose > 0) {
    BidiMark tail{static_cast<uint32_t>(wordIndex + 1), {0, 0, 0, 0}, 0, {0, 0, 0, 0}, own->nClose};
    for (uint8_t i = 0; i < own->nClose; ++i) tail.closes[i] = own->closes[i];
    own->nClose = 0;
    bidiMarks.push_back(tail);
  }
}

void ParsedText::addWord(std::string word, const EpdFontFamily::Style fontStyle, const bool underline,
                         const bool attachToPrevious, const bool backgroundBlack) {
  if (word.empty()) return;

  if (!bidiPendingOpens.empty()) {
    BidiMark& m = bidiMarkAt(static_cast<uint32_t>(words.size()));
    for (const uint32_t cp : bidiPendingOpens) {
      if (m.nOpen < 4) m.opens[m.nOpen++] = cp;
    }
    bidiPendingOpens.clear();
  }

  EpdFontFamily::Style baseStyle = fontStyle;
  if (underline) {
    baseStyle = static_cast<EpdFontFamily::Style>(baseStyle | EpdFontFamily::UNDERLINE);
  }
  const bool wordStartsRtl = !hasRtlWord && mayContainRtlBytes(word.c_str()) &&
                             BidiUtils::startsWithRtl(word.c_str(), RTL_PER_WORD_PROBE_DEPTH);

  // GUIDE READING: insert middle dot (U+00B7) between non-continuation words.
  if (guideReadingEnabled && !attachToPrevious && !words.empty()) {
    words.emplace_back("\xc2\xb7");
    wordStyles.push_back(EpdFontFamily::REGULAR);
    wordContinues.push_back(false);
    wordIsBionicSuffix.push_back(false);
    wordIsGuideDot.push_back(true);
    wordBackgroundBlack.push_back(false);
  }

  // Han/kana/CJK punctuation: no spaces in the source, so split into tokens a line may break between.
  if (containsSpacelessCjk(word)) {
    addSpacelessCjkWord(word, baseStyle, attachToPrevious, backgroundBlack);
    return;
  }

  // Already-bold text should stay fully bold; bionic splitting would make its suffix regular later.
  if (!this->bionicReadingEnabled || (baseStyle & EpdFontFamily::BOLD) != 0) {
    words.push_back(std::move(word));
    wordStyles.push_back(baseStyle);
    wordContinues.push_back(attachToPrevious);
    wordIsBionicSuffix.push_back(false);
    wordIsGuideDot.push_back(false);
    wordBackgroundBlack.push_back(backgroundBlack);
    if (wordStartsRtl) {
      hasRtlWord = true;
    }
    return;
  }

  // --- FOCUS READING LOGIC BELOW ---

  // Pre-reserve capacity to prevent mid-word heap reallocations.
  size_t maxPossibleNewTokens = word.length();
  size_t requiredSize = words.size() + maxPossibleNewTokens;

  if (words.capacity() < requiredSize) {
    // Emulate standard geometric growth (doubling) to ensure we don't reallocate on every word.
    size_t newCapacity = words.capacity() * 2;

    // Ensure the doubled capacity is actually enough for this specific word
    if (newCapacity < requiredSize) {
      newCapacity = requiredSize;
    }
    // Set a sensible minimum starting size so the first few words don't trigger tiny reallocations
    if (newCapacity < 16) {
      newCapacity = 16;
    }

    words.reserve(newCapacity);
    wordStyles.reserve(newCapacity);
    wordContinues.reserve(newCapacity);
    wordIsBionicSuffix.reserve(newCapacity);
    wordIsGuideDot.reserve(newCapacity);
    wordBackgroundBlack.reserve(newCapacity);
  }

  // Lambda helper to process and push individual sub-segments of the string
  // Use std::string_view to avoid heap allocations when slicing
  auto processSegment = [&](std::string_view segment, bool isWord, bool attach) {
    if (!isWord) {
      // Punctuation and Numbers stay regular
      words.emplace_back(segment);
      wordStyles.push_back(baseStyle);
      wordContinues.push_back(attach);
      wordIsBionicSuffix.push_back(false);
      wordIsGuideDot.push_back(false);
      wordBackgroundBlack.push_back(backgroundBlack);
    } else {
      size_t charCount = 0;
      const unsigned char* countPtr = reinterpret_cast<const unsigned char*>(segment.data());
      const unsigned char* countEnd = countPtr + segment.length();

      while (countPtr < countEnd) {
        utf8NextCodepoint(&countPtr);
        charCount++;
      }

      // Target 43% for 1-bold at 4 chars and 3-bold at 7 chars with floor truncation
      constexpr size_t FOCUS_READING_PERCENT = 43;
      size_t targetBoldChars = (charCount * FOCUS_READING_PERCENT) / 100;
      targetBoldChars = std::clamp<size_t>(targetBoldChars, 1, 9);

      if (targetBoldChars >= charCount) {
        // Whole segment is bold - no suffix split needed
        words.emplace_back(segment);
        wordStyles.push_back(static_cast<EpdFontFamily::Style>(baseStyle | EpdFontFamily::BOLD));
        wordContinues.push_back(attach);
        wordIsBionicSuffix.push_back(false);
        wordIsGuideDot.push_back(false);
        wordBackgroundBlack.push_back(backgroundBlack);
      } else {
        countPtr = reinterpret_cast<const unsigned char*>(segment.data());
        for (size_t i = 0; i < targetBoldChars; ++i) {
          utf8NextCodepoint(&countPtr);
        }
        size_t splitByteOffset = countPtr - reinterpret_cast<const unsigned char*>(segment.data());

        // Bold prefix
        words.emplace_back(segment.substr(0, splitByteOffset));
        wordStyles.push_back(static_cast<EpdFontFamily::Style>(baseStyle | EpdFontFamily::BOLD));
        wordContinues.push_back(attach);
        wordIsBionicSuffix.push_back(false);
        wordIsGuideDot.push_back(false);
        wordBackgroundBlack.push_back(backgroundBlack);

        // Regular suffix - marked so extractLine can merge it back into one TextBlock entry
        words.emplace_back(segment.substr(splitByteOffset));
        wordStyles.push_back(baseStyle);
        wordContinues.push_back(true);
        wordIsBionicSuffix.push_back(true);
        wordIsGuideDot.push_back(false);
        wordBackgroundBlack.push_back(backgroundBlack);
      }
    }
  };

  // Tokenize the string by alternating states (Word vs. Non-Word)
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(word.c_str());
  const unsigned char* end = ptr + word.length();

  const unsigned char* segmentStart = ptr;
  uint32_t firstCp = utf8NextCodepoint(&ptr);  // Consume the first char to determine initial state
  bool inWordSegment = isWordCharacter(firstCp);

  bool isFirstSegment = true;

  while (ptr < end) {
    const unsigned char* currentCpStart = ptr;
    uint32_t cp = utf8NextCodepoint(&ptr);
    bool isWordChar = isWordCharacter(cp);

    // Whenever the character type flips, slice off the segment we just completed and process it
    if (isWordChar != inWordSegment) {
      size_t segmentLen = currentCpStart - segmentStart;
      std::string_view segment(reinterpret_cast<const char*>(segmentStart), segmentLen);

      // Only the very first segment inherits the original attachToPrevious flag.
      // Every subsequent segment MUST attach=true so it glues seamlessly to the prefix.
      processSegment(segment, inWordSegment, isFirstSegment ? attachToPrevious : true);

      // Setup for the next segment
      segmentStart = currentCpStart;
      inWordSegment = isWordChar;
      isFirstSegment = false;
    }
  }

  // Process the final remaining segment
  size_t segmentLen = end - segmentStart;
  std::string_view segment(reinterpret_cast<const char*>(segmentStart), segmentLen);
  processSegment(segment, inWordSegment, isFirstSegment ? attachToPrevious : true);
  if (wordStartsRtl) {
    hasRtlWord = true;
  }
}

void ParsedText::addSpacelessCjkWord(const std::string& word, const EpdFontFamily::Style style,
                                     const bool attachToPrevious, const bool backgroundBlack) {
  const auto* const begin = reinterpret_cast<const unsigned char*>(word.c_str());
  const auto* p = begin;
  const auto* tokenStart = begin;

  // The first token may break from the previous word only where kinsoku allows it; otherwise it
  // stays attached exactly as the caller asked.
  bool attachFirst = attachToPrevious;
  if (attachToPrevious && !words.empty()) {
    const auto* probe = begin;
    const uint32_t firstCp = utf8NextCodepoint(&probe);
    const uint32_t lastCp = lastCodepoint(words.back());
    if (isSpacelessCjkBoundary(lastCp, firstCp) && cjk_break::breakAllowed(lastCp, firstCp)) attachFirst = false;
  }

  // Whether the token being built is attached (no break, no gap) to the one before it.
  bool tokenAttached = attachFirst;
  auto emit = [&](const unsigned char* from, const unsigned char* to) {
    words.emplace_back(reinterpret_cast<const char*>(from), static_cast<size_t>(to - from));
    wordStyles.push_back(style);
    wordContinues.push_back(tokenAttached);
    wordIsBionicSuffix.push_back(false);
    wordIsGuideDot.push_back(false);
    wordBackgroundBlack.push_back(backgroundBlack);
  };

  uint32_t prev = 0;
  while (*p) {
    const auto* cpStart = p;
    const uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0) break;
    if (prev != 0 && isSpacelessCjkBoundary(prev, cp)) {
      const bool lineMayBreak = cjk_break::breakAllowed(prev, cp);
      // A punctuation mark is a word of its own (its width and position depend on its neighbour), attached to
      // what it follows or precedes when kinsoku forbids a break there.
      if (lineMayBreak || isAdjustedMark(prev) || isAdjustedMark(cp)) {
        emit(tokenStart, cpStart);
        tokenStart = cpStart;
        tokenAttached = !lineMayBreak;
      }
    }
    prev = cp;
  }
  emit(tokenStart, p);
}

void ParsedText::setRubyGroupAt(const size_t startIndex, const size_t count, const std::string& ruby) {
  if (count == 0 || ruby.empty() || startIndex >= words.size()) return;
  const size_t end = std::min(words.size(), startIndex + count);
  if (rubyTexts.size() < end) rubyTexts.resize(end);
  rubyTexts[startIndex] = ruby;
  for (size_t i = startIndex + 1; i < end; ++i) {
    wordStyles[i] = static_cast<EpdFontFamily::Style>(wordStyles[i] | EpdFontFamily::RUBY_CONTINUE);
    wordContinues[i] = true;
  }
}

// A group whose annotation is wider than its base gets the difference as padding, half on each side,
// so the annotation never overlaps the neighbouring text.
void ParsedText::widenRubyGroups(const GfxRenderer& renderer, const int fontId,
                                 std::vector<uint16_t>& wordWidths) const {
  for (size_t i = 0; i < rubyTexts.size() && i < words.size(); ++i) {
    if (rubyTexts[i].empty()) continue;
    size_t end = i + 1;
    while (end < words.size() && (wordStyles[end] & EpdFontFamily::RUBY_CONTINUE) != 0) ++end;
    int baseWidth = 0;
    for (size_t k = i; k < end; ++k) baseWidth += wordWidths[k];
    const int rubyWidth = renderer.getTextAdvanceX(fontId, rubyTexts[i].c_str(), EpdFontFamily::SUP);
    if (rubyWidth > baseWidth) {
      const int extra = rubyWidth - baseWidth;
      const int left = extra / 2;
      wordWidths[i] = static_cast<uint16_t>(std::min(65535, wordWidths[i] + left));
      wordWidths[end - 1] = static_cast<uint16_t>(std::min(65535, wordWidths[end - 1] + extra - left));
    }
    i = end - 1;
  }
}

int ParsedText::resolveFirstLineIndent(const bool isFirstLine, const GfxRenderer& renderer, const int fontId) const {
  const int indent = naturalFirstLineIndent(isFirstLine, renderer, fontId);
  return gridEm_ > 0 ? CjkPunctuation::wholeEmIndent(indent, gridEm_) : indent;
}

int ParsedText::naturalFirstLineIndent(const bool isFirstLine, const GfxRenderer& renderer, const int fontId) const {
  const bool naturalAlign =
      blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::None ||
      (blockStyle.isRtl ? blockStyle.alignment == CssTextAlign::Right : blockStyle.alignment == CssTextAlign::Left);
  if (!isFirstLine || !naturalAlign) {
    return 0;
  }
  if (blockStyle.textIndentDefined) {
    if (blockStyle.textIndent < 0 || !extraParagraphSpacing || forceParagraphIndents) {
      return blockStyle.textIndent;
    }
    return 0;
  }
  if (!extraParagraphSpacing || forceParagraphIndents) {
    return renderer.getSpaceWidth(fontId, EpdFontFamily::REGULAR) * 3;
  }
  return 0;
}

// Consumes data to minimize memory usage
void ParsedText::shapeIndicWords(const GfxRenderer& renderer, const int fontId) {
  constexpr size_t kMaxWordBytes = 512;
  for (size_t w = 0; w < words.size(); ++w) {
    const std::string& word = words[w];
    if (word.empty() || word.size() > kMaxWordBytes) continue;
    bool any = false;
    for (size_t i = 0; i + 1 < word.size(); ++i) {
      if (indic::startsIndic(reinterpret_cast<const unsigned char*>(word.c_str() + i))) {
        any = true;
        break;
      }
    }
    if (!any) continue;

    std::vector<uint32_t> cps;
    const auto* p = reinterpret_cast<const unsigned char*>(word.c_str());
    while (*p) cps.push_back(utf8NextCodepoint(&p));

    std::string shaped;
    bool changed = false;
    for (size_t i = 0; i < cps.size();) {
      const auto* script = indic::scriptOf(cps[i]);
      if (!script) {
        utf8AppendCodepoint(cps[i++], shaped);
        continue;
      }
      // The run: this script's code points, with the joiners that steer conjuncts and the Vedic signs that sit on them.
      size_t j = i + 1;
      while (j < cps.size()) {
        const auto* next = indic::scriptOf(cps[j]);
        const bool joiner = cps[j] == indic::ZWNJ || cps[j] == indic::ZWJ || (cps[j] >= 0x0951 && cps[j] <= 0x0954);
        if (next ? next != script : !joiner) break;
        ++j;
      }
      const size_t before = shaped.size();
      if (renderer.shapeIndicRun(fontId, wordStyles[w], cps.data() + i, static_cast<unsigned>(j - i),
                                 static_cast<uint8_t>(indic::indexOf(*script)), "", &shaped)) {
        changed = true;
      } else {
        shaped.resize(before);
        for (size_t k = i; k < j; ++k) utf8AppendCodepoint(cps[k], shaped);
      }
      i = j;
    }
    if (changed) words[w] = std::move(shaped);
  }
}

void ParsedText::layoutAndExtractLines(const GfxRenderer& renderer, const int bodyFontId, const uint16_t viewportWidth,
                                       const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                                       const bool includeLastLine, const size_t maxLines,
                                       const size_t trailingLinesToKeep, const bool paragraphContinuation) {
  const LetterSpacing::Scope<GfxRenderer> letterSpacing(renderer, letterSpacingPx_);
  if (words.empty()) {
    return;
  }
  const int bookFontId = fontFamily_ != 0 ? BookFont::resolve(renderer, fontFamily_) : 0;
  const int fontId = bookFontId != 0 ? bookFontId : (heading_ ? HeadingFont::resolve(renderer, bodyFontId) : bodyFontId);

  if (!blockStyle.directionDefined && hasRtlWord) {
    const size_t wordsToScan = std::min(words.size(), RTL_PARAGRAPH_PROBE_WORDS);
    for (size_t i = 0; i < wordsToScan; ++i) {
      if (BidiUtils::startsWithRtl(words[i].c_str(), BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH)) {
        blockStyle.isRtl = true;
        break;
      }
    }
  }

  shapeIndicWords(renderer, fontId);

  // Ensure SD card font glyph metrics are loaded before measuring word widths.
  // For flash-based fonts isSdCardFont() returns false and this block is skipped
  // entirely — no heap allocation. For SD card fonts this reads glyph metadata
  // (advanceX only, no bitmaps) for all unique codepoints in this paragraph so
  // that calculateWordWidths() can measure text without on-demand SD I/O.
  if (renderer.isSdCardFont(fontId)) {
    // Style mask: only ask the SD font to load advances for styles actually
    // used in this paragraph. Style index is the low two bits (regular/bold/
    // italic/bold-italic); the underline bit is irrelevant to advance metrics.
    uint8_t styleMask = 0;
    for (auto s : wordStyles) {
      styleMask |= static_cast<uint8_t>(1u << (static_cast<uint8_t>(s) & 0x03));
    }
    if (styleMask == 0) styleMask = 0x01;  // defensive: regular only
    renderer.ensureSdCardFontReady(fontId, words, hyphenationEnabled, styleMask);
  }

  int pageWidth = viewportWidth;
  gridEm_ = 0;
  gridShift_ = 0;
  if (CjkPunctuation::bookProfile().grid) {
    // The whole-em grid (spec 5.5), for a paragraph that has CJK in it: the text width is a whole number of ems.
    const bool hasCjk = std::any_of(words.begin(), words.end(), [](const std::string& w) { return containsSpacelessCjk(w); });
    const int em = hasCjk ? renderer.getTextAdvanceX(fontId, "\xE4\xB8\x80", EpdFontFamily::REGULAR) : 0;  // 一
    if (em >= 8 && em * 4 <= viewportWidth) {
      gridEm_ = em;
      pageWidth = CjkPunctuation::wholeEmWidth(viewportWidth, em);
      gridShift_ = (viewportWidth - pageWidth) / 2;
    }
  }
  auto wordWidths = calculateWordWidths(renderer, fontId);

  std::vector<size_t> lineBreakIndices;
  // Full justification without hyphenation produces very visible white rivers
  // on a narrow 480 px reader.  When the user explicitly chooses "Justify",
  // use the language-aware hyphenator even if the separate hyphenation toggle
  // is off. Left/center/right alignment keep respecting that toggle exactly.
  if (hyphenationEnabled) {
    // The hyphenating breaker is already incremental/greedy, so its committed
    // prefix is stable when more text arrives in a later RAM window.
    lineBreakIndices = computeHyphenatedLineBreaks(renderer, fontId, pageWidth, wordWidths, wordContinues, paragraphContinuation);
  } else if (streamingParagraphMode) {
    // IMPORTANT: the legacy non-hyphenated breaker optimizes the whole
    // paragraph with dynamic programming. That means adding words in the next
    // RAM chunk can retroactively change an earlier line, which made chunk
    // seams visible as odd whitespace, indents and page distribution. Once a
    // paragraph starts streaming, use prefix-stable greedy wrapping for *all*
    // remaining chunks and for its final flush.
    lineBreakIndices = computeGreedyLineBreaks(renderer, fontId, pageWidth, wordWidths, wordContinues, paragraphContinuation);
  } else {
    lineBreakIndices = computeLineBreaks(renderer, fontId, pageWidth, wordWidths, wordContinues, paragraphContinuation);
  }
  size_t availableLineCount = lineBreakIndices.size();
  if (!includeLastLine) {
    // Streaming pagination must not commit the seam too aggressively. The
    // paragraph line breaker looks ahead when balancing line endings, so
    // keeping two trailing lines lets the next RAM window recompute the join
    // with its continuation instead of visibly tearing the paragraph.
    const size_t keep = std::min(trailingLinesToKeep, availableLineCount);
    availableLineCount -= keep;
  }
  const size_t lineCount = std::min(availableLineCount, maxLines);

  for (size_t i = 0; i < lineCount; ++i) {
    extractLine(i, pageWidth, wordWidths, wordContinues, lineBreakIndices, processLine, renderer, fontId, paragraphContinuation);
  }

  // Remove consumed words so size() reflects only remaining words
  if (lineCount > 0) {
    const size_t consumed = lineBreakIndices[lineCount - 1];
    words.erase(words.begin(), words.begin() + consumed);
    wordStyles.erase(wordStyles.begin(), wordStyles.begin() + consumed);
    wordContinues.erase(wordContinues.begin(), wordContinues.begin() + consumed);
    wordIsBionicSuffix.erase(wordIsBionicSuffix.begin(), wordIsBionicSuffix.begin() + consumed);
    wordIsGuideDot.erase(wordIsGuideDot.begin(), wordIsGuideDot.begin() + consumed);
    wordBackgroundBlack.erase(wordBackgroundBlack.begin(), wordBackgroundBlack.begin() + consumed);
    bidiShiftAfterErase(consumed);
    if (!rubyTexts.empty()) {
      rubyTexts.erase(rubyTexts.begin(), rubyTexts.begin() + std::min(consumed, rubyTexts.size()));
    }
    if (!wordDrawShift.empty()) {
      wordDrawShift.erase(wordDrawShift.begin(), wordDrawShift.begin() + std::min(consumed, wordDrawShift.size()));
    }
  }
}

std::vector<uint16_t> ParsedText::calculateWordWidths(const GfxRenderer& renderer, const int fontId) {
  std::vector<uint16_t> wordWidths;
  wordWidths.reserve(words.size());

  for (size_t i = 0; i < words.size(); ++i) {
    wordWidths.push_back(measureWordWidth(renderer, fontId, words[i], wordStyles[i]));
  }
  if (!rubyTexts.empty()) widenRubyGroups(renderer, fontId, wordWidths);
  applyCjkPunctuation(renderer, fontId, wordWidths);

  return wordWidths;
}

// Full-width CJK marks next to other marks give up part of their blank half-em (JLREQ / CLREQ, spec 5.2). The
// narrowed mark is drawn shifted so its ink stays where the glyph put it; wordDrawShift carries that shift.
void ParsedText::applyCjkPunctuation(const GfxRenderer& renderer, const int fontId, std::vector<uint16_t>& wordWidths) {
  using namespace CjkPunctuation;
  wordDrawShift.clear();
  const Table table = bookProfile().table;
  int emWidth = -1;  // measured lazily, only for curly quotes
  for (size_t i = 0; i < words.size(); ++i) {
    const std::string& w = words[i];
    if (w.empty() || static_cast<unsigned char>(w[0]) < 0x80) continue;
    const auto* ptr = reinterpret_cast<const unsigned char*>(w.c_str());
    const uint32_t cp = utf8NextCodepoint(&ptr);
    if (static_cast<size_t>(ptr - reinterpret_cast<const unsigned char*>(w.c_str())) != w.size()) continue;  // one character only
    const Class mark = classOf(cp);
    if (mark == Class::None || mark == Class::IdeographicSpace || mark == Class::AmbiguousQuote) continue;

    // The curly quotes are CJK punctuation only where the font draws them full-width; in a Latin font they are
    // narrow and must be left alone.
    if (isCurlyQuote(cp)) {
      if (emWidth < 0) emWidth = renderer.getTextAdvanceX(fontId, utf8Of(0x4E00).c_str(), wordStyles[i]);
      if (emWidth <= 0 || wordWidths[i] * 8 < emWidth * 7) continue;
    }

    // An opening mark looks at the character before it, every other mark at the one after it.
    Class neighbour = Class::None;
    if (mark == Class::Opening) {
      if (i > 0 && !words[i - 1].empty()) neighbour = classOf(lastCodepoint(words[i - 1]));
    } else if (i + 1 < words.size() && !words[i + 1].empty()) {
      neighbour = classOf(firstCodepoint(words[i + 1]));
    }
    const Adjust adj = adjustFor(table, mark, neighbour, wordWidths[i]);
    if (adj.widthDelta <= 0) continue;
    wordWidths[i] = static_cast<uint16_t>(wordWidths[i] - adj.widthDelta);
    if (wordDrawShift.empty()) wordDrawShift.assign(words.size(), 0);
    wordDrawShift[i] = adj.drawShift;
  }
}

std::vector<size_t> ParsedText::computeLineBreaks(const GfxRenderer& renderer, const int fontId, const int pageWidth,
                                                  std::vector<uint16_t>& wordWidths, std::vector<bool>& continuesVec,
                                                  const bool paragraphContinuation) {
  if (words.empty()) {
    return {};
  }

  auto nextTokenAttaches = [&](const size_t index, const size_t totalWordCount) {
    return index + 1 < totalWordCount && (continuesVec[index + 1] || wordIsGuideDot[index + 1]);
  };

  // A streaming RAM window is not a new paragraph. Only the true first
  // chunk may use the author's first-line indent.
  const int firstLineIndent = resolveFirstLineIndent(!paragraphContinuation, renderer, fontId);

  // Hyphenation is disabled here. Do not silently split oversized words.

  const size_t totalWordCount = words.size();

  // DP table to store the minimum badness (cost) of lines starting at index i
  std::vector<int> dp(totalWordCount);
  // 'ans[i]' stores the index 'j' of the *last word* in the optimal line starting at 'i'
  std::vector<size_t> ans(totalWordCount);

  // Base Case
  dp[totalWordCount - 1] = 0;
  ans[totalWordCount - 1] = totalWordCount - 1;

  for (int i = totalWordCount - 2; i >= 0; --i) {
    int currlen = 0;
    dp[i] = MAX_COST;

    // First line has reduced width due to text-indent
    const int effectivePageWidth = i == 0 ? pageWidth - firstLineIndent : pageWidth;

    for (size_t j = i; j < totalWordCount; ++j) {
      // Add space before word j, unless it's the first word on the line or a continuation
      int gap = 0;
      if (j > static_cast<size_t>(i) && !continuesVec[j]) {
        gap =
            spaceAdvance(renderer, fontId, lastCodepoint(words[j - 1]), firstCodepoint(words[j]), wordStyles[j - 1]);
      } else if (j > static_cast<size_t>(i) && continuesVec[j]) {
        // Cross-boundary kerning for continuation words (e.g. nonbreaking spaces, attached punctuation)
        gap = renderer.getKerning(fontId, lastCodepoint(words[j - 1]), firstCodepoint(words[j]), wordStyles[j - 1]);
      }
      currlen += wordWidths[j] + gap;

      if (currlen > effectivePageWidth) {
        break;
      }

      // Cannot break after word j if the next word attaches to it (continuation group)
      if (nextTokenAttaches(j, totalWordCount)) {
        continue;
      }

      int cost;
      if (j == totalWordCount - 1) {
        cost = 0;  // Last line
      } else {
        const int remainingSpace = effectivePageWidth - currlen;
        // Use long long for the square to prevent overflow
        const long long cost_ll = static_cast<long long>(remainingSpace) * remainingSpace + dp[j + 1];

        if (cost_ll > MAX_COST) {
          cost = MAX_COST;
        } else {
          cost = static_cast<int>(cost_ll);
        }
      }

      if (cost < dp[i]) {
        dp[i] = cost;
        ans[i] = j;  // j is the index of the last word in this optimal line
      }
    }

    // Handle oversized word: if no valid configuration found, force single-word line
    // This prevents cascade failure where one oversized word breaks all preceding words
    if (dp[i] == MAX_COST) {
      ans[i] = i;  // Just this word on its own line
      // Inherit cost from next word to allow subsequent words to find valid configurations
      if (i + 1 < static_cast<int>(totalWordCount)) {
        dp[i] = dp[i + 1];
      } else {
        dp[i] = 0;
      }
    }
  }

  // Stores the index of the word that starts the next line (last_word_index + 1)
  std::vector<size_t> lineBreakIndices;
  size_t currentWordIndex = 0;

  while (currentWordIndex < totalWordCount) {
    size_t nextBreakIndex = ans[currentWordIndex] + 1;

    // Safety check: prevent infinite loop if nextBreakIndex doesn't advance
    if (nextBreakIndex <= currentWordIndex) {
      // Force advance by at least one word to avoid infinite loop
      nextBreakIndex = currentWordIndex + 1;
    }

    lineBreakIndices.push_back(nextBreakIndex);
    currentWordIndex = nextBreakIndex;
  }

  return lineBreakIndices;
}

// Prefix-stable line breaker used only after a paragraph enters streaming mode.
// It never treats a RAM window as a paragraph boundary and it never inserts,
// removes or synthesizes whitespace. A committed line depends only on tokens
// up to the first token that does not fit, so future chunks cannot change it.
std::vector<size_t> ParsedText::computeGreedyLineBreaks(const GfxRenderer& renderer, const int fontId,
                                                        const int pageWidth,
                                                        std::vector<uint16_t>& wordWidths,
                                                        std::vector<bool>& continuesVec,
                                                        const bool paragraphContinuation) {
  if (words.empty()) {
    return {};
  }

  const int firstLineIndent = resolveFirstLineIndent(!paragraphContinuation, renderer, fontId);
  std::vector<size_t> lineBreakIndices;
  lineBreakIndices.reserve((words.size() / 6) + 2);

  auto tokenAttaches = [&](const size_t index) {
    return index < wordWidths.size() && (continuesVec[index] || wordIsGuideDot[index]);
  };

  size_t currentIndex = 0;
  bool isFirstLine = !paragraphContinuation;
  while (currentIndex < wordWidths.size()) {
    const size_t lineStart = currentIndex;
    int lineWidth = 0;
    const int effectivePageWidth = std::max(1, pageWidth - (isFirstLine ? firstLineIndent : 0));

    while (currentIndex < wordWidths.size()) {
      const bool firstToken = currentIndex == lineStart;
      int spacing = 0;
      if (!firstToken && !continuesVec[currentIndex]) {
        spacing = spaceAdvance(renderer, fontId, lastCodepoint(words[currentIndex - 1]),
                                           firstCodepoint(words[currentIndex]), wordStyles[currentIndex - 1]);
      } else if (!firstToken && continuesVec[currentIndex]) {
        spacing = renderer.getKerning(fontId, lastCodepoint(words[currentIndex - 1]),
                                      firstCodepoint(words[currentIndex]), wordStyles[currentIndex - 1]);
      }

      const int candidateWidth = spacing + wordWidths[currentIndex];
      if (firstToken || lineWidth + candidateWidth <= effectivePageWidth) {
        lineWidth += candidateWidth;
        ++currentIndex;
        continue;
      }
      break;
    }

    // Never split a non-breaking/continuation group at the RAM seam. Move the
    // whole attached tail to the next line, exactly as the normal breaker does.
    while (currentIndex > lineStart + 1 && tokenAttaches(currentIndex)) {
      --currentIndex;
    }

    if (currentIndex <= lineStart) {
      currentIndex = lineStart + 1;
    }
    lineBreakIndices.push_back(currentIndex);
    isFirstLine = false;
  }

  return lineBreakIndices;
}

// Builds break indices while opportunistically splitting the word that would overflow the current line.
std::vector<size_t> ParsedText::computeHyphenatedLineBreaks(const GfxRenderer& renderer, const int fontId,
                                                            const int pageWidth, std::vector<uint16_t>& wordWidths,
                                                            std::vector<bool>& continuesVec,
                                                            const bool paragraphContinuation) {
  // Continuation chunks must not re-apply text-indent.
  const int firstLineIndent = resolveFirstLineIndent(!paragraphContinuation, renderer, fontId);

  std::vector<size_t> lineBreakIndices;
  size_t currentIndex = 0;
  bool isFirstLine = !paragraphContinuation;
  auto currentTokenAttaches = [&](const size_t index) {
    return index < wordWidths.size() && (continuesVec[index] || wordIsGuideDot[index]);
  };

  while (currentIndex < wordWidths.size()) {
    const size_t lineStart = currentIndex;
    int lineWidth = 0;

    // First line has reduced width due to text-indent
    const int effectivePageWidth = isFirstLine ? pageWidth - firstLineIndent : pageWidth;

    // Consume as many words as possible for current line, splitting when prefixes fit
    while (currentIndex < wordWidths.size()) {
      const bool isFirstWord = currentIndex == lineStart;
      int spacing = 0;
      if (!isFirstWord && !continuesVec[currentIndex]) {
        spacing = spaceAdvance(renderer, fontId, lastCodepoint(words[currentIndex - 1]),
                                           firstCodepoint(words[currentIndex]), wordStyles[currentIndex - 1]);
      } else if (!isFirstWord && continuesVec[currentIndex]) {
        // Cross-boundary kerning for continuation words (e.g. nonbreaking spaces, attached punctuation)
        spacing = renderer.getKerning(fontId, lastCodepoint(words[currentIndex - 1]),
                                      firstCodepoint(words[currentIndex]), wordStyles[currentIndex - 1]);
      }
      const int candidateWidth = spacing + wordWidths[currentIndex];

      // Word fits on current line
      if (lineWidth + candidateWidth <= effectivePageWidth) {
        lineWidth += candidateWidth;
        ++currentIndex;
        continue;
      }

      // Word would overflow — try to split based on hyphenation points
      const int availableWidth = effectivePageWidth - lineWidth - spacing;
      const bool allowFallbackBreaks = isFirstWord;  // Only for first word on line

      if (availableWidth > 0 &&
          hyphenateWordAtIndex(currentIndex, availableWidth, renderer, fontId, wordWidths, allowFallbackBreaks)) {
        // Prefix now fits; append it to this line and move to next line
        lineWidth += spacing + wordWidths[currentIndex];
        ++currentIndex;
        break;
      }

      // Could not split: force at least one word per line to avoid infinite loop
      if (currentIndex == lineStart) {
        lineWidth += candidateWidth;
        ++currentIndex;
      }
      break;
    }

    // Don't break before a continuation word (e.g., orphaned "?" after "question").
    // Backtrack to the start of the continuation group so the whole group moves to the next line.
    while (currentIndex > lineStart + 1 && currentTokenAttaches(currentIndex)) {
      --currentIndex;
    }

    lineBreakIndices.push_back(currentIndex);
    isFirstLine = false;
  }

  return lineBreakIndices;
}

// Splits words[wordIndex] into prefix (adding a hyphen only when needed) and remainder when a legal breakpoint fits the
// available width.
bool ParsedText::hyphenateWordAtIndex(const size_t wordIndex, const int availableWidth, const GfxRenderer& renderer,
                                      const int fontId, std::vector<uint16_t>& wordWidths,
                                      const bool allowFallbackBreaks) {
  // Guard against invalid indices or zero available width before attempting to split.
  if (availableWidth <= 0 || wordIndex >= words.size()) {
    return false;
  }

  const std::string& word = words[wordIndex];
  const auto style = wordStyles[wordIndex];

  if (allowFallbackBreaks && isPathologicalUnbrokenToken(word)) {
    return splitPathologicalTokenAtIndex(wordIndex, availableWidth, renderer, fontId, wordWidths);
  }

  // Collect candidate breakpoints (byte offsets and hyphen requirements).
  auto breakInfos = Hyphenator::breakOffsets(word, allowFallbackBreaks);
  if (breakInfos.empty()) {
    return false;
  }

  size_t chosenOffset = 0;
  int chosenWidth = -1;
  bool chosenNeedsHyphen = true;

  // Iterate over each legal breakpoint and retain the widest prefix that still fits.
  for (const auto& info : breakInfos) {
    const size_t offset = info.byteOffset;
    if (offset == 0 || offset >= word.size()) {
      continue;
    }

    const bool needsHyphen = info.requiresInsertedHyphen;
    const int prefixWidth = measureWordWidth(renderer, fontId, word.substr(0, offset), style, needsHyphen);
    if (prefixWidth > availableWidth || prefixWidth <= chosenWidth) {
      continue;  // Skip if too wide or not an improvement
    }

    chosenWidth = prefixWidth;
    chosenOffset = offset;
    chosenNeedsHyphen = needsHyphen;
  }

  if (chosenWidth < 0) {
    // No hyphenation point produced a prefix that fits in the remaining space.
    return false;
  }

  // Split the word at the selected breakpoint and append a hyphen if required.
  std::string remainder = word.substr(chosenOffset);
  words[wordIndex].resize(chosenOffset);
  if (chosenNeedsHyphen) {
    words[wordIndex].push_back('-');
  }

  // Insert the remainder word (with matching style and continuation flag) directly after the prefix.
  words.insert(words.begin() + wordIndex + 1, remainder);
  wordStyles.insert(wordStyles.begin() + wordIndex + 1, style);
  wordBackgroundBlack.insert(wordBackgroundBlack.begin() + wordIndex + 1, wordBackgroundBlack[wordIndex]);
  bidiSplitWord(wordIndex);
  // The hyphen remainder is neither a bionic suffix nor a guide dot - it starts fresh on the next line.
  wordIsBionicSuffix.insert(wordIsBionicSuffix.begin() + wordIndex + 1, false);
  wordIsGuideDot.insert(wordIsGuideDot.begin() + wordIndex + 1, false);
  if (rubyTexts.size() > wordIndex + 1) rubyTexts.insert(rubyTexts.begin() + wordIndex + 1, std::string());
  if (wordDrawShift.size() > wordIndex + 1) wordDrawShift.insert(wordDrawShift.begin() + wordIndex + 1, 0);

  // Continuation flag handling after splitting a word into prefix + remainder.
  //
  // The prefix keeps the original word's continuation flag so that no-break-space groups
  // stay linked. The remainder always gets continues=false because it starts on the next
  // line and is not attached to the prefix.
  //
  // Example: "200&#xA0;Quadratkilometer" produces tokens:
  //   [0] "200"               continues=false
  //   [1] " "                 continues=true
  //   [2] "Quadratkilometer"  continues=true   <-- the word being split
  //
  // After splitting "Quadratkilometer" at "Quadrat-" / "kilometer":
  //   [0] "200"         continues=false
  //   [1] " "           continues=true
  //   [2] "Quadrat-"    continues=true   (KEPT — still attached to the no-break group)
  //   [3] "kilometer"   continues=false  (NEW — starts fresh on the next line)
  //
  // This lets the backtracking loop keep the entire prefix group ("200 Quadrat-") on one
  // line, while "kilometer" moves to the next line.
  // wordContinues[wordIndex] is intentionally left unchanged — the prefix keeps its original attachment.
  wordContinues.insert(wordContinues.begin() + wordIndex + 1, false);

  // Update cached widths to reflect the new prefix/remainder pairing.
  wordWidths[wordIndex] = static_cast<uint16_t>(chosenWidth);
  const uint16_t remainderWidth = measureWordWidth(renderer, fontId, remainder, style);
  wordWidths.insert(wordWidths.begin() + wordIndex + 1, remainderWidth);
  return true;
}

bool ParsedText::splitPathologicalTokenAtIndex(const size_t wordIndex, const int availableWidth,
                                               const GfxRenderer& renderer, const int fontId,
                                               std::vector<uint16_t>& wordWidths) {
  if (availableWidth <= 0 || wordIndex >= words.size() || wordIndex >= wordWidths.size()) {
    return false;
  }

  const std::string& word = words[wordIndex];
  if (word.size() < 2) {
    return false;
  }

  const auto style = wordStyles[wordIndex];
  size_t low = 1;
  size_t high = std::min(word.size() - 1, PATHOLOGICAL_TOKEN_SCAN_BYTES);
  size_t chosenOffset = 0;
  int chosenWidth = -1;
  std::string prefix;
  prefix.reserve(high);

  while (low <= high) {
    const size_t mid = low + (high - low) / 2;
    prefix.assign(word.data(), mid);
    const int prefixWidth = measureWordWidth(renderer, fontId, prefix, style);
    if (prefixWidth <= availableWidth) {
      chosenOffset = mid;
      chosenWidth = prefixWidth;
      low = mid + 1;
    } else {
      high = mid - 1;
    }
  }

  if (chosenOffset == 0) {
    return false;
  }

  std::string remainder = word.substr(chosenOffset);
  words[wordIndex].resize(chosenOffset);
  words.insert(words.begin() + wordIndex + 1, remainder);
  wordStyles.insert(wordStyles.begin() + wordIndex + 1, style);
  wordBackgroundBlack.insert(wordBackgroundBlack.begin() + wordIndex + 1, wordBackgroundBlack[wordIndex]);
  bidiSplitWord(wordIndex);
  wordIsBionicSuffix.insert(wordIsBionicSuffix.begin() + wordIndex + 1, false);
  wordIsGuideDot.insert(wordIsGuideDot.begin() + wordIndex + 1, false);
  if (rubyTexts.size() > wordIndex + 1) rubyTexts.insert(rubyTexts.begin() + wordIndex + 1, std::string());
  if (wordDrawShift.size() > wordIndex + 1) wordDrawShift.insert(wordDrawShift.begin() + wordIndex + 1, 0);
  wordContinues.insert(wordContinues.begin() + wordIndex + 1, false);

  wordWidths[wordIndex] = static_cast<uint16_t>(chosenWidth);
  const uint16_t remainderWidth = remainder.size() >= PATHOLOGICAL_TOKEN_MIN_BYTES
                                      ? std::numeric_limits<uint16_t>::max()
                                      : measureWordWidth(renderer, fontId, remainder, style);
  wordWidths.insert(wordWidths.begin() + wordIndex + 1, remainderWidth);
  return true;
}

void ParsedText::extractLine(const size_t breakIndex, const int pageWidth, const std::vector<uint16_t>& wordWidths,
                             const std::vector<bool>& continuesVec, const std::vector<size_t>& lineBreakIndices,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                             const GfxRenderer& renderer, const int fontId, const bool paragraphContinuation) {
  const size_t lineBreak = lineBreakIndices[breakIndex];
  const size_t lastBreakAt = breakIndex > 0 ? lineBreakIndices[breakIndex - 1] : 0;
  const size_t lineWordCount = lineBreak - lastBreakAt;

  const int firstLineIndent = resolveFirstLineIndent(breakIndex == 0 && !paragraphContinuation, renderer, fontId);

  std::vector<std::string> lineWords;
  std::vector<EpdFontFamily::Style> lineWordStyles;
  std::vector<uint16_t> lineWordWidths;
  std::vector<bool> lineBionicSuffix;
  std::vector<bool> lineGuideDot;
  std::vector<uint8_t> lineBackgroundBlack;
  std::vector<std::string> lineRuby;  // stays empty for a line without ruby
  std::vector<int16_t> lineDrawShift;  // stays empty for a line without adjusted CJK marks
  std::vector<std::string> lineDisplay;  // the drawn forms of a bidi line's pieces; empty for any other line
  lineWords.reserve(lineWordCount);
  lineWordStyles.reserve(lineWordCount);
  lineWordWidths.reserve(lineWordCount);
  lineBionicSuffix.reserve(lineWordCount);
  lineGuideDot.reserve(lineWordCount);
  lineBackgroundBlack.reserve(lineWordCount);

  for (size_t i = 0; i < lineWordCount; ++i) {
    const size_t sourceIndex = lastBreakAt + i;
    std::string word = std::move(words[sourceIndex]);
    if (containsSoftHyphen(word)) {
      stripSoftHyphensInPlace(word);
    }
    lineWords.push_back(std::move(word));
    lineWordStyles.push_back(wordStyles[sourceIndex]);
    lineWordWidths.push_back(wordWidths[sourceIndex]);
    lineBionicSuffix.push_back(wordIsBionicSuffix[sourceIndex]);
    lineGuideDot.push_back(wordIsGuideDot[sourceIndex]);
    lineBackgroundBlack.push_back(wordBackgroundBlack[sourceIndex]);
    if (!wordDrawShift.empty()) {
      if (lineDrawShift.empty()) lineDrawShift.assign(lineWordCount, 0);
      lineDrawShift[i] = sourceIndex < wordDrawShift.size() ? wordDrawShift[sourceIndex] : 0;
    }
    if (sourceIndex < rubyTexts.size() && !rubyTexts[sourceIndex].empty()) {
      if (lineRuby.empty()) lineRuby.resize(lineWordCount);
      lineRuby[i] = std::move(rubyTexts[sourceIndex]);
    }
  }

  // Calculate total word width for this line, count actual word gaps,
  // and accumulate total natural gap widths (including space kerning adjustments).
  int lineWordWidthSum = 0;
  size_t actualGapCount = 0;
  int totalNaturalGaps = 0;

  for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
    lineWordWidthSum += lineWordWidths[wordIdx];
    // Count gaps: each word after the first creates a gap, unless it's a continuation
    if (wordIdx > 0 && !continuesVec[lastBreakAt + wordIdx]) {
      // The dash-to-word gap in FB2 dialogue is fixed-width, not a
      // justification opportunity even if it arrived in separate tokens.
      if (!isFixedDialogueGap(preserveNaturalWordSpaces, lineWords[wordIdx - 1])) {
        actualGapCount++;
      }
      totalNaturalGaps += spaceAdvance(renderer, fontId, lastCodepoint(lineWords[wordIdx - 1]),
                                                   firstCodepoint(lineWords[wordIdx]), lineWordStyles[wordIdx - 1]);
    } else if (wordIdx > 0 && continuesVec[lastBreakAt + wordIdx]) {
      // Continuation tokens (including NBSP represented as a literal space word)
      // are not justification opportunities.  Stretching NBSPs is what made
      // some Russian source text visually explode into "п р и в е т".
      // The space token's own width is already included in lineWordWidthSum.
      totalNaturalGaps += renderer.getKerning(fontId, lastCodepoint(lineWords[wordIdx - 1]),
                                              firstCodepoint(lineWords[wordIdx]), lineWordStyles[wordIdx - 1]);
    }
  }

  // Calculate spacing (account for indent reducing effective page width on first line)
  const int effectivePageWidth = pageWidth - firstLineIndent;
  const bool isLastLine = breakIndex == lineBreakIndices.size() - 1;
  const CssTextAlign effectiveAlignment =
      (blockStyle.isRtl && !blockStyle.textAlignDefined && blockStyle.alignment == CssTextAlign::Left)
          ? CssTextAlign::Right
          : blockStyle.alignment;

  // Keep every rendered glyph inside the text viewport. Earlier FB2
  // justification used optical "hanging punctuation" and subtracted the final
  // comma/quote/hyphen from the measured line width. On the narrow X3/X4
  // screen that visibly pushed punctuation and hyphenation marks into the
  // right page margin. Preserve natural FB2 word spacing, but do not hang
  // terminal glyphs outside the text box.
  const int hangingRight = 0;

  // For justified text, compute per-gap extra to distribute remaining space evenly.
  const int spareSpace = effectivePageWidth - (lineWordWidthSum - hangingRight) - totalNaturalGaps;
  int justifyExtra = (effectiveAlignment == CssTextAlign::Justify && !isLastLine && actualGapCount >= 1)
                          ? spareSpace / static_cast<int>(actualGapCount)
                          : 0;
  // FB2 source text frequently contains NBSP/regular-space mixtures and long
  // justified Russian lines. Rounding differences after hyphenation can make
  // spareSpace slightly negative. The generic path then compresses every word
  // gap, and on narrow lines an ordinary space can collapse to ~0 px ("ж я"
  // becomes "жя"). For FB2 keep at least the font's natural space width.
  // EPUB keeps its existing justification behavior unchanged.
  if (preserveNaturalWordSpaces && justifyExtra < 0) {
    justifyExtra = 0;
  }
  // Keep the anti-"giant gap" guard only for genuinely sparse lines.
  // Applying the stretch cap to normal prose (3+ gaps) leaves a visible ragged
  // right edge even when the user explicitly selected Justify. With several
  // gaps there is enough room to distribute the remainder naturally, so let
  // those lines reach the full text width. One/two-gap lines retain the old
  // protection against absurdly wide spaces.
  // A line with a break between two CJK characters (a gap that has no natural width) ends flush (spec 5.1): the
  // cap above would hold it to 2 px a gap, so such a line is capped at an eighth of the width instead, and what
  // the even share leaves over goes 1 px each to evenly spaced gaps. Latin lines keep their rounding.
  bool cjkGapOnLine = false;
  for (size_t wordIdx = 1; wordIdx < lineWordCount && !cjkGapOnLine; wordIdx++) {
    cjkGapOnLine = !continuesVec[lastBreakAt + wordIdx] &&
                   isSpacelessCjkBoundary(lastCodepoint(lineWords[wordIdx - 1]), firstCodepoint(lineWords[wordIdx]));
  }
  if (justifyExtra > 0 && actualGapCount >= 1 && actualGapCount <= 2 && !cjkGapOnLine) {
    const int averageNaturalGap = std::max(1, totalNaturalGaps / static_cast<int>(actualGapCount));
    justifyExtra = std::min(justifyExtra, averageNaturalGap * 2);
  }
  if (cjkGapOnLine) justifyExtra = std::min(justifyExtra, effectivePageWidth / 8);
  const int justifyRemainder = cjkGapOnLine && justifyExtra >= 0 && spareSpace > 0 && justifyExtra < effectivePageWidth / 8 &&
                                       effectiveAlignment == CssTextAlign::Justify && !isLastLine
                                   ? spareSpace - justifyExtra * static_cast<int>(actualGapCount)
                                   : 0;
  int stretchedGaps = 0;
  const auto gapExtra = [&]() {
    const int g = stretchedGaps++;
    const int gaps = static_cast<int>(actualGapCount);
    if (gaps == 0 || justifyRemainder <= 0) return justifyExtra;
    return justifyExtra + ((g + 1) * justifyRemainder / gaps - g * justifyRemainder / gaps);
  };

  visualOrderScratch.clear();
  visualOrderScratch.reserve(lineWordCount);
  const bool shouldResolveVisualOrder = blockStyle.isRtl || hasRtlWord;

  // The bidi line pass (BidiLine.h, as in witchhunt): one UAX #9 pass over the whole line, so a word whose characters
  // land apart is split into pieces and brackets and digits resolve against their neighbours. It sets the line's
  // pieces in visual order (`bidiLogical` their logical text, `reorderedWordsScratch` what each draws). A line with
  // bionic splits, guide dots, ruby or CJK mark shifts, a line over the pass's limits, and a line the pass leaves
  // exactly as it was take the word-order path below.
  std::vector<std::string> bidiLogical;
  std::vector<uint16_t> bidiPieceToken;
  bool usedBidiLine = false;

  // Markup controls (dir, <bdi>, <bdo>): each marked word becomes a token wrapped in its openers and closers.
  bool lineMarked = false;
  if (!bidiMarks.empty()) {
    for (const auto& m : bidiMarks) {
      if (m.word >= lastBreakAt && m.word < lineBreak) {
        lineMarked = true;
        break;
      }
    }
  }
  std::vector<std::string> markedTokens;  // the tokens' texts, controls included; empty for an unmarked line
  if (lineMarked) {
    markedTokens.reserve(lineWordCount);
    for (size_t i = 0; i < lineWordCount; ++i) {
      std::string tok;
      const BidiMark* mark = nullptr;
      for (const auto& m : bidiMarks) {
        if (m.word == lastBreakAt + i) {
          mark = &m;
          break;
        }
      }
      if (mark != nullptr) {
        for (uint8_t k = 0; k < mark->nOpen; ++k) utf8AppendCodepoint(tok, mark->opens[k]);
      }
      tok += lineWords[i];
      if (mark != nullptr) {
        for (uint8_t k = 0; k < mark->nClose; ++k) utf8AppendCodepoint(tok, mark->closes[k] == 'I' ? 0x2069 : 0x202C);
      }
      markedTokens.push_back(std::move(tok));
    }
  }
  BidiLine::OpenControls lineOpen;
  for (uint8_t k = 0; k < bidiCarryN_; ++k) lineOpen.cps[k] = bidiCarry_[k];
  lineOpen.count = bidiCarryN_;
  lineOpen.overflow = bidiCarryOver_;

  if ((shouldResolveVisualOrder || lineMarked) && lineRuby.empty() && lineDrawShift.empty()) {
    bool plainLine = true;
    for (size_t i = 0; i < lineWordCount && plainLine; ++i) plainLine = !lineBionicSuffix[i] && !lineGuideDot[i];
    BidiLine::Scratch* const scratch = plainLine ? BidiLine::heapScratch() : nullptr;
    if (scratch != nullptr) {
      std::vector<BidiLine::Token> tokens;
      tokens.reserve(lineWordCount);
      for (size_t i = 0; i < lineWordCount; ++i) {
        const std::string_view text = lineMarked ? std::string_view(markedTokens[i]) : std::string_view(lineWords[i]);
        tokens.push_back({text, i > 0 && !continuesVec[lastBreakAt + i]});
      }
      BidiLine::Line bidiLine;
      if (BidiLine::layoutLine(tokens.data(), tokens.size(), lineOpen, blockStyle.isRtl ? 1 : 0, *scratch,
                               bidiLine) &&
          !bidiLine.fallback) {
        bool changed = bidiLine.reordered || bidiLine.pieces.size() != lineWordCount;
        reorderedWordsScratch.clear();
        for (const auto& piece : bidiLine.pieces) {
          if (piece.displayLength == 0) continue;  // only controls or joiners: nothing to draw
          std::string logical(tokens[piece.token].text.substr(piece.begin, piece.end - piece.begin));
          if (lineMarked) BidiLine::dropControls(logical);
          reorderedWordsScratch.emplace_back(bidiLine.display.data() + piece.displayBegin, piece.displayLength);
          bidiLogical.push_back(std::move(logical));
          bidiPieceToken.push_back(piece.token);
          changed = changed || reorderedWordsScratch.back() != lineWords[piece.token];
        }
        usedBidiLine = changed && !reorderedWordsScratch.empty();
        // Gap kinds, in visual order, for the continuation flags built below.
        if (usedBidiLine) {
          bidiGapScratch.clear();
          for (const auto& piece : bidiLine.pieces) {
            if (piece.displayLength == 0) continue;
            bidiGapScratch.push_back(piece.gapBefore == BidiLine::Gap::Space ? 0 : 1);
          }
        }
      }
    }
  }
  if (lineMarked) {  // what the line leaves open carries into the next
    BidiLine::OpenControls carry = lineOpen;
    for (const auto& tok : markedTokens) carry.advance(tok);
    for (uint8_t k = 0; k < carry.count && k < 8; ++k) bidiCarry_[k] = carry.cps[k];
    bidiCarryN_ = carry.count;
    bidiCarryOver_ = carry.overflow;
  }
  const bool willReorder =
      usedBidiLine ||
      (shouldResolveVisualOrder && BidiUtils::computeVisualWordOrder(lineWords, blockStyle.isRtl, visualOrderScratch));

  std::vector<int16_t> lineXPos;
  lineXPos.reserve(lineWordCount);

  if (willReorder) {
    if (!usedBidiLine) reorderedWordsScratch.clear();
    reorderedStylesScratch.clear();
    reorderedWidthsScratch.clear();
    reorderedContinuesScratch.clear();
    reorderedBionicSuffixScratch.clear();
    reorderedGuideDotScratch.clear();
    reorderedBackgroundBlackScratch.clear();
    reorderedStylesScratch.reserve(lineWordCount);
    reorderedWidthsScratch.reserve(lineWordCount);
    reorderedContinuesScratch.reserve(lineWordCount);
    reorderedBionicSuffixScratch.reserve(lineWordCount);
    reorderedGuideDotScratch.reserve(lineWordCount);
    reorderedBackgroundBlackScratch.reserve(lineWordCount);

    if (usedBidiLine) {
      for (size_t i = 0; i < reorderedWordsScratch.size(); ++i) {
        const uint16_t token = bidiPieceToken[i];
        reorderedStylesScratch.push_back(lineWordStyles[token]);
        reorderedWidthsScratch.push_back(
            measureWordWidth(renderer, fontId, reorderedWordsScratch[i], lineWordStyles[token]));
        reorderedBionicSuffixScratch.push_back(false);
        reorderedGuideDotScratch.push_back(false);
        reorderedBackgroundBlackScratch.push_back(lineBackgroundBlack[token]);
        reorderedContinuesScratch.push_back(i > 0 && bidiGapScratch[i] != 0);
      }
    }
    for (size_t i = 0; !usedBidiLine && i < visualOrderScratch.size(); ++i) {
      const uint16_t src = visualOrderScratch[i];
      reorderedWordsScratch.push_back(std::move(lineWords[src]));
      reorderedStylesScratch.push_back(lineWordStyles[src]);
      reorderedWidthsScratch.push_back(lineWordWidths[src]);
      reorderedBionicSuffixScratch.push_back(lineBionicSuffix[src]);
      reorderedGuideDotScratch.push_back(lineGuideDot[src]);
      reorderedBackgroundBlackScratch.push_back(lineBackgroundBlack[src]);

      bool continues = false;
      if (i > 0) {
        const size_t prevSrc = visualOrderScratch[i - 1];
        const size_t currSrc = src;
        const bool forwardAdjacent = currSrc == prevSrc + 1;
        const bool reverseAdjacent = prevSrc == currSrc + 1;

        if (forwardAdjacent && continuesVec[lastBreakAt + currSrc]) {
          continues = true;
        } else if (reverseAdjacent && continuesVec[lastBreakAt + prevSrc]) {
          continues = true;
        }
      }
      reorderedContinuesScratch.push_back(continues);
    }

    int reorderedWordWidthSum = 0;
    size_t reorderedGapCount = 0;
    int reorderedNaturalGaps = 0;
    for (size_t wordIdx = 0; wordIdx < reorderedWidthsScratch.size(); ++wordIdx) {
      reorderedWordWidthSum += reorderedWidthsScratch[wordIdx];
      if (wordIdx > 0 && !reorderedContinuesScratch[wordIdx]) {
        if (!isFixedDialogueGap(preserveNaturalWordSpaces, reorderedWordsScratch[wordIdx - 1])) {
          reorderedGapCount++;
        }
        reorderedNaturalGaps += spaceAdvance(renderer, fontId, lastCodepoint(reorderedWordsScratch[wordIdx - 1]),
                                                         firstCodepoint(reorderedWordsScratch[wordIdx]),
                                                         reorderedStylesScratch[wordIdx - 1]);
      } else if (wordIdx > 0 && reorderedContinuesScratch[wordIdx]) {
        // NBSP/continuation boundaries are intentionally non-stretchable.
        reorderedNaturalGaps +=
            renderer.getKerning(fontId, lastCodepoint(reorderedWordsScratch[wordIdx - 1]),
                                firstCodepoint(reorderedWordsScratch[wordIdx]), reorderedStylesScratch[wordIdx - 1]);
      }
    }

    // Same no-overhang rule for visually reordered (BiDi/RTL) lines.
    const int reorderedHangingRight = 0;
    const int reorderedSpare =
        effectivePageWidth - (reorderedWordWidthSum - reorderedHangingRight) - reorderedNaturalGaps;
    int reorderedJustifyExtra =
        (effectiveAlignment == CssTextAlign::Justify && !isLastLine && reorderedGapCount >= 1)
            ? reorderedSpare / static_cast<int>(reorderedGapCount)
            : 0;
    if (preserveNaturalWordSpaces && reorderedJustifyExtra < 0) {
      reorderedJustifyExtra = 0;
    }
    // Same cap as the non-reordered path above, for the same reason - see
    // its comment for why.
    if (reorderedJustifyExtra > 0 && reorderedGapCount >= 1) {
      const int averageNaturalGap = std::max(1, reorderedNaturalGaps / static_cast<int>(reorderedGapCount));
      reorderedJustifyExtra = std::min(reorderedJustifyExtra, averageNaturalGap * 2);
    }
    const int justifyContribution = (effectiveAlignment == CssTextAlign::Justify && !isLastLine)
                                        ? reorderedJustifyExtra * static_cast<int>(reorderedGapCount)
                                        : 0;
    const int contentWidth = reorderedWordWidthSum + reorderedNaturalGaps + justifyContribution;

    int xpos = 0;
    if (blockStyle.isRtl) {
      if (effectiveAlignment == CssTextAlign::Right || effectiveAlignment == CssTextAlign::Justify) {
        xpos = effectivePageWidth - contentWidth;
      } else if (effectiveAlignment == CssTextAlign::Center) {
        xpos = (effectivePageWidth - contentWidth) / 2;
      }
    } else {
      xpos = firstLineIndent;
      if (effectiveAlignment == CssTextAlign::Right) {
        xpos = effectivePageWidth - contentWidth;
      } else if (effectiveAlignment == CssTextAlign::Center) {
        xpos = (effectivePageWidth - contentWidth) / 2;
      }
    }

    for (size_t wordIdx = 0; wordIdx < reorderedWidthsScratch.size(); ++wordIdx) {
      lineXPos.push_back(static_cast<int16_t>(xpos));
      xpos += reorderedWidthsScratch[wordIdx];

      const bool nextIsContinuation =
          wordIdx + 1 < reorderedWidthsScratch.size() && reorderedContinuesScratch[wordIdx + 1];
      if (nextIsContinuation) {
        int advance =
            renderer.getKerning(fontId, lastCodepoint(reorderedWordsScratch[wordIdx]),
                                firstCodepoint(reorderedWordsScratch[wordIdx + 1]), reorderedStylesScratch[wordIdx]);
        xpos += advance;
      } else if (wordIdx + 1 < reorderedWidthsScratch.size()) {
        int gap = spaceAdvance(renderer, fontId, lastCodepoint(reorderedWordsScratch[wordIdx]),
                                           firstCodepoint(reorderedWordsScratch[wordIdx + 1]),
                                           reorderedStylesScratch[wordIdx]);
        if (effectiveAlignment == CssTextAlign::Justify && !isLastLine &&
            !isFixedDialogueGap(preserveNaturalWordSpaces, reorderedWordsScratch[wordIdx])) {
          gap += reorderedJustifyExtra;
        }
        xpos += gap;
      }
    }

    if (usedBidiLine) {
      lineDisplay = std::move(reorderedWordsScratch);  // the drawn forms
      reorderedWordsScratch.clear();
      lineWords = std::move(bidiLogical);              // the logical text of each piece
    } else {
      lineWords.swap(reorderedWordsScratch);
    }
    lineWordStyles.swap(reorderedStylesScratch);
    lineWordWidths.swap(reorderedWidthsScratch);
    lineBionicSuffix.swap(reorderedBionicSuffixScratch);
    lineGuideDot.swap(reorderedGuideDotScratch);
    lineBackgroundBlack.swap(reorderedBackgroundBlackScratch);
    lineRuby.clear();  // annotations are not carried through visual reordering
  } else {
    // Standard LTR/RTL positioning loop when no visual reordering is needed
    if (blockStyle.isRtl) {
      // RTL: position words from right to left
      int xpos = effectivePageWidth;
      if (effectiveAlignment == CssTextAlign::Left) {
        // Explicit left alignment in RTL context
        xpos = lineWordWidthSum + totalNaturalGaps;
      } else if (effectiveAlignment == CssTextAlign::Center) {
        xpos = (effectivePageWidth + lineWordWidthSum + totalNaturalGaps) / 2;
      }
      // For Right and Justify, start from right edge (xpos = effectivePageWidth)

      for (size_t wordIdx = 0; wordIdx < lineWordCount; ++wordIdx) {
        xpos -= lineWordWidths[wordIdx];
        lineXPos.push_back(static_cast<int16_t>(xpos));

        const bool nextIsContinuation = wordIdx + 1 < lineWordCount && continuesVec[lastBreakAt + wordIdx + 1];
        if (nextIsContinuation) {
          int advance = renderer.getKerning(fontId, lastCodepoint(lineWords[wordIdx]),
                                            firstCodepoint(lineWords[wordIdx + 1]), lineWordStyles[wordIdx]);
          xpos -= advance;
        } else {
          int gap = 0;
          if (wordIdx + 1 < lineWordCount) {
            gap = spaceAdvance(renderer, fontId, lastCodepoint(lineWords[wordIdx]),
                                           firstCodepoint(lineWords[wordIdx + 1]), lineWordStyles[wordIdx]);
          }
          if (effectiveAlignment == CssTextAlign::Justify && !isLastLine &&
              !(wordIdx + 1 < lineWordCount &&
                isFixedDialogueGap(preserveNaturalWordSpaces, lineWords[wordIdx]))) {
            gap += gapExtra();
          }
          xpos -= gap;
        }
      }
    } else {
      // LTR: position words from left to right
      int xpos = firstLineIndent;
      if (effectiveAlignment == CssTextAlign::Right) {
        xpos = effectivePageWidth - lineWordWidthSum - totalNaturalGaps;
      } else if (effectiveAlignment == CssTextAlign::Center) {
        xpos = (effectivePageWidth - lineWordWidthSum - totalNaturalGaps) / 2;
      }

      for (size_t wordIdx = 0; wordIdx < lineWordCount; ++wordIdx) {
        lineXPos.push_back(static_cast<int16_t>(xpos + (lineDrawShift.empty() ? 0 : lineDrawShift[wordIdx])));

        const bool nextIsContinuation = wordIdx + 1 < lineWordCount && continuesVec[lastBreakAt + wordIdx + 1];
        if (nextIsContinuation) {
          int advance = lineWordWidths[wordIdx];
          advance += renderer.getKerning(fontId, lastCodepoint(lineWords[wordIdx]),
                                         firstCodepoint(lineWords[wordIdx + 1]), lineWordStyles[wordIdx]);
          xpos += advance;
        } else {
          int gap = 0;
          if (wordIdx + 1 < lineWordCount) {
            gap = spaceAdvance(renderer, fontId, lastCodepoint(lineWords[wordIdx]),
                                           firstCodepoint(lineWords[wordIdx + 1]), lineWordStyles[wordIdx]);
          }
          if (effectiveAlignment == CssTextAlign::Justify && !isLastLine &&
              !(wordIdx + 1 < lineWordCount &&
                isFixedDialogueGap(preserveNaturalWordSpaces, lineWords[wordIdx]))) {
            gap += gapExtra();
          }
          xpos += lineWordWidths[wordIdx] + gap;
        }
      }
    }
  }

  const size_t finalCount = lineWords.size();  // pieces of a bidi line can outnumber the line's tokens
  bool lineHasBionicSplit = false;
  bool lineHasGuideDot = false;
  for (size_t i = 0; i < finalCount; i++) {
    if (lineBionicSuffix[i]) {
      lineHasBionicSplit = true;
    }
    if (lineGuideDot[i]) {
      lineHasGuideDot = true;
    }
    if (lineHasBionicSplit && lineHasGuideDot) {
      break;
    }
  }

  // Merge bionic suffix tokens and guide dot tokens back into their preceding word entry so each
  // original word occupies one TextBlock slot. Both splits are recorded as per-word annotations
  // applied at render time, cutting the token count significantly when either feature is active.
  // Bionic boundary/suffix and guide-dot vectors stay empty when this line has none.
  std::vector<std::string> outWords;
  std::vector<int16_t> outXPos;
  std::vector<EpdFontFamily::Style> outStyles;
  std::vector<uint8_t> outBoundaries;
  std::vector<uint16_t> outSuffixX;
  std::vector<uint16_t> outGuideDotXOffset;
  std::vector<uint8_t> outBackgroundBlack;
  outWords.reserve(finalCount);
  outXPos.reserve(finalCount);
  outStyles.reserve(finalCount);
  if (lineHasBionicSplit) {
    outBoundaries.reserve(finalCount);
    outSuffixX.reserve(finalCount);
  }
  if (lineHasGuideDot) {
    outGuideDotXOffset.reserve(finalCount);
  }
  outBackgroundBlack.reserve(finalCount);
  std::vector<std::string> outRuby;
  if (!lineRuby.empty()) outRuby.reserve(finalCount);
  std::vector<std::string> outDisplay;
  if (!lineDisplay.empty()) outDisplay.reserve(finalCount);

  for (size_t i = 0; i < finalCount; i++) {
    if (lineBionicSuffix[i] && !outWords.empty()) {
      // Bionic suffix: merge string into the preceding bold-prefix entry.
      outWords.back() += lineWords[i];
    } else if (lineGuideDot[i] && !outWords.empty()) {
      // Guide dot: annotate the preceding word entry with the dot's pixel offset.
      // Offset is relative to that word's x so render can place it without extra data.
      if (lineHasGuideDot) {
        const int dotDelta = static_cast<int>(lineXPos[i]) - static_cast<int>(outXPos.back());
        outGuideDotXOffset.back() = static_cast<uint16_t>(dotDelta > 0 ? dotDelta : 0);
      }
    } else {
      // Normal word: check for a following bionic suffix to record the byte boundary.
      uint8_t boundary = 0;
      uint16_t suffixX = 0;
      if (i + 1 < finalCount && lineBionicSuffix[i + 1]) {
        boundary = static_cast<uint8_t>(std::min(lineWords[i].size(), size_t{255}));
        // Suffix x offset = layout-time advance of the bold prefix, already known from xpos table.
        const int suffixDelta = static_cast<int>(lineXPos[i + 1]) - static_cast<int>(lineXPos[i]);
        suffixX = static_cast<uint16_t>(suffixDelta > 0 ? suffixDelta : 0);
      }
      outWords.push_back(std::move(lineWords[i]));
      outXPos.push_back(static_cast<int16_t>(lineXPos[i] + gridShift_));
      // For bionic entries with a suffix, strip BOLD from the stored style.
      // Render re-applies it to the prefix portion only, via the boundary field.
      const EpdFontFamily::Style storedStyle =
          boundary > 0 ? static_cast<EpdFontFamily::Style>(lineWordStyles[i] & ~EpdFontFamily::BOLD)
                       : lineWordStyles[i];
      outStyles.push_back(storedStyle);
      if (lineHasBionicSplit) {
        outBoundaries.push_back(boundary);
        outSuffixX.push_back(suffixX);
      }
      if (lineHasGuideDot) {
        outGuideDotXOffset.push_back(0);  // filled in later if a guide dot follows
      }
      outBackgroundBlack.push_back(lineBackgroundBlack[i]);
      if (!lineRuby.empty()) outRuby.push_back(std::move(lineRuby[i]));
      if (!lineDisplay.empty()) outDisplay.push_back(std::move(lineDisplay[i]));
    }
  }

  auto lineBlock = std::make_shared<TextBlock>(std::move(outWords), std::move(outXPos), std::move(outStyles),
                                               std::move(outBoundaries), std::move(outSuffixX),
                                               std::move(outGuideDotXOffset), std::move(outBackgroundBlack), blockStyle,
                                               std::move(outRuby), std::move(outDisplay));
  const int bookFontId = fontFamily_ != 0 ? BookFont::resolve(renderer, fontFamily_) : 0;
  const bool isBookFont = bookFontId != 0 && bookFontId == fontId;
  lineBlock->setHeading(!isBookFont && heading_ && fontId != 0 && fontId == HeadingFont::get());
  lineBlock->setFontFamily(isBookFont ? fontFamily_ : 0);
  processLine(std::move(lineBlock));
}
