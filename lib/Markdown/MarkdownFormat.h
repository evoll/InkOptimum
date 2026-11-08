// MarkdownFormat.h - recognising Markdown books by file name.
#pragma once

#include <string_view>

#include <PathUtil.h>

namespace markdown {

// ".md" or ".markdown" (ASCII case-insensitive).
inline bool hasMarkdownExtension(std::string_view path) {
  return fictionbook::endsWithIgnoreCase(path, ".md") || fictionbook::endsWithIgnoreCase(path, ".markdown");
}

}  // namespace markdown
