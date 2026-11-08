#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Minimal byte source the GIF decoder pulls from. Keeps lib/GifDecoder free of
// any storage/Arduino dependency: firmware adapts FsFile, tests use memory.
class GifSource {
 public:
  virtual ~GifSource() = default;
  // Reads up to `len` bytes, returns the number actually read (0 on EOF/error).
  virtual int read(uint8_t* buf, int len) = 0;
  // Skips `len` bytes forward. Returns false when the source ends first.
  virtual bool skip(uint32_t len) = 0;
};

class MemoryGifSource final : public GifSource {
 public:
  MemoryGifSource(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  int read(uint8_t* buf, int len) override {
    if (len <= 0 || pos_ >= size_) return 0;
    const size_t n = (size_ - pos_) < static_cast<size_t>(len) ? (size_ - pos_) : static_cast<size_t>(len);
    memcpy(buf, data_ + pos_, n);
    pos_ += n;
    return static_cast<int>(n);
  }

  bool skip(uint32_t len) override {
    if (len > size_ - pos_) {
      pos_ = size_;
      return false;
    }
    pos_ += len;
    return true;
  }

 private:
  const uint8_t* data_;
  size_t size_;
  size_t pos_ = 0;
};
