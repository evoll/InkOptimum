#include "OpcPackage.h"

#include <algorithm>
#include <cstring>

#include <XmlPullReader.h>
#include "OpcNames.h"

namespace opc {

namespace {
constexpr size_t kMaxOverrides = 512;
constexpr size_t kMaxDefaults = 64;
}  // namespace

const char* mimeFor(ImageKind kind) {
  switch (kind) {
    case ImageKind::Jpeg: return "image/jpeg";
    case ImageKind::Png: return "image/png";
    default: return "";
  }
}

ImageKind imageKindFromMime(const std::string& mimeLower) {
  if (mimeLower == "image/jpeg" || mimeLower == "image/jpg" || mimeLower == "image/pjpeg") return ImageKind::Jpeg;
  if (mimeLower == "image/png" || mimeLower == "image/x-png") return ImageKind::Png;
  return ImageKind::None;
}

size_t MemoryByteReader::read(void* dst, size_t len) {
  const size_t available = pos_ < data_.size() ? data_.size() - pos_ : 0;
  const size_t count = std::min(len, available);
  if (count != 0) std::memcpy(dst, data_.data() + pos_, count);
  pos_ += count;
  return count;
}

bool MemoryByteReader::seek(uint64_t pos) {
  if (pos > data_.size()) return false;
  pos_ = static_cast<size_t>(pos);
  return true;
}

std::string_view localName(const std::string& qname) {
  const size_t colon = qname.find_last_of(':');
  return colon == std::string::npos ? std::string_view(qname) : std::string_view(qname).substr(colon + 1);
}

PartRef makePartRef(std::string norm) {
  PartRef p;
  p.norm = std::move(norm);
  return p;
}

bool lookupParts(IByteReader& zip, std::vector<PartRef>& parts) {
  std::vector<size_t> order(parts.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return parts[a].norm < parts[b].norm; });
  size_t remaining = 0;
  for (PartRef& p : parts) {
    p.found = false;
    if (!p.norm.empty()) ++remaining;
  }
  if (remaining == 0) return true;

  uint32_t ordinal = 0;
  return forEachZipEntry(zip, [&](const ZipEntryInfo& e) {
    const uint32_t current = ordinal++;
    if (e.filename.empty() || e.filename.back() == '/') return true;
    const std::string norm = normalizePartName(e.filename);
    auto it = std::lower_bound(order.begin(), order.end(), norm,
                               [&](size_t idx, const std::string& n) { return parts[idx].norm < n; });
    // Duplicate wanted names (e.g. two relationships to one image) all match.
    for (; it != order.end() && parts[*it].norm == norm; ++it) {
      PartRef& p = parts[*it];
      if (!p.found) {
        p.found = true;
        p.ordinal = current;
        p.entry = e;
        --remaining;
      }
    }
    return remaining > 0;
  });
}

bool findPart(IByteReader& zip, const std::string& norm, PartRef& out) {
  std::vector<PartRef> one(1);
  one[0].norm = norm;
  if (!lookupParts(zip, one) || !one[0].found) return false;
  out = std::move(one[0]);
  return true;
}

bool readEntryToString(IByteReader& zip, const ZipEntryInfo& entry, size_t limit, std::string& out) {
  out.clear();
  return extractZipEntry(zip, entry, [&](const uint8_t* d, size_t n) {
    if (out.size() >= limit) return;
    out.append(reinterpret_cast<const char*>(d), std::min(n, limit - out.size()));
  });
}

bool zipEntryByOrdinal(IByteReader& zip, uint32_t ordinal, ZipEntryInfo& entry) {
  uint32_t index = 0;
  bool found = false;
  forEachZipEntry(zip, [&](const ZipEntryInfo& e) {
    if (index++ == ordinal) {
      entry = e;
      found = true;
      return false;
    }
    return true;
  });
  return found;
}

bool extractEntryByOrdinal(IByteReader& zip, uint32_t ordinal, const ZipOutputFn& out) {
  ZipEntryInfo entry;
  if (!zipEntryByOrdinal(zip, ordinal, entry)) return false;
  return extractZipEntry(zip, entry, out);
}

