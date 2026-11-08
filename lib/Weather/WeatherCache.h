#pragma once

#include <cstdlib>
#include <ctime>
#include <string>

// Cache file layout: "<signature>\n<fetchedAt>\n<raw Open-Meteo JSON>".
// Keeping the raw response means the same parser serves live and cached data.
namespace weather {

inline std::string wrapCache(const std::string& signature, time_t fetchedAt, const std::string& json) {
  return signature + "\n" + std::to_string(static_cast<long long>(fetchedAt)) + "\n" + json;
}

inline bool unwrapCache(const std::string& text, std::string& signature, time_t& fetchedAt, std::string& json) {
  const size_t a = text.find('\n');
  if (a == std::string::npos) return false;
  const size_t b = text.find('\n', a + 1);
  if (b == std::string::npos) return false;

  const std::string stamp = text.substr(a + 1, b - a - 1);
  char* end = nullptr;
  const long long v = strtoll(stamp.c_str(), &end, 10);
  if (stamp.empty() || *end != '\0' || v <= 0) return false;

  signature = text.substr(0, a);
  fetchedAt = static_cast<time_t>(v);
  json = text.substr(b + 1);
  return !json.empty();
}

// Cached data older than this is shown but flagged as stale.
inline constexpr long CACHE_TTL_SECONDS = 30 * 60;

inline bool isFresh(time_t fetchedAt, time_t now) {
  return now >= fetchedAt && (now - fetchedAt) < CACHE_TTL_SECONDS;
}

}  // namespace weather
