#pragma once

#include <cstddef>
#include <cstdint>

#include "GifSource.h"

// Streaming decoder for the first frame of a GIF87a/GIF89a image.
//
// Rows are delivered one at a time through a callback so callers never hold the
// whole picture in memory (the ESP32-C3 has no PSRAM). Interlaced images are
// delivered in storage order together with the display-order row index.
// Transparent pixels are rendered as white; animation frames after the first
// are ignored.
//
// The object is ~18 KB: allocate it on the heap (new (std::nothrow)).
class GifDecoder {
 public:
  static constexpr int kMaxCodes = 4096;

  // Receives each decoded row: (user, displayRowY, indexedPixels, width).
  // Return false to abort decoding.
  using RowCallback = bool (*)(void* user, int y, const uint8_t* indexed, int width);

  // Parses the 10-byte header prefix ("GIF" + version + logical screen size).
  static bool parseDimensions(const uint8_t* buf, size_t len, int& width, int& height);

  // Reads header, colour table and extensions up to the first image
  // descriptor and prepares LZW decoding. Returns false for truncated or
  // unsupported data.
  bool open(GifSource& src);

  int frameLeft() const { return frameLeft_; }
  int frameTop() const { return frameTop_; }
  int frameWidth() const { return frameWidth_; }
  int frameHeight() const { return frameHeight_; }
  int screenWidth() const { return screenWidth_; }
  int screenHeight() const { return screenHeight_; }
  bool interlaced() const { return interlaced_; }

  // Decodes the frame, invoking `onRow` per completed row. `rowBuf` must hold
  // frameWidth() bytes. Returns the number of rows delivered, or -1 when the
  // callback aborted.
  int decode(GifSource& src, uint8_t* rowBuf, RowCallback onRow, void* user);

  // Converts palette indices of the current frame to 8-bit luma (255 = white).
  void toGray(const uint8_t* indexed, uint8_t* gray, int width) const;

 private:
  bool readColorTable(GifSource& src, uint8_t* table, int colors);
  bool skipSubBlocks(GifSource& src);
  int readByte(GifSource& src);
  int readCode(GifSource& src);
  void initLzw(int minCodeSize);

  uint16_t prefix_[kMaxCodes];
  uint8_t suffix_[kMaxCodes];
  uint8_t stack_[kMaxCodes];

  uint8_t globalColors_[256 * 3];
  uint8_t localColors_[256 * 3];
  const uint8_t* colors_ = nullptr;
  int colorCount_ = 0;

  int screenWidth_ = 0, screenHeight_ = 0;
  int frameLeft_ = 0, frameTop_ = 0, frameWidth_ = 0, frameHeight_ = 0;
  bool interlaced_ = false;

  bool hasTransparent_ = false;
  uint8_t transparentIndex_ = 0;

  int minCodeSize_ = 0, clearCode_ = 0, eoiCode_ = 0, nextCode_ = 0, codeSize_ = 0;
  uint32_t bitBuffer_ = 0;
  int bitsInBuffer_ = 0;

  uint8_t block_[256];
  int blockPos_ = 0, blockLen_ = 0;
};
