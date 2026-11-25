#include "InkMODWebServer.h"

#include <Memory.h>

#include "CompanionApi.h"
#include "MultipartRoute.h"
#include <PluginItems.h>
#include "util/UrlUtils.h"
#include "util/TaskWatchdog.h"

#include <ArduinoJson.h>
#ifdef SIMULATOR
#include <ArduinoJsonStringCompat.h>
#endif
#include <Epub.h>
#include <Fb2.h>
#include <FileReplace.h>
#include <FsHelpers.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>
#include <ZipFile.h>
#include <esp_task_wdt.h>

#include <fcntl.h>
#include <lwip/sockets.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <iterator>

#include "AppVersion.h"
#include "WebAccessGuard.h"
#include "InkMODSettings.h"
#include "diagnostics/DiagnosticManager.h"
#include "InkMODState.h"
#include "FontInstaller.h"
#include "StaticEtag.h"
#include <FontUploadRules.h>
#include <VectorFontSupport.h>
#include "OpdsProfile.h"
#include "OpdsServerStore.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#include "WebDAVHandler.h"
#include "WifiCredentialStore.h"
#include "html/FilesPageHtml.generated.h"
#include "html/EpubKitPageHtml.generated.h"
#include "html/FontsPageHtml.generated.h"
#include "html/HomePageHtml.generated.h"
#include "html/LogoPng.generated.h"
#include "html/SettingsPageHtml.generated.h"
#include "html/WallpapersPageHtml.generated.h"
#include "html/StyleCss.generated.h"
#include "html/js/jszip_minJs.generated.h"
#include "html/js/i18nJs.generated.h"
#include "html/js/bookprepWorkerJs.generated.h"
#include "util/BookCacheUtils.h"
#include "util/StringUtils.h"

namespace {
// Bounded streaming JSON writer for large SD-font catalogs. Keeping a full
// ArduinoJson document plus the serialized String at the same time can exhaust
// the fragmented network heap on X3/X4 (CrossInk #742).
class FontListJsonWriter {
 public:
  explicit FontListJsonWriter(WebServer& server) : server_(server) {}
  void append(const char* text) { append(text, strlen(text)); }
  void append(const char* text, size_t len) {
    while (len) {
      if (length_ == sizeof(buffer_)) flush();
      const size_t n = std::min(len, sizeof(buffer_) - length_);
      memcpy(buffer_ + length_, text, n);
      length_ += n;
      text += n;
      len -= n;
    }
  }
  void appendUnsigned(unsigned long value) {
    char number[16];
    const int n = snprintf(number, sizeof(number), "%lu", value);
    append(number, static_cast<size_t>(n));
  }
  void appendJsonString(const char* value) {
    append("\"");
    for (const unsigned char* q = reinterpret_cast<const unsigned char*>(value); *q; ++q) {
      switch (*q) {
        case '\"': append("\\\""); break;
        case '\\': append("\\\\"); break;
        case '\n': append("\\n"); break;
        case '\r': append("\\r"); break;
        case '\t': append("\\t"); break;
        default:
          if (*q < 0x20) {
            static constexpr char hex[] = "0123456789ABCDEF";
            const char esc[] = {'\\','u','0','0',hex[*q >> 4],hex[*q & 0x0F]};
            append(esc, sizeof(esc));
          } else append(reinterpret_cast<const char*>(q), 1);
      }
    }
    append("\"");
  }
  void flush() {
    if (!length_) return;
    server_.sendContent(buffer_, length_);
    length_ = 0;
    resetTaskWatchdogIfSubscribed();
    yield();
  }
 private:
  WebServer& server_;
  char buffer_[192];
  size_t length_ = 0;
};
// Folders/files to hide from the web interface file browser.
// Dot-prefixed items are hidden unless showHiddenFiles is enabled.
constexpr const char* HIDDEN_ITEMS[] = {"System Volume Information", "XTCache"};
constexpr uint16_t UDP_PORTS[] = {54982, 48123, 39001, 44044, 59678};
constexpr uint16_t LOCAL_UDP_PORT = 8134;

// millis() of each WebSocket client's last event, 0 for a free slot.
uint32_t wsLastActivity[WEBSOCKETS_SERVER_CLIENT_MAX] = {};
// A client that vanishes mid-upload (phone asleep, Wi-Fi lost) would keep its slot and the upload lock until TCP
// gives up. Clients send data without pause, so close one silent for two minutes. No ping heartbeat: the Calibre
// plugin does not answer pings while it uploads.
constexpr uint32_t WS_IDLE_TIMEOUT_MS = 120000;

void closeIdleWsClients(WebSocketsServer& ws) {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
    if (wsLastActivity[i] == 0) {
      if (ws.clientIsConnected(i)) wsLastActivity[i] = now;  // time a new client from its first sight
    } else if (now - wsLastActivity[i] > WS_IDLE_TIMEOUT_MS && ws.clientIsConnected(i)) {
      LOG_INF("WS", "Client %u sent nothing for %u s, closing it", i, static_cast<unsigned>(WS_IDLE_TIMEOUT_MS / 1000));
      ws.disconnect(i);
    }
  }
}

// Static pointer for WebSocket callback (WebSocketsServer requires C-style callback)
InkMODWebServer* wsInstance = nullptr;

uint8_t enumDisplayIndexForRawValue(const SettingInfo& setting, uint8_t rawValue) {
  if (setting.enumRawValues.empty()) {
    return rawValue;
  }

  auto it = std::find(setting.enumRawValues.begin(), setting.enumRawValues.end(), rawValue);
  if (it == setting.enumRawValues.end()) {
    return 0;
  }
  return static_cast<uint8_t>(std::distance(setting.enumRawValues.begin(), it));
}

uint8_t enumRawValueForDisplayIndex(const SettingInfo& setting, uint8_t displayIndex) {
  if (setting.enumRawValues.empty()) {
    return displayIndex;
  }
  if (displayIndex >= setting.enumRawValues.size()) {
    return setting.enumRawValues.front();
  }
  return setting.enumRawValues[displayIndex];
}

// WebSocket upload state
HalFile wsUploadFile;
String wsUploadFileName;
String wsUploadPath;
size_t wsUploadSize = 0;
size_t wsUploadReceived = 0;
unsigned long wsUploadStartTime = 0;
bool wsUploadInProgress = false;
uint8_t wsUploadClientNum = 255;  // 255 = no active upload client
size_t wsLastProgressSent = 0;
String wsLastCompleteName;
size_t wsLastCompleteSize = 0;
unsigned long wsLastCompleteAt = 0;

String normalizeWebPath(const String& inputPath) {
  if (inputPath.isEmpty() || inputPath == "/") {
    return "/";
  }
  std::string normalized = FsHelpers::normalisePath(inputPath.c_str());
  String result = normalized.c_str();
  if (result.isEmpty()) {
    return "/";
  }
  if (!result.startsWith("/")) {
    result = "/" + result;
  }
  if (result.length() > 1 && result.endsWith("/")) {
    result = result.substring(0, result.length() - 1);
  }
  return result;
}

// Suffix of the file a plain upload is written to before it replaces its target.
constexpr const char* kUploadTempSuffix = ".upl";

bool isProtectedPath(const String& path) {
  // Credential files are refused unconditionally, even with "show hidden files" enabled.
  if (web_access::isSecretPath(path.c_str())) return true;
  // Managed system folders are hidden from listings but their dedicated web
  // tools must still be able to upload files while "show hidden" is disabled.
  if (path == "/.dictionaries" || path.startsWith("/.dictionaries/") || path == "/.screenshots" ||
      path.startsWith("/.screenshots/")) {
    return false;
  }
  // Check every segment of the path, not just the last one.
  // This prevents access to e.g. /.hidden/somefile or /System Volume Information/foo
  int start = 0;
  while (start < (int)path.length()) {
    if (path.charAt(start) == '/') {
      start++;
      continue;
    }
    int end = path.indexOf('/', start);
    if (end == -1) end = path.length();

    String segment = path.substring(start, end);

    if (!SETTINGS.showHiddenFiles && segment.startsWith(".")) return true;

    for (const auto* item : HIDDEN_ITEMS) {
      if (segment.equals(item)) return true;
    }

    start = end + 1;
  }

  return false;
}
}  // namespace

// File listing page template - now using generated headers:
// - HomePageHtml (from html/HomePage.html)
// - FilesPageHeaderHtml (from html/FilesPageHeader.html)
// - FilesPageFooterHtml (from html/FilesPageFooter.html)
InkMODWebServer::InkMODWebServer() {}

InkMODWebServer::~InkMODWebServer() { stop(); }

