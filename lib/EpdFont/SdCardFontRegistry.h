#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "VectorFontSupport.h"

struct SdCardFontFileInfo {
  std::string path;   // v4 on-disk naming: "/<root>/<Family>/<Family>_<size>.cpfont"
                      // where <root> is "/.fonts" (preferred, hidden) or "/fonts" (visible).
                      // e.g. "/.fonts/NotoSansCJK/NotoSansCJK_14.cpfont"
  uint8_t pointSize;  // parsed from filename: 14
  uint8_t style;      // always 0 in v4 (all 4 styles bundled in one file);
                      // kept for potential future formats
};

struct SdCardFontFamilyInfo {
  std::string name;  // directory name, e.g. "NotoSansCJK"
  std::vector<SdCardFontFileInfo> files;

  // TrueType/OpenType family (.ttf/.otf/.ttc in the family folder; PSRAM boards only). It renders at
  // any size, so `files` holds one synthetic entry per selectable point size (all pointing at the
  // regular file) and the size UI / selectFile() work unchanged. `vectorPaths` is indexed by style
  // role: 0=regular 1=bold 2=italic 3=bold-italic (empty = not supplied; regular is always set).
  bool vector = false;
  std::string vectorPaths[4];

  const SdCardFontFileInfo* findFile(uint8_t size, uint8_t style = 0) const;
  const SdCardFontFileInfo* findClosestFile(uint8_t targetSize, uint8_t style = 0) const;
  const SdCardFontFileInfo* selectFile(uint8_t targetSize, uint8_t sizeStep, uint8_t style = 0) const;
  bool hasSize(uint8_t size) const;
  std::vector<uint8_t> availableSizes() const;
};

class SdCardFontRegistry {
 public:
  // Selectable reader sizes for vector families (points at 150 DPI, like the .cpfont files).
  static constexpr uint8_t VECTOR_POINT_SIZES[] = {10, 12, 14, 16, 18, 20, 22, 24};

  static constexpr int MAX_SD_FAMILIES = 128;
  // Two top-level roots are scanned at discovery time. Hidden is preferred
  // when creating new installs; both are read from if present.
  static constexpr const char* FONTS_DIR_HIDDEN = "/.fonts";
  static constexpr const char* FONTS_DIR_VISIBLE = "/fonts";

  // Returns the existing root for `familyName` (the one that contains
  // /<root>/<familyName>/), or nullptr if the family is not installed in
  // either root. Used by writers to keep re-installs in their existing dir.
  static const char* findFamilyRoot(const char* familyName);

  // Returns the root path that should be used when creating a brand-new
  // family on disk (no prior install): the existing root if exactly one of
  // the two roots exists, otherwise the hidden root.
  static const char* defaultWriteRoot();

  // Scan SD card, populate families_. Returns true if any families found.
  bool discover();

  const std::vector<SdCardFontFamilyInfo>& getFamilies() const { return families_; }
  const SdCardFontFamilyInfo* findFamily(const std::string& name) const;
  int getFamilyIndex(const std::string& name) const;
  int getFamilyCount() const { return static_cast<int>(families_.size()); }

 private:
  std::vector<SdCardFontFamilyInfo> families_;  // sorted alphabetically

  static bool parseVectorFontName(const char* filename, size_t& baseLen);
  static uint8_t parseVectorStyle(const char* baseName, size_t baseLen);

  static bool parseFilename(const char* filename, uint8_t& size, uint8_t& style);
  static void scanDirectory(const char* dirPath, SdCardFontFamilyInfo& family);
  // Scan one root (e.g. "/.fonts"), append families to `out`, dedup by name.
  static void scanRoot(const char* rootPath, std::vector<SdCardFontFamilyInfo>& out);
};
