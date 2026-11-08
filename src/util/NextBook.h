#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include <FsHelpers.h>

// Picks the book that follows `currentName` in a folder, in the same natural order the file browser
// uses. `names` are plain file names (no directories); the caller filters them down to openable books.
// Returns an empty string when the current book is last, absent from the list, or the list has no
// other book.
namespace next_book {

inline std::string findNext(std::vector<std::string> names, const std::string& currentName) {
  if (names.size() < 2) return {};
  FsHelpers::sortFileList(names);
  const auto it = std::find(names.begin(), names.end(), currentName);
  if (it == names.end() || std::next(it) == names.end()) return {};
  return *std::next(it);
}

// Directory part of `path` including the trailing slash ("/Books/a.epub" -> "/Books/"), "/" for root files.
inline std::string folderOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? std::string("/") : path.substr(0, slash + 1);
}

inline std::string fileNameOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace next_book
