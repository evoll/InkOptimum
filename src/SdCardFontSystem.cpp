#include "SdCardFontSystem.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <SdCardFont.h>

#include <new>

#include <Epub/BookFont.h>
#include <Epub/HeadingFont.h>

#include <MemoryBudget.h>

#include "InkMODSettings.h"

void SdCardFontSystem::begin(GfxRenderer& renderer) {
  // Scan the card now only if a saved font needs the list; otherwise leave the scan to the first consumer.
  if (SETTINGS.sdFontFamilyName[0] != '\0' || SETTINGS.uiFontFamily[0] != '\0') {
    registry_.discover();
  } else {
    registryDirty_.store(true, std::memory_order_release);
  }

  // Register this system as the SD font ID resolver in settings.
  // Uses a static trampoline since InkMODSettings stores a plain function pointer.
  SETTINGS.sdFontIdResolver = [](void* ctx, const char* familyName, uint8_t fontSizeEnum) -> int {
    return static_cast<SdCardFontSystem*>(ctx)->resolveFontId(familyName, fontSizeEnum);
  };
  SETTINGS.sdFontResolverCtx = this;

  // If user has a saved SD font selection, load it
  if (SETTINGS.sdFontFamilyName[0] != '\0') {
    const auto* family = registry_.findFamily(SETTINGS.sdFontFamilyName);
    if (family) {
      if (manager_.loadFamily(*family, renderer, SETTINGS.getSdFontTargetPointSize(), SETTINGS.fontSize)) {
        LOG_DBG("SDFS", "Loaded SD card font family: %s", SETTINGS.sdFontFamilyName);
      } else {
        LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", SETTINGS.sdFontFamilyName);
        SETTINGS.sdFontFamilyName[0] = '\0';
      }
    } else {
      LOG_DBG("SDFS", "SD font family not found on card: %s (clearing)", SETTINGS.sdFontFamilyName);
      SETTINGS.sdFontFamilyName[0] = '\0';
    }
  }

  applyUiFont(renderer);
  syncHeadingFont(renderer);

  LOG_DBG("SDFS", "SD font system ready (%s)", registryDirty_.load() ? "font scan deferred" : "fonts discovered");
}

void SdCardFontSystem::applyUiFont(GfxRenderer& renderer) {
  const std::string wanted = SETTINGS.uiFontFamily;
  if (wanted.empty() && uiFontLoadedFamily_.empty()) return;  // nothing to load or drop: no need to scan the card
  refreshIfDirty();
  // The glyph font follows the interface text size: 10 pt standard, 11 pt medium, 12 pt large (the built-in Inter
  // sizes), so CJK/other-script UI text keeps the proportions of the Latin text beside it.
  const uint8_t wantedPt = SETTINGS.uiTextSize == InkMODSettings::UI_TEXT_SIZE_LARGE    ? 12
                           : SETTINGS.uiTextSize == InkMODSettings::UI_TEXT_SIZE_MEDIUM ? 11
                                                                                        : 10;
  if (wanted == uiFontLoadedFamily_ && (wanted.empty() || (uiFont_ && uiFontLoadedPt_ == wantedPt))) return;

  renderer.setDedicatedUiGlyphFont(nullptr);  // before the font goes away
  uiFont_.reset();
  uiFontLoadedFamily_.clear();
  uiFontLoadedPt_ = 0;
  if (wanted.empty()) return;

  const auto* family = registry_.findFamily(wanted);
  // Vector (TTF/OTF) families render at any size through the reader path; the UI glyph font is a bitmap
  // .cpfont only, so those stay unsupported here.
  if (!family || family->vector) {
    LOG_DBG("SDFS", "UI font family unusable: %s", wanted.c_str());
    return;
  }
  const auto* file = family->findClosestFile(wantedPt);  // matches the built-in UI body size
  if (!file) return;
  if (ESP.getMaxAllocHeap() < 48 * 1024) {
    LOG_ERR("SDFS", "Not enough heap for the UI font %s", wanted.c_str());
    return;
  }
  std::unique_ptr<SdCardFont> font(new (std::nothrow) SdCardFont());
  if (!font || !font->load(file->path.c_str())) {
    LOG_ERR("SDFS", "Failed to load UI font %s", file->path.c_str());
    return;
  }
  uiFont_ = std::move(font);
  uiFontLoadedFamily_ = wanted;
  uiFontLoadedPt_ = wantedPt;
  renderer.setDedicatedUiGlyphFont(uiFont_.get());
  LOG_DBG("SDFS", "UI glyph font: %s (%u pt)", wanted.c_str(), file->pointSize);
}

