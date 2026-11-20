#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "Epub.h"

class Page;
class GfxRenderer;
namespace reader {
class ReaderCancellationToken;
}

class Section {
  std::shared_ptr<Epub> epub;
  const int spineIndex;
  GfxRenderer& renderer;
  const bool useInlineFootnotes;
  std::string filePath;
  HalFile file;
  static inline bool inlineFootnotePreviewsEnabled = false;

  // Layout generation is part of the cache filename rather than the binary
  // record format. This forces one clean repagination after layout-semantics
  // fixes (notably Paragraph spacing=None preserving structural title/subtitle
  // spacing) without changing TextBlock/Page serialization or requiring the
  // user to clear .inkmod manually.
  static constexpr const char* LAYOUT_CACHE_GENERATION = "_layout66";

  bool writeSectionFileHeader(int fontId, float lineCompression, uint8_t extraParagraphSpacing, bool forceParagraphIndents,
                              uint8_t paragraphAlignment, uint16_t viewportWidth, uint16_t viewportHeight,
                              bool hyphenationEnabled, bool embeddedStyle, uint8_t imageRendering,
                              bool bionicReadingEnabled, bool guideReadingEnabled);
  uint32_t onPageComplete(std::unique_ptr<Page> page);

 public:
  uint16_t pageCount = 0;
  uint16_t leadingFrontMatterPages = 0;
  int currentPage = 0;

  // Inline footnote previews put the note text into the pages, so those pages live in their own cache
  // variant ("_inlfn"). The reader switches this on per opened book (setting on and footnotes.bin
  // gathered); a failed note read switches it off again so the next build falls back to plain pages.
  static void setInlineFootnotePreviews(bool enabled) { inlineFootnotePreviewsEnabled = enabled; }
  static bool inlineFootnotePreviewsActive() { return inlineFootnotePreviewsEnabled; }

  explicit Section(const std::shared_ptr<Epub>& epub, const int spineIndex, GfxRenderer& renderer,
                   const char* cacheSuffix = "")
      : epub(epub),
        spineIndex(spineIndex),
        renderer(renderer),
        useInlineFootnotes(inlineFootnotePreviewsEnabled),
        filePath(epub->getCachePath() + "/sections/" + std::to_string(spineIndex) + LAYOUT_CACHE_GENERATION +
                 (cacheSuffix ? cacheSuffix : "") + (inlineFootnotePreviewsEnabled ? "_inlfn" : "") + ".bin") {}
  ~Section() = default;
  // A missing section cache is a normal first-open condition, not an I/O error.
  // This lightweight probe lets callers avoid opening/logging cache variants
  // that have never been generated. A .bak also counts because loadSectionFile()
  // can restore it after an interrupted atomic promotion.
  bool hasSectionFile() const;
  bool loadSectionFile(int fontId, float lineCompression, uint8_t extraParagraphSpacing, bool forceParagraphIndents,
                       uint8_t paragraphAlignment, uint16_t viewportWidth, uint16_t viewportHeight,
                       bool hyphenationEnabled, bool embeddedStyle, uint8_t imageRendering, bool bionicReadingEnabled,
                       bool guideReadingEnabled);
  bool clearCache() const;
  bool createSectionFile(int fontId, float lineCompression, uint8_t extraParagraphSpacing, bool forceParagraphIndents,
                         uint8_t paragraphAlignment, uint16_t viewportWidth, uint16_t viewportHeight,
                         bool hyphenationEnabled, bool embeddedStyle, uint8_t imageRendering, bool bionicReadingEnabled,
                         bool guideReadingEnabled, const std::function<void()>& popupFn = nullptr,
                         bool* imagesWereSuppressed = nullptr, bool* layoutAbortedForLowMemory = nullptr,
                         const reader::ReaderCancellationToken* cancellationToken = nullptr,
                         bool* cancelled = nullptr);
  std::unique_ptr<Page> loadPageFromSectionFile();

  // Background preparation of a chapter that is not on screen. Decoding an
  // inline image uses the shared frame buffer as scratch space, which would
  // corrupt the page the reader is showing. With this set, createSectionFile()
  // stops right after the XHTML source is cached when the chapter references
  // images and reports it through skippedForImages; nothing else is written.
  bool noFramebufferScratch = false;
  bool skippedForImages = false;

  // Look up the page number for an anchor id from the section cache file.
  std::optional<uint16_t> getPageForAnchor(const std::string& anchor) const;

  // Look up the page number for a synthetic paragraph index from XPath p[N].
  std::optional<uint16_t> getPageForParagraphIndex(uint16_t pIndex) const;

  // Look up the page number for a running list-item index from the li LUT.
  std::optional<uint16_t> getPageForListItemIndex(uint16_t liIndex) const;

  // Look up the synthetic paragraph index for the given rendered page.
  std::optional<uint16_t> getParagraphIndexForPage(uint16_t page) const;
};