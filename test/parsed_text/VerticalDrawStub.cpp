// The real VerticalDraw needs the device renderer's turned-frame API; these tests draw horizontal lines only.
#include <GfxRenderer.h>

#include "Epub/VerticalDraw.h"

int columnEm(const GfxRenderer&, int, EpdFontFamily::Style) { return 0; }

void drawTextVertical(const GfxRenderer& renderer, const int fontId, const int x, const int y, const char* text,
                      const EpdFontFamily::Style style, const bool black) {
  renderer.drawText(fontId, x, y, text, black, style);
}
