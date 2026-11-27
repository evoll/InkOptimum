#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class FileBrowserActivity final : public Activity {
 public:
  // Books = standard reader browser; PickFirmware = filter to .bin only and return path via ActivityResult.
  enum class Mode { Books, PickFirmware, PickImage };

 private:
  // Deletion
  void promptDeleteFile(const std::string& fullPath, const std::string& entry);
  void promptDeleteDirectory(const std::string& fullPath, const std::string& entry,
                             bool ignoreInitialConfirmRelease = false);
  void promptRenamePath(const std::string& fullPath, const std::string& entry, bool isDirectory,
                        bool ignoreInitialConfirmRelease = false);
  void showDirectoryActionMenu(const std::string& entry, bool ignoreInitialConfirmRelease = false);
  void pinSleepFavorite(const std::string& fullPath);
  void unpinSleepFavorite();
  bool isPinnedSleepFavorite(const std::string& fullPath) const;
  void setPreferredSleepFolder(const std::string& fullPath);
  void clearPreferredSleepFolder();
  bool isPreferredSleepFolder(const std::string& fullPath) const;
  bool isSleepFavoriteFolder(const std::string& fullPath) const;
  void showFileActionMenu(const std::string& entry, bool ignoreInitialConfirmRelease = false);

  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;

  bool lockLongPressBack = false;
  bool longPressBackHandled = false;
  bool longPressConfirmHandled = false;
  bool pendingCompletedFeedback = false;
  bool completedFeedbackIsFinished = false;
  unsigned long completedFeedbackShowTime = 0UL;
  // True when this activity was entered while Confirm was already held; we must swallow the next
  // release so we don't immediately auto-open the first entry.
  bool lockNextConfirmRelease = false;

  Mode mode = Mode::Books;
  // True only when the normal Books browser was opened without an explicit
  // start path. In that case we restore the last visited directory/selection.
  bool restoreLastLocation = false;

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;
  bool fileListMemoryLimited = false;
  // Books marked finished (stats.bin), looked up when a row is first drawn and kept until the list is reloaded or a
  // book action may have changed it. Key: the entry as listed.
  mutable std::unordered_map<std::string, bool> finishedCache_;
  bool isFinishedBook(const std::string& entry) const;

  // Data loading
  void loadFiles();
  void toggleHiddenFiles();
  size_t findEntry(const std::string& name) const;

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = {},
                               Mode mode = Mode::Books)
      : Activity("FileBrowser", renderer, mappedInput),
        mode(mode),
        restoreLastLocation(mode == Mode::Books && initialPath.empty()),
        basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
