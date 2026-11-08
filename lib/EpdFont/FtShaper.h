#pragma once

// OpenType shaping for a TrueType/OpenType face read through FtFont: the
// bridge between lib/OtShaper (an engine over GDEF/GSUB/GPOS bytes) and the
// glyph-ID-addressed font path (glyph keys, lib/Utf8/GlyphKey.h).
//
// The face's cmap and hmtx stay in FreeType (a CJK-merged font's would be
// hundreds of KB); only GDEF, GSUB and GPOS are read into memory, and only
// while a shaper exists. A shaper is built on the first Indic word of a build
// and dropped with the build's other caches (release()).

#include "VectorFontSupport.h"

#if INKMOD_VECTOR_FONTS

#include <FtFont.h>
#include <OtShaper.h>
#include <IndicScripts.h>

#include <cstdint>
#include <memory>
#include <string>

class FtShaper {
 public:
  explicit FtShaper(freeink::font::FtFont& ft) : ft_(ft) {}
  ~FtShaper() { release(); }
  FtShaper(const FtShaper&) = delete;
  FtShaper& operator=(const FtShaper&) = delete;

  // Shapes one word (code points of one Indic script plus joiners) and appends
  // a glyph key per resulting glyph to `out`. False, with `out` untouched, when
  // the face cannot shape it: no GSUB, a table too big for the heap, a run
  // longer than the buffer, or the lookups failed it. `size26_6` is the face's
  // pixel size in 26.6, `lang` a BCP 47 tag or "" (language system choice).
  bool shapeWord(const uint32_t* cps, unsigned count, ot::Script script, const char* lang, uint32_t size26_6,
                 std::string& out);

  // Frees the tables, plans and buffer.
  void release();

  static constexpr unsigned MAX_WORD = 96;  // code points; longer words stay unshaped

 private:
  bool ensureReady();
  static bool nominalThunk(void* ctx, uint32_t cp, uint32_t* glyph);
  static int32_t advanceThunk(void* ctx, uint32_t glyph);

  freeink::font::FtFont& ft_;
  bool tried_ = false;
  bool ready_ = false;
  const uint8_t* table_[3] = {nullptr, nullptr, nullptr};  // GDEF, GSUB, GPOS
  bool owned_[3] = {false, false, false};
  size_t tableSize_[3] = {0, 0, 0};
  uint8_t head_[54] = {};
  uint8_t maxp_[6] = {};
  ot::Face face_;
  std::unique_ptr<ot::Plan> plans_[10];
  std::unique_ptr<uint8_t[]> storage_;
  std::unique_ptr<ot::Buffer> buffer_;
  // advance cache (font units) by glyph ID, direct-mapped
  struct AdvanceSlot {
    uint32_t glyph = 0xFFFFFFFFu;
    int32_t advance = 0;
  };
  AdvanceSlot advances_[64];
  std::string langKey_;
};

#endif  // INKMOD_VECTOR_FONTS
