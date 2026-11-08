#pragma once
// Host stand-in: ZipFile.cpp only needs the ESP heap counters for log lines.
#include <cstdint>
struct EspStub {
  uint32_t getFreeHeap() const { return 0; }
  uint32_t getMaxAllocHeap() const { return 0; }
};
inline EspStub ESP;
