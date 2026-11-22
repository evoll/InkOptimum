#pragma once
#include <GfxRenderer.h>

// The font h1-h3 headings are laid out and drawn in (a larger size of the reading family), set by the SD font
// system whenever it loads or drops it. 0 = none: headings use the body font as before.
namespace HeadingFont {
inline int& storage() {
  static int id = 0;
  return id;
}
inline void set(const int fontId) { storage() = fontId; }
inline int get() { return storage(); }

// The font a heading block uses: the heading font while it is loaded, else the body font.
inline int resolve(const GfxRenderer& renderer, const int bodyFontId) {
  const int id = storage();
  return (id != 0 && renderer.getFontMap().count(id) != 0) ? id : bodyFontId;
}

// What the section cache records instead of the plain font id, so that loading or dropping the heading font
// invalidates the cached layout.
inline int layoutKey(const int bodyFontId) {
  const int id = storage();
  return id == 0 ? bodyFontId : static_cast<int>(static_cast<unsigned>(bodyFontId) * 31u ^ static_cast<unsigned>(id));
}
}  // namespace HeadingFont
