#include "GifDecoder.h"

#include <cstring>

namespace {
constexpr uint16_t kNoPrefix = 0xFFFF;
}

bool GifDecoder::parseDimensions(const uint8_t* buf, size_t len, int& width, int& height) {
  if (!buf || len < 10) return false;
  if (buf[0] != 'G' || buf[1] != 'I' || buf[2] != 'F') return false;
  const unsigned w = static_cast<unsigned>(buf[6] | (buf[7] << 8));
  const unsigned h = static_cast<unsigned>(buf[8] | (buf[9] << 8));
  if (w == 0 || h == 0 || w > 0x7FFF || h > 0x7FFF) return false;
  width = static_cast<int>(w);
  height = static_cast<int>(h);
  return true;
}

bool GifDecoder::readColorTable(GifSource& src, uint8_t* table, int colors) {
  return src.read(table, colors * 3) == colors * 3;
}

bool GifDecoder::skipSubBlocks(GifSource& src) {
  for (;;) {
    uint8_t len;
    if (src.read(&len, 1) != 1) return false;
    if (len == 0) return true;
    if (!src.skip(len)) return false;
  }
}

void GifDecoder::initLzw(int minCodeSize) {
  minCodeSize_ = minCodeSize;
  clearCode_ = 1 << minCodeSize;
  eoiCode_ = clearCode_ + 1;
  nextCode_ = eoiCode_ + 1;
  codeSize_ = minCodeSize + 1;
  bitBuffer_ = 0;
  bitsInBuffer_ = 0;
  blockPos_ = 0;
  blockLen_ = 0;
  for (int i = 0; i < clearCode_; i++) {
    suffix_[i] = static_cast<uint8_t>(i);
    prefix_[i] = kNoPrefix;
  }
}

int GifDecoder::readByte(GifSource& src) {
  while (blockPos_ >= blockLen_) {
    uint8_t len;
    if (src.read(&len, 1) != 1 || len == 0) return -1;
    if (src.read(block_, len) != len) return -1;
    blockPos_ = 0;
    blockLen_ = len;
  }
  return block_[blockPos_++];
}

int GifDecoder::readCode(GifSource& src) {
  while (bitsInBuffer_ < codeSize_) {
    const int b = readByte(src);
    if (b < 0) return -1;
    bitBuffer_ |= static_cast<uint32_t>(b) << bitsInBuffer_;
    bitsInBuffer_ += 8;
  }
  const int code = static_cast<int>(bitBuffer_ & ((1u << codeSize_) - 1u));
  bitBuffer_ >>= codeSize_;
  bitsInBuffer_ -= codeSize_;
  return code;
}

bool GifDecoder::open(GifSource& src) {
  uint8_t hdr[6];
  if (src.read(hdr, 6) != 6 || hdr[0] != 'G' || hdr[1] != 'I' || hdr[2] != 'F') return false;

  uint8_t lsd[7];
  if (src.read(lsd, 7) != 7) return false;
  screenWidth_ = lsd[0] | (lsd[1] << 8);
  screenHeight_ = lsd[2] | (lsd[3] << 8);
  const bool hasGct = (lsd[4] >> 7) & 1;
  const int gctColors = hasGct ? (2 << (lsd[4] & 0x07)) : 0;
  hasTransparent_ = false;
  transparentIndex_ = 0;
  colors_ = nullptr;
  colorCount_ = 0;

  if (hasGct) {
    if (!readColorTable(src, globalColors_, gctColors)) return false;
    colors_ = globalColors_;
    colorCount_ = gctColors;
  }

  for (;;) {
    uint8_t blockId;
    if (src.read(&blockId, 1) != 1) return false;
    if (blockId == 0x3B) return false;  // trailer, no image

    if (blockId == 0x21) {  // extension
      uint8_t label;
      if (src.read(&label, 1) != 1) return false;
      if (label == 0xF9) {  // graphic control extension
        uint8_t subLen;
        if (src.read(&subLen, 1) != 1) return false;
        uint8_t data[4] = {};
        const int toRead = subLen < 4 ? subLen : 4;
        if (toRead > 0 && src.read(data, toRead) != toRead) return false;
        if (subLen > toRead && !src.skip(static_cast<uint32_t>(subLen - toRead))) return false;
        if (data[0] & 0x01) {
          hasTransparent_ = true;
          transparentIndex_ = data[3];
        }
      }
      if (!skipSubBlocks(src)) return false;
      continue;
    }

    if (blockId == 0x2C) {  // image descriptor
      uint8_t desc[9];
      if (src.read(desc, 9) != 9) return false;
      frameLeft_ = desc[0] | (desc[1] << 8);
      frameTop_ = desc[2] | (desc[3] << 8);
      frameWidth_ = desc[4] | (desc[5] << 8);
      frameHeight_ = desc[6] | (desc[7] << 8);
      interlaced_ = (desc[8] >> 6) & 1;
      const bool hasLct = (desc[8] >> 7) & 1;
      if (hasLct) {
        const int lctColors = 2 << (desc[8] & 0x07);
        if (!readColorTable(src, localColors_, lctColors)) return false;
        colors_ = localColors_;
        colorCount_ = lctColors;
      }
      if (frameWidth_ <= 0 || frameHeight_ <= 0 || !colors_) return false;

      uint8_t lzwMin;
      if (src.read(&lzwMin, 1) != 1 || lzwMin < 2 || lzwMin > 11) return false;
      initLzw(lzwMin);
      return true;
    }

    return false;  // unknown block
  }
}

