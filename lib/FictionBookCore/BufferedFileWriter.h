// BufferedFileWriter.h
//
// Coalesces the many small writes produced while extracting/indexing a book
// into 4 KiB SD-card writes. If the buffer cannot be allocated it transparently
// falls back to direct writes. Shared by the FB2 document class and the FB3
// cache preparation.
#pragma once

#include <HalStorage.h>
#include <Print.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

class BufferedFileWriter final : public Print {
 public:
  explicit BufferedFileWriter(HalFile& file, size_t capacity = 4096)
      : file_(file), capacity_(capacity), buffer_(new (std::nothrow) uint8_t[capacity]) {}
  ~BufferedFileWriter() override { flushBuffer(); }
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t* data, size_t length) override {
    if (failed_) return 0;
    if (!buffer_) {
      const size_t written = file_.write(data, length);
      failed_ = written != length;
      return written;
    }
    size_t accepted = 0;
    while (accepted < length) {
      if (used_ == capacity_ && !flushBuffer()) return accepted;
      const size_t chunk = std::min(length - accepted, capacity_ - used_);
      memcpy(buffer_.get() + used_, data + accepted, chunk);
      used_ += chunk;
      accepted += chunk;
    }
    return accepted;
  }
  // Flushes and reports whether every write so far succeeded.
  bool finish() { return flushBuffer() && !failed_; }

 private:
  bool flushBuffer() {
    if (failed_) return false;
    if (used_ == 0) return true;
    const size_t written = file_.write(buffer_.get(), used_);
    if (written != used_) {
      failed_ = true;
      return false;
    }
    used_ = 0;
    return true;
  }
  HalFile& file_;
  size_t capacity_;
  std::unique_ptr<uint8_t[]> buffer_;
  size_t used_ = 0;
  bool failed_ = false;
};
