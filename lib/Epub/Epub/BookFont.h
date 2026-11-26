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

// Section-cache font key: unchanged unless book fonts are in play.
inline int layoutKey(const int key) {
  return enabled() ? static_cast<int>(static_cast<unsigned>(key) * 16777619u ^ 0xB00F0001u) : key;
}
}  // namespace BookFont
