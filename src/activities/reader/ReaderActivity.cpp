#include "ReaderActivity.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <cstdio>

#include "InkMODSettings.h"
#include "util/PluginEventBus.h"
#include "Epub.h"
#include "Fb2.h"
#include "EpubReaderActivity.h"
#include "SdCardFontSystem.h"
#include "Txt.h"
#include "TxtReaderActivity.h"
#include "Xtc.h"
#include "XtcReaderActivity.h"
#include "activities/util/BmpViewerActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "components/UITheme.h"

bool ReaderActivity::isXtcFile(const std::string& path) { return FsHelpers::hasXtcExtension(path); }

bool ReaderActivity::isTxtFile(const std::string& path) { return FsHelpers::hasTxtExtension(path); }

// Markdown has its own native parser (see lib/Markdown/MarkdownParser.h): it is
// prepared into an EPUB-shaped package through Fb2, like FB2/FB3 books.
bool ReaderActivity::isMarkdownFile(const std::string& path) { return FsHelpers::hasMarkdownExtension(path); }

// Word documents likewise have their own parser (lib/Docx) behind Fb2.
bool ReaderActivity::isDocxFile(const std::string& path) { return (FsHelpers::checkFileExtension(path, ".docx") || FsHelpers::hasConvertedBookExtension(path)); }

static bool isImagePreviewFile(const std::string& path) {
  return FsHelpers::hasBmpExtension(path) || FsHelpers::hasPngExtension(path) || FsHelpers::hasJpgExtension(path);
}

bool ReaderActivity::shouldShowLoadingPopup(const std::string& path) {
  if (isXtcFile(path) || isTxtFile(path) || isMarkdownFile(path) || isDocxFile(path) ||
      isImagePreviewFile(path)) {
    return true;
  }

  // A browser-prepared EPUB/FB2 package already has book.bin before it has
  // ever been opened on the device. The old fast-cache shortcut therefore
  // suppressed the loading popup, even though the reader still had first-open
  // work to do (section/layout/cache validation). On e-ink that looked exactly
  // like a freeze.
  //
  // Keep the no-extra-refresh behaviour for books that have genuinely been
  // opened before, but show feedback whenever there is no saved reader
  // progress yet. Web preparation intentionally does not create progress.bin.
  Epub epub(path, "/.inkmod");
  const bool hasMetadataCache = Epub::hasCache(path, "/.inkmod");
  if (!hasMetadataCache) {
    return true;
  }

  const std::string progressPath = epub.getCachePath() + "/progress.bin";
  const std::string progressBackupPath = progressPath + ".bak";
  const bool hasReaderProgress = Storage.exists(progressPath.c_str()) || Storage.exists(progressBackupPath.c_str());
  return !hasReaderProgress;
}

std::string ReaderActivity::lastProtectionError;

std::unique_ptr<Epub> ReaderActivity::loadEpub(const std::string& path, const std::function<void(uint8_t)>& onProgress) {
  char breadcrumb[48];
  snprintf(breadcrumb, sizeof(breadcrumb), "Open EPUB %.34s", path.c_str());
  HalSystem::recordBreadcrumb(breadcrumb);
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  // Stability path for X3/X4:
  // open the original EPUB directly with the proven Epub/ERS cache pipeline.
  //
  // The optional pre-splitter (epubsplit_*) is deliberately bypassed here.
  // In release builds, some large EPUBs could reboot the ESP32-C3 during
  // EpubChapterSplitter::resolveReadPath() before the normal EPUB reader was
  // even created. Developer logs confirmed that opening the same original
  // EPUB directly proceeds through BMC/ERS and builds sections successfully.
  //
  // Keep EpubChapterSplitter sources in the project for future work/tests;
  // this change only removes it from the runtime open path.
  const std::string& readPath = path;

  auto epub = makeUniqueNoThrow<Epub>(readPath, "/.inkmod");
  if (!epub) {
    LOG_ERR("READER", "OOM: could not allocate EPUB reader");
    return nullptr;
  }
  if (epub->load(true, SETTINGS.embeddedStyle == 0, onProgress)) {
    return epub;
  }

  LOG_ERR("READER", "Failed to load epub");
  lastProtectionError = epub->getProtectionError();  // shown by onEnter()
  return nullptr;
}

std::unique_ptr<Xtc> ReaderActivity::loadXtc(const std::string& path) {
  char breadcrumb[48];
  snprintf(breadcrumb, sizeof(breadcrumb), "Open XTC %.35s", path.c_str());
  HalSystem::recordBreadcrumb(breadcrumb);
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto xtc = makeUniqueNoThrow<Xtc>(path, "/.inkmod");
  if (!xtc) {
    LOG_ERR("READER", "OOM: could not allocate XTC reader");
    return nullptr;
  }
  if (xtc->load()) {
    return xtc;
  }

  LOG_ERR("READER", "Failed to load XTC");
  return nullptr;
}

std::unique_ptr<Txt> ReaderActivity::loadTxt(const std::string& path) {
  char breadcrumb[48];
  snprintf(breadcrumb, sizeof(breadcrumb), "Open TXT %.35s", path.c_str());
  HalSystem::recordBreadcrumb(breadcrumb);
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto txt = makeUniqueNoThrow<Txt>(path, "/.inkmod");
  if (!txt) {
    LOG_ERR("READER", "OOM: could not allocate TXT reader");
    return nullptr;
  }
  if (txt->load()) {
    return txt;
  }

  LOG_ERR("READER", "Failed to load TXT");
  return nullptr;
}

