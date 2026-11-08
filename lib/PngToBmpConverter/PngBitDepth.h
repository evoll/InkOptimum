#pragma once

#include <cstdint>

// PNG spec table 11.1: the bit depths each colour type allows. Any other pairing is a corrupt IHDR
// that the row unpacker cannot handle: a grayscale/palette depth of 3/5/6/7 indexes past the end of
// the row buffer, and a sub-byte path with a depth above 8 (grayscale 9-15, palette 16) makes
// pixelsPerByte = 8 / bitDepth zero and divides by it. RISC-V does not trap that division, so a
// corrupt image read memory it did not own instead of being rejected.
// From witchhunt-reader a35e1a48 (crosspoint-reader PR #3979, phucisstupid).
namespace png {

inline constexpr bool isValidBitDepth(const uint8_t colorType, const uint8_t bitDepth) {
  switch (colorType) {
    case 0:  // grayscale
      return bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8 || bitDepth == 16;
    case 3:  // palette
      return bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8;
    case 2:  // RGB
    case 4:  // grayscale + alpha
    case 6:  // RGBA
      return bitDepth == 8 || bitDepth == 16;
    default:
      return false;
  }
}

}  // namespace png
