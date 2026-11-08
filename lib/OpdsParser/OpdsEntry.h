#pragma once
// Entry types shared by the Atom and JSON OPDS readers. Pure C++, no Arduino or Expat.
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

constexpr size_t MAX_OPDS_FEED_ENTRIES = 32;

/**
 * Type of OPDS entry.
 */
enum class OpdsEntryType {
  NAVIGATION,  // Link to another catalog
  BOOK,        // Downloadable book
  SEARCH,      // Synthetic "Search" row inserted by the browser
  REFRESH,     // Synthetic "Refresh catalog" row inserted by the browser
  INFO         // Metadata-only OPDS entry (author/book information)
};

// Values are persisted in the OPDS feed cache: append only, never reorder.
// LCP_LICENSE is not a book: the link is a Readium license document (.lcpl) that the LCP service turns into
// an EPUB, and whose passphrase unlocks it.
enum class OpdsBookFormat : uint8_t { EPUB, FB2, FB2_ZIP, FB3, MOBI, AZW3, RTF, DOCX, LCP_LICENSE };

struct OpdsAcquisition {
  OpdsBookFormat format = OpdsBookFormat::EPUB;
  std::string href;
};

constexpr size_t MAX_OPDS_ACQUISITIONS = 3;

enum class OpdsParserError { NONE, NO_ENTRY_BUFFER, INVALID_INPUT, PARSER_MEMORY, BUFFER_MEMORY, XML_PARSE, SINK_ERROR };

/**
 * Represents an entry from an OPDS feed (either a navigation link or a book).
 */
struct OpdsEntry {
  OpdsEntryType type = OpdsEntryType::NAVIGATION;
  std::string title;
  std::string author;  // Only for books
  std::string description;  // Short OPDS summary/content text
  std::string isbn;    // Normalized ISBN from dc:identifier (books only, may be empty)
  std::string asin;    // Amazon ASIN from dc:identifier (may be empty)
  std::string series;  // "Series #N" from calibre:series / series_index (may be empty)
  std::string href;    // Primary navigation/download URL (legacy compatibility)
  std::string id;
  std::string navigationHref;  // Optional details/navigation link for a publication
  std::array<OpdsAcquisition, MAX_OPDS_ACQUISITIONS> acquisitions{};
  uint8_t acquisitionCount = 0;
};

// Legacy alias for backward compatibility
using OpdsBook = OpdsEntry;
