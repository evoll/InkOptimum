#include "Epub.h"

#include <Fb2.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <JpegToBmpConverter.h>
#include <JpegTypeDetector.h>
#include <Logging.h>
#include <Memory.h>
#include <PngToBmpConverter.h>
#include <ReaderWork.h>
#include <ZipFile.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <utility>

#include "Epub/BookCover.h"
#include "Epub/CoverImageRefScanner.h"
#include "Epub/CoverNameScan.h"
#include "Epub/NavCoverLandmark.h"
#include "Epub/TocFallbackPolicy.h"
#include "Epub/parsers/ContainerParser.h"
#include "Epub/parsers/ContentOpfParser.h"
#include "Epub/parsers/TocNavParser.h"
#include "Epub/parsers/TocNcxParser.h"

namespace {

// A markup document rather than a picture: an SVG wrapper (or an XHTML page) named as the cover.
bool isCoverWrapperDocument(const std::string& href) {
  for (const char* ext : {".svg", ".xhtml", ".html", ".htm"}) {
    if (FsHelpers::checkFileExtension(href, ext)) return true;
  }
  return false;
}

constexpr int kDefaultThumbHeight = 180;

int32_t readLe32(const uint8_t* data) {
  return static_cast<int32_t>(static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                              (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24));
}

void normalizeThumbDimensions(int& width, int& height) {
  if (height <= 0) {
    height = kDefaultThumbHeight;
  }
  if (width <= 0) {
    width = static_cast<int>((static_cast<int64_t>(height) * 3 + 2) / 5);
  }
}

bool cachedBmpMatchesDimensions(const std::string& path, const int width, const int height,
                                const bool allowContainedDimensions = false) {
  if (!Storage.exists(path.c_str())) {
    return false;
  }

  FsFile file;
  if (!Storage.openFileForRead("EBP", path, file)) {
    return false;
  }

  uint8_t header[26] = {};
  const bool hasHeader = file.size() >= sizeof(header) && file.read(header, sizeof(header)) == sizeof(header);
  file.close();
  const bool isBmp = hasHeader && header[0] == 'B' && header[1] == 'M';
  const int32_t bmpWidth = isBmp ? readLe32(header + 18) : 0;
  const int32_t bmpHeight = isBmp ? readLe32(header + 22) : 0;
  const int32_t absHeight = bmpHeight < 0 ? -bmpHeight : bmpHeight;
  const bool exactMatch = isBmp && bmpWidth == width && absHeight == height;
  const bool containedMatch = allowContainedDimensions && isBmp && bmpWidth > 0 && absHeight > 0 && bmpWidth <= width &&
                              absHeight <= height && (bmpWidth == width || absHeight == height);
  const bool matches = exactMatch || containedMatch;
  if (!matches) {
    LOG_DBG("EBP", "Removing stale thumbnail dimensions: %s (%dx%d expected %dx%d)", path.c_str(), bmpWidth, absHeight,
            width, height);
    Storage.remove(path.c_str());
  }
  return matches;
}

std::string getThumbBmpPathForDimensions(const std::string& cachePath, int width, int height) {
  return cachePath + "/thumb_" + std::to_string(width) + "x" + std::to_string(height) + ".bmp";
}

std::string getAdaptiveThumbBmpPathForDimensions(const std::string& cachePath, int width, int height) {
  return cachePath + "/thumb_" + std::to_string(width) + "x" + std::to_string(height) + "_fit.bmp";
}

std::string legacyCachePathForFilePath(const std::string& filepath, const std::string& cacheDir) {
  return cacheDir + "/epub_" + std::to_string(std::hash<std::string>{}(filepath));
}
}  // namespace

Epub::Epub(std::string filepath, const std::string& cacheDir) : filepath(std::move(filepath)) {
  HalFile source;
  if (Storage.openFileForRead("EBP", this->filepath, source)) {
    unpackedPackage = source.isDirectory();
    source.close();
  }
  cachePath = cachePathForFilePath(this->filepath, cacheDir);
  isFb2Origin = unpackedPackage && Storage.exists((cachePath + Fb2::SOURCE_MARKER_FILE).c_str());
  migrateLegacyCachePath(cacheDir);
}

std::string Epub::itemPath(const std::string& itemHref) const {
  const std::string normalized = FsHelpers::normalisePath(itemHref);
  return unpackedPackage ? filepath + "/" + normalized : normalized;
}

std::string Epub::cachePathForFilePath(const std::string& filepath, const std::string& cacheDir) {
  constexpr char kPackageSuffix[] = "/package.epub";
  constexpr size_t kSuffixLen = sizeof(kPackageSuffix) - 1;
  if (filepath.size() > kSuffixLen && filepath.compare(filepath.size() - kSuffixLen, kSuffixLen, kPackageSuffix) == 0) {
    const std::string parentDir = filepath.substr(0, filepath.size() - kSuffixLen);
    const std::string prefix = cacheDir + "/";
    if (parentDir.compare(0, prefix.size(), prefix) == 0 &&
        (Storage.exists((filepath + "/META-INF/container.xml").c_str()) ||
         Storage.exists((parentDir + "/.browser_prepared_epub").c_str()))) {
      return parentDir;
    }
  }
  return cacheDir + "/epub_" + std::to_string(ZipFile::fnvHash64(filepath.c_str(), filepath.size()));
}

bool Epub::hasCache(const std::string& filepath, const std::string& cacheDir) {
  return BookMetadataCache::exists(cachePathForFilePath(filepath, cacheDir));
}

void Epub::migrateLegacyCachePath(const std::string& cacheDir) const {
  if (Storage.exists(cachePath.c_str())) {
    return;
  }

  const std::string legacyCachePath = legacyCachePathForFilePath(filepath, cacheDir);
  if (legacyCachePath == cachePath || !Storage.exists(legacyCachePath.c_str())) {
    return;
  }

  if (Storage.rename(legacyCachePath.c_str(), cachePath.c_str())) {
    LOG_INF("EBP", "Migrated legacy EPUB cache: %s -> %s", legacyCachePath.c_str(), cachePath.c_str());
  } else {
    LOG_ERR("EBP", "Failed to migrate legacy EPUB cache: %s -> %s", legacyCachePath.c_str(), cachePath.c_str());
  }
}

bool Epub::findContentOpfFile(std::string* contentOpfFile) const {
  const auto containerPath = "META-INF/container.xml";
  size_t containerSize;

  if (!getItemSize(containerPath, &containerSize)) {
    LOG_ERR("EBP", "Could not find or size META-INF/container.xml");
    return false;
  }

  ContainerParser containerParser(containerSize);

  if (!containerParser.setup()) {
    return false;
  }

  if (!readItemContentsToStream(containerPath, containerParser, 512)) {
    LOG_ERR("EBP", "Could not read META-INF/container.xml");
    return false;
  }

  if (containerParser.fullPath.empty()) {
    LOG_ERR("EBP", "Could not find valid rootfile in container.xml");
    return false;
  }

  *contentOpfFile = std::move(containerParser.fullPath);
  return true;
}

