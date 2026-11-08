// Fb3Package.h
//
// Reading the OPC (ZIP) container of an FB3 book. See OpcNames.h (lib/FictionBookCore) for the naming
// rules that make real-world packages tricky (case-insensitive percent-decoded
// part names, relative relationship targets, extension-less parts, content
// types declared out of band).
//
// FB3 is read natively: the body part stays an XML document that Fb3Parser
// scans and renders section by section, exactly like an FB2 file. Because a
// ZIP member cannot be seeked inside, the one body part is inflated ONCE to a
// plain file in the book cache (the same extract-once idea FB2.ZIP uses); the
// parser then works on that file with byte offsets. Images are never copied
// into the XML: each stays a ZIP member and is inflated on demand, addressed
// by its ordinal in the ZIP central directory.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <IByteReader.h>
#include <OpcPackage.h>
#include <ZipReader.h>

namespace fb3 {

// The OPC container layer is shared with other package formats (lib/FictionBookCore).
using opc::ImageKind;
using opc::MemoryByteReader;
using opc::mimeFor;
using opc::zipEntryByOrdinal;
using opc::extractEntryByOrdinal;

struct Image {
  std::string relId;          // id used by <img src="..."> in the body
  ImageKind kind = ImageKind::None;
  uint32_t entryOrdinal = 0;  // index of the ZIP central-directory entry
};

struct PackageInfo {
  uint32_t bodyOrdinal = 0;
  uint32_t descriptionOrdinal = 0;
  std::vector<Image> images;   // JPEG/PNG only (what the firmware decodes)
  std::string coverBinaryId;   // relId of the cover candidate, "" if none
  std::string descriptionXml;  // raw description part (bounded)
};

// Caps that keep RAM bounded on the ESP32-C3.
constexpr size_t kMaxImages = 512;
constexpr size_t kMaxDescriptionBytes = 128 * 1024;
// Synthetic image id used when the cover is the package thumbnail.
constexpr const char* kThumbnailCoverId = "fb3-package-thumbnail";

// True if `zip` is an OPC package whose root relationships declare a
// FictionBook3 "Book" part.
bool isFb3Package(IByteReader& zip);

// For "book.fb3.zip" archives that wrap a plain .fb3: finds the inner entry.
bool findNestedFb3Entry(IByteReader& zip, ZipEntryInfo& entry);

// Resolves relationships, content types, body/description parts, image table
// and the cover candidate. Returns false for a structurally broken package.
bool readPackageInfo(IByteReader& zip, PackageInfo& out);

}  // namespace fb3
