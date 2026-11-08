#pragma once

#include <EpdFontFamily.h>

#include <cstdint>

class GfxRenderer;

// The glyph a column is measured against: 一, and its UTF-8.
constexpr uint32_t kColumnEmCodepoint = 0x4E00;
constexpr char kColumnEmUtf8[] = "\xE4\xB8\x80";

// The em of vertical drawing for fontId: the advance of 一, else the ascender (a font without CJK).
int columnEm(const GfxRenderer& renderer, int fontId, EpdFontFamily::Style style);

// A column's centre line and its over edge (right of the column), from the line's baseline: the ideographic em box
// sits 880/1000 above the baseline and 120 below.
constexpr int columnAxis(const int baseline, const int em) { return baseline - (em * 38 + 50) / 100; }
constexpr int columnOverEdge(const int baseline, const int em) { return baseline - (em * 88 + 50) / 100; }

// Draws `text` down a column. Called in the turned frame (Page::render's scope) with what drawText would get there:
// x the pen along the column, y the line's top. Upright characters (VerticalOrientation.h) are drawn in the reader's
// frame, centred on the column; sideways runs through the turned frame, as the horizontal code draws them. Each piece
// advances by its own horizontal advance, which is what layout measured. Allocates nothing: one char buffer of
// kMaxColumnRunBytes + 1 on the stack.
void drawTextVertical(const GfxRenderer& renderer, int fontId, int x, int y, const char* text,
                      EpdFontFamily::Style style, bool black = true);