bool Epub::parseContentOpf(BookMetadataCache::BookMetadata& bookMetadata, const bool writeSpineEntries) {
  std::string contentOpfFilePath;
  if (!findContentOpfFile(&contentOpfFilePath)) {
    LOG_ERR("EBP", "Could not find content.opf in zip");
    return false;
  }

  contentBasePath = contentOpfFilePath.substr(0, contentOpfFilePath.find_last_of('/') + 1);

  LOG_DBG("EBP", "Parsing content.opf: %s", contentOpfFilePath.c_str());

  size_t contentOpfSize;
  if (!getItemSize(contentOpfFilePath, &contentOpfSize)) {
    LOG_ERR("EBP", "Could not get size of content.opf");
    return false;
  }

  ContentOpfParser opfParser(getCachePath(), getBasePath(), contentOpfSize,
                             writeSpineEntries ? bookMetadataCache.get() : nullptr);
  if (!opfParser.setup()) {
    LOG_ERR("EBP", "Could not setup content.opf parser");
    return false;
  }

  if (!readItemContentsToStream(contentOpfFilePath, opfParser, 1024)) {
    LOG_ERR("EBP", "Could not read content.opf");
    return false;
  }

  bookMetadata.title = opfParser.title;
  bookMetadata.author = opfParser.author;
  bookMetadata.language = opfParser.language;
  bookMetadata.coverItemHref = opfParser.coverItemHref;

  // The manifest can name an SVG wrapper as the cover image instead of the picture itself:
  //   <item properties="cover-image" media-type="image/svg+xml" href="cover.svg"/>
  //   cover.svg: <svg viewBox="0 0 1135 1600"><image xlink:href="cover.jpg"/></svg>
  // Cover generation decodes JPEG and PNG only, so the picture inside is the cover. A wrapper that
  // holds none is dropped, and the guide scan below looks for a cover instead.
  // Ported from crosspoint-reader PR #3539 (OSAKANA TARO, @osakanataro) via witchhunt-reader.
  if (isCoverWrapperDocument(bookMetadata.coverItemHref)) {
    std::string inWrapper = findCoverImageInDocument(bookMetadata.coverItemHref);
    if (inWrapper.empty()) {
      LOG_DBG("EBP", "Cover document holds no JPEG/PNG reference: %s", bookMetadata.coverItemHref.c_str());
    } else {
      LOG_DBG("EBP", "Cover document %s wraps %s", bookMetadata.coverItemHref.c_str(), inWrapper.c_str());
    }
    bookMetadata.coverItemHref = std::move(inWrapper);
  }

  // Without a cover picture from the manifest, or with one missing from the archive, the fallbacks
  // run in turn, the book's own word first: a <meta name="cover"> that names a page, the guide's cover
  // page, then a manifest item whose id says cover. Each counts only when it leads to a picture that
  // is in the archive.
  const auto hasReadableCover = [&](const std::string& path) {
    if (path.empty()) return false;
    size_t coverSize = 0;
    return getItemSize(path, &coverSize);
  };
  bool haveCover = hasReadableCover(bookMetadata.coverItemHref);
  if (!haveCover && !bookMetadata.coverItemHref.empty()) {
    LOG_DBG("EBP", "Manifest cover not found in archive, trying the fallbacks: %s", bookMetadata.coverItemHref.c_str());
    bookMetadata.coverItemHref.clear();
  }
  const auto takeCover = [&](const char* source, const std::string& pageHref) {
    if (haveCover || pageHref.empty()) return;
    std::string found = coverPictureOf(pageHref);
    if (!hasReadableCover(found)) return;
    LOG_DBG("EBP", "Found cover image via %s: %s", source, found.c_str());
    bookMetadata.coverItemHref = std::move(found);
    haveCover = true;
  };
  takeCover("meta cover page", opfParser.metaCoverPageHref);
  takeCover("guide", opfParser.guideCoverPageHref);
  takeCover("manifest id", opfParser.coverIdItemHref);
  // A book that names its cover only in the navigation document's landmarks.
  if (!haveCover && !opfParser.tocNavPath.empty()) {
    takeCover("nav landmarks", coverPageFromNavLandmarks(opfParser.tocNavPath));
  }
  // Last resort: a picture called cover.jpg / cover.png somewhere in the archive.
  if (!haveCover) {
    takeCover("file name", coverPictureByName());
  }

  bookMetadata.textReferenceHref = opfParser.textReferenceHref;

  if (!opfParser.tocNcxPath.empty()) {
    tocNcxItem = opfParser.tocNcxPath;
  }

  if (!opfParser.tocNavPath.empty()) {
    tocNavItem = opfParser.tocNavPath;
  }

  if (!opfParser.cssFiles.empty()) {
    cssFiles = opfParser.cssFiles;
  }

  LOG_DBG("EBP", "Successfully parsed content.opf");
  return true;
}

std::string Epub::coverPageFromNavLandmarks(const std::string& navHref) const {
  const auto scanner = makeUniqueNoThrow<NavCoverLandmark>();
  if (!scanner) {
    LOG_ERR("EBP", "No memory to scan the landmarks of %s", navHref.c_str());
    return {};
  }
  if (!readItemContentsToStream(navHref, *scanner, 1024)) {
    LOG_DBG("EBP", "Could not read navigation document %s", navHref.c_str());
  }
  if (scanner->result().empty()) return {};
  // The link is relative to the navigation document's own folder.
  const size_t lastSlash = navHref.rfind('/');
  std::string path = lastSlash == std::string::npos ? std::string{} : navHref.substr(0, lastSlash + 1);
  path.append(scanner->result());
  return FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(path));
}

std::string Epub::coverPictureByName() const {
  if (unpackedPackage) return {};
  CoverNameScan scan;
  ZipFile ownZip(filepath);
  ZipFile& zf = sharedZip_ ? *sharedZip_ : ownZip;
  if (!zf.enumerateFilePaths([&](const std::string_view path) { scan.feed(path); })) {
    LOG_DBG("EBP", "Could not list the archive to look for a cover by name");
    return {};
  }
  return scan.result();
}

std::string Epub::coverPictureOf(const std::string& href) const {
  if (href.empty()) return {};
  std::string target = href.substr(0, href.find('#'));
  if (FsHelpers::hasJpgExtension(std::string_view{target}) || FsHelpers::hasPngExtension(std::string_view{target})) {
    return target;
  }
  return findCoverImageInDocument(target);
}

std::string Epub::findCoverImageInDocument(const std::string& docHref) const {
  // ~0.5 KB of fixed buffers, once per OPF parse that needs it: on the heap, not the loop stack.
  const auto scanner = makeUniqueNoThrow<CoverImageRefScanner>();
  if (!scanner) {
    LOG_ERR("EBP", "No memory to scan cover document %s", docHref.c_str());
    return {};
  }
  if (!readItemContentsToStream(docHref, *scanner, 1024)) {
    LOG_DBG("EBP", "Could not read cover document %s", docHref.c_str());
  }
  const std::string_view ref = scanner->result();
  if (ref.empty()) return {};

  // Relative references inside the document resolve against its own folder.
  const size_t lastSlash = docHref.rfind('/');
  std::string path = lastSlash == std::string::npos ? std::string{} : docHref.substr(0, lastSlash + 1);
  path.append(ref);
  return FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(path));
}

std::string Epub::resolvedCoverHref() const {
  const std::string& href = bookMetadataCache->coreMetadata.coverItemHref;
  // A book indexed before SVG-wrapper covers were followed still holds the wrapper's path.
  if (!isCoverWrapperDocument(href)) return href;
  return findCoverImageInDocument(href);
}

