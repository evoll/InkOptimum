// HtmlSource.h
//
// Storage-facing half of the HTML library. Standalone HTML books are read in
// place; only a file in a legacy single-byte code page (or UTF-16) is converted
// once into UTF-8 in the cache. Relative image files are loaded from the
// folder of the user's book.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "HtmlParser.h"

namespace html {

struct PreparedSource {
  std::string bodyPath;  // value for the document's sourcePath
};

// True for HTML files and for the converted body written by this library and by
// the RTF and MOBI readers, i.e. "HtmlParser reads this source".
bool isBodySource(const std::string& sourcePath);

bool prepareSource(const std::string& bookPath, const std::string& cachePath,
                   const std::function<void(int percent)>& onProgress, PreparedSource& out);

// Parser for scanning / rendering / decoding images. `originalBookPath` is the
// user's file (relative <img src> references resolve against its folder).
std::unique_ptr<HtmlParser> makeRenderParser(const std::string& originalBookPath);

}  // namespace html
