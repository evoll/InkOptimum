#include "DailyReadingSeconds.h"

#include <algorithm>

void DailyReadingSeconds::add(const uint32_t dayIndex, const uint32_t seconds) {
  auto it = std::lower_bound(entries_.begin(), entries_.end(), dayIndex,
                             [](const Entry& e, const uint32_t d) { return e.dayIndex < d; });
  if (it != entries_.end() && it->dayIndex == dayIndex) {
    it->seconds = std::min(SECONDS_PER_DAY, it->seconds + seconds);
  } else {
    entries_.insert(it, Entry{dayIndex, std::min(SECONDS_PER_DAY, seconds)});
  }
  if (entries_.size() > MAX_DAYS) {
    entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(entries_.size() - MAX_DAYS));
  }
}

void DailyReadingSeconds::record(uint32_t dayIndex, uint32_t secondOfDay, uint32_t seconds) {
  if (seconds == 0 || secondOfDay >= SECONDS_PER_DAY) return;
  while (seconds > 0) {
    const uint32_t untilMidnight = SECONDS_PER_DAY - secondOfDay;
    const uint32_t segment = std::min(seconds, untilMidnight);
    add(dayIndex, segment);
    seconds -= segment;
    dayIndex++;
    secondOfDay = 0;
  }
}

uint32_t DailyReadingSeconds::secondsFor(const uint32_t dayIndex) const {
  auto it = std::lower_bound(entries_.begin(), entries_.end(), dayIndex,
                             [](const Entry& e, const uint32_t d) { return e.dayIndex < d; });
  return (it != entries_.end() && it->dayIndex == dayIndex) ? it->seconds : 0;
}

uint32_t DailyReadingSeconds::secondsBetween(const uint32_t fromDay, const uint32_t toDay) const {
  uint32_t sum = 0;
  for (const auto& e : entries_) {
    if (e.dayIndex >= fromDay && e.dayIndex <= toDay) sum += e.seconds;
  }
  return sum;
}

static void put32(std::vector<uint8_t>& v, const uint32_t x) {
  for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
static uint32_t get32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

std::vector<uint8_t> DailyReadingSeconds::serialize() const {
  std::vector<uint8_t> v;
  v.reserve(3 + entries_.size() * 8);
  v.push_back(FILE_VERSION);
  v.push_back(static_cast<uint8_t>(entries_.size() & 0xFF));
  v.push_back(static_cast<uint8_t>((entries_.size() >> 8) & 0xFF));
  for (const auto& e : entries_) {
    put32(v, e.dayIndex);
    put32(v, e.seconds);
  }
  return v;
}

bool DailyReadingSeconds::deserialize(const uint8_t* data, const size_t size, DailyReadingSeconds& out) {
  out.entries_.clear();
  if (data == nullptr || size < 3 || data[0] != FILE_VERSION) return false;
  const size_t count = static_cast<size_t>(data[1]) | (static_cast<size_t>(data[2]) << 8);
  if (count > MAX_DAYS || size != 3 + count * 8) return false;
  uint32_t prev = 0;
  for (size_t i = 0; i < count; ++i) {
    const uint8_t* p = data + 3 + i * 8;
    const Entry e{get32(p), get32(p + 4)};
    if ((i > 0 && e.dayIndex <= prev) || e.seconds > SECONDS_PER_DAY) {
      out.entries_.clear();
      return false;
    }
    prev = e.dayIndex;
    out.entries_.push_back(e);
  }
  return true;
}
