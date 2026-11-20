#pragma once

#include <cstdint>

namespace LetterSpacing {
// Reader letter spacing, 0..3 = 0..3 extra pixels after every glyph of the book text. Like WordSpacing the reader sets
// it before a section is built or loaded (it is part of the section cache key) and TextBlock applies it only while it
// draws book text, so menus and the status bar keep the font's own advances.
inline uint8_t& current() {
  static uint8_t value = 0;
  return value;
}
constexpr uint8_t MAX_VALUE = 3;
inline int pixels() { return current() > MAX_VALUE ? MAX_VALUE : current(); }

// Sets the renderer's letter spacing for the scope and puts the old value back after.
template <class Renderer>
struct Scope {
  const Renderer& renderer;
  int previous;
  Scope(const Renderer& r, const int px) : renderer(r), previous(r.getLetterSpacing()) { r.setLetterSpacing(px); }
  ~Scope() { renderer.setLetterSpacing(previous); }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
};
}  // namespace LetterSpacing
