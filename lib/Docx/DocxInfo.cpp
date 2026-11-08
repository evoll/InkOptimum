#include "DocxInfo.h"

#include <algorithm>
#include <cstdlib>

namespace docx {

namespace {

constexpr char kHeader[] = "DOCXINFO1";

std::string clean(const std::string& in, size_t limit) {
  std::string out;
  out.reserve(std::min(in.size(), limit));
  for (const char c : in) {
    if (out.size() >= limit) break;
    out.push_back((c == '\t' || c == '\n' || c == '\r') ? ' ' : c);
  }
  return out;
}

std::vector<std::string> splitTabs(const std::string& line) {
  std::vector<std::string> out;
  size_t start = 0;
  for (;;) {
    const size_t tab = line.find('\t', start);
    if (tab == std::string::npos) {
      out.push_back(line.substr(start));
      return out;
    }
    out.push_back(line.substr(start, tab - start));
    start = tab + 1;
  }
}

uint32_t toU32(const std::string& s) { return static_cast<uint32_t>(std::strtoul(s.c_str(), nullptr, 10)); }

}  // namespace

const StyleInfo* PackageInfo::findStyle(const std::string& id) const {
  auto it = std::lower_bound(styles.begin(), styles.end(), id,
                             [](const StyleInfo& s, const std::string& key) { return s.id < key; });
  return (it != styles.end() && it->id == id) ? &*it : nullptr;
}

const Image* PackageInfo::findImage(const std::string& relId) const {
  for (const Image& img : images) {
    if (img.relId == relId) return &img;
  }
  return nullptr;
}

bool PackageInfo::isOrdered(uint32_t numId, uint8_t ilvl) const {
  auto it = std::lower_bound(numberings.begin(), numberings.end(), numId,
                             [](const NumberingInfo& n, uint32_t key) { return n.numId < key; });
  if (it == numberings.end() || it->numId != numId || ilvl > 15) return false;
  return (it->orderedLevels >> ilvl) & 1u;
}

void PackageInfo::sortTables() {
  std::sort(styles.begin(), styles.end(), [](const StyleInfo& a, const StyleInfo& b) { return a.id < b.id; });
  std::sort(numberings.begin(), numberings.end(),
            [](const NumberingInfo& a, const NumberingInfo& b) { return a.numId < b.numId; });
}

std::string serialize(const PackageInfo& info) {
  std::string out = kHeader;
  out += '\n';
  const auto field = [&](char tag, const std::string& value) {
    if (value.empty()) return;
    out += tag;
    out += '\t';
    out += clean(value, kMaxMetadataBytes);
    out += '\n';
  };
  field('T', info.metadata.title);
  field('A', info.metadata.author);
  field('L', info.metadata.language);
  field('D', info.metadata.date);
  for (const StyleInfo& s : info.styles) {
    out += "S\t" + clean(s.id, 96) + '\t' + std::to_string(s.heading) + '\t' + std::to_string(s.flags) + '\t' +
           std::to_string(s.numId) + '\t' + std::to_string(s.ilvl) + '\n';
  }
  for (const NumberingInfo& n : info.numberings) {
    out += "N\t" + std::to_string(n.numId) + '\t' + std::to_string(n.orderedLevels) + '\n';
  }
  for (const Image& img : info.images) {
    out += "I\t" + clean(img.relId, 96) + '\t' + std::to_string(static_cast<int>(img.kind)) + '\t' +
           std::to_string(img.entryOrdinal) + '\n';
  }
  return out;
}

bool parse(const std::string& text, PackageInfo& out) {
  out = PackageInfo();
  size_t pos = text.find('\n');
  if (pos == std::string::npos || text.compare(0, pos, kHeader) != 0) return false;
  ++pos;
  while (pos < text.size()) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string::npos) eol = text.size();
    const std::string line = text.substr(pos, eol - pos);
    pos = eol + 1;
    if (line.size() < 3 || line[1] != '\t') continue;
    const std::vector<std::string> f = splitTabs(line.substr(2));
    switch (line[0]) {
      case 'T': out.metadata.title = f[0]; break;
      case 'A': out.metadata.author = f[0]; break;
      case 'L': out.metadata.language = f[0]; break;
      case 'D': out.metadata.date = f[0]; break;
      case 'S':
        if (f.size() >= 5 && out.styles.size() < kMaxStyles) {
          StyleInfo s;
          s.id = f[0];
          s.heading = static_cast<uint8_t>(std::min<uint32_t>(toU32(f[1]), 9));
          s.flags = static_cast<uint16_t>(toU32(f[2]));
          s.numId = toU32(f[3]);
          s.ilvl = static_cast<uint8_t>(std::min<uint32_t>(toU32(f[4]), 8));
          out.styles.push_back(std::move(s));
        }
        break;
      case 'N':
        if (f.size() >= 2 && out.numberings.size() < kMaxNumberings) {
          out.numberings.push_back({toU32(f[0]), static_cast<uint16_t>(toU32(f[1]))});
        }
        break;
      case 'I':
        if (f.size() >= 3 && out.images.size() < kMaxImages) {
          Image img;
          img.relId = f[0];
          const uint32_t kind = toU32(f[1]);
          img.kind = kind == 1 ? opc::ImageKind::Jpeg : kind == 2 ? opc::ImageKind::Png : opc::ImageKind::None;
          img.entryOrdinal = toU32(f[2]);
          if (img.kind != opc::ImageKind::None) out.images.push_back(std::move(img));
        }
        break;
      default: break;
    }
  }
  out.sortTables();
  return true;
}

}  // namespace docx