void InkMODWebServer::begin() {
  if (running) {
    LOG_DBG("WEB", "Web server already running");
    return;
  }

  // Check if we have a valid network connection (either STA connected or AP mode)
  const wifi_mode_t wifiMode = WiFi.getMode();
  const bool isStaConnected = (wifiMode & WIFI_MODE_STA) && (WiFi.status() == WL_CONNECTED);
  const bool isInApMode = (wifiMode & WIFI_MODE_AP) && (WiFi.softAPgetStationNum() >= 0);  // AP is running

  if (!isStaConnected && !isInApMode) {
    LOG_DBG("WEB", "Cannot start webserver - no valid network (mode=%d, status=%d)", wifiMode, WiFi.status());
    return;
  }

  // Store AP mode flag for later use (e.g., in handleStatus)
  apMode = isInApMode;

  LOG_DBG("WEB", "[MEM] Free heap before begin: %d bytes", ESP.getFreeHeap());
  LOG_DBG("WEB", "Network mode: %s", apMode ? "AP" : "STA");

  LOG_DBG("WEB", "Creating web server on port %d...", port);
  server.reset(new WebServer(port));

  // Disable WiFi sleep to improve responsiveness and prevent 'unreachable' errors.
  // This is critical for reliable web server operation on ESP32.
  WiFi.setSleep(false);
  // Default varies by ESP32 core version. The activity's loss-recovery loop
  // relies on driver retries during transient disconnects.
  WiFi.setAutoReconnect(true);

  // Note: WebServer class doesn't have setNoDelay() in the standard ESP32 library.
  // We rely on disabling WiFi sleep for responsiveness.

  LOG_DBG("WEB", "[MEM] Free heap after WebServer allocation: %d bytes", ESP.getFreeHeap());

  if (!server) {
    LOG_ERR("WEB", "Failed to create WebServer!");
    return;
  }

  // Hotspot: send captive-portal checks (GET/HEAD for a foreign host name that our DNS answered with this
  // address) to the home page so the phone opens it. Added before the handlers because WebDAV claims every GET.
  if (apMode) {
    server->addMiddleware([](WebServer& s, Middleware::Callback next) {
      const bool check = s.method() == HTTP_GET || s.method() == HTTP_HEAD;
      if (!check || !UrlUtils::isForeignHost(s.header("Host").c_str())) return next();
      s.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
      s.send(302, "text/plain", "");
      return true;
    });
  }

  // Optional password (Settings > System > Web password): HTTP Basic for everything except /api/v1/*, which has its
  // own bearer tokens (and an open /api/v1/info). Browsers prompt once and then resend the credentials, so the web
  // pages, their fetch() calls and WebDAV clients keep working.
  server->addMiddleware([](WebServer& s, Middleware::Callback next) {
    if (SETTINGS.webPassword[0] == '\0') return next();
    if (s.uri().startsWith("/api/v1/")) return next();
    if (s.authenticate("inkoptimum", SETTINGS.webPassword)) return next();
    s.requestAuthentication(BASIC_AUTH, "InkOptimum");
    return true;
  });

  // Setup routes
  LOG_DBG("WEB", "Setting up routes...");
  server->on("/", HTTP_GET, [this] { handleRoot(); });
  server->on("/files", HTTP_GET, [this] { handleFileList(); });
  server->on("/epubkit", HTTP_GET, [this] { handleEpubKitPage(); });
  server->on("/wallpapers", HTTP_GET, [this] { handleWallpapersPage(); });
  server->on("/js/jszip.min.js", HTTP_GET, [this] { handleJszip(); });
  server->on("/js/i18n.js", HTTP_GET, [this] { handleI18nJs(); });
  server->on("/js/bookprep-worker.js", HTTP_GET, [this] {
    server->sendHeader("Content-Encoding", "gzip");
    server->sendHeader("Cache-Control", "public, max-age=31536000");
    server->send_P(200, "application/javascript", bookprepWorkerJs, bookprepWorkerJsCompressedSize);
  });
  server->on("/style.css", HTTP_GET, [this] { handleStyleCss(); });
  server->on("/logo.png", HTTP_GET, [this] { handleLogo(); });

  companionapi::registerRoutes(*server);
  server->on("/api/status", HTTP_GET, [this] { handleStatus(); });
  server->on("/api/diagnostics", HTTP_GET, [this] { handleDiagnostics(); });
  server->on("/api/files", HTTP_GET, [this] { handleFileListData(); });
  server->on("/download", HTTP_GET, [this] { handleDownload(); });

  // File-taking POST routes accept multipart/form-data and nothing else (MultipartRoute.h): a body of any other
  // type used to reach server->upload() through the framework's raw() path and dereference null.
  // Ownership passes to the WebServer, which deletes the handler when stopped.
  const auto addMultipartRoute = [this](const char* path, MultipartRoute::Action finished,
                                        MultipartRoute::Action part) {
    if (auto route = makeUniqueNoThrow<MultipartRoute>(path, std::move(finished), std::move(part))) {
      server->addHandler(route.release());
    } else {
      LOG_ERR("WEB", "OOM: route %s not available", path);
    }
  };

  // Upload endpoint with special handling for multipart form data
  addMultipartRoute("/upload", [this] { handleUploadPost(upload); }, [this] { handleUpload(upload); });
  // Browser-generated native metadata goes straight to the hidden cache,
  // never beside the user's books as hundreds of visible .inkprep files.
  addMultipartRoute("/api/books/cache", [this] { handleUploadPost(bookCacheUpload); },
                    [this] { handleUpload(bookCacheUpload, true); });
  addMultipartRoute("/api/books/fb2-package", [this] { handleUploadPost(fb2PackageUpload); },
                    [this] { handleUpload(fb2PackageUpload, false, true); });
  addMultipartRoute("/api/books/wrapped-epub", [this] { handleUploadPost(wrappedEpubUpload); },
                    [this] { handleUpload(wrappedEpubUpload, false, false, true); });

  // Create folder endpoint
  server->on("/mkdir", HTTP_POST, [this] { handleCreateFolder(); });

  // Rename file endpoint
  server->on("/rename", HTTP_POST, [this] { handleRename(); });

  // Move file endpoint
  server->on("/move", HTTP_POST, [this] { handleMove(); });

  // Delete file/folder endpoint
  server->on("/delete", HTTP_POST, [this] { handleDelete(); });

  // Settings endpoints
  server->on("/settings", HTTP_GET, [this] { handleSettingsPage(); });
  server->on("/api/settings", HTTP_GET, [this] { handleGetSettings(); });
  server->on("/api/settings", HTTP_POST, [this] { handlePostSettings(); });
  server->on("/api/wallpapers/apply", HTTP_POST, [this] { handleApplyWallpaper(); });

  // Weather, notes and diagnostics (WebExtras.cpp)
  server->on("/weather", HTTP_GET, [this] { handleWeatherPage(); });
  server->on("/notes", HTTP_GET, [this] { handleNotesPage(); });
  server->on("/diagnostics", HTTP_GET, [this] { handleDiagnosticsPage(); });
  server->on("/stats", HTTP_GET, [this] { handleStatsPage(); });
  server->on("/api/stats", HTTP_GET, [this] { handleGetStats(); });
  server->on("/api/weather", HTTP_GET, [this] { handleGetWeather(); });
  server->on("/api/weather/settings", HTTP_POST, [this] { handlePostWeatherSettings(); });
  server->on("/api/weather/cache", HTTP_POST, [this] { handlePostWeatherCache(); });
  server->on("/api/notes", HTTP_GET, [this] { handleGetNotes(); });
  server->on("/api/notes/book", HTTP_GET, [this] { handleGetBookNotes(); });
  server->on("/api/notes/bookmark/rename", HTTP_POST, [this] { handleRenameBookmark(); });
  server->on("/api/notes/bookmark/delete", HTTP_POST, [this] { handleDeleteBookmark(); });
  server->on("/api/notes/clipping/delete", HTTP_POST, [this] { handleDeleteClipping(); });

  // Font management endpoints
  server->on("/fonts", HTTP_GET, [this] { handleFontsPage(); });
  server->on("/api/fonts", HTTP_GET, [this] { handleFontList(); });
  addMultipartRoute("/api/fonts/upload", [this] { handleFontUpload(); }, [this] { handleFontUploadData(); });
  server->on("/dictionary", HTTP_GET, [this] { handleDictionaryPage(); });
  server->on("/api/dictionaries", HTTP_GET, [this] { handleListDictionaries(); });
  server->on("/api/dictionaries/lookup", HTTP_GET, [this] { handleLookupDictionary(); });
  server->on("/api/dictionaries/delete", HTTP_POST, [this] { handleDeleteDictionary(); });
  server->on("/api/koreader/profiles", HTTP_GET, [this] { handleGetKoProfiles(); });
  server->on("/api/koreader/profiles", HTTP_POST, [this] { handlePostKoProfile(); });
  server->on("/api/koreader/profiles/activate", HTTP_POST, [this] { handleKoProfileAction(false); });
  server->on("/api/koreader/profiles/delete", HTTP_POST, [this] { handleKoProfileAction(true); });
  server->on("/api/fonts/catalog", HTTP_GET, [this] { handleFontCatalog(); });
  server->on("/api/fonts/delete", HTTP_POST, [this] { handleFontDelete(); });

  // Web plugins (WebPlugins.cpp)
  server->on("/plugins", HTTP_GET, [this] { handlePluginsPage(); });
  server->on("/plugins-run", HTTP_GET, [this] { handlePluginsPage(); });
  server->on("/api/plugins", HTTP_GET, [this] { handleGetPlugins(); });
  server->on("/plugin", HTTP_GET, [this] { handlePluginFile(); });
  server->on("/api/relay", HTTP_POST, [this] { handlePluginRelay(); });
  server->on("/api/crypto", HTTP_POST, [this] { handlePluginCrypto(); });
  server->on("/api/book-key", HTTP_POST, [this] { handlePluginBookKey(); });
  server->on("/api/lcp-unlock", HTTP_POST, [this] { handlePluginLcpUnlock(); });
  server->on("/api/fetch", HTTP_POST, [this] { handlePluginFetch(); });
  addMultipartRoute("/api/plugin-fs", [this] { handlePluginFsDone(); }, [this] { handlePluginFsData(); });
  server->on("/api/plugin-jobs", HTTP_POST, [this] { handlePluginJobEnqueue(); });
  server->on("/api/plugin-jobs/claim", HTTP_GET, [this] { handlePluginJobClaim(); });
  server->on("/api/plugin-jobs/complete", HTTP_POST, [this] { handlePluginJobComplete(); });
  server->on("/api/plugin-jobs/status", HTTP_GET, [this] { handlePluginJobStatus(); });

  // OPDS server endpoints
  server->on("/api/opds", HTTP_GET, [this] { handleGetOpdsServers(); });
  server->on("/api/opds", HTTP_POST, [this] { handlePostOpdsServer(); });
  server->on("/api/opds/delete", HTTP_POST, [this] { handleDeleteOpdsServer(); });

  // Wi-Fi credential endpoints
  server->on("/api/wifi", HTTP_GET, [this] { handleGetWifiNetworks(); });
  server->on("/api/wifi", HTTP_POST, [this] { handlePostWifiNetwork(); });
  server->on("/api/wifi/delete", HTTP_POST, [this] { handleDeleteWifiNetwork(); });

  server->onNotFound([this] { handleNotFound(); });
  LOG_DBG("WEB", "[MEM] Free heap after route setup: %d bytes", ESP.getFreeHeap());

  // Collect WebDAV headers and register handler
  // Host is collected for the hotspot's captive-portal check.
  const char* davHeaders[] = {"Depth", "Destination", "Overwrite", "If", "Lock-Token", "Timeout", "Host", "If-None-Match",
                             "Authorization"};
  server->collectHeaders(davHeaders, 9);
  server->addHandler(new WebDAVHandler());  // Note: WebDAVHandler will be deleted by WebServer when server is stopped
  LOG_DBG("WEB", "WebDAV handler initialized");

  server->begin();

  // Start WebSocket server for fast binary uploads
  LOG_DBG("WEB", "Starting WebSocket server on port %d...", wsPort);
  wsServer.reset(new WebSocketsServer(wsPort));
  std::fill(std::begin(wsLastActivity), std::end(wsLastActivity), 0);
  wsInstance = const_cast<InkMODWebServer*>(this);
  wsServer->begin();
  wsServer->onEvent(wsEventCallback);
  LOG_DBG("WEB", "WebSocket server started");

  openDiscoverySocket();
  LOG_DBG("WEB", "Discovery UDP %s on port %d", discoverySocket >= 0 ? "enabled" : "failed", LOCAL_UDP_PORT);

  running = true;

  LOG_DBG("WEB", "Web server started on port %d", port);
  // Show the correct IP based on network mode
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  LOG_DBG("WEB", "Access at http://%s/", ipAddr.c_str());
  LOG_DBG("WEB", "WebSocket at ws://%s:%d/", ipAddr.c_str(), wsPort);
  LOG_DBG("WEB", "[MEM] Free heap after server.begin(): %d bytes", ESP.getFreeHeap());
}

void InkMODWebServer::abortWsUpload(const char* tag) {
  // Explicit close() required: file-scope global persists beyond function scope
  wsUploadFile.close();
  String filePath = wsUploadPath;
  if (!filePath.endsWith("/")) filePath += "/";
  filePath += wsUploadFileName;
  if (Storage.remove(filePath.c_str())) {
    LOG_DBG(tag, "Deleted incomplete upload: %s", filePath.c_str());
  } else {
    LOG_DBG(tag, "Failed to delete incomplete upload: %s", filePath.c_str());
  }
  wsUploadInProgress = false;
  wsUploadClientNum = 255;
  wsLastProgressSent = 0;
}

void InkMODWebServer::stop() {
  companionapi::reset();
  if (!running || !server) {
    LOG_DBG("WEB", "stop() called but already stopped (running=%d, server=%p)", running, server.get());
    return;
  }

  LOG_DBG("WEB", "STOP INITIATED - setting running=false first");
  running = false;  // Set this FIRST to prevent handleClient from using server

  LOG_DBG("WEB", "[MEM] Free heap before stop: %d bytes", ESP.getFreeHeap());

  // Close any in-progress WebSocket upload and remove partial file
  if (wsUploadInProgress && wsUploadFile) {
    abortWsUpload("WEB");
  }

  // Stop WebSocket server
  if (wsServer) {
    LOG_DBG("WEB", "Stopping WebSocket server...");
    wsServer->close();
    wsServer.reset();
    wsInstance = nullptr;
    LOG_DBG("WEB", "WebSocket server stopped");
  }

  closeDiscoverySocket();

  // Brief delay to allow any in-flight handleClient() calls to complete
  delay(20);

  server->stop();
  LOG_DBG("WEB", "[MEM] Free heap after server->stop(): %d bytes", ESP.getFreeHeap());

  // Brief delay before deletion
  delay(10);

  server.reset();
  LOG_DBG("WEB", "Web server stopped and deleted");
  LOG_DBG("WEB", "[MEM] Free heap after delete server: %d bytes", ESP.getFreeHeap());

  // Note: Static upload variables (uploadFileName, uploadPath, uploadError) are declared
  // later in the file and will be cleared when they go out of scope or on next upload
  LOG_DBG("WEB", "[MEM] Free heap final: %d bytes", ESP.getFreeHeap());
}

void InkMODWebServer::handleClient() {
  static unsigned long lastDebugPrint = 0;

  // Check running flag FIRST before accessing server
  if (!running) {
    return;
  }

  // Double-check server pointer is valid
  if (!server) {
    LOG_DBG("WEB", "WARNING: handleClient called with null server!");
    return;
  }

  // Print debug every 10 seconds to confirm handleClient is being called
  if (millis() - lastDebugPrint > 10000) {
    LOG_DBG("WEB", "handleClient active, server running on port %d", port);
    lastDebugPrint = millis();
  }

  server->handleClient();

  // Handle WebSocket events
  if (wsServer) {
    wsServer->loop();
    closeIdleWsClients(*wsServer);
  }

  answerDiscovery();
}

// Discovery uses a plain socket: NetworkUDP keeps a 1,460-byte send buffer while it is open, for a
// reply of a few dozen bytes.
void InkMODWebServer::openDiscoverySocket() {
  if (discoverySocket >= 0) return;
  const int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) return;
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(LOCAL_UDP_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
      fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) < 0) {
    close(fd);
    return;
  }
  discoverySocket = fd;
}

