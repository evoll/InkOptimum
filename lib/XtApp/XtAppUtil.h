#pragma once
// Pure helpers for the XtApp host: hashes, deflate and image blitting. No hardware, no Lua.

#include <cstdint>
#include <string>
#include <vector>

namespace xtapp {

struct Canvas;

struct ImageData {
  int w = 0, h = 0;
  std::vector<uint8_t> gray;  // w*h, 0 = black, 255 = white
};

std::string sha1(const std::string& data);    // 20 raw bytes
std::string sha256(const std::string& data);  // 32 raw bytes

// zlib stream (RFC 1950) or raw deflate (RFC 1951), auto-detected from the header. False on corrupt data or when the
// output would exceed maxOut.
bool inflateData(const std::string& in, std::string& out, size_t maxOut);

// Scales `img` to dw x dh at (x, y), nearest neighbour, ordered 4x4 dither to the 1-bit canvas. Every pixel of the
// area is written (white pixels paint white), so an all-white image blanks the area under it.
void blitImage(Canvas& c, const ImageData& img, int x, int y, int dw, int dh);

}  // namespace xtapp
