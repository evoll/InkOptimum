#pragma once

#include <HalStorage.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

#include <memory>
#include <string>
#include <vector>

// Structure to hold file information
struct FileInfo {
  String name;
  size_t size;
  bool isEpub;
  bool isDirectory;
};

class InkMODWebServer {
 public:
  struct WsUploadStatus {
    bool inProgress = false;
    size_t received = 0;
    size_t total = 0;
    std::string filename;
    std::string lastCompleteName;
    size_t lastCompleteSize = 0;
    unsigned long lastCompleteAt = 0;
  };

  // Used by POST upload handler
  struct UploadState {
    HalFile file;
    String fileName;
    String path = "/";
    size_t size = 0;
    bool success = false;
    String error = "";

    // Upload write buffer - batches small writes into larger SD card operations
    // 4KB is a good balance: large enough to reduce syscall overhead, small enough
    // to keep individual write times short and avoid watchdog issues
    static constexpr size_t UPLOAD_BUFFER_SIZE = 4096;  // 4KB buffer
    std::vector<uint8_t> buffer;
    size_t bufferPos = 0;

    // Heap kept free for the HTTP stack and TLS while a buffer is being added.
    static constexpr size_t HEAP_HEADROOM = 16 * 1024;

    explicit UploadState(bool allocateBuffer = true) {
      if (allocateBuffer) buffer.resize(UPLOAD_BUFFER_SIZE);
    }

    // Allocates the write buffer only when the heap can spare it. The firmware builds with
    // -fno-exceptions, so a failed std::vector allocation would abort the device; refusing the
    // upload instead lets the browser show an error.
    bool ensureBuffer() {
      if (buffer.size() == UPLOAD_BUFFER_SIZE) return true;
      if (ESP.getMaxAllocHeap() < UPLOAD_BUFFER_SIZE + HEAP_HEADROOM) return false;
      buffer.resize(UPLOAD_BUFFER_SIZE);
      return true;
    }
  } upload, bookCacheUpload, fb2PackageUpload, wrappedEpubUpload{false};

  InkMODWebServer();
  ~InkMODWebServer();

  // Start the web server (call after WiFi is connected)
  void begin();

  // Stop the web server
  void stop();

  // Call this periodically to handle client requests
  void handleClient();

  // Check if server is running
  bool isRunning() const { return running; }

  WsUploadStatus getWsUploadStatus() const;

  // Get the port number
  uint16_t getPort() const { return port; }

 private:
  std::unique_ptr<WebServer> server = nullptr;
  std::unique_ptr<WebSocketsServer> wsServer = nullptr;
  bool running = false;
  bool apMode = false;  // true when running in AP mode, false for STA mode
  uint16_t port = 80;
  uint16_t wsPort = 81;  // WebSocket port
  int discoverySocket = -1;  // UDP discovery, -1 when closed
  void openDiscoverySocket();
  void closeDiscoverySocket();
  void answerDiscovery() const;

  // WebSocket upload state
  void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
  static void wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
  void abortWsUpload(const char* tag);

  // File scanning
  void scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const;
  String formatFileSize(size_t bytes) const;
  bool isEpubFile(const String& filename) const;

  // Request handlers
  void handleRoot() const;
  void handleJszip() const;
  void handleI18nJs() const;
  void handleStyleCss() const;
  void handleLogo() const;
  void handleNotFound() const;
  void handleStatus() const;
  void handleDiagnostics() const;
  void handleFileList() const;
  void handleEpubKitPage() const;
  void handleWallpapersPage() const;
  void handleFileListData() const;
  void handleDownload() const;
  void handleUpload(UploadState& state, bool preparedBookCache = false, bool preparedFb2Package = false,
                    bool preparedWrappedEpub = false) const;
  void handleUploadPost(UploadState& state) const;
  void handleCreateFolder() const;
  void handleRename() const;
  void handleMove() const;
  void handleDelete() const;

  // Settings handlers
  void handleSettingsPage() const;
  void handleGetSettings() const;
  void handlePostSettings();
  void handleApplyWallpaper();

  // Weather / Notes / Diagnostics pages (WebExtras.cpp)
  void handleWeatherPage() const;
  void handleNotesPage() const;
  void handleDiagnosticsPage() const;
  void handleStatsPage() const;
  void handleGetStats() const;
  void handleGetWeather() const;
  void handlePostWeatherSettings();
  void handlePostWeatherCache();
  void handleGetNotes() const;
  void handleGetBookNotes() const;
  void handleRenameBookmark();
  void handleDeleteBookmark();
  void handleDeleteClipping();

  // Font management handlers
  void handleFontsPage() const;
  void handleFontCatalog() const;
  void handleDictionaryPage() const;
  void handleListDictionaries() const;
  void handleLookupDictionary() const;
  void handleDeleteDictionary();
  void handleGetKoProfiles() const;
  void handlePostKoProfile();
  void handleKoProfileAction(bool remove);
  void handleFontList() const;
  void handleFontUpload();
  void handleFontUploadData();
  void handleFontDelete();

  // Font upload state
  struct FontUploadState {
    HalFile file;
    std::string familyName;
    std::string filePath;
    std::string tempPath;
    bool valid = false;
    bool magicChecked = false;
    uint8_t kind = 0;  // font_upload::Kind of the file being received
    std::string error;  // user-facing reason when the upload was refused up front
    size_t bytesWritten = 0;
    static constexpr size_t BUFFER_SIZE = 4096;
    std::vector<uint8_t> buffer;
    size_t bufferPos = 0;

    // Same guard as UploadState::ensureBuffer(): refuse instead of aborting when the heap is short.
    bool ensureBuffer() {
      if (buffer.size() == BUFFER_SIZE) return true;
      if (ESP.getMaxAllocHeap() < BUFFER_SIZE + UploadState::HEAP_HEADROOM) return false;
      buffer.resize(BUFFER_SIZE);
      return true;
    }
    // Releases the buffer once the request is over so the steady-state footprint stays small.
    void releaseBuffer() { std::vector<uint8_t>().swap(buffer); }
  } fontUpload;

  // Web plugins (WebPlugins.cpp)
  void handlePluginsPage() const;
  void handleGetPlugins() const;
  void handlePluginFile() const;
  void handlePluginRelay();
  void handlePluginCrypto();
  void handlePluginBookKey();
  void handlePluginLcpUnlock();
  void handlePluginFetch();
  void handlePluginFsDone();
  void handlePluginFsData();
  void handlePluginJobEnqueue();
  void handlePluginJobClaim();
  void handlePluginJobComplete();
  void handlePluginJobStatus() const;
  struct PluginFsState {
    HalFile file;
    std::string path, tempPath;
    size_t bytes = 0;
    bool ok = false, tooLarge = false, started = false;
  } pluginFs;

  // OPDS server handlers
  void handleGetOpdsServers() const;
  void handlePostOpdsServer();
  void handleDeleteOpdsServer();

  // Wi-Fi credential handlers
  void handleGetWifiNetworks() const;
  void handlePostWifiNetwork();
  void handleDeleteWifiNetwork();
};
