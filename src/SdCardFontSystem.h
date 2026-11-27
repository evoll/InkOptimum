#pragma once

#include <SdCardFont.h>
#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>

#include <atomic>
#include <mutex>
#include <memory>
#include <string>

class GfxRenderer;

/// Facade that owns the SD card font registry, manager, and resolver logic.
/// Hides implementation details behind a single begin() + ensureLoaded() API.
class SdCardFontSystem {
 public:
  SdCardFontSystem() = default;
  SdCardFontSystem(const SdCardFontSystem&) = delete;
  SdCardFontSystem& operator=(const SdCardFontSystem&) = delete;
  /// Load the user's saved SD font selection. Call once during setup. The font list is scanned here only when a
  /// saved reading or UI font needs it; otherwise the scan waits for the first consumer (font picker, web UI,
  /// reader with an SD font), which saves the boot time and the list's heap for users who read in built-in fonts.
  void begin(GfxRenderer& renderer);

  /// Ensure the correct SD font family is loaded for the current settings.
  /// Call before entering the reader or after settings change.
  /// Also re-discovers if the registry has been marked dirty (e.g. by web upload).
  void ensureLoaded(GfxRenderer& renderer);

  /// Temporarily unload the active SD font without clearing the saved setting.
  /// Call ensureLoaded() later to restore it before reader rendering.
  void releaseLoadedFont(GfxRenderer& renderer);

  /// Resolve an SD card font ID from family name + fontSize enum.
  /// Returns 0 if not found. Used by InkMODSettings::getReaderFontId().
  int resolveFontId(const char* familyName, uint8_t fontSizeEnum) const;

  /// Load an arbitrary family at a fixed point size for a one-off preview
  /// (e.g. the font picker), independent of the user's saved selection.
  /// Returns the resolved font ID, or 0 if the family isn't found or fails
  /// to load. SdCardFontManager only ever keeps one family resident at a
  /// time, so this transparently replaces whatever was loaded before it -
  /// including the real active reader font. Callers MUST call
  /// ensureLoaded() again before returning to the reader / relying on the
  /// real active font, since this does not restore it automatically.
  int loadPreviewFamily(const std::string& familyName, GfxRenderer& renderer, uint8_t targetPointSize);

  /// Change the reader font size using the active SD family when one is selected.
  bool changeReaderFontSize(bool larger);

  /// Load (or drop) the dedicated UI glyph font named by SETTINGS.uiFontFamily: the family file closest to the
  /// built-in UI size, kept apart from the reading font. Cheap when nothing changed. Call after the setting changes.
  void applyUiFont(GfxRenderer& renderer);

  /// Load (or drop) the spare font for book text: the SETTINGS.uiFontFamily family (bitmap .cpfont only) at the
  /// reading point size, when SETTINGS.bookSpareFont is on and an SD reading font is active. Supplies glyphs the
  /// reading font lacks. Publishes its identity to BookFont so section caches are laid out again when it changes.
  /// Cheap when nothing changed.
  void applySpareFont(GfxRenderer& renderer);

  /// Access the registry (e.g. for settings UI to enumerate available fonts).
  /// The SD card is scanned on the first call when nothing at boot needed the list (see begin()).
  const SdCardFontRegistry& registry() const {
    refreshIfDirty();
    return registry_;
  }

  /// Non-const access to the registry (for FontInstaller).
  SdCardFontRegistry& registry() {
    refreshIfDirty();
    return registry_;
  }

  /// Mark the registry as needing re-discovery.
  /// Thread-safe: can be called from the web server task.
  void markRegistryDirty() { registryDirty_.store(true, std::memory_order_release); }

  /// If the registry is dirty, re-scan the SD card now and clear the flag.
  /// Used by the web UI so uploaded/deleted fonts appear in the list
  /// without waiting for the reader activity to run ensureLoaded().
  /// A second task that asks while the scan runs waits for it (it never sees a half-built list).
  void refreshIfDirty() const {
    std::lock_guard<std::mutex> lock(discoverMutex_);
    if (registryDirty_.exchange(false, std::memory_order_acquire)) {
      registry_.discover();
    }
  }

 private:
  mutable SdCardFontRegistry registry_;
  SdCardFontManager manager_;
  // Loads, keeps or drops the larger heading size to match SETTINGS.largeHeadings and the loaded body font, and
  // publishes it to HeadingFont. A failed attempt is not repeated for the same body font.
  void syncHeadingFont(GfxRenderer& renderer);
  void ensureBodyLoaded(GfxRenderer& renderer);
  std::string headingAttemptKey_;
  mutable std::mutex discoverMutex_;
  mutable std::atomic<bool> registryDirty_{false};
  std::unique_ptr<SdCardFont> uiFont_;
  std::string uiFontLoadedFamily_;
  std::unique_ptr<SdCardFont> spareFont_;
  std::string spareLoadedFamily_;
  uint8_t spareLoadedPt_ = 0;
  uint8_t uiFontLoadedPt_ = 0;  // point size of the loaded UI glyph font (follows the interface text size)
};

// Global SD card font system instance (defined in main.cpp).
extern SdCardFontSystem sdFontSystem;
