#pragma once
// Test stub: an in-memory file so TextBlock::serialize/deserialize can round-trip.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

class HalFile {
 public:
  std::vector<uint8_t> data;
  size_t pos = 0;
  size_t write(const uint8_t* buf, size_t n) {
    if (pos + n > data.size()) data.resize(pos + n);
    std::memcpy(data.data() + pos, buf, n);
    pos += n;
    return n;
  }
  int read(void* buf, size_t n) {
    const size_t take = n < data.size() - pos ? n : data.size() - pos;
    std::memcpy(buf, data.data() + pos, take);
    pos += take;
    return static_cast<int>(take);
  }
  int available() const { return static_cast<int>(data.size() - pos); }
  size_t position() const { return pos; }
  bool seek(size_t p) { pos = p; return p <= data.size(); }
  void rewind() { pos = 0; }
};
using FsFile = HalFile;
