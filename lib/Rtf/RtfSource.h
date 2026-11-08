// RtfSource.h
//
// Storage-facing half of the RTF library: converts a .rtf on the SD card, once,
// into the normalised HTML body (html::kConvertedBodyName) that HtmlParser reads.
// Pictures stay in the user's .rtf and are decoded on demand from their hex ranges.
#pragma once

#include <functional>
#include <string>

namespace rtf {

struct PreparedSource {
  std::string bodyPath;  // value for the document's sourcePath
};

bool prepareSource(const std::string& bookPath, const std::string& cachePath,
                   const std::function<void(int percent)>& onProgress, PreparedSource& out);

}  // namespace rtf
