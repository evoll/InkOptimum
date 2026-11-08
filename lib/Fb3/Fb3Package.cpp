#include "Fb3Package.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include <OpcNames.h>
#include <OpcPackage.h>

namespace fb3 {

namespace {

using namespace opc;

bool typeEndsWith(const std::string& type, const char* tailLower) { return endsWithLower(type, tailLower); }

bool isBookRelationship(const std::string& type) {
  const std::string lower = asciiLowerCopy(type);
  return lower.find("fictionbook3") != std::string::npos && typeEndsWith(lower, "/relationships/book");
}

}  // namespace

// --------------------------------------------------------------------------
// Public API
// --------------------------------------------------------------------------

bool isFb3Package(IByteReader& zip) {
  std::vector<Relationship> rels;
  bool exists = false;
  if (!readRelationships(zip, "", rels, &exists) || !exists) return false;
  for (const Relationship& r : rels) {
    if (isBookRelationship(r.type)) return true;
  }
  return false;
}

bool findNestedFb3Entry(IByteReader& zip, ZipEntryInfo& entry) {
  bool found = false;
  forEachZipEntry(zip, [&](const ZipEntryInfo& e) {
    if (!e.filename.empty() && e.filename.back() != '/' && endsWithLower(e.filename, ".fb3")) {
      entry = e;
      found = true;
      return false;
    }
    return true;
  });
  return found;
}

bool readPackageInfo(IByteReader& zip, PackageInfo& out) {
  out = PackageInfo();

  // 1. Root relationships -> Book (description) part, optional thumbnail.
  std::vector<Relationship> rels;
  bool exists = false;
  if (!readRelationships(zip, "", rels, &exists) || !exists) return false;
  std::string descNorm, thumbNorm;
  for (const Relationship& r : rels) {
    if (descNorm.empty() && isBookRelationship(r.type)) {
      descNorm = r.target;
    } else if (thumbNorm.empty() && typeEndsWith(asciiLowerCopy(r.type), "/relationships/thumbnail")) {
      thumbNorm = r.target;
    }
  }
  if (descNorm.empty()) return false;

  // 2. Description relationships -> body part.
  if (!readRelationships(zip, descNorm, rels, &exists)) return false;
  std::string bodyNorm;
  for (const Relationship& r : rels) {
    if (typeEndsWith(asciiLowerCopy(r.type), "/relationships/body")) {
      bodyNorm = r.target;
      break;
    }
  }
  if (bodyNorm.empty()) return false;

  // 3. Body relationships -> candidate images (<img src="relId">).
  if (!readRelationships(zip, bodyNorm, rels, &exists)) return false;
  struct Candidate {
    std::string relId;
    std::string target;
  };
  std::vector<Candidate> candidates;
  for (const Relationship& r : rels) {
    if (!typeEndsWith(asciiLowerCopy(r.type), "/relationships/image")) continue;
    if (candidates.size() >= kMaxImages) break;
    candidates.push_back({r.id, r.target});
  }

  opc::ContentTypes ct;
  opc::readContentTypes(zip, ct);

  // 4. One directory pass resolves description, body, thumbnail and images.
  std::vector<PartRef> wanted;
  wanted.reserve(candidates.size() + 3);
  wanted.push_back(makePartRef(descNorm));
  wanted.push_back(makePartRef(bodyNorm));
  wanted.push_back(makePartRef(thumbNorm));
  for (const Candidate& c : candidates) wanted.push_back(makePartRef(c.target));
  if (!lookupParts(zip, wanted)) return false;
  if (!wanted[0].found || !wanted[1].found) return false;
  out.descriptionOrdinal = wanted[0].ordinal;
  out.bodyOrdinal = wanted[1].ordinal;

  for (size_t i = 0; i < candidates.size(); ++i) {
    const PartRef& w = wanted[i + 3];
    if (!w.found || w.entry.uncompressedSize == 0) continue;
    if (w.entry.method != 0 && w.entry.method != 8) continue;
    const ImageKind kind = ct.imageKindFor(candidates[i].target);
    if (kind == ImageKind::None) continue;
    Image img;
    img.relId = candidates[i].relId;
    img.kind = kind;
    img.entryOrdinal = w.ordinal;
    out.images.push_back(std::move(img));
  }

  // 5. Cover: a body image whose relationship id says "cover" (real packages
  // name it "Cover.png"), else the package thumbnail.
  for (const Image& img : out.images) {
    if (asciiLowerCopy(img.relId).compare(0, 5, "cover") == 0) {
      out.coverBinaryId = img.relId;
      break;
    }
  }
  if (out.coverBinaryId.empty() && !thumbNorm.empty() && wanted[2].found && wanted[2].entry.uncompressedSize != 0 &&
      (wanted[2].entry.method == 0 || wanted[2].entry.method == 8)) {
    const ImageKind kind = ct.imageKindFor(thumbNorm);
    if (kind != ImageKind::None && out.images.size() < kMaxImages) {
      Image img;
      img.relId = kThumbnailCoverId;
      img.kind = kind;
      img.entryOrdinal = wanted[2].ordinal;
      out.images.push_back(std::move(img));
      out.coverBinaryId = kThumbnailCoverId;
    }
  }

  // 6. Description XML, kept for metadata and the annotation page.
  ZipEntryInfo descEntry;
  if (zipEntryByOrdinal(zip, out.descriptionOrdinal, descEntry)) {
    readEntryToString(zip, descEntry, kMaxDescriptionBytes, out.descriptionXml);
  }
  return true;
}

}  // namespace fb3
