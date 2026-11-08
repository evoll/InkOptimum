// RtfFormat.h - recognising RTF books by file name.
#pragma once

#include <string_view>

#include <PathUtil.h>

namespace rtf {

inline bool hasRtfExtension(std::string_view path) { return fictionbook::endsWithIgnoreCase(path, ".rtf"); }

}  // namespace rtf
