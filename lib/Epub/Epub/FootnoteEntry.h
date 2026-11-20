#pragma once

#include <cstring>

#define FOOTNOTE_NUMBER_LEN 32
#define FOOTNOTE_HREF_LEN 96

struct FootnoteEntry {
  char number[FOOTNOTE_NUMBER_LEN];
  char href[FOOTNOTE_HREF_LEN];

  // Zero the whole buffers: they are written to the section cache as raw bytes, and stale heap bytes
  // after the terminator made the same book produce different cache files (crosspoint #3813).
  FootnoteEntry() : number{}, href{} {}
};
