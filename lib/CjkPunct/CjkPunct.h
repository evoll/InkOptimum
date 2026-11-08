#pragma once

#include <cstdint>


// The widths of full-width CJK punctuation (spec section 5.2). A full-width mark is ink on one half of its em
// and blank on the other; set next to other punctuation or at a line edge, part of that blank goes, as the
// layout requirements of each language describe (W3C JLREQ, CLREQ). The rules are written from those documents;
// KOReader's crengine tables (textlang.cpp) were the cross-check, not the source.
// Ported from witchhunt-reader (lib/Epub/Epub/CjkPunctuation).
namespace CjkPunctuation {

// The punctuation width table a language uses (spec 5.2). Written from W3C JLREQ and CLREQ.
enum class Table : uint8_t { SimplifiedChinese, TraditionalChinese, Japanese };

// The JLREQ classes of the marks a table adjusts.
enum class Class : uint8_t {
  None,              // text: Han, kana, Latin, and marks no table adjusts
  Opening,           // 「『（【〔〈《〘〖｟〝［｛, and the curly “‘ where full-width
  Closing,           // 」』）】〕〉》〙〗｠〞〟］｝, and the curly ”’ where full-width
  Dividing,          // ！？‼⁇⁈⁉
  MiddleDot,         // ・：；
  FullStop,          // 。．
  Comma,             // 、，
  IdeographicSpace,  // U+3000, which keeps its full width
  AmbiguousQuote,    // ＂＇, which may open or close
};

Class classOf(uint32_t cp);

// The curly quotes are CJK punctuation only where the font draws them full-width (a CJK font in CJK text); in a
// Latin font they are narrow and are left alone. The caller checks the width.
bool isCurlyQuote(uint32_t cp);

// A width in eighths of the mark's full advance (8: unchanged). `forced` always applies; `minimum` is how far
// the line may shrink the mark when it needs room (push-in, spec 5.3).
struct Width {
  uint8_t forced;
  uint8_t minimum;
};

// The width of `mark` beside its neighbour. An opening mark looks at the character BEFORE it, every other mark
// at the character AFTER it, as JLREQ's spacing does. `neighbour` is None for text or a space there.
// `atLineEdge`: a line starts at the opening mark, or ends after the other mark, so the neighbour is the edge.
Width widthOf(Table table, Class mark, Class neighbour, bool atLineEdge);

// The gap between Han or kana and a Latin letter or digit, in eighths of the CJK side's em.
constexpr uint8_t kCjkLatinGap = 2;

// Ideographs, 々〆〇, and the kana letters with ー; not ・, not half-width katakana, not Hangul.
bool isHanOrKana(uint32_t cp);

// ASCII letters and digits, and the accented Latin letters; not full-width forms, not Greek or Cyrillic.
bool isLatinLetterOrDigit(uint32_t cp);

// Whether a gap sits between two adjacent characters: one is Han or kana, the other a Latin letter or digit.
bool takesCjkLatinGap(uint32_t leftCp, uint32_t rightCp);

// ---- language profile --------------------------------------------------------------------------------------

struct Profile {
  Table table = Table::SimplifiedChinese;
  bool cjkLatinGap = false;  // a quarter em between Han/kana and a Latin letter or digit
  bool grid = false;         // the whole-em grid (spec 5.5): Chinese and Japanese books only
};

// The whole-em grid (spec 5.5). A CJK paragraph's text width is rounded down to a whole number of ems, so pure
// CJK lines fill it exactly and the characters line up in columns; half of what is cut goes to each side.
// em <= 0 (no usable CJK glyph) leaves the width alone.
int wholeEmWidth(int width, int em);
// A non-zero first-line indent rounds down to whole ems, at least one. No indent stays none, and a negative
// (hanging) indent is left as it is.
int wholeEmIndent(int indent, int em);

// For a BCP 47 tag ("ja", "zh-Hant-TW", "zh_CN", "ko", "en-US"): the Japanese table for ja, traditional for
// zh-Hant / TW / HK / MO / lzh, Simplified otherwise; the CJK-Latin gap only for Chinese and Japanese (Korean
// spaces its words and uses Western punctuation). Unknown or empty tags get the Simplified table (the rules
// apply to CJK marks in any book) and no gap.
Profile profileForLanguage(const char* tag);

// The language of the book being laid out (OPF dc:language); set once per section build.
void setBookLanguage(const char* tag);
const Profile& bookProfile();

// What a mark's width changes to beside its neighbour: widthDelta is the width taken off the mark's full
// `advance` (never negative), drawShift how far the glyph moves so its ink stays where it was (an opening mark
// has its ink on the right of its em, so it moves left by what was taken; a middle dot is centred, so by half).
struct Adjust {
  int16_t widthDelta = 0;
  int16_t drawShift = 0;
};
Adjust adjustFor(Table table, Class mark, Class neighbour, int advance);

}  // namespace CjkPunctuation