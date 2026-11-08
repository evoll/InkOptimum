// HtmlEntities.h - character reference decoding for the HTML reader.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace html {

// Appends the UTF-8 encoding of `codePoint` (invalid values become U+FFFD).
void appendUtf8(std::string& out, uint32_t codePoint);

// Resolves a named entity ("amp", "nbsp", ...) to a code point; 0 if unknown.
uint32_t lookupNamedEntity(std::string_view name);

// Decodes every character reference in `text` in place-equivalent fashion;
// unknown or malformed references are kept literally.
std::string decodeEntities(std::string_view text);

}  // namespace html
