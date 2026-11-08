// DocxSource.h
//
// Storage-facing half of the DOCX library: turns a .docx on the SD card into the
// cache files DocxParser works from, and finds them again later. (Package
// structure, reduction and parsing are platform-free: DocxPackage.h /
// DocxParser.h; this unit is the only part that touches HalStorage.)
//
// Cache layout, inside the book's cache directory:
//
//   .source.docxbody   <docx-book> stream: document (+ footnotes + endnotes) XML,
//                      inflated once (a ZIP member cannot be seeked inside)
//   .docx_info         DocxInfo sidecar: metadata, reduced styles, list kinds,
//                      image table
//
// Images are NOT copied: each stays a member of the user's .docx and is inflated
// on demand by its ZIP ordinal.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "DocxInfo.h"
#include "DocxParser.h"

namespace docx {

struct PreparedSource {
  std::string bodyPath;  // value for the document's sourcePath
  uint32_t imageCount = 0;
};

// True for the body file prepareSource() writes (the document's sourcePath),
// i.e. "this book is a .docx".
bool isBodySource(const std::string& sourcePath);

// Writes the cache files above into `cachePath` (which must exist).
bool prepareSource(const std::string& bookPath, const std::string& cachePath,
                   const std::function<void(int percent)>& onProgress, PreparedSource& out);

// Reads the sidecar written by prepareSource().
bool loadInfo(const std::string& cachePath, PackageInfo& out);

// The file images are read from: the user's .docx (empty if it is gone).
std::string packagePath(const std::string& bookPath);

// Parser for rendering chapters / decoding images of an already prepared book.
std::unique_ptr<DocxParser> makeRenderParser(const std::string& cachePath);

}  // namespace docx