void InkMODWebServer::closeDiscoverySocket() {
  if (discoverySocket < 0) return;
  close(discoverySocket);
  discoverySocket = -1;
}

// Answers a "hello" broadcast with the device name and WebSocket port.
void InkMODWebServer::answerDiscovery() const {
  if (discoverySocket < 0) return;
  char buffer[16];
  sockaddr_in from = {};
  socklen_t fromLen = sizeof(from);
  const int len =
      recvfrom(discoverySocket, buffer, sizeof(buffer) - 1, 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
  if (len <= 0) return;
  buffer[len] = '\0';
  if (strcmp(buffer, "hello") != 0) return;
  const char* hostname = WiFi.getHostname();
  char message[96];
  const int messageLen = snprintf(message, sizeof(message), "crosspoint (on %s);%u",
                                  hostname && hostname[0] ? hostname : "inkmod", static_cast<unsigned>(wsPort));
  if (messageLen <= 0) return;
  sendto(discoverySocket, message, std::min(static_cast<size_t>(messageLen), sizeof(message) - 1), 0,
         reinterpret_cast<sockaddr*>(&from), fromLen);
}

InkMODWebServer::WsUploadStatus InkMODWebServer::getWsUploadStatus() const {
  WsUploadStatus status;
  status.inProgress = wsUploadInProgress;
  status.received = wsUploadReceived;
  status.total = wsUploadSize;
  status.filename = wsUploadFileName.c_str();
  status.lastCompleteName = wsLastCompleteName.c_str();
  status.lastCompleteSize = wsLastCompleteSize;
  status.lastCompleteAt = wsLastCompleteAt;
  return status;
}

// Pages are baked into flash, so a content hash is a stable validator for the lifetime of the image. A matching
// If-None-Match is answered with 304; `no-cache` makes the browser revalidate instead of guessing freshness.
static bool sendNotModifiedIfCurrent(WebServer* server, const char* data, size_t len) {
  const std::string etag = static_etag::make(reinterpret_cast<const uint8_t*>(data), len);
  server->sendHeader("ETag", etag.c_str());
  server->sendHeader("Cache-Control", "no-cache");
  if (static_etag::matches(server->header("If-None-Match").c_str(), etag)) {
    server->send(304);
    return true;
  }
  return false;
}

static void sendHtmlContent(WebServer* server, const char* data, size_t len) {
  if (sendNotModifiedIfCurrent(server, data, len)) return;
  server->sendHeader("Content-Encoding", "gzip");
  server->send_P(200, "text/html", data, len);
}

void InkMODWebServer::handleRoot() const {
  sendHtmlContent(server.get(), HomePageHtml, sizeof(HomePageHtml));
  LOG_DBG("WEB", "Served root page");
}

void InkMODWebServer::handleJszip() const {
  if (sendNotModifiedIfCurrent(server.get(), reinterpret_cast<const char*>(jszip_minJs), jszip_minJsCompressedSize)) return;
  server->sendHeader("Content-Encoding", "gzip");
  server->send_P(200, "application/javascript", jszip_minJs, jszip_minJsCompressedSize);
  LOG_DBG("WEB", "Served jszip.min.js");
}

void InkMODWebServer::handleI18nJs() const {
  if (sendNotModifiedIfCurrent(server.get(), reinterpret_cast<const char*>(i18nJs), i18nJsCompressedSize)) return;
  server->sendHeader("Content-Encoding", "gzip");
  server->send_P(200, "application/javascript", i18nJs, i18nJsCompressedSize);
  LOG_DBG("WEB", "Served i18n.js");
}

// Shared stylesheet and logo are referenced with a content-hashed ?v= query,
// so they can be cached aggressively: a new build changes the URL.
void InkMODWebServer::handleStyleCss() const {
  server->sendHeader("Content-Encoding", "gzip");
  server->sendHeader("Cache-Control", "public, max-age=31536000, immutable");
  server->send_P(200, "text/css", StyleCss, StyleCssCompressedSize);
  LOG_DBG("WEB", "Served style.css");
}

void InkMODWebServer::handleLogo() const {
  // Raw PNG (already compressed); no Content-Encoding.
  server->sendHeader("Cache-Control", "public, max-age=31536000, immutable");
  server->send_P(200, "image/png", LogoPng, LogoPngSize);
  LOG_DBG("WEB", "Served logo.png");
}

void InkMODWebServer::handleNotFound() const {
  String message = "404 Not Found\n\n";
  message += "URI: " + server->uri() + "\n";
  server->send(404, "text/plain", message);
}

void InkMODWebServer::handleStatus() const {
  // Get correct IP based on AP vs STA mode
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();

  JsonDocument doc;
  doc["version"] = INKMOD_VERSION;
  doc["ip"] = ipAddr;
  doc["mode"] = apMode ? "AP" : "STA";
  doc["rssi"] = apMode ? 0 : WiFi.RSSI();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;
  doc["device"] = gpio.deviceIsX3() ? "X3" : "X4";
  doc["firmwareFamily"] = plugincat::identity::FAMILY;
  doc["pluginApi"] = plugincat::identity::PLUGIN_API;
  doc["pluginCapabilities"] = plugincat::identity::CAPABILITIES;  // same list CrossPoint reports

  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

// Plain-text diagnostic report for bug reports. `?download=1` makes the browser save it as a file.
void InkMODWebServer::handleDiagnostics() const {
  const String report = DIAGNOSTICS.buildReport();
  if (server->hasArg("download")) {
    server->sendHeader("Content-Disposition", "attachment; filename=\"inkmod-diagnostic.txt\"");
  }
  server->sendHeader("Cache-Control", "no-store");
  server->send(200, "text/plain; charset=utf-8", report);
}

void InkMODWebServer::scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const {
  HalFile root = Storage.open(path);
  if (!root) {
    LOG_DBG("WEB", "Failed to open directory: %s", path);
    return;
  }

  if (!root.isDirectory()) {
    LOG_DBG("WEB", "Not a directory: %s", path);
    root.close();
    return;
  }

  LOG_DBG("WEB", "Scanning files in: %s", path);

  HalFile file = root.openNextFile();
  char name[500];
  while (file) {
    file.getName(name, sizeof(name));
    auto fileName = String(name);

    // Skip hidden items (starting with ".")
    bool shouldHide = !SETTINGS.showHiddenFiles && fileName.startsWith(".");

    // Check against explicitly hidden items list
    if (!shouldHide) {
      for (const auto* item : HIDDEN_ITEMS) {
        if (fileName.equals(item)) {
          shouldHide = true;
          break;
        }
      }
    }

    if (!shouldHide) {
      FileInfo info;
      info.name = fileName;
      info.isDirectory = file.isDirectory();

      if (info.isDirectory) {
        info.size = 0;
        info.isEpub = false;
      } else {
        info.size = file.size();
        info.isEpub = isEpubFile(info.name);
      }

      callback(info);
    }

    file.close();
    yield();               // Yield to allow WiFi and other tasks to process during long scans
    resetTaskWatchdogIfSubscribed();  // Reset watchdog to prevent timeout on large directories
    file = root.openNextFile();
  }
  root.close();
}

bool InkMODWebServer::isEpubFile(const String& filename) const { return FsHelpers::hasEpubExtension(filename); }

void InkMODWebServer::handleFileList() const {
  sendHtmlContent(server.get(), FilesPageHtml, sizeof(FilesPageHtml));
}

void InkMODWebServer::handleEpubKitPage() const {
  sendHtmlContent(server.get(), EpubKitPageHtml, sizeof(EpubKitPageHtml));
  LOG_DBG("WEB", "Served EPUB Kit page");
}


void InkMODWebServer::handleWallpapersPage() const {
  sendHtmlContent(server.get(), WallpapersPageHtml, sizeof(WallpapersPageHtml));
  LOG_DBG("WEB", "Served sleep-screen generator page");
}

void InkMODWebServer::handleFileListData() const {
  // Get current path from query string (default to root)
  String currentPath = "/";
  if (server->hasArg("path")) {
    currentPath = normalizeWebPath(server->arg("path"));
  }

  if (isProtectedPath(currentPath)) {
    server->send(403, "application/json", "[]");
    return;
  }

  // Stable paged mode for the web UI. Some ESP32 WebServer/browser
  // combinations can leave a long chunked JSON response open, which makes
  // fetch().json() wait forever and leaves the spinner visible. Keep each
  // response small and give it a normal Content-Length instead.
  if (server->hasArg("paged")) {
    int offset = server->hasArg("offset") ? server->arg("offset").toInt() : 0;
    int limit = server->hasArg("limit") ? server->arg("limit").toInt() : 16;
    offset = std::max(0, offset);
    limit = std::clamp(limit, 1, 24);

    String body;
    body.reserve(static_cast<size_t>(limit) * 192 + 2);
    body += "[";

    char output[512];
    constexpr size_t outputSize = sizeof(output);
    JsonDocument doc;
    int visibleIndex = 0;
    int emitted = 0;
    bool hasMore = false;

    scanFiles(currentPath.c_str(), [&body, &output, &doc, &visibleIndex, &emitted, &hasMore, offset, limit](
                                           const FileInfo& info) mutable {
      if (visibleIndex++ < offset) return;
      if (emitted >= limit) {
        hasMore = true;
        return;
      }

      doc.clear();
      doc["name"] = info.name;
      doc["size"] = info.size;
      doc["isDirectory"] = info.isDirectory;
      doc["isEpub"] = info.isEpub;

      const size_t written = serializeJson(doc, output, outputSize);
      if (written == 0 || written >= outputSize) return;
      if (emitted > 0) body += ",";
      body.concat(output, written);
      ++emitted;
    });

    body += "]";
    server->sendHeader("X-InkMOD-Has-More", hasMore ? "1" : "0");
    server->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server->sendHeader("Pragma", "no-cache");
    server->send(200, "application/json", body);
    LOG_DBG("WEB", "Served paged file list path=%s offset=%d count=%d more=%d", currentPath.c_str(), offset,
            emitted, hasMore ? 1 : 0);
    return;
  }

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server->sendHeader("Pragma", "no-cache");
  server->send(200, "application/json", "");
  server->sendContent("[");
  char output[512];
  constexpr size_t outputSize = sizeof(output);
  bool seenFirst = false;
  JsonDocument doc;

  scanFiles(currentPath.c_str(), [this, &output, &doc, seenFirst](const FileInfo& info) mutable {
    doc.clear();
    doc["name"] = info.name;
    doc["size"] = info.size;
    doc["isDirectory"] = info.isDirectory;
    doc["isEpub"] = info.isEpub;

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) {
      // JSON output truncated; skip this entry to avoid sending malformed JSON
      LOG_DBG("WEB", "Skipping file entry with oversized JSON for name: %s", info.name.c_str());
      return;
    }

    if (seenFirst) {
      resetTaskWatchdogIfSubscribed();
      yield();
      server->sendContent(",");
    } else {
      seenFirst = true;
    }
    server->sendContent(output);
  });
  server->sendContent("]");
  // End of streamed response, empty chunk to signal client
  server->sendContent("");
  LOG_DBG("WEB", "Served file listing page for path: %s", currentPath.c_str());
}

void InkMODWebServer::handleDownload() const {
  if (!server->hasArg("path")) {
    server->send(400, "text/plain", "Missing path");
    return;
  }

  String itemPath = normalizeWebPath(server->arg("path"));
  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }

  if (isProtectedPath(itemPath)) {
    server->send(403, "text/plain", "Access denied to protected path");
    return;
  }

  if (!Storage.exists(itemPath.c_str())) {
    server->send(404, "text/plain", "Item not found");
    return;
  }

  HalFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server->send(400, "text/plain", "Path is a directory");
    return;
  }

  String contentType = "application/octet-stream";
  if (isEpubFile(itemPath)) {
    contentType = "application/epub+zip";
  }

  char nameBuf[128] = {0};
  String filename = "download";
  if (file.getName(nameBuf, sizeof(nameBuf))) {
    filename = nameBuf;
  }

  server->setContentLength(file.size());
  server->sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  server->send(200, contentType.c_str(), "");

  NetworkClient client = server->client();
  const size_t chunkSize = 4096;
  uint8_t buffer[chunkSize];

  bool downloadOk = true;
  while (downloadOk && file.available()) {
    int result = file.read(buffer, chunkSize);
    if (result <= 0) break;
    size_t bytesRead = static_cast<size_t>(result);
    size_t totalWritten = 0;
    while (totalWritten < bytesRead) {
      resetTaskWatchdogIfSubscribed();
      size_t wrote = client.write(buffer + totalWritten, bytesRead - totalWritten);
      if (wrote == 0) {
        downloadOk = false;
        break;
      }
      totalWritten += wrote;
    }
  }
