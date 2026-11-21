#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Reading seconds per calendar day, newest MAX_DAYS days kept. Days are absolute day indices (the same numbering
// as readingStatsDayIndex()); the class knows nothing about calendars or storage.
class DailyReadingSeconds {
 public:
  struct Entry {
    uint32_t dayIndex;
    uint32_t seconds;
  };

  static constexpr size_t MAX_DAYS = 400;
  static constexpr uint32_t SECONDS_PER_DAY = 24u * 3600u;
  static constexpr uint8_t FILE_VERSION = 1;

  // Adds `seconds` of reading that began at `secondOfDay` (0..86399) of `dayIndex`; a span that crosses midnight is
  // split between the days it covers.
  void record(uint32_t dayIndex, uint32_t secondOfDay, uint32_t seconds);

  uint32_t secondsFor(uint32_t dayIndex) const;
  // Sum over the inclusive range [fromDay, toDay].
  uint32_t secondsBetween(uint32_t fromDay, uint32_t toDay) const;
  const std::vector<Entry>& entries() const { return entries_; }  // ascending by day
  bool empty() const { return entries_.empty(); }

  // File image: version u8, count u16, then count x (dayIndex u32, seconds u32), little endian.
  std::vector<uint8_t> serialize() const;
  static bool deserialize(const uint8_t* data, size_t size, DailyReadingSeconds& out);

 private:
  void add(uint32_t dayIndex, uint32_t seconds);
  std::vector<Entry> entries_;
};