bool Epub::parseTocNcxFile() const {
  if (tocNcxItem.empty()) {
    LOG_DBG("EBP", "No ncx file specified");
    return false;
  }

  LOG_DBG("EBP", "Parsing toc ncx file: %s", tocNcxItem.c_str());

  size_t ncxSize = 0;
  if (!getItemSize(tocNcxItem, &ncxSize)) {
    LOG_ERR("EBP", "Could not get toc ncx size");
    return false;
  }

  // The NCX's hrefs are relative to the NCX itself, which need not sit in the OPF's folder (witchhunt 1b5ee33a);
  // the nav document below already works this way.
  const std::string ncxContentBasePath = tocNcxItem.substr(0, tocNcxItem.find_last_of('/') + 1);
  TocNcxParser ncxParser(ncxContentBasePath, ncxSize, bookMetadataCache.get());
  if (!ncxParser.setup()) {
    LOG_ERR("EBP", "Could not setup toc ncx parser");
    return false;
  }

  if (!readItemContentsToStream(tocNcxItem, ncxParser, 1024)) {
    LOG_ERR("EBP", "Could not stream toc ncx data");
    return false;
  }

  LOG_DBG("EBP", "Parsed TOC items");
  return true;
}

bool Epub::parseTocNavFile() const {
  if (tocNavItem.empty()) {
    LOG_DBG("EBP", "No nav file specified");
    return false;
  }

  LOG_DBG("EBP", "Parsing toc nav file: %s", tocNavItem.c_str());

  size_t navSize = 0;
  if (!getItemSize(tocNavItem, &navSize)) {
    LOG_ERR("EBP", "Could not get toc nav size");
    return false;
  }

  const std::string navContentBasePath = tocNavItem.substr(0, tocNavItem.find_last_of('/') + 1);
  TocNavParser navParser(navContentBasePath, navSize, bookMetadataCache.get());
  if (!navParser.setup()) {
    LOG_ERR("EBP", "Could not setup toc nav parser");
    return false;
  }

  if (!readItemContentsToStream(tocNavItem, navParser, 1024)) {
    LOG_ERR("EBP", "Could not stream toc nav data");
    return false;
  }

  LOG_DBG("EBP", "Parsed TOC nav items");
  return true;
}

void Epub::discoverCssFilesFromZip() {
  if (unpackedPackage) return;

  const std::string& opfDir = contentBasePath;
  ZipFile ownZip(filepath);
  ZipFile& zf = sharedZip_ ? *sharedZip_ : ownZip;

  if (!zf.enumerateFilePaths([&](std::string_view filePath) {
        if (!opfDir.empty() && filePath.find(opfDir) != 0) {
          return;
        }

        if (!FsHelpers::hasCssExtension(filePath)) {
          return;
        }

        if (std::find(cssFiles.begin(), cssFiles.end(), filePath) != cssFiles.end()) {
          return;
        }

        LOG_DBG("EBP", "Discovered CSS file via ZIP enumeration: %.*s", (int)filePath.size(), filePath.data());
        cssFiles.push_back(std::string{filePath});
      })) {
    LOG_ERR("EBP", "Failed to enumerate ZIP file paths for CSS discovery");
  }
}

Epub::CssParseStatus Epub::parseCssFiles(const bool forceRebuild) const {
  constexpr size_t MAX_CSS_FILE_SIZE = 128 * 1024;
  constexpr size_t MIN_HEAP_FOR_CSS_PARSING = 64 * 1024;

  if (cssFiles.empty()) {
    LOG_DBG("EBP", "No CSS files to parse, but CssParser created for inline styles");
  }

  LOG_DBG("EBP", "CSS files to parse: %zu", cssFiles.size());

  if (cssParser->hasCache() && !forceRebuild) {
    LOG_DBG("EBP", "CSS cache exists, skipping parseCssFiles");
    return cssParser->isCachePartial() ? CssParseStatus::Partial : CssParseStatus::Complete;
  }

  bool parsedAllCss = true;
  size_t parsedCssFileCount = 0;
  size_t failedCssFileIndex = 0;
  std::string failedCssPath;
  for (size_t cssFileIndex = 0; cssFileIndex < cssFiles.size(); ++cssFileIndex) {
    const auto& cssPath = cssFiles[cssFileIndex];
    LOG_DBG("EBP", "Parsing CSS file: %s", cssPath.c_str());

    const uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < MIN_HEAP_FOR_CSS_PARSING) {
      LOG_ERR("EBP", "Insufficient heap for CSS parsing (%u bytes free, need %zu), skipping: %s", freeHeap,
              MIN_HEAP_FOR_CSS_PARSING, cssPath.c_str());
      parsedAllCss = false;
      failedCssFileIndex = cssFileIndex + 1;
      failedCssPath = cssPath;
      break;
    }

    size_t cssFileSize = 0;
    if (getItemSize(cssPath, &cssFileSize)) {
      if (cssFileSize > MAX_CSS_FILE_SIZE) {
        LOG_ERR("EBP", "CSS file too large (%zu bytes > %zu max), skipping: %s", cssFileSize, MAX_CSS_FILE_SIZE,
                cssPath.c_str());
        continue;
      }
    }

    const auto tmpCssPath = getCachePath() + "/.tmp.css";
    FsFile tempCssFile;
    if (!Storage.openFileForWrite("EBP", tmpCssPath, tempCssFile)) {
      LOG_ERR("EBP", "Could not create temp CSS file");
      continue;
    }
    if (!readItemContentsToStream(cssPath, tempCssFile, 1024)) {
      LOG_ERR("EBP", "Could not read CSS file: %s", cssPath.c_str());
      tempCssFile.close();
      Storage.remove(tmpCssPath.c_str());
      continue;
    }
    tempCssFile.close();

    if (!Storage.openFileForRead("EBP", tmpCssPath, tempCssFile)) {
      LOG_ERR("EBP", "Could not open temp CSS file for reading");
      Storage.remove(tmpCssPath.c_str());
      continue;
    }
    {
      const size_t slash = cssPath.rfind('/');
      cssParser->setStylesheetDir(slash == std::string::npos ? std::string() : cssPath.substr(0, slash + 1));
    }
    if (!cssParser->loadFromStream(tempCssFile)) {
      failedCssFileIndex = cssFileIndex + 1;
      failedCssPath = cssPath;
      LOG_ERR("EBP", "CSS parsing failed for file %zu/%zu after %zu parsed files: %s", failedCssFileIndex,
              cssFiles.size(), parsedCssFileCount, cssPath.c_str());
      parsedAllCss = false;
    } else {
      ++parsedCssFileCount;
    }
    tempCssFile.close();
    Storage.remove(tmpCssPath.c_str());
    if (!parsedAllCss) {
      break;
    }
  }

  if (!parsedAllCss && cssParser->empty()) {
    LOG_ERR("EBP", "CSS parsing failed for %s before any usable rules were loaded; CSS cache will not be written",
            failedCssPath.empty() ? "<unknown>" : failedCssPath.c_str());
    cssParser->clear();
    return CssParseStatus::Failed;
  }

  if (!parsedAllCss) {
    LOG_ERR("EBP", "Saving %zu partial CSS rules after parse stopped in %s", cssParser->ruleCount(),
            failedCssPath.empty() ? "<unknown>" : failedCssPath.c_str());
  }

  if (!cssParser->saveToCache(parsedAllCss)) {
    LOG_ERR("EBP", "Failed to save CSS rules to cache");
    cssParser->clear();
    return CssParseStatus::Failed;
  }
  if (!cssParser->saveFontFaces()) LOG_ERR("EBP", "Failed to save @font-face table");

  LOG_DBG("EBP", "Loaded %zu %s CSS style rules from %zu/%zu files", cssParser->ruleCount(),
          parsedAllCss ? "complete" : "partial", parsedCssFileCount, cssFiles.size());
  cssParser->clear();
  return parsedAllCss ? CssParseStatus::Complete : CssParseStatus::Partial;
}