#ifndef SIMULATOR
  client.clear();
#endif
  file.close();
}

// Diagnostic counters for upload performance analysis
static unsigned long uploadStartTime = 0;
static unsigned long totalWriteTime = 0;
static size_t writeCount = 0;

static bool flushUploadBuffer(InkMODWebServer::UploadState& state) {
  if (state.bufferPos > 0 && state.file) {
    resetTaskWatchdogIfSubscribed();  // Reset watchdog before potentially slow SD write
    const unsigned long writeStart = millis();
    const size_t written = state.file.write(state.buffer.data(), state.bufferPos);
    totalWriteTime += millis() - writeStart;
    writeCount++;
    resetTaskWatchdogIfSubscribed();  // Reset watchdog after SD write

    if (written != state.bufferPos) {
      LOG_DBG("WEB", "[UPLOAD] Buffer flush failed: expected %d, wrote %d", state.bufferPos, written);
      state.bufferPos = 0;
      return false;
    }
    state.bufferPos = 0;
  }
  return true;
}

void InkMODWebServer::handleUpload(UploadState& state, const bool preparedBookCache,
                                   const bool preparedFb2Package,
                                   const bool preparedWrappedEpub) const {
  static size_t lastLoggedSize = 0;

  // Reset watchdog at start of every upload callback - HTTP parsing can be slow
  resetTaskWatchdogIfSubscribed();

  // Safety check: ensure server is still valid
  if (!running || !server) {
    LOG_DBG("WEB", "[UPLOAD] ERROR: handleUpload called but server not running!");
    return;
  }

  const HTTPUpload& upload = server->upload();

  if (upload.status == UPLOAD_FILE_START) {
    // Reset watchdog - this is the critical 1% crash point
    resetTaskWatchdogIfSubscribed();

    state.file.close();

    // v1.1.7 added wrappedEpubUpload as a fourth UploadState. Keep that
    // state buffer lazy so ordinary SoftAP transfers retain the same steady
    // heap footprint as v1.1.6. Allocate it only when this endpoint is used.
    const bool bufferReady = state.ensureBuffer();

    // Browsers (notably Safari directory uploads) may report multipart
    // filename as "TopFolder/sub/file.ext". The destination directory is
    // already supplied separately in ?path=, so only keep the basename here.
    String multipartName = upload.filename;
    multipartName.replace('\\', '/');
    const int multipartSlash = multipartName.lastIndexOf('/');
    if (multipartSlash >= 0) multipartName = multipartName.substring(multipartSlash + 1);
    state.fileName = preparedBookCache ? "book.bin.tmp"
                                       : preparedFb2Package ? "package.epub.tmp"
                                       : preparedWrappedEpub ? "wrapped.epub.tmp"
                                                             : StringUtils::sanitizeFilename(multipartName.c_str()).c_str();
    state.size = 0;
    state.success = false;
    state.error = "";
    uploadStartTime = millis();
    lastLoggedSize = 0;
    state.bufferPos = 0;
    totalWriteTime = 0;
    writeCount = 0;

    if (!bufferReady) {
      // Not enough contiguous heap for the write buffer: refuse cleanly instead of aborting.
      state.error = "Not enough free memory for the upload, try again";
      LOG_ERR("WEB", "[UPLOAD] Refused: maxAlloc=%u", static_cast<unsigned>(ESP.getMaxAllocHeap()));
      return;
    }

    // Get upload path from query parameter (defaults to root if not specified)
    // Note: We use query parameter instead of form data because multipart form
    // fields aren't available until after file upload completes
    if (preparedBookCache || preparedFb2Package || preparedWrappedEpub) {
      if (!server->hasArg("bookPath")) {
        state.error = "Missing bookPath";
        return;
      }
      const String bookPath = normalizeWebPath(server->arg("bookPath"));
      String lowerBookPath = bookPath;
      lowerBookPath.toLowerCase();
      const bool wrappedMode = preparedWrappedEpub || (preparedBookCache && server->arg("wrapped") == "1");
      const bool fb2Mode = preparedFb2Package || (preparedBookCache && server->arg("fb2") == "1");
      const bool extensionOk = wrappedMode ? lowerBookPath.endsWith(".zip")
                               : fb2Mode ? (lowerBookPath.endsWith(".fb2") || lowerBookPath.endsWith(".fb3") || lowerBookPath.endsWith(".docx") || FsHelpers::hasConvertedBookExtension(lowerBookPath) || lowerBookPath.endsWith(".zip"))
                                         : lowerBookPath.endsWith(".epub");
      if (isProtectedPath(bookPath) || !extensionOk || !Storage.exists(bookPath.c_str())) {
        state.error = "Invalid or missing book path";
        return;
      }

      if (wrappedMode) {
        HalFile outer;
        if (!Storage.openFileForRead("WEB", bookPath, outer)) {
          state.error = "Failed to inspect wrapped EPUB";
          return;
        }
        const uint64_t outerSize = outer.fileSize64();
        outer.close();
        uint32_t h = 2166136261u;
        for (size_t i = 0; i < bookPath.length(); ++i) {
          h ^= static_cast<uint8_t>(bookPath[i]);
          h *= 16777619u;
        }
        for (int i = 0; i < 8; ++i) {
          h ^= static_cast<uint8_t>(outerSize >> (i * 8));
          h *= 16777619u;
        }
        char wrappedPath[64];
        std::snprintf(wrappedPath, sizeof(wrappedPath), "/.inkmod/wrapped_epub/%08lx.epub",
                      static_cast<unsigned long>(h));
        if (preparedWrappedEpub) {
          state.path = "/.inkmod/wrapped_epub";
          char wrappedName[32];
          std::snprintf(wrappedName, sizeof(wrappedName), "%08lx.epub.tmp", static_cast<unsigned long>(h));
          state.fileName = wrappedName;
        } else {
          state.path = Epub::cachePathForFilePath(wrappedPath, "/.inkmod").c_str();
        }
      } else if (fb2Mode) {
        Fb2 fb2(bookPath.c_str(), "/.inkmod");
        state.path = fb2.getCachePath().c_str();
      } else {
        state.path = Epub::cachePathForFilePath(bookPath.c_str(), "/.inkmod").c_str();
      }
      Storage.mkdir(state.path.c_str(), true);
    } else if (server->hasArg("path")) {
      state.path = normalizeWebPath(server->arg("path"));
    } else {
      state.path = "/";
    }

    LOG_DBG("WEB", "[UPLOAD] START: %s to path: %s", state.fileName.c_str(), state.path.c_str());
    LOG_DBG("WEB", "[UPLOAD] Free heap: %d bytes", ESP.getFreeHeap());

    // Create file path
    String filePath = state.path;
    if (!filePath.endsWith("/")) filePath += "/";
    filePath += state.fileName;

    if (!preparedBookCache && !preparedFb2Package && !preparedWrappedEpub && isProtectedPath(filePath)) {
      state.error = "Access denied to protected path";
      LOG_DBG("WEB", "[UPLOAD] FAILED: Access denied to protected path: %s", filePath.c_str());
      return;
    }

    // Check if file already exists - SD operations can be slow
    resetTaskWatchdogIfSubscribed();
    // A plain upload is written beside its target and swapped in when it completes, so an upload
    // that breaks off keeps the file it would have replaced. The prepared-cache uploads already
    // arrive under a temporary name.
    const bool plainUpload = !preparedBookCache && !preparedFb2Package && !preparedWrappedEpub;
    if (plainUpload) {
      filePath += kUploadTempSuffix;
      Storage.remove(filePath.c_str());
    } else if (Storage.exists(filePath.c_str())) {
      LOG_DBG("WEB", "[UPLOAD] Overwriting existing file: %s", filePath.c_str());
      resetTaskWatchdogIfSubscribed();
      Storage.remove(filePath.c_str());
    }

    // Open file for writing - this can be slow due to FAT cluster allocation
    resetTaskWatchdogIfSubscribed();
    if (!Storage.openFileForWrite("WEB", filePath, state.file)) {
      state.error = "Failed to create file on SD card";
      LOG_DBG("WEB", "[UPLOAD] FAILED to create file: %s", filePath.c_str());
      return;
    }
    resetTaskWatchdogIfSubscribed();

    LOG_DBG("WEB", "[UPLOAD] File created successfully: %s", filePath.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (state.file && state.error.isEmpty()) {
      // Buffer incoming data and flush when buffer is full
      // This reduces SD card write operations and improves throughput
      const uint8_t* data = upload.buf;
      size_t remaining = upload.currentSize;

      while (remaining > 0) {
        const size_t space = UploadState::UPLOAD_BUFFER_SIZE - state.bufferPos;
        const size_t toCopy = (remaining < space) ? remaining : space;

        memcpy(state.buffer.data() + state.bufferPos, data, toCopy);
        state.bufferPos += toCopy;
        data += toCopy;
        remaining -= toCopy;

        // Flush buffer when full
        if (state.bufferPos >= UploadState::UPLOAD_BUFFER_SIZE) {
          if (!flushUploadBuffer(state)) {
            state.error = "Failed to write to SD card - disk may be full";
            state.file.close();
            return;
          }
        }
      }

      state.size += upload.currentSize;

      // Log progress every 100KB
      if (state.size - lastLoggedSize >= 102400) {
        const unsigned long elapsed = millis() - uploadStartTime;
        const float kbps = (elapsed > 0) ? (state.size / 1024.0) / (elapsed / 1000.0) : 0;
        LOG_DBG("WEB", "[UPLOAD] %d bytes (%.1f KB), %.1f KB/s, %d writes", state.size, state.size / 1024.0, kbps,
                writeCount);
        lastLoggedSize = state.size;
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (state.file) {
      // Flush any remaining buffered data
      if (!flushUploadBuffer(state)) {
        state.error = "Failed to write final data to SD card";
      }
      state.file.close();

      if (state.error.isEmpty()) {
        state.success = true;
        const unsigned long elapsed = millis() - uploadStartTime;
        const float avgKbps = (elapsed > 0) ? (state.size / 1024.0) / (elapsed / 1000.0) : 0;
        const float writePercent = (elapsed > 0) ? (totalWriteTime * 100.0 / elapsed) : 0;
        LOG_DBG("WEB", "[UPLOAD] Complete: %s (%d bytes in %lu ms, avg %.1f KB/s)", state.fileName.c_str(), state.size,
                elapsed, avgKbps);
        LOG_DBG("WEB", "[UPLOAD] Diagnostics: %d writes, total write time: %lu ms (%.1f%%)", writeCount, totalWriteTime,
                writePercent);

        String filePath = state.path;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += state.fileName;
        if (preparedBookCache) {
          // Validate the native header before promotion: 0x425843FF + v7.
          HalFile check;
          uint8_t header[5] = {};
          const bool valid = Storage.openFileForRead("WEB", filePath, check) &&
                             check.read(header, sizeof(header)) == sizeof(header) &&
                             header[0] == 0xFF && header[1] == 0x43 && header[2] == 0x58 &&
                             header[3] == 0x42 && header[4] == 7;
          check.close();
          const String finalPath = state.path + "/book.bin";
          if (!valid || !FileReplace::replace(Storage, filePath.c_str(), finalPath.c_str())) {
            Storage.remove(filePath.c_str());
            state.success = false;
            state.error = "Prepared cache validation/install failed";
          } else {
            LOG_INF("WEB", "Installed browser-prepared cache: %s", finalPath.c_str());
            // Do not decode the cover while the web server and upload buffers
            // still occupy RAM. HomeActivity already creates the required theme
            // thumbnail lazily from package.epub after the server has stopped.
          }
        } else if (preparedFb2Package) {
          const String finalPath = state.path + "/package.epub";
          const String markerPath = state.path + "/.browser_prepared_epub";
          if (!FileReplace::replace(Storage, filePath.c_str(), finalPath.c_str()) ||
              !Storage.writeFile(markerPath.c_str(), "1")) {
            Storage.remove(filePath.c_str());
            Storage.remove(markerPath.c_str());
            state.success = false;
            state.error = "Prepared FB2 package install failed";
          } else {
            LOG_INF("WEB", "Installed browser-prepared FB2 package: %s", finalPath.c_str());
          }
        } else if (preparedWrappedEpub) {
          String finalPath = filePath;
          if (finalPath.endsWith(".tmp")) finalPath.remove(finalPath.length() - 4);
          size_t containerSize = 0;
          const bool valid = ZipFile(filePath.c_str()).getInflatedFileSize("META-INF/container.xml", &containerSize) &&
                             containerSize > 0;
          if (!valid || !FileReplace::replace(Storage, filePath.c_str(), finalPath.c_str())) {
            Storage.remove(filePath.c_str());
            state.success = false;
            state.error = "Prepared wrapped EPUB install failed";
          } else {
            LOG_INF("WEB", "Installed browser-prepared wrapped EPUB: %s", finalPath.c_str());
          }
        } else {
          const String tempPath = filePath + kUploadTempSuffix;
          if (FileReplace::replace(Storage, tempPath.c_str(), filePath.c_str())) {
            // Clear epub cache to prevent stale metadata issues when overwriting files
            clearBookCachePreservingUserState(filePath.c_str());
          } else {
            Storage.remove(tempPath.c_str());
            state.success = false;
            state.error = "Failed to move the uploaded file into place";
          }
        }
      } else if (!preparedBookCache && !preparedFb2Package && !preparedWrappedEpub) {
        String tempPath = state.path;
        if (!tempPath.endsWith("/")) tempPath += "/";
        tempPath += state.fileName;
        tempPath += kUploadTempSuffix;
        Storage.remove(tempPath.c_str());  // failed write: the old file stays
      }
    }
    if (preparedWrappedEpub) {
      // Return the temporary 4 KB buffer after the wrapped-EPUB request so
      // ordinary SoftAP mode goes back to the v1.1.6 steady-state footprint.
      std::vector<uint8_t>().swap(state.buffer);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    state.bufferPos = 0;  // Discard buffered data
    if (state.file) {
      state.file.close();
      // Try to delete the incomplete file
      String filePath = state.path;
      if (!filePath.endsWith("/")) filePath += "/";
      filePath += state.fileName;
      if (!preparedBookCache && !preparedFb2Package && !preparedWrappedEpub) filePath += kUploadTempSuffix;
      Storage.remove(filePath.c_str());
    }
    state.error = "Upload aborted";
    if (preparedWrappedEpub) {
      std::vector<uint8_t>().swap(state.buffer);
    }
    LOG_DBG("WEB", "Upload aborted");
  }
}

void InkMODWebServer::handleUploadPost(UploadState& state) const {
  if (state.success) {
    server->send(200, "text/plain", "File uploaded successfully: " + state.fileName);
  } else {
    const String error = state.error.isEmpty() ? "Unknown error during upload" : state.error;
    server->send(400, "text/plain", error);
  }
}

void InkMODWebServer::handleCreateFolder() const {
  // Get folder name from form data
  if (!server->hasArg("name")) {
    server->send(400, "text/plain", "Missing folder name");
    return;
  }

  const String requestedName = server->arg("name");
  // sanitizeFilename() intentionally strips leading dots from ordinary user
  // names. These two roots are managed by InkMOD's web tools, though, and
  // must keep their leading dot (otherwise dictionary preparation creates an
  // empty /dictionaries next to the real /.dictionaries store).
  const bool managedHiddenFolder = requestedName == ".dictionaries" || requestedName == ".screenshots";
  const String folderName =
      managedHiddenFolder ? requestedName : StringUtils::sanitizeFilename(requestedName.c_str()).c_str();

  // Validate folder name
  if (folderName.isEmpty() || folderName == "book") {
    server->send(400, "text/plain", "Invalid folder name");
    return;
  }

  // Get parent path
  String parentPath = "/";
  if (server->hasArg("path")) {
    parentPath = normalizeWebPath(server->arg("path"));
  }

  // Build full folder path
  String folderPath = parentPath;
  if (!folderPath.endsWith("/")) folderPath += "/";
  folderPath += folderName;

  if (isProtectedPath(folderPath)) {
    server->send(403, "text/plain", "Access denied to protected path");
    return;
  }

  LOG_DBG("WEB", "Creating folder: %s", folderPath.c_str());

  // Check if already exists
  if (Storage.exists(folderPath.c_str())) {
    server->send(400, "text/plain", "Folder already exists");
    return;
  }

  // Create the folder
  if (Storage.mkdir(folderPath.c_str())) {
    LOG_DBG("WEB", "Folder created successfully: %s", folderPath.c_str());
    server->send(200, "text/plain", "Folder created: " + folderName);
  } else {
    LOG_DBG("WEB", "Failed to create folder: %s", folderPath.c_str());
    server->send(500, "text/plain", "Failed to create folder");
  }
}

void InkMODWebServer::handleRename() const {
  if (!server->hasArg("path") || !server->hasArg("name")) {
    server->send(400, "text/plain", "Missing path or new name");
    return;
  }

  String itemPath = normalizeWebPath(server->arg("path"));
  String newName = StringUtils::sanitizeFilename(server->arg("name").c_str()).c_str();

  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }
  if (newName.isEmpty()) {
    server->send(400, "text/plain", "New name cannot be empty");
    return;
  }
  if (isProtectedPath(itemPath)) {
    server->send(403, "text/plain", "Cannot rename protected item");
    return;
  }

  // Calculate new path to check if it's protected
  String parentPath = itemPath.substring(0, itemPath.lastIndexOf('/'));
  if (parentPath.isEmpty()) {
    parentPath = "/";
  }
  String newPath = parentPath;
  if (!newPath.endsWith("/")) {
    newPath += "/";
  }
  newPath += newName;

  if (isProtectedPath(newPath)) {
    server->send(403, "text/plain", "Cannot rename to protected path");
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (newName == itemName) {
    server->send(200, "text/plain", "Name unchanged");
    return;
  }

  if (!Storage.exists(itemPath.c_str())) {
    server->send(404, "text/plain", "Item not found");
    return;
  }

  HalFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", "Failed to open item");
    return;
  }
  const bool isDirectory = file.isDirectory();

  if (Storage.exists(newPath.c_str())) {
    file.close();
    server->send(409, "text/plain", "Target already exists");
    return;
  }

  if (!isDirectory) {
    clearBookCache(itemPath.c_str());
  }
  const bool success = file.rename(newPath.c_str());
  file.close();

  if (success) {
    LOG_DBG("WEB", "Renamed %s: %s -> %s", isDirectory ? "folder" : "file", itemPath.c_str(), newPath.c_str());
    server->send(200, "text/plain", "Renamed successfully");
  } else {
    LOG_ERR("WEB", "Failed to rename %s: %s -> %s", isDirectory ? "folder" : "file", itemPath.c_str(),
            newPath.c_str());
    server->send(500, "text/plain", "Failed to rename item");
  }
}

void InkMODWebServer::handleMove() const {
  if (!server->hasArg("path") || !server->hasArg("dest")) {
    server->send(400, "text/plain", "Missing path or destination");
    return;
  }

  String itemPath = normalizeWebPath(server->arg("path"));
  String destPath = normalizeWebPath(server->arg("dest"));

  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }
  if (destPath.isEmpty()) {
    server->send(400, "text/plain", "Invalid destination");
    return;
  }

  if (isProtectedPath(itemPath)) {
    server->send(403, "text/plain", "Cannot move protected item");
    return;
  }
  if (isProtectedPath(destPath)) {
    server->send(403, "text/plain", "Cannot move into protected folder");
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);

  if (!Storage.exists(itemPath.c_str())) {
    server->send(404, "text/plain", "Item not found");
    return;
  }

  HalFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server->send(400, "text/plain", "Only files can be moved");
    return;
  }

  if (!Storage.exists(destPath.c_str())) {
    file.close();
    server->send(404, "text/plain", "Destination not found");
    return;
  }
  HalFile destDir = Storage.open(destPath.c_str());
  if (!destDir || !destDir.isDirectory()) {
    if (destDir) {
      destDir.close();
    }
    file.close();
    server->send(400, "text/plain", "Destination is not a folder");
    return;
  }
  destDir.close();

  String newPath = destPath;
  if (!newPath.endsWith("/")) {
    newPath += "/";
  }
  newPath += itemName;

  if (newPath == itemPath) {
    file.close();
    server->send(200, "text/plain", "Already in destination");
    return;
  }
  if (Storage.exists(newPath.c_str())) {
    file.close();
    server->send(409, "text/plain", "Target already exists");
    return;
  }

  clearBookCache(itemPath.c_str());
  const bool success = file.rename(newPath.c_str());
  file.close();

  if (success) {
    LOG_DBG("WEB", "Moved file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(200, "text/plain", "Moved successfully");
  } else {
    LOG_ERR("WEB", "Failed to move file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(500, "text/plain", "Failed to move file");
  }
}

namespace {
// Every level of this walk keeps a directory handle and a name buffer on the stack of the web task, so
// the depth is capped: FAT allows ~125 nested folders and an unbounded walk would overflow the stack.
// Books deeper than the cap only leave an orphaned cache folder behind, which cache cleanup removes.
constexpr int kCacheClearMaxDepth = 8;

void clearBookCachesRecursively(const String& itemPath, const int depth = 0) {
  if (depth > kCacheClearMaxDepth) return;
  HalFile node = Storage.open(itemPath.c_str());
  if (!node) return;

  if (!node.isDirectory()) {
    node.close();
    clearBookCache(itemPath.c_str());
    return;
  }

  HalFile child = node.openNextFile();
  char name[256];  // FAT long file names are at most 255 characters
  while (child) {
    child.getName(name, sizeof(name));
    const bool isDir = child.isDirectory();
    child.close();

    String childPath = itemPath;
    if (!childPath.endsWith("/")) childPath += "/";
    childPath += name;
    if (isDir)
      clearBookCachesRecursively(childPath, depth + 1);
    else
      clearBookCache(childPath.c_str());

    resetTaskWatchdogIfSubscribed();
    yield();
    child = node.openNextFile();
  }
  node.close();
}
}  // namespace

void InkMODWebServer::handleDelete() const {
  // To ensure backwards compatibility, plain `path` is mapped
  // to a single element JSON array.
  bool hasPathArg = server->hasArg("path");
  bool hasPathsArg = server->hasArg("paths");
  // Check 'paths' or `path` argument is provided
  if (!(hasPathArg || hasPathsArg)) {
    server->send(400, "text/plain", "Missing `path` or `paths` argument");
    return;
  }
  if (hasPathArg && hasPathsArg) {
    server->send(400, "text/plain", "Provide either 'path' or 'paths', not both");
    return;
  }

  // Parse paths
  String pathsArg;
  JsonDocument doc;
  DeserializationError error = DeserializationError(DeserializationError::Code::Ok);
  if (hasPathsArg) {
    pathsArg = server->arg("paths");
    error = deserializeJson(doc, pathsArg);
  } else {
    pathsArg = server->arg("path");
    doc.add(pathsArg);
  }
  if (error) {
    server->send(400, "text/plain", "Invalid paths format");
    return;
  }

  auto paths = doc.as<JsonArray>();
  if (paths.isNull() || paths.size() == 0) {
    server->send(400, "text/plain", "No paths provided");
    return;
  }

  // Iterate over paths and delete each item
  bool allSuccess = true;
  String failedItems;

  for (const auto& p : paths) {
    auto itemPath = p.as<String>();

    // Validate path
    if (itemPath.isEmpty() || itemPath == "/") {
      failedItems += itemPath + " (cannot delete root); ";
      allSuccess = false;
      continue;
    }

    // Ensure path starts with /
    if (!itemPath.startsWith("/")) {
      itemPath = "/" + itemPath;
    }

    // Security check: prevent deletion of protected items
    if (isProtectedPath(itemPath)) {
      failedItems += itemPath + " (protected path); ";
      allSuccess = false;
      continue;
    }

    // Check if item exists
    if (!Storage.exists(itemPath.c_str())) {
      failedItems += itemPath + " (not found); ";
      allSuccess = false;
      continue;
    }

    // Decide whether it's a directory or file by opening it
    bool success = false;
    HalFile f = Storage.open(itemPath.c_str());
    if (f && f.isDirectory()) {
      f.close();
      // Clear every book cache while the directory tree still exists, then
      // remove the selected directory. This prevents web deletions from
      // leaving orphaned /.inkmod/fb2_* / epub_* cache directories behind.
      clearBookCachesRecursively(itemPath);
      success = Storage.removeDir(itemPath.c_str());
    } else {
      // Clear by original path before deletion; cache keys are path-based and
      // do not require the book file to remain present, but doing it first is
      // safer for format-specific cache implementations.
      if (f) f.close();
      clearBookCache(itemPath.c_str());
      success = Storage.remove(itemPath.c_str());
    }

    if (!success) {
      failedItems += itemPath + " (deletion failed); ";
      allSuccess = false;
    }
  }

  if (allSuccess) {
    server->send(200, "text/plain", "All items deleted successfully");
  } else {
    server->send(500, "text/plain", "Failed to delete some items: " + failedItems);
  }
}

void InkMODWebServer::handleApplyWallpaper() {
  if (!server->hasArg("path")) {
    server->send(400, "text/plain", "Missing path");
    return;
  }

  const String path = normalizeWebPath(server->arg("path"));
  if (path.isEmpty() || path == "/" || isProtectedPath(path)) {
    server->send(400, "text/plain", "Invalid wallpaper path");
    return;
  }

  if (!path.endsWith(".png") && !path.endsWith(".PNG")) {
    server->send(400, "text/plain", "Wallpaper must be PNG");
    return;
  }

  if (!Storage.exists(path.c_str())) {
    server->send(404, "text/plain", "Uploaded PNG not found");
    return;
  }

  APP_STATE.favoriteSleepImagePath = path.c_str();
  SETTINGS.sleepScreen = InkMODSettings::OVERLAY;

  const bool stateSaved = APP_STATE.saveToFile();
  const bool settingsSaved = SETTINGS.saveToFile();
  if (!stateSaved || !settingsSaved) {
    server->send(500, "text/plain", "PNG uploaded, but sleep-screen setting could not be saved");
    return;
  }

  LOG_INF("WEB", "Sleep overlay set from web generator: %s", path.c_str());
  server->send(200, "application/json", "{\"ok\":true}");
}

void InkMODWebServer::handleSettingsPage() const {
  sendHtmlContent(server.get(), SettingsPageHtml, sizeof(SettingsPageHtml));
  LOG_DBG("WEB", "Served settings page");
}

void InkMODWebServer::handleGetSettings() const {
  // /api/settings is one of the largest dynamic JSON responses in the web UI.
  // Do not build it as one Arduino String (large contiguous heap allocation),
  // and do not use dozens of tiny HTTP chunks (can block the ESP32 WebServer
  // when the browser/TCP receive window briefly stalls).  Measure first, then
  // send the exact Content-Length using a bounded ~TCP-sized output buffer.
  resetTaskWatchdogIfSubscribed();
  yield();
  const auto settings = getSettingsList(nullptr);

  auto fillDocument = [](const SettingInfo& setting, JsonDocument& doc) -> bool {
    if (!setting.key) return false;
    if (std::strcmp(setting.key, "fontFamily") == 0 || std::strcmp(setting.key, "fontSize") == 0) return false;

    doc.clear();
    doc["key"] = setting.key;
    doc["name"] = I18N.get(setting.nameId);
    doc["category"] = I18N.get(setting.category);

    switch (setting.type) {
      case SettingType::TOGGLE: {
        doc["type"] = "toggle";
        if (setting.valuePtr) {
          const bool rawValue = SETTINGS.*(setting.valuePtr) != 0;
          doc["value"] = static_cast<int>(setting.invertedToggleDisplay ? !rawValue : rawValue);
        }
        return true;
      }
      case SettingType::ENUM: {
        doc["type"] = "enum";
        if (setting.valuePtr) {
          doc["value"] = static_cast<int>(enumDisplayIndexForRawValue(setting, SETTINGS.*(setting.valuePtr)));
        } else if (setting.valueGetter) {
          doc["value"] = static_cast<int>(setting.valueGetter());
        }
        JsonArray options = doc["options"].to<JsonArray>();
        if (!setting.enumStringValues.empty()) {
          for (const auto& opt : setting.enumStringValues) options.add(opt);
        } else {
          for (const auto& opt : setting.enumValues) options.add(I18N.get(opt));
        }
        return true;
      }
      case SettingType::VALUE:
        doc["type"] = "value";
        if (setting.valuePtr) doc["value"] = static_cast<int>(SETTINGS.*(setting.valuePtr));
        doc["min"] = setting.valueRange.min;
        doc["max"] = setting.valueRange.max;
        doc["step"] = setting.valueRange.step;
        return true;
      case SettingType::STRING:
        doc["type"] = "string";
        if (setting.stringGetter) {
          doc["value"] = setting.stringGetter();
        } else if (setting.stringMaxLen > 0 && !setting.obfuscated) {
          doc["value"] = reinterpret_cast<const char*>(&SETTINGS) + setting.stringOffset;
        } else if (setting.obfuscated) {
          doc["value"] = "";  // never echo credentials
        }
        return true;
      default:
        return false;
    }
  };

  // First pass: exact HTTP body length.  The web settings list has no mutable
  // font getter here (fontFamily/fontSize are intentionally omitted), so values
  // cannot change underneath this synchronous request.
  JsonDocument doc;
  size_t bodyLength = 2;  // '[' + ']'
  size_t itemCount = 0;
  for (const auto& setting : settings) {
    if (!fillDocument(setting, doc)) continue;
    const size_t n = measureJson(doc);
    if (n == 0 || n >= 2048) {
      LOG_DBG("WEB", "Skipping oversized setting JSON for: %s (%u bytes)", setting.key,
              static_cast<unsigned>(n));
      continue;
    }
    bodyLength += n + (itemCount ? 1u : 0u);
    ++itemCount;
    if ((itemCount & 0x07u) == 0u) {
      resetTaskWatchdogIfSubscribed();
      yield();
    }
  }

  server->setContentLength(bodyLength);
  server->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  // Settings are requested infrequently; closing this response avoids leaving a
  // half-open keep-alive socket monopolising the single synchronous WebServer.
  server->sendHeader("Connection", "close");
  server->send(200, "application/json", "");

  class SettingsWriter {
   public:
    explicit SettingsWriter(WebServer& server) : server_(server) {}
    void append(const char* data, size_t len) {
      while (len) {
        if (used_ == sizeof(buffer_)) flush();
        const size_t copy = std::min(len, sizeof(buffer_) - used_);
        memcpy(buffer_ + used_, data, copy);
        used_ += copy;
        data += copy;
        len -= copy;
      }
    }
    void append(char c) { append(&c, 1); }
    void flush() {
      if (!used_) return;
      server_.sendContent(buffer_, used_);
      used_ = 0;
      resetTaskWatchdogIfSubscribed();
      yield();
    }
   private:
    WebServer& server_;
    // Below one TCP MSS, but large enough that /api/settings normally needs
    // only a handful of socket writes instead of one write per setting.
    char buffer_[1024];
    size_t used_ = 0;
  } writer(*server);

  writer.append('[');
  bool first = true;
  char output[2048];

  for (const auto& setting : settings) {
    if (!fillDocument(setting, doc)) continue;
    const size_t expected = measureJson(doc);
    if (expected == 0 || expected >= sizeof(output)) continue;
    const size_t written = serializeJson(doc, output, sizeof(output));
    if (written != expected) {
      LOG_DBG("WEB", "Skipping short setting JSON for: %s (%u/%u bytes)", setting.key,
              static_cast<unsigned>(written), static_cast<unsigned>(expected));
      continue;
    }
    if (!first) writer.append(',');
    first = false;
    writer.append(output, written);
  }
  writer.append(']');
  writer.flush();

  resetTaskWatchdogIfSubscribed();
  LOG_DBG("WEB", "Served settings API (%u bytes, %u items)", static_cast<unsigned>(bodyLength),
          static_cast<unsigned>(itemCount));
}

void InkMODWebServer::handlePostSettings() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  resetTaskWatchdogIfSubscribed();
  yield();
  const auto settings = getSettingsList(nullptr);
  resetTaskWatchdogIfSubscribed();
  yield();
  int applied = 0;

  for (const auto& s : settings) {
    if (!s.key) continue;
    if (std::strcmp(s.key, "fontFamily") == 0 || std::strcmp(s.key, "fontSize") == 0) continue;
    if (!doc[s.key].is<JsonVariant>()) continue;

    switch (s.type) {
      case SettingType::TOGGLE: {
        const bool displayValue = doc[s.key].as<int>() != 0;
        const int val = s.invertedToggleDisplay ? !displayValue : displayValue;
        if (s.valuePtr) {
          SETTINGS.*(s.valuePtr) = val;
        }
        applied++;
        break;
      }
      case SettingType::ENUM: {
        const int val = doc[s.key].as<int>();
        const int maxVal = s.enumStringValues.empty() ? static_cast<int>(s.enumValues.size())
                                                      : static_cast<int>(s.enumStringValues.size());
        if (val >= 0 && val < maxVal) {
          if (s.valuePtr) {
            SETTINGS.*(s.valuePtr) = enumRawValueForDisplayIndex(s, static_cast<uint8_t>(val));
          } else if (s.valueSetter) {
            s.valueSetter(static_cast<uint8_t>(val));
          }
          applied++;
        }
        break;
      }
      case SettingType::VALUE: {
        const int val = doc[s.key].as<int>();
        if (val >= s.valueRange.min && val <= s.valueRange.max) {
          if (s.valuePtr) {
            SETTINGS.*(s.valuePtr) = static_cast<uint8_t>(val);
          }
          applied++;
        }
        break;
      }
      case SettingType::STRING: {
        const std::string val = doc[s.key].as<std::string>();
        if (s.obfuscated && val.empty()) break;  // masked on read: an empty value means "unchanged"
        if (std::strcmp(s.key, "deviceName") == 0 && (val.length() < InkMODSettings::MIN_DEVICE_NAME_LENGTH ||
                                                      val.length() > InkMODSettings::MAX_DEVICE_NAME_LENGTH)) {
          break;
        }
        if (s.stringSetter) {
          s.stringSetter(val);
        } else if (s.stringMaxLen > 0) {
          char* ptr = reinterpret_cast<char*>(&SETTINGS) + s.stringOffset;
          strncpy(ptr, val.c_str(), s.stringMaxLen - 1);
          ptr[s.stringMaxLen - 1] = '\0';
        }
        applied++;
        break;
      }
      default:
        break;
    }
  }

  const bool saved = SETTINGS.saveToFile();

  LOG_DBG("WEB", "Applied %d setting(s), saved=%d", applied, saved);
  if (!saved) {
    // The new values are active until reboot, but they did not reach the SD card.
    server->send(500, "text/plain", "Settings applied but could not be saved to the SD card");
    return;
  }
  server->send(200, "text/plain", String("Applied ") + String(applied) + " setting(s)");
}

// ---- OPDS Server API ----

void InkMODWebServer::handleGetOpdsServers() const {
  const auto& servers = OPDS_STORE.getServers();

  // Stream JSON array incrementally to avoid allocating the full response in memory
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  char output[896];  // name/url + user agent (120) + custom header (~250)
  constexpr size_t outputSize = sizeof(output);
  JsonDocument doc;

  bool seenFirst = false;  // an oversize entry is skipped, so the index cannot place the commas
  for (size_t i = 0; i < servers.size(); i++) {
    doc.clear();
    doc["index"] = i;
    doc["name"] = servers[i].name;
    doc["url"] = servers[i].url;
    doc["username"] = servers[i].username;
    doc["filenameFormat"] = opdsFilenameFormatToJson(servers[i].filenameFormat);
    doc["userAgent"] = servers[i].userAgent;
    doc["customHeader"] = servers[i].customHeader;
    doc["downloadFolder"] = servers[i].downloadFolder;
    // Never expose passwords over the API вЂ” only indicate whether one is set
    doc["hasPassword"] = !servers[i].password.empty();

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) continue;

    if (seenFirst) server->sendContent(",");
    seenFirst = true;
    server->sendContent(output);
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("WEB", "Served OPDS servers API (%zu servers)", servers.size());
}

void InkMODWebServer::handlePostOpdsServer() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  OpdsServer opdsServer;
  opdsServer.name = doc["name"] | std::string("");
  opdsServer.url = doc["url"] | std::string("");
  opdsServer.username = doc["username"] | std::string("");
  opdsServer.filenameFormat = opdsFilenameFormatFromJson(doc["filenameFormat"] | "");

  // HTTP identity. Fields absent from the payload (older clients) keep the stored values below.
  const bool hasUserAgentField = doc["userAgent"].is<const char*>() || doc["userAgent"].is<std::string>();
  const bool hasHeaderField = doc["customHeader"].is<const char*>() || doc["customHeader"].is<std::string>();
  if (hasUserAgentField) {
    const std::string rawAgent = doc["userAgent"] | std::string("");
    opdsServer.userAgent = opds_profile::sanitizeUserAgent(rawAgent);
    if (opdsServer.userAgent.empty() && !rawAgent.empty()) {
      server->send(400, "text/plain", "Invalid User-Agent");
      return;
    }
  }
  const bool hasFolderField = doc["downloadFolder"].is<const char*>() || doc["downloadFolder"].is<std::string>();
  if (hasFolderField) {
    const std::string rawFolder = doc["downloadFolder"] | std::string("");
    opdsServer.downloadFolder = opds_profile::sanitizeDownloadFolder(rawFolder);
    const bool rootOnly = rawFolder.find_first_not_of(" \t/") == std::string::npos;
    if (opdsServer.downloadFolder.empty() && !rootOnly) {
      server->send(400, "text/plain", "Invalid download folder (use an SD path like /Books/OPDS)");
      return;
    }
  }
  if (hasHeaderField) {
    opds_profile::Header header;
    if (!opds_profile::parseHeaderLine(doc["customHeader"] | std::string(""), header)) {
      server->send(400, "text/plain", "Invalid custom header (use \"Name: value\"; reserved names are not allowed)");
      return;
    }
    opdsServer.customHeader = opds_profile::formatHeaderLine(header);
  }

  // The password field is optional in the JSON payload. When absent (vs. present but empty),
  // we preserve the existing password вЂ” the web UI omits it when the user hasn't changed it.
  bool hasPasswordField = doc["password"].is<const char*>() || doc["password"].is<std::string>();
  std::string password = doc["password"] | std::string("");
  const bool hasFilenameFormatField =
      doc["filenameFormat"].is<const char*>() || doc["filenameFormat"].is<std::string>();

  if (doc["index"].is<int>()) {
    int idx = doc["index"].as<int>();
    if (idx < 0 || idx >= static_cast<int>(OPDS_STORE.getCount())) {
      server->send(400, "text/plain", "Invalid server index");
      return;
    }
    const auto* existing = OPDS_STORE.getServer(static_cast<size_t>(idx));
    // Preserve existing values for fields older clients do not know how to send.
    if (existing && !hasPasswordField) {
      password = existing->password;
    }
    if (existing && !hasFilenameFormatField) {
      opdsServer.filenameFormat = existing->filenameFormat;
    }
    if (existing && !hasUserAgentField) opdsServer.userAgent = existing->userAgent;
    if (existing && !hasHeaderField) opdsServer.customHeader = existing->customHeader;
    if (existing && !hasFolderField) opdsServer.downloadFolder = existing->downloadFolder;
    opdsServer.password = password;
    OPDS_STORE.updateServer(static_cast<size_t>(idx), opdsServer);
    LOG_DBG("WEB", "Updated OPDS server at index %d", idx);
  } else {
    opdsServer.password = password;
    if (!OPDS_STORE.addServer(opdsServer)) {
      server->send(400, "text/plain", "Cannot add server (limit reached)");
      return;
    }
    LOG_DBG("WEB", "Added new OPDS server: %s", opdsServer.name.c_str());
  }

  server->send(200, "text/plain", "OK");
}

// Uses POST (not HTTP DELETE) because ESP32 WebServer doesn't support DELETE with body.
void InkMODWebServer::handleDeleteOpdsServer() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  if (!doc["index"].is<int>()) {
    server->send(400, "text/plain", "Missing index");
    return;
  }

  int idx = doc["index"].as<int>();
  if (idx < 0 || idx >= static_cast<int>(OPDS_STORE.getCount())) {
    server->send(400, "text/plain", "Invalid server index");
    return;
  }

  OPDS_STORE.removeServer(static_cast<size_t>(idx));
  LOG_DBG("WEB", "Deleted OPDS server at index %d", idx);
  server->send(200, "text/plain", "OK");
}

