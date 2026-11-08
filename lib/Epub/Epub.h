#pragma once

#include <ContentProtection.h>
#include <Print.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Epub/BookMetadataCache.h"
#include "Epub/css/CssParser.h"

class ZipFile;
namespace reader {
class ReaderCancellationToken;
}

class Epub {
  // the ncx file (EPUB 2)
  std::string tocNcxItem;
  // the nav file (EPUB 3)
  std::string tocNavItem;
  // where is the EPUBfile?
  std::string filepath;
  // FB2 is converted to an unpacked EPUB package.  Normal EPUBs remain ZIP
  // archives, but all item access also supports this directory form.
  bool unpackedPackage = false;
  // Set once in the constructor (see Epub.cpp) rather than re-checked with
  // Storage.exists() on every readItemContentsToStream() call, since that
  // runs for every chapter/image/CSS read of every book, FB2-origin or not.
  bool isFb2Origin = false;
  // the base path for items in the EPUB file
  std::string contentBasePath;
  // Stable cache path based on filepath
  std::string cachePath;
  // Spine and TOC cache
  std::unique_ptr<BookMetadataCache> bookMetadataCache;
  // Optional on-read access to protected (encrypted) entries; null for ordinary books.
  std::unique_ptr<freeink::content::ContentDecryptor> decryptor;
  // Why a protected book could not be opened ("access expired", "no content key for this book", ...).
  std::string protectionError;
  bool openProtection();
  // CSS parser for styling
  std::unique_ptr<CssParser> cssParser;
  // CSS files
  std::vector<std::string> cssFiles;
  enum class CssParseStatus : uint8_t {
    Failed,
    Partial,
    Complete,
  };

  void migrateLegacyCachePath(const std::string& cacheDir) const;
  std::string itemPath(const std::string& itemHref) const;
  bool findContentOpfFile(std::string* contentOpfFile) const;
  bool parseContentOpf(BookMetadataCache::BookMetadata& bookMetadata, bool writeSpineEntries = true);
  // The first JPEG/PNG reference in a cover document (the guide's cover page, or an SVG wrapper
  // named as the cover image), resolved against the document's folder; empty when it holds none.
  // Streams the document, never reads it whole into RAM.
  std::string findCoverImageInDocument(const std::string& docHref) const;
  // The cover picture's href, following an SVG/XHTML wrapper left in an older book cache.
  std::string resolvedCoverHref() const;
  // The picture a cover reference leads to: the reference itself when it names a JPEG or PNG, else
  // the picture of the document it names. A fragment is dropped.
  std::string coverPictureOf(const std::string& href) const;
  // The cover page an EPUB 3 navigation document's landmarks name (epub:type="cover"), resolved against the
  // document's folder; empty when it names none.
  std::string coverPageFromNavLandmarks(const std::string& navHref) const;
  // The best JPEG/PNG in the archive whose file name says cover (cover.jpg, cover-image.png ...).
  std::string coverPictureByName() const;
  bool parseTocNcxFile() const;
  bool parseTocNavFile() const;
  CssParseStatus parseCssFiles(bool forceRebuild = false) const;
  void discoverCssFilesFromZip();

 public:
  using ProgressFn = std::function<void(uint8_t progress)>;
  const std::string& getProtectionError() const { return protectionError; }
  explicit Epub(std::string filepath, const std::string& cacheDir);
  ~Epub() = default;
  static std::string cachePathForFilePath(const std::string& filepath, const std::string& cacheDir);
  // True when a metadata cache already exists for this book, i.e. load() will
  // hit the fast path instead of rebuilding. Cheap: no parsing, just a stat.
  static bool hasCache(const std::string& filepath, const std::string& cacheDir);
  std::string& getBasePath() { return contentBasePath; }
  bool load(bool buildIfMissing = true, bool skipLoadingCss = false, const ProgressFn& onProgress = nullptr);
  bool clearCache() const;
  void setupCacheDir() const;
  const std::string& getCachePath() const;
  const std::string& getTocNavItem() const { return tocNavItem; }
  const std::string& getTocNcxItem() const { return tocNcxItem; }
  const std::string& getPath() const;
  const std::string& getTitle() const;
  const std::string& getAuthor() const;
  const std::string& getLanguage() const;
  std::string getCoverBmpPath(bool cropped = false) const;
  bool generateCoverBmp(bool cropped = false, bool deferProgressive = false) const;
  std::string getThumbBmpPath() const;
  // Deprecated compatibility wrapper; forwards to getThumbBmpPath(0, height).
  [[deprecated("use getThumbBmpPath(int width, int height)")]]
  std::string getThumbBmpPath(int height) const;
  // Returns the thumbnail cache path. width <= 0 derives the default 3:5
  // (width:height) thumbnail width from height; height <= 0 uses the default
  // thumbnail height.
  std::string getThumbBmpPath(int width, int height) const;
  // Returns a Minimal-style adaptive thumbnail path. Normal cover ratios fill
  // the requested box; unusual ratios are contained inside the box.
  std::string getAdaptiveThumbBmpPath(int width, int height) const;
  // Deprecated compatibility wrapper; forwards to generateThumbBmp(0, height).
  [[deprecated("use generateThumbBmp(int width, int height)")]]
  bool generateThumbBmp(int height) const;
  // Writes a thumbnail BMP to cache. width <= 0 derives the default 3:5
  // (width:height) thumbnail width from height; height <= 0 uses the default
  // thumbnail height.
  // Returns false on missing cache/cover, unsupported image format, or conversion failure.
  bool generateThumbBmp(int width, int height) const;
  // Writes a thumbnail that can either crop-to-fill or contain unusual cover
  // ratios, depending on the source image dimensions.
  bool generateAdaptiveThumbBmp(int width, int height, bool deferProgressive = false) const;
  uint8_t* readItemContentsToBytes(const std::string& itemHref, size_t* size = nullptr,
                                   bool trailingNullByte = false) const;
  bool readItemContentsToStream(const std::string& itemHref, Print& out, size_t chunkSize,
                                const reader::ReaderCancellationToken* cancellationToken = nullptr) const;
  bool getItemSize(const std::string& itemHref, size_t* size) const;
  BookMetadataCache::SpineEntry getSpineItem(int spineIndex) const;
  BookMetadataCache::TocEntry getTocItem(int tocIndex) const;
  int getSpineItemsCount() const;
  int getTocItemsCount() const;
  int getSpineIndexForTocIndex(int tocIndex) const;
  int getTocIndexForSpineIndex(int spineIndex) const;

  // Resolve memory-safe internal spine chunks back to one visible chapter.
  // FB2 uses its persisted section identity; split EPUBs use generated
  // *_splitN hrefs. Returns [spineIndex, spineIndex] for unsplit content.
  bool getLogicalChapterBounds(int spineIndex, int& startIndex, int& endIndex) const;
  size_t getCumulativeSpineItemSize(int spineIndex) const;
  int getSpineIndexForTextReference() const;

  size_t getBookSize() const;
  float calculateProgress(int currentSpineIndex, float currentSpineRead) const;
  CssParser* getCssParser() const { return cssParser.get(); }
  // Section cache v48 is binary-compatible with v49 for ordinary EPUBs. FB2
  // packages must rebuild it because v49 changed their virtual split layout.
  bool isFb2Package() const { return isFb2Origin; }
  int resolveHrefToSpineIndex(const std::string& href) const;

 private:
  bool generateThumbBmpInternal(int width, int height, bool adaptiveContain, bool deferProgressive = false) const;
};
