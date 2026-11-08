// Fb3Source.h
//
// Storage-facing half of the FB3 library: turns an FB3 book on the SD card into
// the cache files that Fb3Parser works from, and locates them again later.
// (Package structure and parsing are platform-free and live in Fb3Package.h /
// Fb3Parser.h; this unit is the only part that touches HalStorage.)
//
// Cache layout, inside the book's cache directory:
//
//   .source.fb3body   the body XML part, inflated once (a ZIP member cannot be
//                     seeked inside; FB2.ZIP has the same extract-once step)
//   .source.fb3desc   the description XML (annotation page)
//   .fb3_cover        the cover's binary id (only when the book has a cover)
//   .package.fb3      a ".fb3.zip" wrapper's inner package, kept because image
//                     bytes are inflated from it on demand (absent otherwise)
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "Fb3Package.h"
#include "Fb3Parser.h"

namespace fb3 {

struct PreparedSource {
  std::string bodyPath;       // value for the document's sourcePath
  bool wrapped = false;       // the book was a ZIP wrapping a .fb3
  uint32_t imageCount = 0;
  bool hasCover = false;
};

// ".fb3" (ASCII case-insensitive). A ".fb3.zip" is a ZIP: callers route it
// through their ZIP handling and fall back to prepareSource().
bool hasFb3Extension(const std::string& path);

// True for the body file prepareSource() writes (what the document keeps as its
// sourcePath), i.e. "this book is FB3".
bool isBodySource(const std::string& sourcePath);

// Accepts: a plain package, an FB3 package renamed to .zip, or a ZIP wrapping a
// .fb3. Writes the cache files above into `cachePath` (which must exist).
bool prepareSource(const std::string& bookPath, const std::string& cachePath,
                   const std::function<void(int percent)>& onProgress, PreparedSource& out);

// File that holds the book's images: the staged inner package when the book was
// wrapped, otherwise `bookPath` itself. Empty if neither exists.
std::string packagePath(const std::string& cachePath, const std::string& bookPath);

// Reads the package structure from the file returned by packagePath().
bool loadPackageInfo(const std::string& cachePath, const std::string& bookPath, PackageInfo& out);

// Parser for rendering chapters / decoding images of an already prepared book:
// description and cover come from the cache sidecars, not from the package.
std::unique_ptr<Fb3Parser> makeRenderParser(const std::string& cachePath);

}  // namespace fb3
