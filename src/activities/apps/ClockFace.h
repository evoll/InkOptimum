#pragma once

class GfxRenderer;

// The clock faces, drawn with rectangles and lines only (no fonts for the digits). Shared by the Clock app and by the
// power-saving standby, which paints the same frame from a minimal boot without any activity.
namespace ClockFace {

constexpr int kFaceCount = 3;  // 0 big digits, 1 digits with the date, 2 analog

// Draws one face into [areaTop, areaTop + areaHeight) over the full width. `date` may be null (the date line is left
// out); the caller clears the screen first and calls displayBuffer afterwards. Faces 1 and 2 use the UI 12 / UI 10
// fonts for the date.
void draw(GfxRenderer& renderer, int face, bool use12h, int hour24, int minute, const char* date, int areaTop,
          int areaHeight);

}  // namespace ClockFace
