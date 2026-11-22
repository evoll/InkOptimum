#pragma once

#include <cstdint>
#include <string>
#include <vector>

class GfxRenderer;
class SdCardFont;
struct SdCardFontFamilyInfo;

class SdCardFontManager {
 public:
  SdCardFontManager() = default;
  ~SdCardFontManager();
  SdCardFontManager(const SdCardFontManager&) = delete;
  SdCardFontManager& operator=(const SdCardFontManager&) = delete;

  // Load the selected font file. Four-size families map the reader size step
  // onto the sorted file list; other counts fall back to closest point size.
  // Only one .cpfont file is loaded; other sizes remain on disk. This keeps
  // resident interval + kern/ligature tables to one size's worth of memory.
  // Returns true on success.
  bool loadFamily(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t targetPointSize, uint8_t sizeStep);

  // Second, larger size of the already loaded family for h1-h3 headings (target = ~1.25x the body size, never the
  // same size). Loaded only when `family` is the family loadFamily() just loaded; a failure leaves the manager as it
  // was (headings simply use the body font). Returns true when a heading font is now resident.
  bool loadHeadingFont(const SdCardFontFamilyInfo& family, GfxRenderer& renderer);
  void unloadHeadingFont(GfxRenderer& renderer);
  // Font ID of the loaded heading size, 0 when none.
  int headingFontId() const { return headingVectorFontId_ != 0 ? headingVectorFontId_ : headingBitmap_.fontId; }

  // Unload everything, unregister from renderer.
  void unloadAll(GfxRenderer& renderer);

  // Look up the font ID for the loaded family. Returns 0 if nothing loaded
  // or familyName doesn't match.
  int getFontId(const std::string& familyName) const;

  // Get name of currently loaded family (empty if none).
  const std::string& currentFamilyName() const { return loadedFamilyName_; };

  // Point size that was actually loaded (closest match to targetPtSize).
  // 0 if nothing loaded.
  uint8_t currentPointSize() const { return loadedPointSize_; };

 private:
  struct LoadedFont {
    SdCardFont* font;  // heap-allocated, owned
    int fontId;
    uint8_t size;
  };
  static int computeFontId(uint32_t contentHash, const char* familyName, uint8_t pointSize);

  // TrueType/OpenType family (PSRAM boards): state lives in the .cpp so this header stays light.
  struct VectorState;
  bool loadVectorFamily(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t pointSize,
                        VectorState*& slot, int& fontIdSlot);
  void unloadVector(GfxRenderer& renderer, VectorState*& slot, int& fontIdSlot);
  VectorState* vector_ = nullptr;
  int vectorFontId_ = 0;
  VectorState* headingVector_ = nullptr;
  int headingVectorFontId_ = 0;
  LoadedFont headingBitmap_{nullptr, 0, 0};

  std::string loadedFamilyName_;
  uint8_t loadedPointSize_ = 0;
  std::vector<LoadedFont> loaded_;
};
