#include "SdCardFontManager.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <SdCardFont.h>
#include <SdCardFontRegistry.h>

#include <new>
#include <utility>

#if INKMOD_VECTOR_FONTS
#include <FontPsram.h>
#include <HalStorage.h>
#include <TtfEpdFont.h>
#include <esp_heap_caps.h>
#endif

#if INKMOD_VECTOR_FONTS
// One open TTF/OTF/TTC per style role. Small files are read fully into PSRAM (fastest, GPOS
// kerning available); big ones (multi-MB variable/CJK) stream from the SD card on demand.
struct SdCardFontManager::VectorState {
  struct Source {
    HalFile file;
    freeink::font::PsramVector<uint8_t> bytes;
    bool present = false;
    bool streamed = false;
    unsigned long size = 0;
  };
  Source sources[4];
  TtfEpdFont font;

  static unsigned long streamRead(void* ctx, unsigned long offset, unsigned char* buffer, unsigned long count) {
    auto* s = static_cast<Source*>(ctx);
    if (!s || !s->file) return 0;
    if (!s->file.seek(static_cast<size_t>(offset))) return 0;
    if (count == 0) return 0;  // seek probe
    const int n = s->file.read(buffer, count);
    return n < 0 ? 0 : static_cast<unsigned long>(n);
  }

  bool open(uint8_t style, const std::string& path) {
    static constexpr size_t kResidentMax = 4 * 1024 * 1024;
    static constexpr size_t kPsramHeadroom = 512 * 1024;
    if (style >= 4 || path.empty()) return false;
    Source& s = sources[style];
    HalFile f = Storage.open(path.c_str());
    if (!f) {
      LOG_ERR("SDMGR", "Failed to open TTF: %s", path.c_str());
      return false;
    }
    const size_t len = f.size();
    if (len == 0) {
      f.close();
      return false;
    }
    const bool resident = len <= kResidentMax && heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) >= len + kPsramHeadroom;
    if (resident) {
      s.bytes.resize(len);
      const int got = f.read(s.bytes.data(), len);
      f.close();
      if (static_cast<size_t>(got) != len) {
        LOG_ERR("SDMGR", "Short read on TTF %s (%d/%u)", path.c_str(), got, static_cast<unsigned>(len));
        s.bytes.clear();
        return false;
      }
      s.streamed = false;
    } else {
      s.file = std::move(f);  // kept open for the font's lifetime
      s.streamed = true;
    }
    s.size = static_cast<unsigned long>(len);
    s.present = true;
    return true;
  }

  void addSources() {
    for (uint8_t st = 0; st < 4; ++st) {
      Source& s = sources[st];
      if (!s.present) continue;
      if (s.streamed) {
        font.addStreamSource(st, &VectorState::streamRead, &s, s.size);
      } else {
        font.addResidentSource(st, s.bytes.data(), static_cast<uint32_t>(s.bytes.size()));
      }
    }
  }
};
#endif

SdCardFontManager::~SdCardFontManager() {
  for (auto& lf : loaded_) {
    delete lf.font;
  }
#if INKMOD_VECTOR_FONTS
  delete vector_;
#endif
}

// FNV-1a continuation: seeds with contentHash, then hashes family name + point size.
// Produces a deterministic ID that is stable across load/unload cycles and reboots,
// and changes when font content changes (different header/TOC = different contentHash).
int SdCardFontManager::computeFontId(uint32_t contentHash, const char* familyName, uint8_t pointSize) {
  static constexpr uint32_t FNV_PRIME = 16777619u;
  uint32_t hash = contentHash;
  while (*familyName) {
    hash ^= static_cast<uint8_t>(*familyName++);
    hash *= FNV_PRIME;
  }
  hash ^= pointSize;
  hash *= FNV_PRIME;
  int id = static_cast<int>(hash);
  return id != 0 ? id : 1;  // 0 is reserved as "not found" sentinel
}

