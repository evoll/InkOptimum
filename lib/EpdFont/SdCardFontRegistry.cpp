#include "SdCardFontRegistry.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <strings.h>

namespace {
constexpr size_t INITIAL_FONT_FAMILY_RESERVE = 8;
}

const SdCardFontFileInfo* SdCardFontFamilyInfo::findFile(uint8_t size, uint8_t style) const {
  for (const auto& f : files) {
    if (f.pointSize == size && f.style == style) return &f;
  }
  return nullptr;
}

const SdCardFontFileInfo* SdCardFontFamilyInfo::findClosestFile(uint8_t targetSize, uint8_t style) const {
  const SdCardFontFileInfo* best = nullptr;
  uint8_t bestDiff = UINT8_MAX;
  for (const auto& f : files) {
    if (f.style != style) continue;
    const uint8_t diff = f.pointSize > targetSize ? f.pointSize - targetSize : targetSize - f.pointSize;
    if (!best || diff < bestDiff || (diff == bestDiff && f.pointSize < best->pointSize)) {
      best = &f;
      bestDiff = diff;
    }
  }
  return best;
}

const SdCardFontFileInfo* SdCardFontFamilyInfo::selectFile(uint8_t targetSize, uint8_t sizeStep, uint8_t style) const {
  bool haveAny = false;
  uint8_t largest = 0;
  uint8_t previous = 0;
  bool havePrevious = false;

  for (uint16_t rank = 0; rank <= sizeStep; ++rank) {
    bool foundNext = false;
    uint8_t next = UINT8_MAX;
    for (const auto& f : files) {
      if (f.style != style) continue;
      haveAny = true;
      if (f.pointSize > largest) largest = f.pointSize;
      if ((!havePrevious || f.pointSize > previous) && (!foundNext || f.pointSize < next)) {
        next = f.pointSize;
        foundNext = true;
      }
    }
    if (!foundNext) break;
    previous = next;
    havePrevious = true;
  }

  if (haveAny) {
    const uint8_t selectedSize = havePrevious ? previous : largest;
    if (const auto* selected = findFile(selectedSize, style)) return selected;
    if (const auto* selected = findFile(largest, style)) return selected;
  }
  return findClosestFile(targetSize, style);
}

bool SdCardFontFamilyInfo::hasSize(uint8_t size) const {
  for (const auto& f : files) {
    if (f.pointSize == size) return true;
  }
  return false;
}

std::vector<uint8_t> SdCardFontFamilyInfo::availableSizes() const {
  std::vector<uint8_t> sizes;
  sizes.reserve(files.size());
  for (const auto& f : files) {
    bool found = false;
    for (uint8_t s : sizes) {
      if (s == f.pointSize) {
        found = true;
        break;
      }
    }
    if (!found) sizes.push_back(f.pointSize);
  }
  std::sort(sizes.begin(), sizes.end());
  return sizes;
}

bool SdCardFontRegistry::parseFilename(const char* filename, uint8_t& size, uint8_t& style) {
  static constexpr char kExt[] = ".cpfont";
  static constexpr size_t kExtLen = sizeof(kExt) - 1;
  const size_t nameLen = strlen(filename);
  if (nameLen <= kExtLen) return false;
  if (strcmp(filename + nameLen - kExtLen, kExt) != 0) return false;
  const char* ext = filename + nameLen - kExtLen;

  size_t baseLen = ext - filename;
  if (baseLen == 0 || baseLen > 127) return false;

  char base[128];
  memcpy(base, filename, baseLen);
  base[baseLen] = '\0';

  char* lastUnderscore = strrchr(base, '_');
  if (!lastUnderscore || lastUnderscore == base) return false;

  const char* sizeStr = lastUnderscore + 1;
  char* endPtr;
  long sizeVal = strtol(sizeStr, &endPtr, 10);
  if (endPtr == sizeStr || *endPtr != '\0' || sizeVal < 1 || sizeVal > 255) return false;
  size = static_cast<uint8_t>(sizeVal);
  style = 0;
  return true;
}