// ---- Wi-Fi Credentials API ----

void InkMODWebServer::handleGetWifiNetworks() const {
  const auto& credentials = WIFI_STORE.getCredentials();
  const std::string& lastConnectedSsid = WIFI_STORE.getLastConnectedSsid();

  // Stream JSON array incrementally to avoid allocating the full response in memory
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  char output[320];
  constexpr size_t outputSize = sizeof(output);
  JsonDocument doc;

  bool seenFirst = false;  // an oversize entry is skipped, so the index cannot place the commas
  for (size_t i = 0; i < credentials.size(); i++) {
    doc.clear();
    doc["index"] = i;
    doc["ssid"] = credentials[i].ssid;
    // Never expose Wi-Fi passwords over the API вЂ” only indicate whether one is set
    doc["hasPassword"] = !credentials[i].password.empty();
    doc["isLastConnected"] = credentials[i].ssid == lastConnectedSsid;

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) continue;

    if (seenFirst) server->sendContent(",");
    seenFirst = true;
    server->sendContent(output);
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("WEB", "Served Wi-Fi credentials API (%zu network(s))", credentials.size());
}

void InkMODWebServer::handlePostWifiNetwork() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  std::string ssid = doc["ssid"] | std::string("");
  if (ssid.empty()) {
    server->send(400, "text/plain", "SSID is required");
    return;
  }

  // The password field is optional in the JSON payload. When absent (vs. present but empty),
  // preserve the existing password for updates. Empty passwords are valid for open networks.
  bool hasPasswordField = doc["password"].is<const char*>() || doc["password"].is<std::string>();
  std::string password = doc["password"] | std::string("");

  if (doc["index"].is<int>()) {
    int idx = doc["index"].as<int>();
    const auto& credentials = WIFI_STORE.getCredentials();
    if (idx < 0 || idx >= static_cast<int>(credentials.size())) {
      server->send(400, "text/plain", "Invalid network index");
      return;
    }

    const std::string oldSsid = credentials[static_cast<size_t>(idx)].ssid;
    if (!hasPasswordField) {
      password = credentials[static_cast<size_t>(idx)].password;
    }

    bool ok = true;
    if (oldSsid != ssid) {
      ok = WIFI_STORE.removeCredential(oldSsid) && WIFI_STORE.addCredential(ssid, password);
    } else {
      ok = WIFI_STORE.addCredential(ssid, password);
    }

    if (!ok) {
      server->send(400, "text/plain", "Failed to update Wi-Fi network");
      return;
    }

    LOG_DBG("WEB", "Updated Wi-Fi network at index %d (SSID: %s)", idx, ssid.c_str());
  } else {
    if (!WIFI_STORE.addCredential(ssid, password)) {
      server->send(400, "text/plain", "Cannot add network (limit reached)");
      return;
    }
    LOG_DBG("WEB", "Added Wi-Fi network: %s", ssid.c_str());
  }

  server->send(200, "text/plain", "OK");
}

