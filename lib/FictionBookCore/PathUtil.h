// PathUtil.h - tiny path helpers shared by the FictionBook-family libraries.
#pragma once

#include <string_view>

namespace fictionbook {

// ASCII case-insensitive suffix test ("Book.FB3" ends with ".fb3").
inline bool endsWithIgnoreCase(std::string_view text, std::string_view suffix) {
  if (text.size() < suffix.size()) return false;
  const size_t offset = text.size() - suffix.size();
  for (size_t i = 0; i < suffix.size(); ++i) {
    char a = text[offset + i];
    char b = suffix[i];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

}  // namespace fictionbook