// Opens the optional encrypted-entry accessor. A null result without an error means normal ZIP reads. A hard
// error refuses the open with a user-presentable reason (getProtectionError()).
bool Epub::openProtection() {
  decryptor.reset();
  protectionError.clear();
  if (unpackedPackage || isFb2Origin) return true;  // converted packages are never protected
  std::string err;
  decryptor = freeink::content::openProtectedBook(filepath, err);
  if (!err.empty()) {
    LOG_ERR("EBP", "protected content unavailable: %s", err.c_str());
    protectionError = err;
    return false;
  }
  if (decryptor) LOG_DBG("EBP", "protected content; on-read access path open");
  return true;
}

bool Epub::load(const bool buildIfMissing, const bool skipLoadingCss, const ProgressFn& onProgress) {
  LOG_DBG("EBP", "Loading ePub: %s", filepath.c_str());
  if (!openProtection()) return false;
  int lastReportedProgress = -5;
  const auto reportProgress = [&](const int progress) {
    if (!onProgress) return;
    const int clamped = std::clamp(progress, 0, 100);
    if (clamped == 100 || clamped - lastReportedProgress >= 5) {
      onProgress(static_cast<uint8_t>(clamped));
      lastReportedProgress = clamped;
    }
  };

  bookMetadataCache.reset(new BookMetadataCache(cachePath));
  cssParser.reset(new CssParser(cachePath));

  // One open archive serves every lookup and read of the indexing passes (container.xml, OPF, cover
  // checks, CSS discovery, NCX/nav) instead of reopening the file for each. Not used for unpacked
  // packages, and entries a decryptor owns bypass the zip anyway. Ported from crosspoint-reader #4619.
  ZipFile sharedZipFile(filepath);
  struct SharedZipScope {
    ZipFile*& slot;
    ZipFile& zip;
    void release() {
      slot = nullptr;
      zip.close();
    }
    ~SharedZipScope() { release(); }
  } sharedScope{sharedZip_, sharedZipFile};
  if (!unpackedPackage && !isFb2Origin && sharedZipFile.open()) sharedZip_ = &sharedZipFile;

  if (bookMetadataCache->load()) {
    if (!skipLoadingCss) {
      bool rebuildCssCache = false;
      bool forceCssRebuild = false;
      bool retryingPartialCssCache = false;
      if (!cssParser->hasCache()) {
        LOG_DBG("EBP", "CSS rules cache missing, attempting to parse CSS files");
        rebuildCssCache = true;
      } else if (!cssParser->loadFromCache()) {
        LOG_DBG("EBP", "CSS rules cache stale, attempting to parse CSS files");
        cssParser->deleteCache();
        rebuildCssCache = true;
      } else if (cssParser->isCachePartial()) {
        LOG_DBG("EBP", "CSS rules cache is partial, attempting to rebuild complete CSS cache");
        cssParser->clear();
        rebuildCssCache = true;
        forceCssRebuild = true;
        retryingPartialCssCache = true;
      } else {
        cssParser->clear();
      }

      if (rebuildCssCache) {
        BookMetadataCache::BookMetadata cachedMetadata = bookMetadataCache->coreMetadata;
        if (!parseContentOpf(cachedMetadata, false)) {
          LOG_ERR("EBP", "Could not parse content.opf from cached bookMetadata for CSS files");
        } else {
          discoverCssFilesFromZip();
        }
        sharedScope.release();  // CSS inflation wants the memory, not an open archive
        bookMetadataCache.reset();
        const CssParseStatus cssStatus = parseCssFiles(forceCssRebuild);
        bookMetadataCache.reset(new BookMetadataCache(cachePath));
        if (!bookMetadataCache->load()) {
          LOG_ERR("EBP", "Failed to reload cache after CSS rebuild");
          return false;
        }
        if (cssStatus == CssParseStatus::Complete ||
            (cssStatus == CssParseStatus::Partial && !retryingPartialCssCache)) {
          Storage.removeDir((cachePath + "/sections").c_str());
        } else if (cssStatus == CssParseStatus::Partial) {
          LOG_ERR("EBP", "CSS cache is still partial after rebuild; preserving existing section caches");
        } else {
          LOG_ERR("EBP", "CSS cache rebuild failed; preserving existing section caches");
        }
      }
    }
    LOG_DBG("EBP", "Loaded ePub: %s", filepath.c_str());
    return true;
  }

  if (!buildIfMissing) {
    return false;
  }

  LOG_DBG("EBP", "Cache not found, building spine/TOC cache");
  reportProgress(0);
  setupCacheDir();

  const uint32_t indexingStart = millis();

  if (!bookMetadataCache->beginWrite()) {
    LOG_ERR("EBP", "Could not begin writing cache");
    return false;
  }

  const uint32_t opfStart = millis();
  BookMetadataCache::BookMetadata bookMetadata;
  if (!bookMetadataCache->beginContentOpfPass()) {
    LOG_ERR("EBP", "Could not begin writing content.opf pass");
    return false;
  }
  if (!parseContentOpf(bookMetadata)) {
    LOG_ERR("EBP", "Could not parse content.opf");
    return false;
  }
  discoverCssFilesFromZip();
  if (!bookMetadataCache->endContentOpfPass()) {
    LOG_ERR("EBP", "Could not end writing content.opf pass");
    return false;
  }
  LOG_INF("EBP-PROF", "OPF pass completed in %lu ms", millis() - opfStart);
  reportProgress(15);

  const uint32_t tocStart = millis();
  if (!bookMetadataCache->beginTocPass()) {
    LOG_ERR("EBP", "Could not begin writing toc pass");
    return false;
  }

  bool tocParsed = false;

  if (!tocNavItem.empty()) {
    LOG_DBG("EBP", "Attempting to parse EPUB 3 nav document");
    tocParsed = parseTocNavFile();
  }

  // Converted books (Amazon exports among them) ship a nav that lists only the parts and front
  // matter next to an NCX that carries every chapter. A nav whose spine-mapped entries cover less
  // than half the spine gives the NCX a chance; it is kept only if it maps strictly more entries
  // (a stale NCX whose hrefs no longer resolve must not replace a smaller nav that works).
  // Ported from crosspoint-reader PR #2603 (Ryan Jarvis, @Cabalist) via witchhunt-reader.
  if (tocParsed && !tocNcxItem.empty() &&
      toc_fallback::navIsSparse(true, bookMetadataCache->getTocMappedCount(), bookMetadataCache->getSpineCount())) {
    const int navMapped = bookMetadataCache->getTocMappedCount();
    LOG_DBG("EBP", "Nav TOC is sparse (%d mapped of %d spine items), trying NCX", navMapped,
            bookMetadataCache->getSpineCount());
    if (bookMetadataCache->resetTocPassOutput()) {
      const bool ncxParsed = parseTocNcxFile();
      const int ncxMapped = bookMetadataCache->getTocMappedCount();
      if (toc_fallback::shouldAdoptNcx(true, navMapped, ncxParsed, ncxMapped)) {
        LOG_DBG("EBP", "NCX maps more entries (%d > %d), using it", ncxMapped, navMapped);
      } else if (bookMetadataCache->resetTocPassOutput()) {
        tocParsed = parseTocNavFile();
      } else {
        tocParsed = false;
      }
    } else {
      tocParsed = false;
    }
  }

  if (!tocParsed && !tocNcxItem.empty()) {
    LOG_DBG("EBP", "Falling back to NCX TOC");
    if (!tocNavItem.empty()) bookMetadataCache->resetTocPassOutput();
    tocParsed = parseTocNcxFile();
  }

  if (!tocParsed) {
    LOG_ERR("EBP", "Warning: Could not parse any TOC format");
  }

  if (!bookMetadataCache->endTocPass()) {
    LOG_ERR("EBP", "Could not end writing toc pass");
    return false;
  }
  LOG_INF("EBP-PROF", "TOC pass completed in %lu ms", millis() - tocStart);
  reportProgress(30);
  sharedScope.release();  // buildBookBin and the CSS pass open the archive themselves

  if (!bookMetadataCache->endWrite()) {
    LOG_ERR("EBP", "Could not end writing cache");
    return false;
  }
  reportProgress(35);

  const uint32_t buildStart = millis();
  int nextBuildProgress = 30;
  if (!bookMetadataCache->buildBookBin(filepath, bookMetadata, unpackedPackage,
                                       [&](const uint16_t completed, const uint16_t total) {
                                         const int buildPercent = total == 0 ? 95 : (completed * 60) / total;
                                         if (completed == total || buildPercent >= nextBuildProgress) {
                                           reportProgress(35 + buildPercent);
                                           while (nextBuildProgress <= buildPercent) nextBuildProgress += 30;
                                         }
                                       })) {
    LOG_ERR("EBP", "Could not update mappings and sizes");
    return false;
  }
  LOG_INF("EBP-PROF", "buildBookBin completed in %lu ms", millis() - buildStart);
  LOG_INF("EBP-PROF", "Total metadata indexing completed in %lu ms", millis() - indexingStart);

  if (!bookMetadataCache->cleanupTmpFiles()) {
    LOG_DBG("EBP", "Could not cleanup tmp files - ignoring");
  }

  if (!skipLoadingCss) {
    bookMetadataCache.reset();
    if (parseCssFiles() != CssParseStatus::Failed) {
      Storage.removeDir((cachePath + "/sections").c_str());
    } else {
      LOG_ERR("EBP", "CSS cache build failed; leaving any existing section caches in place");
    }
  }

  bookMetadataCache.reset(new BookMetadataCache(cachePath));
  if (!bookMetadataCache->load()) {
    LOG_ERR("EBP", "Failed to reload cache after writing");
    return false;
  }

  LOG_DBG("EBP", "Loaded ePub: %s", filepath.c_str());
  reportProgress(100);
  return true;
}