// Uses POST (not HTTP DELETE) because ESP32 WebServer doesn't support DELETE with body.
void InkMODWebServer::handleDeleteWifiNetwork() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  if (!doc["index"].is<int>()) {
    server->send(400, "text/plain", "Missing index");
    return;
  }

  int idx = doc["index"].as<int>();
  const auto& credentials = WIFI_STORE.getCredentials();
  if (idx < 0 || idx >= static_cast<int>(credentials.size())) {
    server->send(400, "text/plain", "Invalid network index");
    return;
  }

  const std::string ssid = credentials[static_cast<size_t>(idx)].ssid;
  if (!WIFI_STORE.removeCredential(ssid)) {
    server->send(400, "text/plain", "Failed to delete Wi-Fi network");
    return;
  }

  LOG_DBG("WEB", "Deleted Wi-Fi network at index %d (SSID: %s)", idx, ssid.c_str());
  server->send(200, "text/plain", "OK");
}

// WebSocket callback trampoline
void InkMODWebServer::wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (wsInstance) {
    wsInstance->onWebSocketEvent(num, type, payload, length);
  }
}

// WebSocket event handler for fast binary uploads
// Protocol:
//   1. Client sends TEXT message: "START:<filename>:<size>:<path>"
//   2. Client sends BINARY messages with file data chunks
//   3. Server sends TEXT "PROGRESS:<received>:<total>" after each chunk
//   4. Server sends TEXT "DONE" or "ERROR:<message>" when complete
void InkMODWebServer::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (num < WEBSOCKETS_SERVER_CLIENT_MAX) wsLastActivity[num] = type == WStype_DISCONNECTED ? 0 : millis();
  switch (type) {
    case WStype_DISCONNECTED:
      LOG_DBG("WS", "Client %u disconnected", num);
      // Only clean up if this is the client that owns the active upload.
      // A new client may have already started a fresh upload before this
      // DISCONNECTED event fires (race condition on quick cancel + retry).
      if (num == wsUploadClientNum && wsUploadInProgress && wsUploadFile) {
        abortWsUpload("WS");
      }
      break;

    case WStype_CONNECTED: {
      LOG_DBG("WS", "Client %u connected", num);
      break;
    }

    case WStype_TEXT: {
      // Parse control messages
      String msg = String((char*)payload);
      LOG_DBG("WS", "Text from client %u: %s", num, msg.c_str());

      if (msg.startsWith("START:")) {
        // Reject any START while an upload is already active to prevent
        // leaking the open wsUploadFile handle (owning client re-START included)
        if (wsUploadInProgress) {
          wsServer->sendTXT(num, "ERROR:Upload already in progress");
          break;
        }

        // Parse: START:<filename>:<size>:<path>
        int firstColon = msg.indexOf(':', 6);
        int secondColon = msg.indexOf(':', firstColon + 1);

        if (firstColon > 0 && secondColon > 0) {
          String requestedUploadName = msg.substring(6, firstColon);
          requestedUploadName.replace('\\', '/');
          const int uploadSlash = requestedUploadName.lastIndexOf('/');
          if (uploadSlash >= 0) requestedUploadName = requestedUploadName.substring(uploadSlash + 1);
          wsUploadFileName = StringUtils::sanitizeFilename(requestedUploadName.c_str()).c_str();
          String sizeToken = msg.substring(firstColon + 1, secondColon);
          bool sizeValid = sizeToken.length() > 0;
          int digitStart = (sizeValid && sizeToken[0] == '+') ? 1 : 0;
          if (digitStart > 0 && sizeToken.length() < 2) sizeValid = false;
          for (int i = digitStart; i < (int)sizeToken.length() && sizeValid; i++) {
            if (!isdigit((unsigned char)sizeToken[i])) sizeValid = false;
          }
          if (!sizeValid) {
            LOG_DBG("WS", "START rejected: invalid size token '%s'", sizeToken.c_str());
            wsServer->sendTXT(num, "ERROR:Invalid START format");
            return;
          }
          wsUploadSize = sizeToken.toInt();
          wsUploadPath = normalizeWebPath(msg.substring(secondColon + 1));
          wsUploadReceived = 0;
          wsLastProgressSent = 0;
          wsUploadStartTime = millis();

          // Build file path
          String filePath = wsUploadPath;
          if (!filePath.endsWith("/")) filePath += "/";
          filePath += wsUploadFileName;

          if (isProtectedPath(filePath)) {
            wsServer->sendTXT(num, "ERROR:Access denied to protected path");
            wsUploadInProgress = false;
            wsUploadClientNum = 255;
            return;
          }

          LOG_DBG("WS", "Starting upload: %s (%d bytes) to %s", wsUploadFileName.c_str(), wsUploadSize,
                  filePath.c_str());

          // Check if file exists and remove it
          resetTaskWatchdogIfSubscribed();
          if (Storage.exists(filePath.c_str())) {
            Storage.remove(filePath.c_str());
          }

          // Open file for writing
          resetTaskWatchdogIfSubscribed();
          if (!Storage.openFileForWrite("WS", filePath, wsUploadFile)) {
            wsServer->sendTXT(num, "ERROR:Failed to create file");
            wsUploadInProgress = false;
            wsUploadClientNum = 255;
            return;
          }
          resetTaskWatchdogIfSubscribed();

          // Zero-byte upload: complete immediately without waiting for BIN frames
          if (wsUploadSize == 0) {
            // Explicit close() required: file-scope global persists beyond function scope
            wsUploadFile.close();
            wsLastCompleteName = wsUploadFileName;
            wsLastCompleteSize = 0;
            wsLastCompleteAt = millis();
            LOG_DBG("WS", "Zero-byte upload complete: %s", filePath.c_str());
            clearBookCachePreservingUserState(filePath.c_str());
            wsServer->sendTXT(num, "DONE");
            wsLastProgressSent = 0;
            break;
          }

          wsUploadClientNum = num;
          wsUploadInProgress = true;
          wsServer->sendTXT(num, "READY");
        } else {
          wsServer->sendTXT(num, "ERROR:Invalid START format");
        }
      }
      break;
    }

    case WStype_BIN: {
      if (!wsUploadInProgress || !wsUploadFile || num != wsUploadClientNum) {
        wsServer->sendTXT(num, "ERROR:No upload in progress");
        return;
      }

      // Write binary data directly to file
      size_t remaining = wsUploadSize - wsUploadReceived;
      if (length > remaining) {
        abortWsUpload("WS");
        wsServer->sendTXT(num, "ERROR:Upload overflow");
        return;
      }
      resetTaskWatchdogIfSubscribed();
      size_t written = wsUploadFile.write(payload, length);
      resetTaskWatchdogIfSubscribed();

      if (written != length) {
        abortWsUpload("WS");
        wsServer->sendTXT(num, "ERROR:Write failed - disk full?");
        return;
      }

      wsUploadReceived += written;

      // Send progress update (every 64KB or at end)
      if (wsUploadReceived - wsLastProgressSent >= 65536 || wsUploadReceived >= wsUploadSize) {
        String progress = "PROGRESS:" + String(wsUploadReceived) + ":" + String(wsUploadSize);
        wsServer->sendTXT(num, progress);
        wsLastProgressSent = wsUploadReceived;
      }

      // Check if upload complete
      if (wsUploadReceived >= wsUploadSize) {
        // Explicit close() required: file-scope global persists beyond function scope
        wsUploadFile.close();
        wsUploadInProgress = false;
        wsUploadClientNum = 255;

        wsLastCompleteName = wsUploadFileName;
        wsLastCompleteSize = wsUploadSize;
        wsLastCompleteAt = millis();

        unsigned long elapsed = millis() - wsUploadStartTime;
        float kbps = (elapsed > 0) ? (wsUploadSize / 1024.0) / (elapsed / 1000.0) : 0;

        LOG_DBG("WS", "Upload complete: %s (%d bytes in %lu ms, %.1f KB/s)", wsUploadFileName.c_str(), wsUploadSize,
                elapsed, kbps);

        // Clear epub cache to prevent stale metadata issues when overwriting files
        String filePath = wsUploadPath;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += wsUploadFileName;
        clearBookCachePreservingUserState(filePath.c_str());

        wsServer->sendTXT(num, "DONE");
        wsLastProgressSent = 0;
      }
      break;
    }

    default:
      break;
  }
}