void ReaderActivity::goToLibrary(const std::string& fromBookPath) {
  // If coming from a book, start in that book's folder; otherwise start from root
  auto initialPath = fromBookPath.empty() ? "/" : FsHelpers::extractFolderPath(fromBookPath);
  activityManager.goToFileBrowser(std::move(initialPath));
}

void ReaderActivity::onGoToEpubReader(std::unique_ptr<Epub> epub) {
  const auto epubPath = epub->getPath();
  currentBookPath = epubPath;
  activityManager.replaceActivity(std::make_unique<EpubReaderActivity>(renderer, mappedInput, std::move(epub)));
}

void ReaderActivity::onGoToBmpViewer(const std::string& path) {
  activityManager.replaceActivity(std::make_unique<BmpViewerActivity>(renderer, mappedInput, path));
}

void ReaderActivity::onGoToXtcReader(std::unique_ptr<Xtc> xtc) {
  const auto xtcPath = xtc->getPath();
  currentBookPath = xtcPath;
  activityManager.replaceActivity(std::make_unique<XtcReaderActivity>(renderer, mappedInput, std::move(xtc)));
}

void ReaderActivity::onGoToTxtReader(std::unique_ptr<Txt> txt) {
  const auto txtPath = txt->getPath();
  currentBookPath = txtPath;
  activityManager.replaceActivity(std::make_unique<TxtReaderActivity>(renderer, mappedInput, std::move(txt)));
}

void ReaderActivity::onEnter() {
  Activity::onEnter();

  // X4 Pro: opening an EPUB can spend several seconds synchronously building
  // metadata/section caches.  The tap that selected the book may still be a
  // live contact at this point; if the finger is released while loading, that
  // release would otherwise be delivered to the newly-entered reader and can
  // be misclassified as a Back/Home/page gesture.  Suppress the current
  // contact before the long load starts.  On button-only X3/X4 this is a no-op.
  if (mappedInput.hasTouch()) {
    mappedInput.suppressTouchContact();
  }

  if (suppressInitialBackRelease) {
    mappedInput.suppressNextBackRelease();
  }

  if (initialBookPath.empty()) {
    goToLibrary();  // Start from root when entering via Browse
    return;
  }

  const bool showLoadingPopup = shouldShowLoadingPopup(initialBookPath);
  Rect loadingPopupRect{};
  std::function<void(uint8_t)> reportProgress;
  if (showLoadingPopup) {
    loadingPopupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
    GUI.fillPopupProgress(renderer, loadingPopupRect, 0);
    reportProgress = [this, loadingPopupRect](const uint8_t progress) {
      GUI.fillPopupProgress(renderer, loadingPopupRect, progress);
    };
  }

  if (isImagePreviewFile(initialBookPath)) {
    onGoToBmpViewer(initialBookPath);
    return;
  }

  sdFontSystem.ensureLoaded(renderer);

  currentBookPath = initialBookPath;
  PluginEventBus::emit("reader.open", {{"book", initialBookPath}});
  if (isXtcFile(initialBookPath)) {
    auto xtc = loadXtc(initialBookPath);
    if (!xtc) {
      onGoBack();
      return;
    }
    onGoToXtcReader(std::move(xtc));
  } else if (isTxtFile(initialBookPath)) {
    auto txt = loadTxt(initialBookPath);
    if (!txt) {
      onGoBack();
      return;
    }
    onGoToTxtReader(std::move(txt));
  } else if (isMarkdownFile(initialBookPath) || isDocxFile(initialBookPath)) {
    // Reached with a raw .md/.docx path (e.g. an older "recent" entry): build the
    // package here, then open it like any other book.
    Fb2 markdownBook(initialBookPath, "/.inkmod");
    const auto markdownProgress = [&reportProgress](int percent) {
      if (reportProgress) reportProgress(static_cast<uint8_t>(percent));
    };
    if (!markdownBook.load(markdownProgress)) {
      LOG_ERR("READER", "Failed to prepare book package: %s", initialBookPath.c_str());
      onGoBack();
      return;
    }
    const std::string packagePath = markdownBook.getPackagePath();
    currentBookPath = packagePath;
    auto epub = loadEpub(packagePath, reportProgress);
    if (!epub) {
      onGoBack();
      return;
    }
    onGoToEpubReader(std::move(epub));
  } else {
    auto epub = loadEpub(initialBookPath, reportProgress);
    if (!epub) {
      showProtectionError();
      onGoBack();
      return;
    }
    onGoToEpubReader(std::move(epub));
  }
}

// A protected book that cannot be opened says why instead of silently going back.
void ReaderActivity::showProtectionError() {
  if (lastProtectionError.empty()) return;
  StrId message = StrId::STR_DRM_PROTECTED_FILE;
  if (lastProtectionError == "access expired") {
    message = StrId::STR_LOAN_EXPIRED;
  } else if (lastProtectionError == "loan date unverified") {
    message = StrId::STR_LOAN_TIME_UNVERIFIED;
  }
  lastProtectionError.clear();
  GUI.drawPopup(renderer, I18N.get(message));
  renderer.displayBuffer();
  delay(3000);
}

void ReaderActivity::onGoBack() { finish(); }
