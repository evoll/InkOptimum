#pragma once

#include <PluginItems.h>
#include <PluginManifest.h>
#include <PluginTemplate.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/PluginHttp.h"

// On-device plugin catalog: browses and downloads what a plugin's device.json describes (JSON catalogs with
// lists, paging, version badges, single-file and bundle downloads). The pure parts live in lib/PluginCatalog;
// this class is the Wi-Fi, network, SD and screen glue.
class PluginCatalogActivity final : public Activity {
 public:
  PluginCatalogActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                        std::string pluginDir)
      : Activity("PluginCatalog", renderer, mappedInput), title_(std::move(title)), pluginDir_(std::move(pluginDir)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override {
    return state_ == State::LOADING || state_ == State::DOWNLOADING || state_ == State::SIGN_IN_CODE;
  }

 private:
  enum class State { CONNECTING, WIFI_SELECTION, SIGN_IN_PROMPT, SIGN_IN_CODE, LISTS, LOADING, ITEMS, DETAILS, DOWNLOADING, MESSAGE, ERROR };
  enum class RowKind { Prev, Item, Next };
  enum class Retry { Lists, Page, None };

  struct Row {
    RowKind kind = RowKind::Item;
    plugincat::Item item;
    bool installed = false;
    bool update = false;
    bool folder = false;  // XML catalogs: Confirm descends
  };

  // setup
  bool loadManifest();
  void connect();
  void onWifiResult(bool connected);
  plugincat::Vars baseVars() const;
  plugincat::Request build(const plugincat::Request& tpl, const plugincat::Vars& vars,
                           const plugincat::Vars& itemVars) const;

  // browsing
  void startBrowsing();
  void loadLists();
  void openList(size_t index);
  void loadPage(int page);
  void loadXml();
  void buildRows(const std::vector<plugincat::Item>& items, bool more);
  void annotateInstalled(Row& row) const;
  std::string bundleDirFor(const plugincat::Item& item) const;

  // sign-in
  bool needsToken() const { return !manifest_.tokenFile.empty(); }
  std::string currentToken() const;
  void setToken(const std::string& token);
  bool mintPasswordToken();
  void beginDeviceCode();
  void pollDeviceCode();
  // True when a 401/403 was handled (silent re-sign-in retried, or the sign-in screen shown).
  bool recoverAuth(const PluginHttp::Result& result, Retry retry);

  // downloads
  void showDetails(size_t rowIndex);
  void confirmInstall(size_t rowIndex);
  void install(const plugincat::Item& item);
  bool installBundle(const plugincat::Item& item);
  bool installFile(const plugincat::Item& item);
  bool pollCancel();
  void onProgress(size_t done, size_t total);

  void fail(const std::string& message, Retry retry);
  void failHttp(const PluginHttp::Result& result);
  void showMessage(const std::string& message);
  void goBack();
  size_t listCount() const;

  std::string title_;
  std::string pluginDir_;
  plugincat::DeviceManifest manifest_;
  std::unique_ptr<PluginHttp> http_;
  plugincat::Vars secretVars_;  // token + cfg.* (never shown)

  State state_ = State::CONNECTING;
  Retry retry_ = Retry::None;
  std::string statusText_, errorText_;
  std::vector<plugincat::BrowseList> lists_;
  int activeList_ = -1;  // -1: the manifest's own browse request
  std::vector<Row> rows_;
  int page_ = 1;
  int selected_ = 0;
  bool cancelled_ = false;
  size_t progressDone_ = 0, progressTotal_ = 0;
  uint32_t lastUiMs_ = 0;
  size_t detailsRow_ = 0;
  std::vector<std::string> detailsLines_;
  int detailsScroll_ = 0;
  std::string xmlUrl_;  // XML catalogs: the folder being listed
  std::vector<std::string> folderStack_;
  bool authRetried_ = false;
  std::string signInCode_, signInUrl_, deviceCode_;
  uint32_t pollIntervalMs_ = 5000, nextPollMs_ = 0, signInDeadlineMs_ = 0;
  ButtonNavigator buttonNavigator_;
};