bool Epub::clearCache() const {
  if (!Storage.exists(cachePath.c_str())) {
    LOG_DBG("EPB", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.removeDir(cachePath.c_str())) {
    LOG_ERR("EPB", "Failed to clear cache");
    return false;
  }

  LOG_DBG("EPB", "Cache cleared successfully");
  return true;
}

void Epub::setupCacheDir() const {
  if (Storage.exists(cachePath.c_str())) {
    return;
  }

  Storage.mkdir(cachePath.c_str());
}

const std::string& Epub::getCachePath() const { return cachePath; }

const std::string& Epub::getPath() const { return filepath; }

const std::string& Epub::getTitle() const {
  static std::string blank;
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return blank;
  }

  return bookMetadataCache->coreMetadata.title;
}

const std::string& Epub::getAuthor() const {
  static std::string blank;
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return blank;
  }

  return bookMetadataCache->coreMetadata.author;
}

const std::string& Epub::getLanguage() const {
  static std::string blank;
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return blank;
  }

  return bookMetadataCache->coreMetadata.language;
}

std::string Epub::getCoverBmpPath(bool cropped) const {
  const auto coverFileName = std::string("cover_q2_v7") + (cropped ? "_crop" : "");
  return cachePath + "/" + coverFileName + ".bmp";
}

bool Epub::readCoverSourceToStream(const std::string& customPath, const std::string& itemHref, Print& out) const {
  if (customPath.empty()) return readItemContentsToStream(itemHref, out, 1024);
  FsFile src;
  if (!Storage.openFileForRead("EBP", customPath, src)) return false;
  uint8_t buffer[1024];
  bool ok = true;
  for (;;) {
    const int n = src.read(buffer, sizeof(buffer));
    if (n < 0) {
      ok = false;
      break;
    }
    if (n == 0) break;
    if (out.write(buffer, static_cast<size_t>(n)) != static_cast<size_t>(n)) {
      ok = false;
      break;
    }
  }
  src.close();
  return ok;
}

void Epub::invalidateCoverCache() const {
  if (cachePath.empty() || !Storage.exists(cachePath.c_str())) return;
  std::vector<std::string> removals;
  HalFile directory = Storage.open(cachePath.c_str());
  if (directory && directory.isDirectory()) {
    char name[160];
    for (HalFile entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
      entry.getName(name, sizeof(name));
      const bool isFile = !entry.isDirectory();
      entry.close();
      if (!isFile) continue;
      const std::string filename(name);
      if (FsHelpers::hasBmpExtension(filename) &&
          (filename.rfind("thumb_", 0) == 0 || filename.rfind("cover_", 0) == 0)) {
        removals.push_back(cachePath + "/" + filename);
      }
    }
  }
  if (directory) directory.close();
  for (const auto& path : removals) Storage.remove(path.c_str());
}

bool Epub::generateCoverBmp(bool cropped, const bool deferProgressive) const {
  if (Storage.exists(getCoverBmpPath(cropped).c_str())) {
    return true;
  }

  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "Cannot generate cover BMP, cache not loaded");
    return false;
  }

  // Converted books (FB2/FB3/DOCX/...) live in a cache package; the cover sits next to the user's original file.
  const std::string customCover = BookCover::findCustom(unpackedPackage ? Fb2::resolveOriginalPath(filepath) : filepath);
  const auto coverImageHref = customCover.empty() ? resolvedCoverHref() : customCover;
  if (coverImageHref.empty()) {
    LOG_ERR("EBP", "No known cover image");
    return false;
  }

  if (FsHelpers::hasJpgExtension(coverImageHref)) {
    LOG_DBG("EBP", "Generating BMP from JPG cover image (%s mode)", cropped ? "cropped" : "fit");
    const auto coverJpgTempPath = getCachePath() + "/.cover.jpg";

    FsFile coverJpg;
    if (!Storage.openFileForWrite("EBP", coverJpgTempPath, coverJpg)) {
      return false;
    }
    if (!readCoverSourceToStream(customCover, coverImageHref, coverJpg)) {
      LOG_ERR("EBP", "Failed to read cover JPG item: %s", coverImageHref.c_str());
      coverJpg.close();
      Storage.remove(coverJpgTempPath.c_str());
      return false;
    }
    coverJpg.close();

    if (!Storage.openFileForRead("EBP", coverJpgTempPath, coverJpg)) {
      Storage.remove(coverJpgTempPath.c_str());
      return false;
    }

    if (deferProgressive && JpegTypeDetector::shouldUseFullProgressive(JpegTypeDetector::inspect(coverJpg))) {
      LOG_INF("PJPG", "Deferred progressive cover decode until actually needed");
      coverJpg.close();
      Storage.remove(coverJpgTempPath.c_str());
      return true;
    }

    FsFile coverBmp;
    if (!Storage.openFileForWrite("EBP", getCoverBmpPath(cropped), coverBmp)) {
      coverJpg.close();
      Storage.remove(coverJpgTempPath.c_str());
      return false;
    }
    const bool success = JpegToBmpConverter::jpegFileToBmpStream(coverJpg, coverBmp, cropped);
    coverJpg.close();
    coverBmp.close();
    Storage.remove(coverJpgTempPath.c_str());

    if (!success) {
      LOG_ERR("EBP", "Failed to generate BMP from cover image");
      Storage.remove(getCoverBmpPath(cropped).c_str());
    }
    LOG_DBG("EBP", "Generated BMP from JPG cover image, success: %s", success ? "yes" : "no");
    return success;
  }

  if (FsHelpers::hasPngExtension(coverImageHref)) {
    LOG_DBG("EBP", "Generating BMP from PNG cover image (%s mode)", cropped ? "cropped" : "fit");
    const auto coverPngTempPath = getCachePath() + "/.cover.png";

    FsFile coverPng;
    if (!Storage.openFileForWrite("EBP", coverPngTempPath, coverPng)) {
      return false;
    }
    if (!readCoverSourceToStream(customCover, coverImageHref, coverPng)) {
      LOG_ERR("EBP", "Failed to read cover PNG item: %s", coverImageHref.c_str());
      coverPng.close();
      Storage.remove(coverPngTempPath.c_str());
      return false;
    }
    coverPng.close();

    if (!Storage.openFileForRead("EBP", coverPngTempPath, coverPng)) {
      Storage.remove(coverPngTempPath.c_str());
      return false;
    }

    FsFile coverBmp;
    if (!Storage.openFileForWrite("EBP", getCoverBmpPath(cropped), coverBmp)) {
      coverPng.close();
      Storage.remove(coverPngTempPath.c_str());
      return false;
    }
    const bool success = PngToBmpConverter::pngFileToBmpStream(coverPng, coverBmp, cropped);
    coverPng.close();
    coverBmp.close();
    Storage.remove(coverPngTempPath.c_str());

    if (!success) {
      LOG_ERR("EBP", "Failed to generate BMP from cover image");
      Storage.remove(getCoverBmpPath(cropped).c_str());
    }
    LOG_DBG("EBP", "Generated BMP from PNG cover image, success: %s", success ? "yes" : "no");
    return success;
  }

  LOG_ERR("EBP", "Cover image is not a supported format, skipping");
  return false;
}

