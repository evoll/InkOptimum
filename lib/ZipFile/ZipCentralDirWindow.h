#pragma once
// Sequential reader for a ZIP central directory through one buffer.
//
// The walks over the central directory (name lookup, size lookup, listing, stat cache) used to read each
// entry with a 46-byte header read, a name read and a seek, three storage calls an entry, every one
// taking the SD/SPI lock. A big book has thousands of entries, and a first open walks the directory
// several times. This reads the directory in blocks of a few KB and parses the entries from RAM, so a
// walk costs a call per block. Pure C++ over any file type with seek(pos) and read(buf, n) -> int, so
// the rules are covered by native tests with an in-memory file.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

namespace zip_cd {

constexpr size_t kHeaderSize = 46;
constexpr uint32_t kSignature = 0x02014b50;
// Names are only handed out up to this length; a longer name is skipped (the old walks did the same).
constexpr size_t kMaxNameLen = 255;
// The smallest buffer that can hold a header and the longest name handed out.
constexpr size_t kMinBuffer = kHeaderSize + kMaxNameLen;

struct Entry {
  uint16_t method = 0;
  uint32_t compressedSize = 0;
  uint32_t uncompressedSize = 0;
  uint32_t localHeaderOffset = 0;
  uint16_t nameLen = 0;
  // Not NUL-terminated; null when the name is longer than kMaxNameLen. Valid until the next next().
  const char* name = nullptr;
  uint32_t start = 0;      // position of this entry's header in the file
  uint32_t nextStart = 0;  // position of the entry after it
};

// A buffer for the window: 4 KB from the heap when it can be had, else a small one inside the object.
class Buffer {
 public:
  static constexpr size_t kPreferred = 4096;
  static constexpr size_t kFallback = 512;

  Buffer() : heap_(new (std::nothrow) uint8_t[kPreferred]) {}
  uint8_t* data() { return heap_ ? heap_.get() : small_; }
  size_t size() const { return heap_ ? kPreferred : kFallback; }

 private:
  std::unique_ptr<uint8_t[]> heap_;
  uint8_t small_[kFallback];
};

template <class File>
class Window {
 public:
  Window(File& file, uint8_t* buffer, const size_t capacity) : file_(file), buf_(buffer), cap_(capacity) {}

  // Starts (or restarts) the walk at `offset`, the first entry's header.
  void begin(const uint32_t offset) {
    base_ = offset;
    pos_ = have_ = 0;
    cursorValid_ = false;
  }

  // The next entry; false at the end of the directory, on a bad signature or on a read failure.
  bool next(Entry& e) {
    if (cap_ < kMinBuffer) return false;
    if (!ensure(kHeaderSize)) return false;
    const uint8_t* h = buf_ + pos_;
    if (le32(h) != kSignature) return false;

    const uint16_t nameLen = le16(h + 28);
    const uint16_t extraLen = le16(h + 30);
    const uint16_t commentLen = le16(h + 32);
    const bool giveName = nameLen <= kMaxNameLen;
    if (giveName && !ensure(kHeaderSize + nameLen)) return false;
    h = buf_ + pos_;  // ensure() may have moved the data

    e.method = le16(h + 10);
    e.compressedSize = le32(h + 20);
    e.uncompressedSize = le32(h + 24);
    e.localHeaderOffset = le32(h + 42);
    e.nameLen = nameLen;
    e.name = giveName ? reinterpret_cast<const char*>(h + kHeaderSize) : nullptr;
    e.start = base_ + static_cast<uint32_t>(pos_);

    const size_t total = kHeaderSize + nameLen + extraLen + commentLen;
    e.nextStart = e.start + static_cast<uint32_t>(total);
    if (pos_ + total <= have_) {
      pos_ += total;
    } else {
      // The rest of this entry is not in the buffer: continue from the entry after it. The name (if any)
      // was inside the buffer and stays readable until the next call.
      pendingSeek_ = e.nextStart;
      skipPending_ = true;
    }
    return true;
  }

 private:
  static uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
  static uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
  }

  // At least `need` bytes at pos_, reading more when the buffer holds fewer.
  bool ensure(const size_t need) {
    if (skipPending_) {
      base_ = pendingSeek_;
      pos_ = have_ = 0;
      cursorValid_ = false;
      skipPending_ = false;
    }
    if (have_ - pos_ >= need) return true;

    // Keep the unread tail, then top the buffer up from the file.
    if (pos_ > 0) {
      std::memmove(buf_, buf_ + pos_, have_ - pos_);
      base_ += static_cast<uint32_t>(pos_);
      have_ -= pos_;
      pos_ = 0;
    }
    const uint32_t at = base_ + static_cast<uint32_t>(have_);
    if (!cursorValid_ || cursor_ != at) {
      if (!file_.seek(at)) return false;
    }
    const int n = file_.read(buf_ + have_, cap_ - have_);
    if (n <= 0) {
      cursorValid_ = false;
      return have_ - pos_ >= need;
    }
    have_ += static_cast<size_t>(n);
    cursor_ = base_ + static_cast<uint32_t>(have_);
    cursorValid_ = true;
    return have_ - pos_ >= need;
  }

  File& file_;
  uint8_t* buf_;
  size_t cap_;
  uint32_t base_ = 0;   // file position of buf_[0]
  size_t pos_ = 0;      // next unread byte in the buffer
  size_t have_ = 0;     // valid bytes in the buffer
  uint32_t cursor_ = 0; // where the file's read position is, when cursorValid_
  bool cursorValid_ = false;
  bool skipPending_ = false;
  uint32_t pendingSeek_ = 0;
};

}  // namespace zip_cd
