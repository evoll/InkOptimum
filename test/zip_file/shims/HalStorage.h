#pragma once
// Host stand-in for the SD-card HAL: a HalFile over a stdio file that counts calls, as the lock-taking
// SD calls would be counted on the device.
#include <cstdint>
#include <cstdio>
#include <string>

#include "Print.h"

inline int g_halCalls = 0;

class HalFile {
 public:
  HalFile() = default;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  ~HalFile() { close(); }

  bool open(const char* path) {
    f_ = std::fopen(path, "rb");
    return f_ != nullptr;
  }
  bool isOpen() const { return f_ != nullptr; }
  operator bool() const { return f_ != nullptr; }
  bool close() {
    if (f_) std::fclose(f_);
    f_ = nullptr;
    return true;
  }
  size_t size() {
    ++g_halCalls;
    const long here = std::ftell(f_);
    std::fseek(f_, 0, SEEK_END);
    const long end = std::ftell(f_);
    std::fseek(f_, here, SEEK_SET);
    return static_cast<size_t>(end);
  }
  bool seek(size_t pos) {
    ++g_halCalls;
    return std::fseek(f_, static_cast<long>(pos), SEEK_SET) == 0;
  }
  bool seek64(uint64_t pos) { return seek(static_cast<size_t>(pos)); }
  bool seekCur(int64_t offset) {
    ++g_halCalls;
    return std::fseek(f_, static_cast<long>(offset), SEEK_CUR) == 0;
  }
  size_t position() const { return static_cast<size_t>(std::ftell(f_)); }
  int read(void* buf, size_t count) {
    ++g_halCalls;
    return static_cast<int>(std::fread(buf, 1, count, f_));
  }

 private:
  FILE* f_ = nullptr;
};

class HalStorage {
 public:
  bool openFileForRead(const char*, const std::string& path, HalFile& file) { return file.open(path.c_str()); }
};

inline HalStorage Storage;