std::string Epub::getThumbBmpPath() const { return cachePath + "/thumb_[WIDTH]x[HEIGHT].bmp"; }
std::string Epub::getThumbBmpPath(int height) const { return getThumbBmpPath(0, height); }
std::string Epub::getThumbBmpPath(int width, int height) const {
  normalizeThumbDimensions(width, height);
  const std::string newPath = getThumbBmpPathForDimensions(cachePath, width, height);
  if (Storage.exists(newPath.c_str())) {
    return newPath;
  }
  const std::string legacyPath = cachePath + "/thumb_" + std::to_string(height) + ".bmp";
  if (Storage.exists(legacyPath.c_str())) {
    return legacyPath;
  }
  return newPath;
}

std::string Epub::getAdaptiveThumbBmpPath(int width, int height) const {
  normalizeThumbDimensions(width, height);
  return getAdaptiveThumbBmpPathForDimensions(cachePath, width, height);
}

bool Epub::generateThumbBmp(int height) const { return generateThumbBmp(0, height); }

bool Epub::generateThumbBmp(int width, int height) const { return generateThumbBmpInternal(width, height, false); }

bool Epub::generateAdaptiveThumbBmp(int width, int height, const bool deferProgressive) const {
  return generateThumbBmpInternal(width, height, true, deferProgressive);
}

bool Epub::generateThumbBmpInternal(int width, int height, const bool adaptiveContain, const bool deferProgressive) const {
  if (height <= 0) {
    LOG_DBG("EBP", "Using default thumb BMP height for requested dimensions: %dx%d", width, height);
  }
  normalizeThumbDimensions(width, height);
  const std::string thumbPath = adaptiveContain ? getAdaptiveThumbBmpPathForDimensions(cachePath, width, height)
                                                : getThumbBmpPathForDimensions(cachePath, width, height);

  if (cachedBmpMatchesDimensions(thumbPath, width, height, adaptiveContain)) {
    return true;
  }

  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "Cannot generate thumb BMP, cache not loaded");
    return false;
  }

  // Converted books (FB2/FB3/DOCX/...) live in a cache package; the cover sits next to the user's original file.
  const std::string customCover = BookCover::findCustom(unpackedPackage ? Fb2::resolveOriginalPath(filepath) : filepath);
  const auto coverImageHref = customCover.empty() ? resolvedCoverHref() : customCover;
  if (coverImageHref.empty()) {
    LOG_DBG("EBP", "No known cover image for thumbnail");
  } else if (FsHelpers::hasJpgExtension(coverImageHref)) {
    LOG_DBG("EBP", "Generating thumb BMP from JPG cover image");
    const auto coverJpgTempPath = getCachePath() + "/.cover.jpg";

    FsFile coverJpg;
    if (!Storage.openFileForWrite("EBP", coverJpgTempPath, coverJpg)) {
      return false;
    }
    if (!readCoverSourceToStream(customCover, coverImageHref, coverJpg)) {
      LOG_ERR("EBP", "Failed to read thumbnail JPG item: %s", coverImageHref.c_str());
      coverJpg.close();
      Storage.remove(coverJpgTempPath.c_str());
      return false;
    }
    coverJpg.close();

    if (!Storage.openFileForRead("EBP", coverJpgTempPath, coverJpg)) {
      Storage.remove(coverJpgTempPath.c_str());
      return false;
    }

    if (deferProgressive && JpegTypeDetector::shouldUseFullProgressive(JpegTypeDetector::inspect(coverJpg))) {
      LOG_INF("PJPG", "Deferred progressive thumbnail decode until actually needed");
      coverJpg.close();
      Storage.remove(coverJpgTempPath.c_str());
      return true;
    }

    FsFile thumbBmp;
    if (!Storage.openFileForWrite("EBP", thumbPath, thumbBmp)) {
      coverJpg.close();
      Storage.remove(coverJpgTempPath.c_str());
      return false;
    }
    int THUMB_TARGET_WIDTH = width;
    int THUMB_TARGET_HEIGHT = height;
    const bool success = JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(coverJpg, thumbBmp, THUMB_TARGET_WIDTH,
                                                                             THUMB_TARGET_HEIGHT, adaptiveContain);
    coverJpg.close();
    thumbBmp.close();
    Storage.remove(coverJpgTempPath.c_str());

    if (!success) {
      LOG_ERR("EBP", "Failed to generate thumb BMP from JPG cover image");
      Storage.remove(thumbPath.c_str());
    }
    LOG_DBG("EBP", "Generated thumb BMP from JPG cover image, success: %s", success ? "yes" : "no");
    return success;
  } else if (FsHelpers::hasPngExtension(coverImageHref)) {
    LOG_DBG("EBP", "Generating thumb BMP from PNG cover image");
    const auto coverPngTempPath = getCachePath() + "/.cover.png";

    FsFile coverPng;
    if (!Storage.openFileForWrite("EBP", coverPngTempPath, coverPng)) {
      return false;
    }
    if (!readCoverSourceToStream(customCover, coverImageHref, coverPng)) {
      LOG_ERR("EBP", "Failed to read thumbnail PNG item: %s", coverImageHref.c_str());
      coverPng.close();
      Storage.remove(coverPngTempPath.c_str());
      return false;
    }
    coverPng.close();

    if (!Storage.openFileForRead("EBP", coverPngTempPath, coverPng)) {
      Storage.remove(coverPngTempPath.c_str());
      return false;
    }

    FsFile thumbBmp;
    if (!Storage.openFileForWrite("EBP", thumbPath, thumbBmp)) {
      coverPng.close();
      Storage.remove(coverPngTempPath.c_str());
      return false;
    }
    int THUMB_TARGET_WIDTH = width;
    int THUMB_TARGET_HEIGHT = height;
    const bool success = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(coverPng, thumbBmp, THUMB_TARGET_WIDTH,
                                                                           THUMB_TARGET_HEIGHT, adaptiveContain);
    coverPng.close();
    thumbBmp.close();
    Storage.remove(coverPngTempPath.c_str());

    if (!success) {
      LOG_ERR("EBP", "Failed to generate thumb BMP from PNG cover image");
      Storage.remove(thumbPath.c_str());
    }
    LOG_DBG("EBP", "Generated thumb BMP from PNG cover image, success: %s", success ? "yes" : "no");
    return success;
  } else {
    LOG_ERR("EBP", "Cover image is not a supported format, skipping thumbnail");
  }

  return false;
}