void SdCardFontSystem::applySpareFont(GfxRenderer& renderer) {
  std::string wanted;
  uint8_t wantedPt = 0;
  if (SETTINGS.bookSpareFont != 0 && SETTINGS.uiFontFamily[0] != '\0' && SETTINGS.sdFontFamilyName[0] != '\0' &&
      !manager_.currentFamilyName().empty()) {
    wanted = SETTINGS.uiFontFamily;
    wantedPt = SETTINGS.getSdFontTargetPointSize();
  }
  if (wanted == spareLoadedFamily_ && wantedPt == spareLoadedPt_) return;

  renderer.setBookSpareFont(nullptr);  // before the font goes away
  BookFont::setSpareKey(0);
  spareFont_.reset();
  spareLoadedFamily_.clear();
  spareLoadedPt_ = 0;
  if (wanted.empty()) return;

  refreshIfDirty();
  const auto* family = registry_.findFamily(wanted);
  if (!family || family->vector) {  // like the UI glyph font: a bitmap .cpfont family only
    LOG_DBG("SDFS", "Spare font family unusable: %s", wanted.c_str());
    return;
  }
  const auto* file = family->findClosestFile(wantedPt);
  if (!file) return;
  constexpr uint32_t kBitmapHeadroom = 70 * 1024;  // a bitmap size keeps its tables in RAM
  if (ESP.getMaxAllocHeap() < kBitmapHeadroom) {
    LOG_ERR("SDFS", "Not enough heap for the spare font %s", wanted.c_str());
    return;
  }
  std::unique_ptr<SdCardFont> font(new (std::nothrow) SdCardFont());
  if (!font || !font->load(file->path.c_str())) {
    LOG_ERR("SDFS", "Failed to load spare font %s", file->path.c_str());
    return;
  }
  spareFont_ = std::move(font);
  spareLoadedFamily_ = wanted;
  spareLoadedPt_ = wantedPt;
  renderer.setBookSpareFont(spareFont_.get());
  // FNV-1a over family name and point size: a different spare font or size must lay the book out again.
  uint32_t key = 2166136261u;
  for (const char c : wanted) key = (key ^ static_cast<uint8_t>(c)) * 16777619u;
  key = (key ^ file->pointSize) * 16777619u;
  BookFont::setSpareKey(key == 0 ? 1u : key);
  LOG_DBG("SDFS", "Spare font: %s (%u pt)", wanted.c_str(), file->pointSize);
}

void SdCardFontSystem::ensureLoaded(GfxRenderer& renderer) {
  ensureBodyLoaded(renderer);
  syncHeadingFont(renderer);
  applySpareFont(renderer);
}

void SdCardFontSystem::syncHeadingFont(GfxRenderer& renderer) {
  if (!SETTINGS.largeHeadings || manager_.currentFamilyName().empty()) {
    manager_.unloadHeadingFont(renderer);
    headingAttemptKey_.clear();
    HeadingFont::set(0);
    return;
  }
  const std::string key = manager_.currentFamilyName() + "@" + std::to_string(manager_.currentPointSize());
  if (manager_.headingFontId() == 0 && key != headingAttemptKey_) {
    headingAttemptKey_ = key;
    const auto* family = registry_.findFamily(manager_.currentFamilyName());
    // A bitmap size keeps its tables in RAM: only take it when the heap has clear room beside the body font.
    constexpr uint32_t kBitmapHeadroom = 70 * 1024;
    if (family && (family->vector || MemoryBudget::snapshot().maxAllocHeap >= kBitmapHeadroom)) {
      if (manager_.loadHeadingFont(*family, renderer)) {
        LOG_DBG("SDFS", "Heading font loaded for %s", key.c_str());
      }
    }
  }
  HeadingFont::set(manager_.headingFontId());
}

void SdCardFontSystem::ensureBodyLoaded(GfxRenderer& renderer) {
  // If the web server (or another task) installed/deleted fonts, re-discover.
  // Track whether we just re-discovered so we can force a reload below even
  // when the wanted family/size still maps to the same point size — the file
  // contents on disk may have changed (e.g. user re-uploaded a new build).
  const char* wantedFamily = SETTINGS.sdFontFamilyName;
  const std::string& currentFamily = manager_.currentFamilyName();
  const uint8_t targetPointSize = SETTINGS.getSdFontTargetPointSize();
  const uint8_t sizeStep = SETTINGS.fontSize;

  if (wantedFamily[0] == '\0') {
    // Built-in font: nothing needs the list, so a pending scan stays pending.
    if (!currentFamily.empty()) {
      manager_.unloadAll(renderer);
    }
    return;
  }

  std::unique_lock<std::mutex> lock(discoverMutex_);
  const bool registryWasDirty = registryDirty_.exchange(false, std::memory_order_acquire);
  if (registryWasDirty) {
    LOG_DBG("SDFS", "Registry dirty — re-discovering fonts");
    registry_.discover();
  }
  lock.unlock();

  // Reload if family changed OR if the user-selected size maps to a
  // different file than what's currently loaded OR if the registry was
  // just rediscovered (file may have been replaced on disk).
  bool familyMatches = (currentFamily == wantedFamily);
  if (familyMatches) {
    const auto* family = registry_.findFamily(wantedFamily);
    if (!family) {
      LOG_DBG("SDFS", "SD font family disappeared: %s (clearing)", wantedFamily);
      manager_.unloadAll(renderer);
      SETTINGS.sdFontFamilyName[0] = '\0';
      return;
    }
    const auto* wantedFile = family->selectFile(targetPointSize, sizeStep);
    uint8_t wantedPt = wantedFile ? wantedFile->pointSize : 0;
    if (!registryWasDirty && wantedPt == manager_.currentPointSize()) return;
    LOG_DBG("SDFS", "Reloading %s: size %u -> %u (target %u step %u)%s", wantedFamily, manager_.currentPointSize(),
            wantedPt, targetPointSize, sizeStep, registryWasDirty ? " [registry dirty]" : "");
  }

  if (!currentFamily.empty()) {
    manager_.unloadAll(renderer);
  }

  const auto* family = registry_.findFamily(wantedFamily);
  if (family) {
    if (manager_.loadFamily(*family, renderer, targetPointSize, sizeStep)) {
      LOG_DBG("SDFS", "Loaded SD card font family: %s", wantedFamily);
    } else {
      LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", wantedFamily);
      SETTINGS.sdFontFamilyName[0] = '\0';
    }
  } else {
    LOG_DBG("SDFS", "SD font family not found: %s (clearing)", wantedFamily);
    SETTINGS.sdFontFamilyName[0] = '\0';
  }
}

