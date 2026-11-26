#pragma once

#include <atomic>
#include <string>

class OtaUpdater {
  bool updateAvailable = false;
  std::string latestVersion;
  std::string releaseName;
  std::string releaseNotes;
  std::string otaUrl;
  std::string otaDigest;
  std::string otaSigUrl;  // "<asset>.sig" of the release, when it has one
  size_t otaSize = 0;
  size_t processedSize = 0;
  size_t totalSize = 0;

 public:
  using ProgressCallback = void (*)(void* ctx);

  enum OtaUpdaterError {
    OK = 0,
    NO_UPDATE,
    HTTP_ERROR,
    JSON_PARSE_ERROR,
    UPDATE_OLDER_ERROR,
    INTERNAL_UPDATE_ERROR,
    OOM_ERROR,
    CANCELLED_ERROR,
    SIGNATURE_ERROR,  // a signing key is configured and the image has no valid signature
    NO_CHECKSUM_ERROR,  // the release lists no SHA-256 for the asset (witchhunt 0d7aeff7b)
  };

  size_t getOtaSize() const { return otaSize; }

  size_t getProcessedSize() const { return processedSize; }

  size_t getTotalSize() const { return totalSize; }

  OtaUpdater() = default;
  bool isUpdateNewer() const;
  const std::string& getLatestVersion() const;
  // Title and the beginning of the release notes of the latest release (may be empty).
  const std::string& getReleaseName() const { return releaseName; }
  const std::string& getReleaseNotes() const { return releaseNotes; }
  OtaUpdaterError checkForUpdate();
  OtaUpdaterError installUpdate(ProgressCallback onProgress = nullptr, void* ctx = nullptr,
                                std::atomic<bool>* cancelRequested = nullptr);
};
