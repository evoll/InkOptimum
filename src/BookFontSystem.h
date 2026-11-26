#pragma once

#include <cstdint>

class Epub;
class GfxRenderer;

// Fonts embedded in the open EPUB (@font-face, TTF/OTF only) for PSRAM boards. attach() reads the book's
// @font-face table (written by the CSS parser), and BookFont::resolve() then loads a family on first use at the
// reader's point size. Off unless SETTINGS.publisherFonts is set and the book embeds at least one usable font.
namespace BookFontSystem {
// Call after the Epub is loaded (its CSS is parsed then) and its cache dir exists.
void attach(const Epub& epub, GfxRenderer& renderer);
// Drops every loaded book font and switches BookFont off. Safe to call repeatedly.
void detach();
}  // namespace BookFontSystem
