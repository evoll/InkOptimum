// DocxFormat.h - file-name test for Word documents.
#pragma once

#include <string>

#include <PathUtil.h>

namespace docx {

// ".docx" (ASCII case-insensitive).
inline bool hasDocxExtension(const std::string& path) { return fictionbook::endsWithIgnoreCase(path, ".docx"); }

}  // namespace docx
