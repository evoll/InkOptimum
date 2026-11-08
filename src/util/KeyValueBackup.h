#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Tiny text format for backing up groups of small integer settings: one "key=value" per line.
// Platform-free so it can be unit tested natively.
namespace kv_backup {

inline std::string serialize(const std::vector<std::pair<std::string, uint8_t>>& entries) {
  std::string out = "# inkmod button settings v1\n";
  for (const auto& e : entries) {
    out += e.first;
    out += '=';
    out += std::to_string(static_cast<unsigned>(e.second));
    out += '\n';
  }
  return out;
}

// Parses what serialize() wrote. Comments, blank lines, malformed lines and out-of-range numbers
// are skipped, so a damaged file can never inject a value.
inline std::vector<std::pair<std::string, uint8_t>> parse(std::string_view text) {
  std::vector<std::pair<std::string, uint8_t>> out;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(pos, end - pos);
    pos = end + 1;

    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
    if (line.empty() || line.front() == '#') continue;
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos || eq == 0 || eq + 1 >= line.size()) continue;

    const std::string key(line.substr(0, eq));
    const std::string num(line.substr(eq + 1));
    char* endPtr = nullptr;
    const long v = strtol(num.c_str(), &endPtr, 10);
    if (endPtr == num.c_str() || *endPtr != '\0' || v < 0 || v > 255) continue;
    out.emplace_back(key, static_cast<uint8_t>(v));
  }
  return out;
}

}  // namespace kv_backup
