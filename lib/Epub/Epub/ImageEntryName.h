#pragma once

#include <string>

// An archive entry whose file name has no extension at all: FB2-to-EPUB conversions write
// <img src="images/img_32"/>. Only its bytes can say whether it is an image the reader decodes.
// A dot in a folder name ("img.d/img_35") does not count as an extension.
inline bool hasNoFileExtension(const std::string& resolvedPath) {
  const size_t slash = resolvedPath.rfind('/');
  return resolvedPath.find('.', slash == std::string::npos ? 0 : slash + 1) == std::string::npos;
}