bool SdCardFontManager::loadFamily(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t targetPointSize,
                                   uint8_t sizeStep) {
  // Unload any previously loaded family first
  if (!loadedFamilyName_.empty()) {
    unloadAll(renderer);
  }

  const SdCardFontFileInfo* selected = family.selectFile(targetPointSize, sizeStep);
  if (!selected) {
    LOG_ERR("SDMGR", "Family %s has no files to load", family.name.c_str());
    return false;
  }

  if (family.vector) {
    if (!loadVectorFamily(family, renderer, selected->pointSize)) return false;
    loadedFamilyName_ = family.name;
    loadedPointSize_ = selected->pointSize;
    return true;
  }

  auto* font = new (std::nothrow) SdCardFont();
  if (!font) {
    LOG_ERR("SDMGR", "Failed to allocate SdCardFont for %s", selected->path.c_str());
    return false;
  }

  if (!font->load(selected->path.c_str())) {
    LOG_ERR("SDMGR", "Failed to load %s", selected->path.c_str());
    delete font;
    return false;
  }

  int fontId = computeFontId(font->contentHash(), family.name.c_str(), selected->pointSize);
  // Guard against collision with built-in font IDs (astronomically unlikely
  // with FNV-1a hashes, but provides a safety net)
  if (renderer.getFontMap().count(fontId) != 0) {
    LOG_ERR("SDMGR", "Font ID %d collides with existing font, skipping %s", fontId, selected->path.c_str());
    delete font;
    return false;
  }
  renderer.registerSdCardFont(fontId, font);
  loaded_.push_back({font, fontId, selected->pointSize});

  LOG_DBG("SDMGR", "Loaded %s size=%u id=%d styles=%u (target=%u step=%u)", selected->path.c_str(), selected->pointSize,
          fontId, font->styleCount(), targetPointSize, sizeStep);

  EpdFontFamily fontFamily(font->getEpdFont(0), font->getEpdFont(1), font->getEpdFont(2), font->getEpdFont(3));
  renderer.insertFont(fontId, fontFamily);

  loadedFamilyName_ = family.name;
  loadedPointSize_ = selected->pointSize;
  return true;
}

bool SdCardFontManager::loadVectorFamily(const SdCardFontFamilyInfo& family, GfxRenderer& renderer,
                                         const uint8_t pointSize) {
#if INKMOD_VECTOR_FONTS
  auto* state = new (std::nothrow) VectorState();
  if (!state) {
    LOG_ERR("SDMGR", "OOM: vector font state for %s", family.name.c_str());
    return false;
  }
  for (uint8_t st = 0; st < 4; ++st) {
    if (!family.vectorPaths[st].empty()) state->open(st, family.vectorPaths[st]);  // optional styles may fail
  }
  if (!state->sources[0].present) {
    LOG_ERR("SDMGR", "Regular face of %s could not be opened", family.name.c_str());
    delete state;
    return false;
  }
  state->addSources();
  if (!state->font.load(pointSize)) {
    LOG_ERR("SDMGR", "Failed to parse TTF family %s", family.name.c_str());
    delete state;
    return false;
  }
  const int fontId = computeFontId(static_cast<uint32_t>(state->sources[0].size) ^ 0x54544Fu, family.name.c_str(), pointSize);
  if (renderer.getFontMap().count(fontId) != 0) {
    LOG_ERR("SDMGR", "Font ID %d collides with existing font, skipping %s", fontId, family.name.c_str());
    delete state;
    return false;
  }
  renderer.insertFont(fontId, state->font.family());
  vector_ = state;
  vectorFontId_ = fontId;
  LOG_DBG("SDMGR", "Loaded TTF family %s size=%u id=%d", family.name.c_str(), pointSize, fontId);
  return true;
#else
  (void)renderer;
  (void)pointSize;
  LOG_ERR("SDMGR", "TTF fonts need a PSRAM board: %s", family.name.c_str());
  return false;
#endif
}

void SdCardFontManager::unloadVector(GfxRenderer& renderer) {
#if INKMOD_VECTOR_FONTS
  if (vector_) {
    renderer.removeFont(vectorFontId_);
    delete vector_;
    vector_ = nullptr;
  }
#else
  (void)renderer;
#endif
  vectorFontId_ = 0;
}

void SdCardFontManager::unloadAll(GfxRenderer& renderer) {
  unloadVector(renderer);
  renderer.clearSdCardFonts();
  for (auto& lf : loaded_) {
    renderer.removeFont(lf.fontId);
    delete lf.font;
  }
  loaded_.clear();
  loadedFamilyName_.clear();
  loadedPointSize_ = 0;
}

int SdCardFontManager::getFontId(const std::string& familyName) const {
  if (familyName != loadedFamilyName_) return 0;
  if (vectorFontId_ != 0) return vectorFontId_;
  if (loaded_.empty()) return 0;
  return loaded_.front().fontId;
}