// --- Font management handlers ---

void InkMODWebServer::handleFontsPage() const {
  sendHtmlContent(server.get(), FontsPageHtml, sizeof(FontsPageHtml));
  LOG_DBG("WEB", "Served fonts page");
}

void InkMODWebServer::handleFontList() const {
  const_cast<SdCardFontSystem&>(sdFontSystem).refreshIfDirty();
  const auto& families = sdFontSystem.registry().getFamilies();

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  FontListJsonWriter out(*server);
  out.append("{\"families\":[");
  bool firstFamily = true;
  for (const auto& family : families) {
    if (!firstFamily) out.append(",");
    firstFamily = false;
    out.append("{\"name\":");
    out.appendJsonString(family.name.c_str());
    out.append(",\"sizes\":[");
    bool firstSize = true;
    for (uint8_t size : family.availableSizes()) {
      if (!firstSize) out.append(",");
      firstSize = false;
      out.appendUnsigned(size);
    }
    out.append("],\"files\":[");
    bool firstFile = true;
    // A TrueType family lists its real files, not the per-size placeholders the size menu uses.
    std::vector<const std::string*> listed;
    if (family.vector) {
      for (const auto& path : family.vectorPaths) {
        if (!path.empty()) listed.push_back(&path);
      }
    } else {
      for (const auto& file : family.files) listed.push_back(&file.path);
    }
    for (const std::string* filePath : listed) {
      if (!firstFile) out.append(",");
      firstFile = false;
      const char* name = strrchr(filePath->c_str(), '/');
      name = name ? name + 1 : filePath->c_str();
      unsigned long size = 0;
      HalFile f;
      if (Storage.openFileForRead("WEB", filePath->c_str(), f)) {
        size = static_cast<unsigned long>(f.size());
        f.close();
      }
      out.append("{\"name\":");
      out.appendJsonString(name);
      out.append(",\"size\":");
      out.appendUnsigned(size);
      out.append("}");
    }
    out.append("]}");
    out.flush();
  }
  out.append("],\"maxFamilies\":");
  out.appendUnsigned(SdCardFontRegistry::MAX_SD_FAMILIES);
  out.append("}");
  out.flush();
  server->sendContent("");
}

