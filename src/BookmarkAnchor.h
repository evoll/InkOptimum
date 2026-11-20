#pragma once

#include <cstdio>
#include <cstring>
#include <vector>

#include "BookmarkStore.h"

// Gives a bookmark made before paragraph anchors existed (paragraphIndex == UINT16_MAX) the anchor of the
// page the reader landed on when it was opened, so a later change of font or layout keeps it on its text.
// Only a bookmark that matches the one that was opened (chapter and stored progress) and has no anchor yet is
// touched; an existing anchor or snippet is never overwritten. Returns true when something changed.
namespace bookmark_anchor {

inline bool apply(std::vector<Bookmark>& bookmarks, const uint16_t spineIndex, const float progress,
                  const uint16_t paragraphIndex, const char* snippet) {
  if (paragraphIndex == UINT16_MAX) return false;
  for (Bookmark& b : bookmarks) {
    if (b.spineIndex != spineIndex || b.progress != progress || b.paragraphIndex != UINT16_MAX) continue;
    b.paragraphIndex = paragraphIndex;
    if (b.snippet[0] == '\0' && snippet != nullptr && snippet[0] != '\0') {
      std::snprintf(b.snippet, sizeof(b.snippet), "%s", snippet);
    }
    return true;
  }
  return false;
}

}  // namespace bookmark_anchor
