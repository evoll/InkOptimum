#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

// Platform-free path policy for the web file manager.
//
// "Show hidden files" only controls what the file browser lists; it must never make files with
// credentials downloadable. Anyone on the same network can reach the web server, and the stored
// Wi-Fi / OPDS / KOReader passwords are only lightly obfuscated, so these files are refused for
// every operation (list, download, upload, rename, delete, move) regardless of any setting.
namespace web_access {

namespace detail {

inline std::string lower(std::string_view s) {
  std::string out(s);
  for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

inline bool startsWith(const std::string& s, std::string_view prefix) {
  return s.size() >= prefix.size() && std::string_view(s).substr(0, prefix.size()) == prefix;
}

// Split on '/' or '\\' (FAT accepts both through some clients) and resolve "." / ".." so
// "/x/../.inkmod/wifi.json" cannot slip past a literal comparison.
inline std::vector<std::string> segments(std::string_view path) {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&] {
    if (cur.empty() || cur == ".") {
      cur.clear();
      return;
    }
    if (cur == "..") {
      if (!out.empty()) out.pop_back();
    } else {
      out.push_back(lower(cur));
    }
    cur.clear();
  };
  for (const char c : path) {
    if (c == '/' || c == '\\') flush();
    else cur.push_back(c);
  }
  flush();
  return out;
}

// FAT also ignores trailing dots/spaces and exposes 8.3 aliases ("WIFI~1.JSO"); treat both as
// possible spellings of a secret file name.
inline std::string stripTrailingDotsAndSpaces(std::string s) {
  while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
  return s;
}

}  // namespace detail

// True for the settings directory and for any file inside it that can hold credentials.
inline bool isSecretPath(std::string_view path) {
  const auto segs = detail::segments(path);
  for (size_t i = 0; i < segs.size(); ++i) {
    if (detail::stripTrailingDotsAndSpaces(segs[i]) != ".inkmod") continue;
    if (i + 1 >= segs.size()) return false;  // the folder itself is not a secret (listing is filtered)
    const std::string name = detail::stripTrailingDotsAndSpaces(segs[i + 1]);
    static constexpr std::string_view kSecretPrefixes[] = {"wifi",    "koreader", "kosync", "opds",
                                                           "settings", "inkmod-settings", "lcp"};
    for (const auto prefix : kSecretPrefixes) {
      if (detail::startsWith(name, prefix)) return true;
    }
    // 8.3 aliases of long names (e.g. "wifi~1.jso", "inkmod~1.jso") and anything unknown with ~N.
    if (name.find('~') != std::string::npos) return true;
  }
  return false;
}

}  // namespace web_access