void InkMODWebServer::handleFontUploadData() {
  HTTPUpload& upload = server->upload();

  switch (upload.status) {
    case UPLOAD_FILE_START: {
      resetTaskWatchdogIfSubscribed();
      String family = server->arg("family");
      fontUpload.file = HalFile();
      fontUpload.familyName.clear();
      fontUpload.filePath.clear();
      fontUpload.tempPath.clear();
      fontUpload.valid = false;
      fontUpload.magicChecked = false;
      fontUpload.kind = 0;
      fontUpload.error.clear();
      fontUpload.bytesWritten = 0;
      fontUpload.bufferPos = 0;

      if (!FontInstaller::isValidFamilyName(family.c_str())) {
        LOG_ERR("WEB", "Invalid font family name: %s", family.c_str());
        break;
      }

      String filename = upload.filename;
      filename.replace(' ', '_');
      // Validate filename: rejects path traversal (../, /, \) and enforces
      // a .cpfont basename of alphanumeric + hyphen + underscore. Without
      // this an attacker could supply "../../.inkmod/settings.json" as
      // a "filename" and have it written outside the fonts directory.
      const font_upload::Kind uploadKind = font_upload::classify(filename.c_str());
      if (uploadKind == font_upload::Kind::None) {
        LOG_ERR("WEB", "Invalid font filename: %s", filename.c_str());
        break;
      }
#if !INKMOD_VECTOR_FONTS
      if (uploadKind == font_upload::Kind::Vector) {
        LOG_ERR("WEB", "TrueType upload refused: this board has no PSRAM");
        fontUpload.error = "TrueType fonts need a board with PSRAM; convert to .cpfont first";
        break;
      }
#endif
      fontUpload.kind = static_cast<uint8_t>(uploadKind);

      fontUpload.familyName = family.c_str();

      if (!fontUpload.ensureBuffer()) {
        LOG_ERR("WEB", "Font upload refused: maxAlloc=%u", static_cast<unsigned>(ESP.getMaxAllocHeap()));
        break;
      }

      // Create a temporary FontInstaller for directory creation
      FontInstaller installer(sdFontSystem.registry());
      if (!installer.ensureFamilyDir(family.c_str())) {
        LOG_ERR("WEB", "Failed to create font family dir");
        break;
      }

      char path[192];
      FontInstaller::buildFontPath(family.c_str(), filename.c_str(), path, sizeof(path));
      fontUpload.filePath = path;
      // Write to a side file and swap it in only after a complete, valid upload, so a dropped
      // connection never leaves a truncated .cpfont (or destroys the one being replaced).
      fontUpload.tempPath = fontUpload.filePath + kUploadTempSuffix;
      Storage.remove(fontUpload.tempPath.c_str());

      if (!Storage.openFileForWrite("WEB", fontUpload.tempPath.c_str(), fontUpload.file)) {
        LOG_ERR("WEB", "Failed to open font file for write: %s", fontUpload.tempPath.c_str());
        break;
      }

      fontUpload.valid = true;
      LOG_DBG("WEB", "Font upload started: %s -> %s", filename.c_str(), path);
      break;
    }

    case UPLOAD_FILE_WRITE: {
      if (!fontUpload.valid) break;
      resetTaskWatchdogIfSubscribed();

      // Validate magic bytes on first chunk only
      if (!fontUpload.magicChecked && upload.currentSize >= 8) {
        if (!font_upload::magicMatches(static_cast<font_upload::Kind>(fontUpload.kind), upload.buf,
                                       upload.currentSize)) {
          LOG_ERR("WEB", "Invalid font magic bytes");
          fontUpload.valid = false;
          break;
        }
        fontUpload.magicChecked = true;
      }

      // Buffer writes for efficiency
      size_t remaining = upload.currentSize;
      const uint8_t* src = upload.buf;
      while (remaining > 0) {
        size_t space = FontUploadState::BUFFER_SIZE - fontUpload.bufferPos;
        size_t chunk = (remaining < space) ? remaining : space;
        memcpy(fontUpload.buffer.data() + fontUpload.bufferPos, src, chunk);
        fontUpload.bufferPos += chunk;
        src += chunk;
        remaining -= chunk;

        if (fontUpload.bufferPos >= FontUploadState::BUFFER_SIZE) {
          if (fontUpload.file.write(fontUpload.buffer.data(), fontUpload.bufferPos) != fontUpload.bufferPos) {
            LOG_ERR("WEB", "Font upload: SD write failed");
            fontUpload.valid = false;
            fontUpload.bufferPos = 0;
            break;
          }
          fontUpload.bytesWritten += fontUpload.bufferPos;
          fontUpload.bufferPos = 0;
          resetTaskWatchdogIfSubscribed();
        }
      }
      break;
    }

    case UPLOAD_FILE_END: {
      // Flush remaining buffer
      if (fontUpload.valid && fontUpload.bufferPos > 0) {
        if (fontUpload.file.write(fontUpload.buffer.data(), fontUpload.bufferPos) != fontUpload.bufferPos) {
          LOG_ERR("WEB", "Font upload: final SD write failed");
          fontUpload.valid = false;
        } else {
          fontUpload.bytesWritten += fontUpload.bufferPos;
        }
        fontUpload.bufferPos = 0;
      }
      if (fontUpload.file.isOpen()) {
        fontUpload.file.close();
      }

      // A font without the magic header (shorter than 8 bytes) is never valid.
      if (fontUpload.valid && !fontUpload.magicChecked) fontUpload.valid = false;

      if (!fontUpload.tempPath.empty()) {
        if (fontUpload.valid) {
          fontUpload.valid = FileReplace::replace(Storage, fontUpload.tempPath.c_str(), fontUpload.filePath.c_str());
        }
        if (!fontUpload.valid) Storage.remove(fontUpload.tempPath.c_str());
      }
      fontUpload.releaseBuffer();

      LOG_DBG("WEB", "Font upload end: valid=%d, %zu bytes", fontUpload.valid, fontUpload.bytesWritten);
      break;
    }

    case UPLOAD_FILE_ABORTED: {
      if (fontUpload.file) {
        fontUpload.file.close();
      }
      if (!fontUpload.tempPath.empty()) {
        Storage.remove(fontUpload.tempPath.c_str());
      }
      fontUpload.valid = false;
      fontUpload.releaseBuffer();
      LOG_DBG("WEB", "Font upload aborted");
      break;
    }
  }
}

void InkMODWebServer::handleFontUpload() {
  if (fontUpload.valid) {
    sdFontSystem.markRegistryDirty();
    server->send(200, "application/json", "{\"ok\":true}");
    LOG_DBG("WEB", "Font upload complete: %s", fontUpload.filePath.c_str());
  } else {
    const char* msg = fontUpload.error.empty() ? "Invalid .cpfont / .ttf / .otf file" : fontUpload.error.c_str();
    JsonDocument errDoc;
    errDoc["error"] = msg;
    String errJson;
    serializeJson(errDoc, errJson);
    server->send(400, "application/json", errJson);
  }
}

void InkMODWebServer::handleFontDelete() {
  String body = server->arg("plain");
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);

  if (err || !doc["family"].is<const char*>()) {
    server->send(400, "application/json", "{\"error\":\"Invalid request\"}");
    return;
  }

  const char* familyName = doc["family"];
  FontInstaller installer(sdFontSystem.registry());
  auto result = installer.deleteFamily(familyName);

  if (result == FontInstaller::Error::OK) {
    sdFontSystem.markRegistryDirty();
    server->send(200, "application/json", "{\"ok\":true}");
    LOG_DBG("WEB", "Deleted font family: %s", familyName);
  } else {
    server->send(500, "application/json", "{\"error\":\"Delete failed\"}");
    LOG_ERR("WEB", "Failed to delete font family: %s", familyName);
  }
}
