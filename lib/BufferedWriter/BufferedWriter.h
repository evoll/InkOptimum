#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Collects many small writes into one block write. A cache record is written a field at a time
// (a byte here, a 2-byte length there), and each of those is a storage call; through this writer the
// same bytes reach the card in blocks of N. The bytes written are identical.
//
// `File` needs `size_t write(const uint8_t*, size_t)` returning the number of bytes written (FsFile
// and HalFile do). A failed or short write makes the writer fail for good: every later write returns
// false and flush() returns false, so a caller may check once at the end.
template <class File, size_t N = 1024>
class BufferedWriter {
 public:
  explicit BufferedWriter(File& file) : file_(file) {}
  BufferedWriter(const BufferedWriter&) = delete;
  BufferedWriter& operator=(const BufferedWriter&) = delete;

  bool write(const void* data, size_t len) {
    if (!ok_) return false;
    const auto* p = static_cast<const uint8_t*>(data);
    while (len > 0) {
      if (used_ == 0 && len >= N) {  // a large block goes straight through
        return passThrough(p, len);
      }
      const size_t room = N - used_;
      const size_t take = len < room ? len : room;
      std::memcpy(buf_ + used_, p, take);
      used_ += take;
      p += take;
      len -= take;
      if (used_ == N && !flush()) return false;
    }
    return true;
  }

  bool writeByte(const uint8_t value) { return write(&value, 1); }

  // Writes what is buffered. Call once at the end; the destructor does not, because a failure there
  // could not be reported.
  bool flush() {
    if (!ok_) return false;
    if (used_ == 0) return true;
    const size_t n = used_;
    used_ = 0;
    return passThrough(buf_, n);
  }

  bool ok() const { return ok_; }

 private:
  bool passThrough(const uint8_t* p, const size_t len) {
    if (file_.write(p, len) != len) ok_ = false;
    return ok_;
  }

  File& file_;
  uint8_t buf_[N];
  size_t used_ = 0;
  bool ok_ = true;
};
