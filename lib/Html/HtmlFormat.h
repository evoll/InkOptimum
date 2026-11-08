// HtmlFormat.h - recognising HTML books by file name, and the conventions the
// converting readers (RTF, MOBI/AZW3) use to feed HtmlParser.
#pragma once

#include <string_view>

#include <PathUtil.h>

namespace html {

// ".html", ".htm" or ".xhtml" (ASCII case-insensitive).
inline bool hasHtmlExtension(std::string_view path) {
  return fictionbook::endsWithIgnoreCase(path, ".html") || fictionbook::endsWithIgnoreCase(path, ".htm") ||
         fictionbook::endsWithIgnoreCase(path, ".xhtml");
}

// Name of the normalised HTML body the RTF and MOBI converters write into the
// book cache. HtmlParser reads it exactly like a user's .html file; only the
// images differ (they stay inside the original book, see HtmlParser.h).
inline constexpr char kConvertedBodyName[] = "/.converted_body.html";

inline bool isConvertedBody(std::string_view path) {
  return fictionbook::endsWithIgnoreCase(path, kConvertedBodyName);
}

}  // namespace html
