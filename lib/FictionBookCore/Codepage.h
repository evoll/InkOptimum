// Codepage.h - single-byte legacy code pages -> Unicode.
//
// Shared by the RTF and MOBI readers (and available to any converter): both
// carry text in Windows code pages and are turned into UTF-8 HTML while the
// book is prepared. Bytes below 0x80 are ASCII in every supported page.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace fictionbook {

// True for 437, 866, 1250, 1251, 1252, 1253, 1254, 1257 and 20866 (KOI8-R).
bool isSupportedCodepage(int codepage);

// Unicode code point of `byte` in `codepage`; 0xFFFD when undefined or the page
// is unsupported. ASCII bytes map to themselves.
uint32_t codepageToUnicode(int codepage, uint8_t byte);

// Appends the UTF-8 form of `codePoint`.
void appendUtf8CodePoint(std::string& out, uint32_t codePoint);

// Charset label ("windows-1251", "cp866", "koi8-r", "iso-8859-1", ...) -> a
// supported code page, or 0 for UTF-8, unknown and unsupported names.
int codepageFromName(std::string_view name);

// Windows character set id (RTF \fcharsetN) -> code page; 0 when unknown/unsupported.
int codepageForCharset(int charset);

}  // namespace fictionbook
