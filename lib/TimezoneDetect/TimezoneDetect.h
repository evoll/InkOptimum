#pragma once

#include <cstdint>
#include <cstdlib>
#include <string_view>

// Turns the reply of an IP-geolocation service into the clock's stored UTC offset.
//
// The clock keeps its offset as quarter hours biased by 48 (0 = UTC-12:00, 48 = UTC+00:00,
// 104 = UTC+14:00). The service used is ip-api.com with `?fields=status,offset`, which answers
//   {"status":"success","offset":7200}
// where `offset` is the zone's *current* offset in seconds, daylight saving included. The device
// has no DST table, so the offset is a snapshot: run the detection again after a clock change.
namespace timezone_detect {

constexpr int kBiasQuarters = 48;
constexpr int kMinQuarters = -48;  // UTC-12:00
constexpr int kMaxQuarters = 56;   // UTC+14:00

// Returns true and sets `biased` when `json` holds a successful reply with a usable offset.
inline bool parseOffsetQuarters(std::string_view json, uint8_t& biased) {
  // A failed lookup says {"status":"fail",...}; never trust an offset next to it.
  const auto status = json.find("\"status\"");
  if (status != std::string_view::npos && json.find("\"success\"", status) == std::string_view::npos) return false;

  const auto key = json.find("\"offset\"");
  if (key == std::string_view::npos) return false;
  size_t i = json.find(':', key);
  if (i == std::string_view::npos) return false;
  ++i;
  while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) ++i;

  bool negative = false;
  if (i < json.size() && (json[i] == '-' || json[i] == '+')) {
    negative = json[i] == '-';
    ++i;
  }
  if (i >= json.size() || json[i] < '0' || json[i] > '9') return false;
  long seconds = 0;
  while (i < json.size() && json[i] >= '0' && json[i] <= '9') {
    seconds = seconds * 10 + (json[i] - '0');
    if (seconds > 100000) return false;
    ++i;
  }
  if (negative) seconds = -seconds;

  // Real-world offsets are whole quarter hours.
  if (seconds % 900 != 0) return false;
  const int quarters = static_cast<int>(seconds / 900);
  if (quarters < kMinQuarters || quarters > kMaxQuarters) return false;
  biased = static_cast<uint8_t>(quarters + kBiasQuarters);
  return true;
}

}  // namespace timezone_detect
