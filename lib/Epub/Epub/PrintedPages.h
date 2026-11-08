#pragma once

#include <optional>
#include <string>

class Epub;

// Printed-page ("go to page N") navigation. The page list comes from the EPUB 3 nav
// `epub:type="page-list"` or the EPUB 2 NCX `<pageList>`. It is extracted lazily, once per
// book, into `<cache>/pagelist.bin`, so opening a book never pays for it, and every query
// below streams that file instead of holding the list in RAM.
//
// File format: u16 count, then per entry three length-prefixed strings (u32 len + bytes):
// resolved spine href, fragment anchor (may be empty), visible label.
namespace PrintedPages {

struct Entry {
  std::string href;
  std::string anchor;
  std::string label;
};

constexpr const char* CACHE_FILENAME = "/pagelist.bin";

// Parses a label as a printed page number: non-empty, digits only, at most 999999.
std::optional<int> parseNumericLabel(const std::string& label);

// Builds pagelist.bin if it does not exist yet (an empty result writes no file and is
// retried only after the cache is cleared). Returns true when a page list is available.
bool ensureBuilt(const Epub& epub);

// True when the list has at least one numeric label.
bool hasNumeric(const std::string& cachePath);

// Inclusive numeric label range. False (outputs untouched) when no numeric label exists.
bool labelRange(const std::string& cachePath, int& minLabel, int& maxLabel);

// First entry whose numeric label equals target (file order).
std::optional<Entry> findByLabel(const std::string& cachePath, int target);

// Entry with the greatest numeric label <= target (file order breaks ties), single pass.
std::optional<Entry> findAtOrBefore(const std::string& cachePath, int target);

}  // namespace PrintedPages