void GifDecoder::toGray(const uint8_t* indexed, uint8_t* gray, int width) const {
  for (int x = 0; x < width; x++) {
    const uint8_t idx = indexed[x];
    if (hasTransparent_ && idx == transparentIndex_) {
      gray[x] = 255;
    } else if (idx < colorCount_) {
      const uint8_t* p = &colors_[idx * 3];
      gray[x] = static_cast<uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
    } else {
      gray[x] = 0;
    }
  }
}

int GifDecoder::decode(GifSource& src, uint8_t* rowBuf, RowCallback onRow, void* user) {
  static const int kPassStart[] = {0, 4, 2, 1};
  static const int kPassStep[] = {8, 8, 4, 2};

  int pixelX = 0, srcRow = 0, pass = 0;
  int actualY = interlaced_ ? kPassStart[0] : 0;
  bool firstCode = true;
  int oldCode = -1;

  while (srcRow < frameHeight_) {
    const int code = readCode(src);
    if (code < 0) break;

    if (code == clearCode_) {
      nextCode_ = eoiCode_ + 1;
      codeSize_ = minCodeSize_ + 1;
      firstCode = true;
      oldCode = -1;
      continue;
    }
    if (code == eoiCode_) break;

    int top = 0;
    int c = code;

    if (!firstCode && code == nextCode_) {
      // KwKwK: S(old) + first_char(old). Slot 0 is reserved for first_char.
      top = 1;
      c = oldCode;
      while (prefix_[c] != kNoPrefix && top < kMaxCodes - 1) {
        stack_[top++] = suffix_[c];
        c = prefix_[c];
      }
      stack_[top++] = suffix_[c];
      stack_[0] = suffix_[c];
    } else if (code < nextCode_) {
      while (prefix_[c] != kNoPrefix && top < kMaxCodes - 1) {
        stack_[top++] = suffix_[c];
        c = prefix_[c];
      }
      stack_[top++] = suffix_[c];
    } else {
      break;  // code beyond table: corrupt stream
    }

    const uint8_t firstChar = stack_[top - 1];

    if (!firstCode && nextCode_ < kMaxCodes) {
      prefix_[nextCode_] = static_cast<uint16_t>(oldCode);
      suffix_[nextCode_] = firstChar;
      nextCode_++;
      if (nextCode_ >= (1 << codeSize_) && codeSize_ < 12) codeSize_++;
    }
    firstCode = false;
    oldCode = code;

    for (int i = top - 1; i >= 0 && srcRow < frameHeight_; i--) {
      rowBuf[pixelX++] = stack_[i];
      if (pixelX >= frameWidth_) {
        if (!onRow(user, actualY, rowBuf, frameWidth_)) return -1;
        pixelX = 0;
        srcRow++;
        if (interlaced_) {
          actualY += kPassStep[pass];
          while (actualY >= frameHeight_ && pass < 3) actualY = kPassStart[++pass];
        } else {
          actualY++;
        }
      }
    }
  }

  return srcRow;
}
