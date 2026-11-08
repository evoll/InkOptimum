// MobiSource.h
//
// Storage-facing half of the MOBI/AZW3 library: decompresses the book into a
// scratch file, converts it into the normalised HTML body
// (html::kConvertedBodyName) that HtmlParser reads, and removes the scratch
// file. Images stay in the user's book and are decoded on demand from their
// record ranges.
#pragma once

#include <functional>
#include <string>

namespace mobi {

struct PreparedSource {
  std::string bodyPath;  // value for the document's sourcePath
  bool drm = false;      // the book is DRM-protected (nothing was written)
};

bool prepareSource(const std::string& bookPath, const std::string& cachePath,
                   const std::function<void(int percent)>& onProgress, PreparedSource& out);

}  // namespace mobi
