#include "VerticalDraw.h"

#include <GfxRenderer.h>

#include <cstring>

#include "VerticalOrientation.h"

int columnEm(const GfxRenderer& renderer, const int fontId, const EpdFontFamily::Style style) {
  if (renderer.hasGlyph(fontId, kColumnEmCodepoint, style)) {
    const int em = renderer.getTextAdvanceX(fontId, kColumnEmUtf8, style);
    if (em > 0) return em;
  }
  return renderer.getFontAscenderSize(fontId);
}

void drawTextVertical(const GfxRenderer& renderer, const int fontId, const int x, const int y, const char* text,
                      const EpdFontFamily::Style style, const bool black) {
  if (renderer.isFontCacheScanning()) {
    // Record 一 in this style too: the draw below measures with it, and an unprewarmed glyph is a miss on SD fonts.
    (void)renderer.hasGlyph(fontId, kColumnEmCodepoint, style);
    renderer.drawText(fontId, x, y, text, black, style);
    return;
  }
  const GfxRenderer::Orientation reader = renderer.getHeldOrientation();
  const GfxRenderer::Orientation content = renderer.getOrientation();
  const int readerWidth = renderer.getScreenWidth(reader);
  const int ascender = renderer.getFontAscenderSize(fontId);
  const int em = columnEm(renderer, fontId, style);
  const int axis = columnAxis(y + ascender, em);

  char piece[kMaxColumnRunBytes + 1];
  int pen = x;
  bool upright = false;
  while (const size_t length = nextColumnPiece(text, upright)) {
    memcpy(piece, text, length);
    piece[length] = '\0';
    text += length;
    const int advance = renderer.getTextAdvanceX(fontId, piece, style);
    if (!upright) {
      renderer.drawText(fontId, pen, y, piece, black, style);
    } else {
      // The column's axis in the turned frame is reader x = readerWidth - 1 - axis; the em box's top sits at the pen.
      renderer.setDrawOrientation(reader);
      renderer.drawText(fontId, readerWidth - 1 - axis - advance / 2, pen + (em * 88 + 50) / 100 - ascender, piece,
                        black, style);
      renderer.setDrawOrientation(content);
    }
    pen += advance;
  }
}