uint8_t* Epub::readItemContentsToBytes(const std::string& itemHref, size_t* size, const bool trailingNullByte) const {
  if (itemHref.empty()) {
    LOG_DBG("EBP", "Failed to read item, empty href");
    return nullptr;
  }

  const std::string path = itemPath(itemHref);

  if (unpackedPackage) {
    HalFile file;
    if (!Storage.openFileForRead("EBP", path, file) || file.isDirectory()) {
      LOG_DBG("EBP", "Failed to read package item %s", path.c_str());
      return nullptr;
    }
    const uint64_t fileSize = file.fileSize64();
    if (fileSize > std::numeric_limits<size_t>::max() - (trailingNullByte ? 1u : 0u)) {
      file.close();
      return nullptr;
    }
    const size_t bytes = static_cast<size_t>(fileSize);
    auto* content = static_cast<uint8_t*>(malloc(bytes + (trailingNullByte ? 1u : 0u)));
    if (!content) {
      file.close();
      return nullptr;
    }
    size_t offset = 0;
    while (offset < bytes) {
      const int read = file.read(content + offset, bytes - offset);
      if (read <= 0) break;
      offset += static_cast<size_t>(read);
    }
    file.close();
    if (offset != bytes) {
      free(content);
      return nullptr;
    }
    if (trailingNullByte) content[bytes] = '\0';
    if (size) *size = bytes;
    return content;
  }

  // Decode encrypted entries on demand in memory.
  if (decryptor && decryptor->isEncrypted(path)) {
    const size_t plainSize = decryptor->decryptedSize(path);
    if (plainSize > SIZE_MAX - (trailingNullByte ? 1 : 0)) return nullptr;
    const size_t total = plainSize + (trailingNullByte ? 1 : 0);
    uint8_t* decrypted = static_cast<uint8_t*>(malloc(total > 0 ? total : 1));
    if (!decrypted) {
      LOG_ERR("EBP", "insufficient memory for %s (%u bytes)", path.c_str(), static_cast<unsigned>(total));
      return nullptr;
    }
    struct BufferSink {
      uint8_t* data;
      size_t capacity;
      size_t written;
    } state{decrypted, plainSize, 0};
    auto append = [](void* context, const uint8_t* data, size_t length) {
      auto* target = static_cast<BufferSink*>(context);
      if (length > target->capacity - target->written) return false;
      memcpy(target->data + target->written, data, length);
      target->written += length;
      return true;
    };
    if (!decryptor->decryptToSink(path, append, &state) || state.written != plainSize) {
      free(decrypted);
      LOG_ERR("EBP", "content read failed for %s", path.c_str());
      return nullptr;
    }
    if (trailingNullByte) decrypted[plainSize] = 0;
    if (size) *size = plainSize;
    return decrypted;
  }

  const auto content = sharedZip_ ? sharedZip_->readFileToMemory(path.c_str(), size, trailingNullByte)
                                   : ZipFile(filepath).readFileToMemory(path.c_str(), size, trailingNullByte);
  if (!content) {
    LOG_DBG("EBP", "Failed to read item %s", path.c_str());
    return nullptr;
  }

  return content;
}

bool Epub::readItemContentsToStream(const std::string& itemHref, Print& out, const size_t chunkSize,
                                    const reader::ReaderCancellationToken* cancellationToken) const {
  if (itemHref.empty()) {
    LOG_DBG("EBP", "Failed to read item, empty href");
    return false;
  }

  const std::string path = itemPath(itemHref);

  if (isFb2Origin) {
    int chapterIndex = -1;
    const size_t prefixPos = itemHref.rfind("chapter_");
    if (prefixPos != std::string::npos) {
      chapterIndex = std::atoi(itemHref.c_str() + prefixPos + 8);
    }
    if (chapterIndex >= 0) {
      return Fb2::renderChapterOnDemand(cachePath, chapterIndex, out, cancellationToken);
    }
    if (!Storage.exists(path.c_str())) {
      Fb2::decodeImageOnDemand(path, cancellationToken);
    }
    if (cancellationToken && cancellationToken->isCancellationRequested()) return false;
  }

  if (unpackedPackage) {
    HalFile file;
    if (!Storage.openFileForRead("EBP", path, file) || file.isDirectory()) return false;
    const size_t bufferSize = std::max<size_t>(chunkSize, 1);
    auto buffer = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[bufferSize]);
    if (!buffer) {
      LOG_ERR("EBP", "Failed to allocate %zu-byte stream buffer", bufferSize);
      file.close();
      return false;
    }
    bool success = true;
    while (file.available() > 0) {
      if (cancellationToken && cancellationToken->isCancellationRequested()) {
        LOG_INF("EBP", "Item stream cancelled: %s", itemHref.c_str());
        success = false;
        break;
      }
      const int read = file.read(buffer.get(), bufferSize);
      if (read <= 0 || out.write(buffer.get(), static_cast<size_t>(read)) != static_cast<size_t>(read)) {
        success = false;
        break;
      }
    }
    file.close();
    return success;
  }
  if (decryptor && decryptor->isEncrypted(path)) {
    struct StreamSink {
      Print* out;
      const reader::ReaderCancellationToken* token;
    } sink{&out, cancellationToken};
    auto append = [](void* context, const uint8_t* data, size_t length) {
      auto* target = static_cast<StreamSink*>(context);
      if (target->token && target->token->isCancellationRequested()) return false;
      return target->out->write(data, length) == length;
    };
    if (!decryptor->decryptToSink(path, append, &sink)) {
      LOG_ERR("EBP", "content read failed for %s", path.c_str());
      return false;
    }
    return true;
  }
  if (sharedZip_) return sharedZip_->readFileToStream(path.c_str(), out, chunkSize, cancellationToken);
  return ZipFile(filepath).readFileToStream(path.c_str(), out, chunkSize, cancellationToken);
}

bool Epub::getItemSize(const std::string& itemHref, size_t* size) const {
  const std::string path = itemPath(itemHref);
  if (unpackedPackage) {
    HalFile file;
    if (!Storage.openFileForRead("EBP", path, file) || file.isDirectory()) return false;
    const uint64_t fileSize = file.fileSize64();
    file.close();
    if (fileSize > std::numeric_limits<size_t>::max()) return false;
    *size = static_cast<size_t>(fileSize);
    return true;
  }
  if (decryptor && decryptor->isEncrypted(path)) {
    *size = decryptor->decryptedSize(path);
    return true;
  }
  if (sharedZip_) return sharedZip_->getInflatedFileSize(path.c_str(), size);
  return ZipFile(filepath).getInflatedFileSize(path.c_str(), size);
}

int Epub::getSpineItemsCount() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return 0;
  }
  return bookMetadataCache->getSpineCount();
}

size_t Epub::getCumulativeSpineItemSize(const int spineIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "getCumulativeSpineItemSize called but cache not loaded");
    return 0;
  }

  return bookMetadataCache->getSpineCumulativeSize(spineIndex);
}

