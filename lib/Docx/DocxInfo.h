// DocxInfo.h
//
// Everything the DOCX parser needs to know about a package besides the body XML
// itself, in a compact RAM-friendly form:
//
//   * metadata (title, author, language, date),
//   * the paragraph/character styles that matter to a reader (headings, title,
//     quote, bold/italic/..., list binding),
//   * which list levels are numbered (versus bulleted),
//   * the JPEG/PNG images (relationship id -> ZIP member ordinal).
//
// styles.xml of a Word document is routinely 100+ KiB, far more than the
// ESP32-C3 can hold as a DOM, so it is reduced ONCE when the book is prepared
// and the result is stored as a small text sidecar (serialize()/parse()).
// Chapter rendering later loads only that sidecar.
//
// Platform-free: no storage, no Arduino headers.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <OpcPackage.h>

namespace docx {

enum StyleFlag : uint16_t {
  kBold = 1,
  kItalic = 2,
  kUnderline = 4,
  kStrike = 8,
  kSmallCaps = 16,
  kSuperscript = 32,
  kSubscript = 64,
  kTitle = 128,     // "Title" paragraph style
  kSubtitle = 256,  // "Subtitle" paragraph style
  kQuote = 512,     // Quote / Intense Quote / Block Text
};

struct StyleInfo {
  std::string id;          // w:styleId
  uint8_t heading = 0;     // 1..9 for Heading 1..9 (or outline level + 1), 0 = not a heading
  uint16_t flags = 0;      // StyleFlag bits (own + inherited through basedOn)
  uint32_t numId = 0;      // list bound to the style (0 = none)
  uint8_t ilvl = 0;
};

// Which levels of one numbering definition are numbered ("1.", "a)") rather
// than bulleted. Bit n set = level n is ordered.
struct NumberingInfo {
  uint32_t numId = 0;
  uint16_t orderedLevels = 0;
};

struct Image {
  std::string relId;          // r:embed / r:id value used by the body
  opc::ImageKind kind = opc::ImageKind::None;
  uint32_t entryOrdinal = 0;  // ZIP central-directory ordinal
};

struct Metadata {
  std::string title;
  std::string author;
  std::string language;
  std::string date;
};

// Caps that keep RAM bounded on the ESP32-C3.
constexpr size_t kMaxImages = 512;
constexpr size_t kMaxStyles = 512;
constexpr size_t kMaxNumberings = 256;
constexpr size_t kMaxMetadataBytes = 256;

struct PackageInfo {
  // Parts of the package (central-directory ordinals). Used while the book is
  // prepared; not part of the sidecar.
  uint32_t documentOrdinal = 0;
  bool hasFootnotes = false;
  uint32_t footnotesOrdinal = 0;
  bool hasEndnotes = false;
  uint32_t endnotesOrdinal = 0;

  Metadata metadata;
  std::vector<StyleInfo> styles;        // sorted by id
  std::vector<NumberingInfo> numberings;  // sorted by numId
  std::vector<Image> images;              // JPEG/PNG only

  const StyleInfo* findStyle(const std::string& id) const;
  const Image* findImage(const std::string& relId) const;
  // True if `ilvl` of list `numId` is numbered; unknown lists are bulleted.
  bool isOrdered(uint32_t numId, uint8_t ilvl) const;

  void sortTables();
};

// Text sidecar. parse() accepts only what serialize() wrote (versioned).
std::string serialize(const PackageInfo& info);
bool parse(const std::string& text, PackageInfo& out);

}  // namespace docx