bool SdCardFontRegistry::parseVectorFontName(const char* filename, size_t& baseLen) {
#if INKMOD_VECTOR_FONTS
  static constexpr const char* kExts[] = {".ttf", ".otf", ".ttc"};
  const size_t nameLen = strlen(filename);
  for (const char* ext : kExts) {
    const size_t extLen = strlen(ext);
    if (nameLen <= extLen) continue;
    if (strcasecmp(filename + nameLen - extLen, ext) == 0) {
      baseLen = nameLen - extLen;
      return baseLen > 0 && baseLen <= 127;
    }
  }
#else
  (void)filename;
  (void)baseLen;
#endif
  return false;
}

// "bold"/"italic"/"oblique" tokens in the base name pick the style role (bit0 bold, bit1 italic).
uint8_t SdCardFontRegistry::parseVectorStyle(const char* baseName, size_t n) {
  bool bold = false;
  bool ital = false;
  for (size_t i = 0; i < n; ++i) {
    if ((n - i) >= 4 && strncasecmp(baseName + i, "bold", 4) == 0) bold = true;
    if ((n - i) >= 6 && strncasecmp(baseName + i, "italic", 6) == 0) ital = true;
    if ((n - i) >= 7 && strncasecmp(baseName + i, "oblique", 7) == 0) ital = true;
  }
  return static_cast<uint8_t>((bold ? 1 : 0) | (ital ? 2 : 0));
}

void SdCardFontRegistry::scanDirectory(const char* dirPath, SdCardFontFamilyInfo& family) {
  HalFile dir = Storage.open(dirPath);
  if (!dir || !dir.isDirectory()) return;

  char nameBuffer[128];
  while (true) {
    HalFile entry = dir.openNextFile();
    if (!entry) break;
    if (entry.isDirectory()) {
      entry.close();
      continue;
    }
    entry.getName(nameBuffer, sizeof(nameBuffer));
    entry.close();
    if (nameBuffer[0] == '.' || nameBuffer[0] == '_') continue;

    size_t vectorBaseLen = 0;
    if (parseVectorFontName(nameBuffer, vectorBaseLen)) {
      const uint8_t role = parseVectorStyle(nameBuffer, vectorBaseLen);
      // Several files may claim one role (e.g. Light + Regular): keep the shortest name, which is
      // normally the plain "Family-Regular.ttf".
      std::string& slot = family.vectorPaths[role];
      const std::string candidate = std::string(dirPath) + "/" + nameBuffer;
      if (slot.empty() || candidate.size() < slot.size()) slot = candidate;
      continue;
    }

    uint8_t size, style;
    if (!parseFilename(nameBuffer, size, style)) continue;

    bool duplicate = false;
    for (const auto& existing : family.files) {
      if (existing.pointSize == size && existing.style == style) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      LOG_ERR("SDREG", "Duplicate font %s in %s — skipping", nameBuffer, dirPath);
      continue;
    }

    SdCardFontFileInfo info;
    info.path = std::string(dirPath) + "/" + nameBuffer;
    info.pointSize = size;
    info.style = style;
    family.files.push_back(std::move(info));
  }
}