bool readRelationships(IByteReader& zip, const std::string& ownerNorm, std::vector<Relationship>& out,
                       bool* exists) {
  out.clear();
  const std::string relsName = ownerNorm.empty() ? std::string("_rels/.rels") : relationshipsPartFor(ownerNorm);
  PartRef part;
  const bool present = findPart(zip, relsName, part);
  if (exists) *exists = present;
  if (!present) return true;

  std::string xmlText;
  if (!readEntryToString(zip, part.entry, kMaxSmallPartBytes, xmlText)) return false;
  MemoryByteReader mem(xmlText);
  XmlPullReader xml(mem, 2048);
  for (;;) {
    const XmlToken tok = xml.next();
    if (tok == XmlToken::Eof) break;
    if (tok == XmlToken::Error) return false;
    if (tok != XmlToken::StartTag && tok != XmlToken::SelfClosing) continue;
    if (localName(xml.name()) != "Relationship") continue;
    if (out.size() >= kMaxRelationships) break;
    const char* mode = xml.attr("TargetMode");
    if (mode && asciiLowerCopy(mode) == "external") continue;
    const char* id = xml.attr("Id");
    const char* type = xml.attr("Type");
    const char* target = xml.attr("Target");
    if (!id || !type || !target) continue;
    std::string resolved = resolveTarget(ownerNorm, target);
    if (resolved.empty()) continue;
    out.push_back({id, type, std::move(resolved)});
  }
  return true;
}

bool relationshipTypeIs(const std::string& type, const char* tailLower) {
  const std::string lower = asciiLowerCopy(type);
  const std::string tail = std::string("/relationships/") + tailLower;
  return endsWithLower(lower, tail.c_str());
}

std::string ContentTypes::mimeOf(const std::string& partNorm) const {
  for (const auto& o : overrides) {
    if (o.first == partNorm) return o.second;
  }
  const size_t slash = partNorm.find_last_of('/');
  const size_t dot = partNorm.find_last_of('.');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return std::string();
  const std::string ext = partNorm.substr(dot + 1);
  for (const auto& d : defaults) {
    if (d.first == ext) return d.second;
  }
  return std::string();
}

ImageKind ContentTypes::imageKindFor(const std::string& partNorm) const {
  const std::string declared = mimeOf(partNorm);
  if (!declared.empty()) return imageKindFromMime(declared);
  const size_t slash = partNorm.find_last_of('/');
  const size_t dot = partNorm.find_last_of('.');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return ImageKind::None;
  const std::string ext = partNorm.substr(dot + 1);
  if (ext == "jpg" || ext == "jpeg") return ImageKind::Jpeg;
  if (ext == "png") return ImageKind::Png;
  return ImageKind::None;
}

void readContentTypes(IByteReader& zip, ContentTypes& out) {
  PartRef part;
  if (!findPart(zip, "[content_types].xml", part)) return;
  std::string xmlText;
  if (!readEntryToString(zip, part.entry, kMaxSmallPartBytes, xmlText)) return;
  MemoryByteReader mem(xmlText);
  XmlPullReader xml(mem, 2048);
  for (;;) {
    const XmlToken tok = xml.next();
    if (tok == XmlToken::Eof || tok == XmlToken::Error) break;
    if (tok != XmlToken::StartTag && tok != XmlToken::SelfClosing) continue;
    const std::string_view n = localName(xml.name());
    const char* type = xml.attr("ContentType");
    if (!type) continue;
    if (n == "Default") {
      const char* ext = xml.attr("Extension");
      if (ext && out.defaults.size() < kMaxDefaults) {
        out.defaults.emplace_back(asciiLowerCopy(ext), asciiLowerCopy(type));
      }
    } else if (n == "Override") {
      const char* partName = xml.attr("PartName");
      if (partName && out.overrides.size() < kMaxOverrides) {
        out.overrides.emplace_back(normalizePartName(partName), asciiLowerCopy(type));
      }
    }
  }
}

}  // namespace opc
