// OpcPackage.h
//
// Reading an OPC (Open Packaging Conventions) ZIP package: the container shared
// by FB3 and DOCX. Format-specific meaning (which relationship is "the body")
// stays in the format libraries; this layer only knows how to
//
//   * find parts by normalized name (see OpcNames.h) with one pass over the ZIP
//     central directory,
//   * read relationship (.rels) parts,
//   * read [Content_Types].xml,
//   * address ZIP members by their central-directory ordinal,
//   * run XmlPullReader over a small in-memory part (MemoryByteReader).
//
// The central directory is re-scanned per lookup batch instead of being cached,
// so a book with hundreds of images never pins tens of KiB of names in RAM.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <IByteReader.h>
#include <ZipReader.h>

namespace opc {

// Bounds that keep RAM use predictable on the ESP32-C3.
constexpr size_t kMaxSmallPartBytes = 256 * 1024;  // rels / content types: tiny; safety bound
constexpr size_t kMaxRelationships = 8192;

enum class ImageKind : uint8_t { None, Jpeg, Png };

const char* mimeFor(ImageKind kind);
ImageKind imageKindFromMime(const std::string& mimeLower);

// Read-only IByteReader over a string; used to run XmlPullReader on small parts.
class MemoryByteReader final : public IByteReader {
 public:
  explicit MemoryByteReader(const std::string& data) : data_(data) {}
  size_t read(void* dst, size_t len) override;
  bool seek(uint64_t pos) override;
  uint64_t tell() const override { return pos_; }
  uint64_t size() const override { return data_.size(); }

 private:
  const std::string& data_;
  size_t pos_ = 0;
};

// Local part of a possibly prefixed XML name ("w:p" -> "p").
std::string_view localName(const std::string& qname);

// A part looked up by normalized name.
struct PartRef {
  std::string norm;       // normalized part name to look for ("" = skip)
  bool found = false;
  uint32_t ordinal = 0;   // central-directory ordinal
  ZipEntryInfo entry;
};

PartRef makePartRef(std::string norm);

// Fills found/ordinal/entry for every name present, in a single directory pass.
bool lookupParts(IByteReader& zip, std::vector<PartRef>& parts);
bool findPart(IByteReader& zip, const std::string& norm, PartRef& out);

// Inflates a member into `out`, keeping at most `limit` bytes (longer parts are
// cut; callers cope with partial XML).
bool readEntryToString(IByteReader& zip, const ZipEntryInfo& entry, size_t limit, std::string& out);

bool zipEntryByOrdinal(IByteReader& zip, uint32_t ordinal, ZipEntryInfo& entry);
bool extractEntryByOrdinal(IByteReader& zip, uint32_t ordinal, const ZipOutputFn& out);

struct Relationship {
  std::string id;
  std::string type;    // original case
  std::string target;  // resolved, normalized part name
};

// Reads the .rels part owned by `ownerNorm` ("" = package root). `exists`
// reports whether the .rels part is present at all. External targets are
// skipped.
bool readRelationships(IByteReader& zip, const std::string& ownerNorm, std::vector<Relationship>& out,
                       bool* exists);

// Relationship type ends with "/relationships/<tail>" (case-insensitive); also
// matches the ISO-strict namespace variants.
bool relationshipTypeIs(const std::string& type, const char* tailLower);

struct ContentTypes {
  std::vector<std::pair<std::string, std::string>> defaults;   // extension -> mime
  std::vector<std::pair<std::string, std::string>> overrides;  // normalized part -> mime

  // The declared MIME type of a part: an Override wins over the extension
  // Default. Empty if undeclared.
  std::string mimeOf(const std::string& partNorm) const;

  // Image kind the firmware can decode. The package's own declaration wins over
  // the file name (the official FB3 samples contain an extension-less part and
  // a ".jPeG" part declared as SVG); only undeclared parts fall back to the
  // extension.
  ImageKind imageKindFor(const std::string& partNorm) const;
};

void readContentTypes(IByteReader& zip, ContentTypes& out);

}  // namespace opc