void SdCardFontSystem::releaseLoadedFont(GfxRenderer& renderer) {
  if (manager_.currentFamilyName().empty()) return;

  // Log while the manager still owns the name. The previous code copied it to
  // a new std::string solely so it survived unloadAll(), creating a heap
  // allocation at exactly the point where this function is trying to free RAM
  // for a low-memory operation. In release LOG_DBG is compiled out entirely.
  LOG_DBG("SDFS", "Releasing SD card font before low-memory operation: %s", manager_.currentFamilyName().c_str());
  manager_.unloadAll(renderer);
  headingAttemptKey_.clear();
  HeadingFont::set(0);
  applySpareFont(renderer);  // no SD reading font left: the spare goes too
}

int SdCardFontSystem::resolveFontId(const char* familyName, uint8_t /*fontSizeEnum*/) const {
  // The manager loads exactly one size (closest to SETTINGS.fontSize), so the
  // enum is implicit — always return the single loaded font ID for this family.
  // ensureLoaded() must have been called with the current settings before this.
  return manager_.getFontId(familyName);
}

int SdCardFontSystem::loadPreviewFamily(const std::string& familyName, GfxRenderer& renderer,
                                        const uint8_t targetPointSize) {
  refreshIfDirty();
  refreshIfDirty();
  const auto* family = registry_.findFamily(familyName);
  if (!family) return 0;

  // SdCardFontFamilyInfo::selectFile() interprets sizeStep as the rank in the
  // family's sorted point sizes (0 = smallest), not as "automatic/closest".
  // Passing 0 here therefore made previews jump between each family's minimum
  // size (18 pt Literata, 24 pt Merriweather, ...), even though the log said
  // targetPt=14. Pick the rank whose actual point size is closest to the
  // reader's current point size so scrolling changes the face, not the scale.
  const auto sizes = family->availableSizes();
  uint8_t previewSizeStep = 0;
  if (!sizes.empty()) {
    uint8_t bestDiff = UINT8_MAX;
    for (size_t i = 0; i < sizes.size(); ++i) {
      const uint8_t size = sizes[i];
      const uint8_t diff = size > targetPointSize ? size - targetPointSize : targetPointSize - size;
      if (diff < bestDiff || (diff == bestDiff && size < sizes[previewSizeStep])) {
        previewSizeStep = static_cast<uint8_t>(i);
        bestDiff = diff;
      }
    }
  }

  HeadingFont::set(0);  // loadFamily replaces whatever was resident, the heading size included
  headingAttemptKey_.clear();
  if (!manager_.loadFamily(*family, renderer, targetPointSize, previewSizeStep)) return 0;
  return manager_.getFontId(familyName);
}

bool SdCardFontSystem::changeReaderFontSize(const bool larger) {
  if (SETTINGS.sdFontFamilyName[0] != '\0') {
    refreshIfDirty();
    const auto* family = registry_.findFamily(SETTINGS.sdFontFamilyName);
    if (family) {
      const auto sizes = family->availableSizes();
      if (sizes.size() > 1) {
        uint8_t current = SETTINGS.fontSize < sizes.size() ? SETTINGS.fontSize : static_cast<uint8_t>(sizes.size() - 1);
        if (larger) {
          current = static_cast<uint8_t>((current + 1) % sizes.size());
        } else {
          current = current == 0 ? static_cast<uint8_t>(sizes.size() - 1) : static_cast<uint8_t>(current - 1);
        }
        SETTINGS.fontSize = current;
        return true;
      }
    }
  }

  // The built-in DejaVu Sans family currently has exactly one compiled size.
  // Keep quick/long-press font-size actions honest instead of cycling the
  // legacy 8-20 pt enum while rendering the same 12 pt glyphs every time.
  if (SETTINGS.sdFontFamilyName[0] == '\0' && SETTINGS.fontFamily == InkMODSettings::TEST_FONTS) {
    const uint8_t stored = InkMODSettings::getStoredReaderFontSize(InkMODSettings::SMALL);
    SETTINGS.fontSize = stored == UINT8_MAX ? 0 : stored;
    return false;
  }

  return SETTINGS.changeReaderFontSize(larger);
}
