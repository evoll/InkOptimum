#pragma once
#include <GfxRenderer.h>

#include <cstdint>

// Fonts the open book embeds through @font-face. The reader app owns the font bytes (BookFontSystem) and
// installs the resolver; layout and drawing only ask "which font id for this family". A block carries the hash of
// its font-family (cssFamilyHash); 0, a family the book does not embed, or the feature being off all mean the
// reader's own font.
namespace BookFont {
using ResolveFn = int (*)(uint32_t family);

struct State {
  ResolveFn resolve = nullptr;
  bool enabled = false;
  uint32_t spareKey = 0;  // identity of the spare SD font for missing glyphs (0 = none); it changes line widths
};
inline State& state() {
  static State s;
  return s;
}

// True while the open book embeds fonts and the setting allows them: section caches then record it, so toggling
// the setting (or opening another book) lays the text out again.
inline bool enabled() { return state().enabled; }
inline void set(const bool on, const ResolveFn resolve) {
  state().enabled = on;
  state().resolve = on ? resolve : nullptr;
}

// The registered font id for `family` (loading it when needed), or 0 for the reader font.
inline int resolve(const GfxRenderer& renderer, const uint32_t family) {
  const State& s = state();
  if (family == 0 || !s.enabled || !s.resolve) return 0;
  const int id = s.resolve(family);
  return (id != 0 && renderer.getFontMap().count(id) != 0) ? id : 0;
}

// The spare SD font (SdCardFontSystem::applySpareFont) supplies glyphs the reading font lacks, so it changes
// measured widths: its identity belongs in every layout key.
inline uint32_t spareKey() { return state().spareKey; }
inline void setSpareKey(const uint32_t key) { state().spareKey = key; }

// Section-cache font key: unchanged unless book fonts or a spare font are in play.
inline int layoutKey(const int key) {
  unsigned k = static_cast<unsigned>(key);
  if (enabled()) k = k * 16777619u ^ 0xB00F0001u;
  if (state().spareKey != 0) k = k * 16777619u ^ state().spareKey;
  return static_cast<int>(k);
}
}  // namespace BookFont