BookMetadataCache::SpineEntry Epub::getSpineItem(const int spineIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "getSpineItem called but cache not loaded");
    return {};
  }

  if (spineIndex < 0 || spineIndex >= bookMetadataCache->getSpineCount()) {
    LOG_ERR("EBP", "getSpineItem index:%d is out of range", spineIndex);
    return bookMetadataCache->getSpineEntry(0);
  }

  return bookMetadataCache->getSpineEntry(spineIndex);
}

BookMetadataCache::TocEntry Epub::getTocItem(const int tocIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_DBG("EBP", "getTocItem called but cache not loaded");
    return {};
  }

  if (tocIndex < 0 || tocIndex >= bookMetadataCache->getTocCount()) {
    LOG_DBG("EBP", "getTocItem index:%d is out of range", tocIndex);
    return {};
  }

  return bookMetadataCache->getTocEntry(tocIndex);
}

int Epub::getTocItemsCount() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return 0;
  }

  return bookMetadataCache->getTocCount();
}

int Epub::getSpineIndexForTocIndex(const int tocIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "getSpineIndexForTocIndex called but cache not loaded");
    return 0;
  }

  if (tocIndex < 0 || tocIndex >= bookMetadataCache->getTocCount()) {
    LOG_ERR("EBP", "getSpineIndexForTocIndex: tocIndex %d out of range", tocIndex);
    return 0;
  }

  const int spineIndex = bookMetadataCache->getTocEntry(tocIndex).spineIndex;
  if (spineIndex < 0) {
    LOG_DBG("EBP", "Section not found for TOC index %d", tocIndex);
    return 0;
  }

  return spineIndex;
}

int Epub::getTocIndexForSpineIndex(const int spineIndex) const { return getSpineItem(spineIndex).tocIndex; }

bool Epub::getLogicalChapterBounds(const int spineIndex, int& startIndex, int& endIndex) const {
  startIndex = spineIndex;
  endIndex = spineIndex;
  const int spineCount = getSpineItemsCount();
  if (spineIndex < 0 || spineIndex >= spineCount) return false;

  if (isFb2Origin) {
    return Fb2::getLogicalChapterBounds(cachePath, spineIndex, startIndex, endIndex);
  }

  auto splitBase = [](const std::string& href, std::string& outBase) -> bool {
    const size_t dot = href.find_last_of('.');
    const size_t searchEnd = dot == std::string::npos ? href.size() : dot;
    const size_t pos = href.rfind("_split", searchEnd);
    if (pos == std::string::npos) return false;

    const size_t digitsBegin = pos + 6;
    if (digitsBegin >= searchEnd) return false;
    for (size_t i = digitsBegin; i < searchEnd; ++i) {
      if (href[i] < '0' || href[i] > '9') return false;
    }

    outBase = href.substr(0, pos);
    if (dot != std::string::npos) outBase += href.substr(dot);
    return true;
  };

  std::string base;
  if (!splitBase(getSpineItem(spineIndex).href, base)) return true;

  startIndex = spineIndex;
  while (startIndex > 0) {
    std::string candidate;
    if (!splitBase(getSpineItem(startIndex - 1).href, candidate) || candidate != base) break;
    --startIndex;
  }

  endIndex = spineIndex;
  while (endIndex + 1 < spineCount) {
    std::string candidate;
    if (!splitBase(getSpineItem(endIndex + 1).href, candidate) || candidate != base) break;
    ++endIndex;
  }
  return true;
}

size_t Epub::getBookSize() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded() || bookMetadataCache->getSpineCount() == 0) {
    return 0;
  }
  return getCumulativeSpineItemSize(getSpineItemsCount() - 1);
}

int Epub::getSpineIndexForTextReference() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "getSpineIndexForTextReference called but cache not loaded");
    return 0;
  }
  LOG_DBG("EBP", "Core Metadata: cover(%d)=%s, textReference(%d)=%s",
          bookMetadataCache->coreMetadata.coverItemHref.size(), bookMetadataCache->coreMetadata.coverItemHref.c_str(),
          bookMetadataCache->coreMetadata.textReferenceHref.size(),
          bookMetadataCache->coreMetadata.textReferenceHref.c_str());

  if (bookMetadataCache->coreMetadata.textReferenceHref.empty()) {
    return 0;
  }

  for (size_t i = 0; i < getSpineItemsCount(); i++) {
    if (getSpineItem(i).href == bookMetadataCache->coreMetadata.textReferenceHref) {
      LOG_DBG("EBP", "Text reference %s found at index %d", bookMetadataCache->coreMetadata.textReferenceHref.c_str(),
              i);
      return i;
    }
  }
  LOG_DBG("EBP", "Section not found for text reference");
  return 0;
}

float Epub::calculateProgress(const int currentSpineIndex, const float currentSpineRead) const {
  const size_t bookSize = getBookSize();
  if (bookSize == 0) {
    return 0.0f;
  }
  const size_t prevChapterSize = (currentSpineIndex >= 1) ? getCumulativeSpineItemSize(currentSpineIndex - 1) : 0;
  const size_t curChapterSize = getCumulativeSpineItemSize(currentSpineIndex) - prevChapterSize;
  const float sectionProgSize = currentSpineRead * static_cast<float>(curChapterSize);
  const float totalProgress = static_cast<float>(prevChapterSize) + sectionProgSize;
  return totalProgress / static_cast<float>(bookSize);
}

int Epub::resolveLinkToSpineIndex(const std::string& href, const int fromSpineIndex) const {
  if (!href.empty() && href[0] == '#') return fromSpineIndex;
  // The folder of the document holding the link; its hrefs are relative to it (witchhunt 1b5ee33a).
  std::string base;
  if (fromSpineIndex >= 0 && fromSpineIndex < getSpineItemsCount()) {
    base = getSpineItem(fromSpineIndex).href;
    const size_t slash = base.find_last_of('/');
    base.resize(slash != std::string::npos ? slash + 1 : 0);
  }
  return resolveHrefToSpineIndex(href, base);
}

int Epub::resolveHrefToSpineIndex(const std::string& href, const std::string& linkBase) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return -1;

  const size_t hashPos = href.find('#');
  const std::string rawTarget = hashPos != std::string::npos ? href.substr(0, hashPos) : href;
  const std::string asWritten = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(rawTarget));
  if (asWritten.empty()) return -1;
  // The href inside its own document's folder: "../b/ch.xhtml" in OEBPS/a/ch.xhtml names OEBPS/b/ch.xhtml, not the
  // first ch.xhtml of the spine. Without a base (TOC entries are book paths already) it is the href as written.
  const std::string target =
      linkBase.empty() ? asWritten : FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(linkBase + rawTarget));

  const size_t targetSlash = asWritten.find_last_of('/');
  const std::string targetFilename = targetSlash != std::string::npos ? asWritten.substr(targetSlash + 1) : asWritten;

  // An exact path match anywhere beats the first filename-only match; the path the href names beats the href as
  // written (a link that spells a book path, against the spec).
  int asWrittenMatch = -1;
  int filenameMatch = -1;
  for (int i = 0; i < getSpineItemsCount(); i++) {
    const auto& spineHref = getSpineItem(i).href;
    if (spineHref == target) return i;
    if (asWrittenMatch == -1 && spineHref == asWritten) asWrittenMatch = i;
    if (filenameMatch != -1) continue;
    const size_t spineSlash = spineHref.find_last_of('/');
    if (spineHref.compare(spineSlash == std::string::npos ? 0 : spineSlash + 1, std::string::npos, targetFilename) == 0)
      filenameMatch = i;
  }
  return asWrittenMatch != -1 ? asWrittenMatch : filenameMatch;
}