void SdCardFontRegistry::scanRoot(const char* rootPath, std::vector<SdCardFontFamilyInfo>& out) {
  HalFile root = Storage.open(rootPath);
  if (!root) {
    LOG_DBG("SDREG", "Fonts directory not found: %s", rootPath);
    return;
  }
  if (!root.isDirectory()) {
    LOG_ERR("SDREG", "Fonts path is not a directory: %s", rootPath);
    return;
  }

  char nameBuffer[128];
  while (true) {
    HalFile entry = root.openNextFile();
    if (!entry) break;
    if (entry.isDirectory()) {
      entry.getName(nameBuffer, sizeof(nameBuffer));
      entry.close();
      if (nameBuffer[0] == '.' || nameBuffer[0] == '_') continue;

      bool exists = false;
      for (const auto& fam : out) {
        if (fam.name == nameBuffer) {
          exists = true;
          break;
        }
      }
      if (exists) continue;

      SdCardFontFamilyInfo family;
      family.name = nameBuffer;
      std::string subDirPath = std::string(rootPath) + "/" + nameBuffer;
      SdCardFontRegistry::scanDirectory(subDirPath.c_str(), family);
      // A folder with .cpfont files stays a bitmap family. Otherwise a regular (or any single)
      // TTF/OTF/TTC makes it a vector family.
      if (family.files.empty()) {
        std::string& reg = family.vectorPaths[0];
        if (reg.empty()) {
          for (const auto& path : family.vectorPaths) {
            if (!path.empty()) {
              reg = path;
              break;
            }
          }
        }
        if (!reg.empty()) {
          family.vector = true;
          for (const uint8_t size : VECTOR_POINT_SIZES) {
            SdCardFontFileInfo info;
            info.path = reg;
            info.pointSize = size;
            info.style = 0;
            family.files.push_back(std::move(info));
          }
        }
      }
      if (!family.files.empty()) {
        out.push_back(std::move(family));
        LOG_DBG("SDREG", "Found family: %s (%d files) in %s", out.back().name.c_str(),
                static_cast<int>(out.back().files.size()), rootPath);
      }
    } else {
      entry.close();
    }
  }
}

bool SdCardFontRegistry::discover() {
  families_.clear();
  // Keep support for up to MAX_SD_FAMILIES, but don't reserve all 128 slots on
  // every boot. Typical readers carry only a handful of families; vector growth
  // remains automatic if a user installs more.
  if (families_.capacity() < INITIAL_FONT_FAMILY_RESERVE) {
    families_.reserve(INITIAL_FONT_FAMILY_RESERVE);
  }

  scanRoot(FONTS_DIR_HIDDEN, families_);
  scanRoot(FONTS_DIR_VISIBLE, families_);

  std::sort(families_.begin(), families_.end(),
            [](const SdCardFontFamilyInfo& a, const SdCardFontFamilyInfo& b) { return a.name < b.name; });

  if (static_cast<int>(families_.size()) > MAX_SD_FAMILIES) {
    families_.resize(MAX_SD_FAMILIES);
  }

  LOG_DBG("SDREG", "Discovery complete: %d families", static_cast<int>(families_.size()));
  return !families_.empty();
}

const char* SdCardFontRegistry::findFamilyRoot(const char* familyName) {
  if (!familyName || !*familyName) return nullptr;
  char path[160];
  snprintf(path, sizeof(path), "%s/%s", FONTS_DIR_HIDDEN, familyName);
  if (Storage.exists(path)) return FONTS_DIR_HIDDEN;
  snprintf(path, sizeof(path), "%s/%s", FONTS_DIR_VISIBLE, familyName);
  if (Storage.exists(path)) return FONTS_DIR_VISIBLE;
  return nullptr;
}

const char* SdCardFontRegistry::defaultWriteRoot() {
  bool hiddenExists = Storage.exists(FONTS_DIR_HIDDEN);
  bool visibleExists = Storage.exists(FONTS_DIR_VISIBLE);
  if (hiddenExists) return FONTS_DIR_HIDDEN;
  if (visibleExists) return FONTS_DIR_VISIBLE;
  return FONTS_DIR_HIDDEN;
}

const SdCardFontFamilyInfo* SdCardFontRegistry::findFamily(const std::string& name) const {
  for (const auto& f : families_) {
    if (f.name == name) return &f;
  }
  return nullptr;
}

int SdCardFontRegistry::getFamilyIndex(const std::string& name) const {
  for (int i = 0; i < static_cast<int>(families_.size()); i++) {
    if (families_[i].name == name) return i;
  }
  return -1;
}