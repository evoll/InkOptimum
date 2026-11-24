#include "AppsActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <XtApp.h>

#include <memory>

#include "BookmarkStore.h"
#include "LcpLoans.h"
#include "activities/apps/ClockActivity.h"
#include "activities/apps/LcpLoansActivity.h"
#include "activities/apps/ReadingCalendarActivity.h"
#include "activities/shelves/ShelvesActivity.h"
#include "activities/apps/XtAppActivity.h"
#include "InkMODSettings.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "activities/plugins/PluginCatalogActivity.h"
#include "activities/plugins/PluginReadmeActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/PluginEventSummary.h"
#include "util/TouchListNavigation.h"

namespace {
constexpr size_t MAX_MANIFEST_BYTES = 8 * 1024;

struct ManifestInfo {
  std::string title;
  std::string description;
  bool hasBrowseUrl = false;
  bool hasEvents = false;
};

bool readManifest(const std::string& path, ManifestInfo& out) {
  if (!Storage.exists(path.c_str())) return false;
  std::string buffer(MAX_MANIFEST_BYTES, '\0');
  const size_t n = Storage.readFileToBuffer(path.c_str(), buffer.data(), buffer.size(), MAX_MANIFEST_BYTES - 1);
  if (n == 0 || n >= MAX_MANIFEST_BYTES - 1) return false;
  buffer.resize(n);

  JsonDocument filter;
  filter["title"] = true;
  filter["description"] = true;
  filter["browse"]["url"] = true;
  filter["events"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, buffer, DeserializationOption::Filter(filter))) return false;

  out.title = doc["title"] | "";
  out.description = doc["description"] | "";
  const char* url = doc["browse"]["url"] | "";
  out.hasBrowseUrl = url[0] != '\0';
  out.hasEvents = !doc["events"].isNull();
  return true;
}

// Lua mini apps live in /XTApps/<id>/ (manifest.json + index.lua). Official Licorice packages (*.xtapp) are listed
// too, but only to say that they are encrypted and cannot run here.
struct XtEntry {
  std::string title;
  std::string description;
  std::string dir;
  std::string error;
};

void scanXtApps(std::vector<XtEntry>& out) {
  HalFile root = Storage.open("/XTApps");
  if (!root || !root.isDirectory()) return;
  for (HalFile entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    char name[128];
    if (entry.getName(name, sizeof(name)) == 0 || name[0] == '.') continue;
    XtEntry item;
    const std::string path = std::string("/XTApps/") + name;
    if (entry.isDirectory()) {
      std::string buffer(MAX_MANIFEST_BYTES, '\0');
      const size_t n = Storage.readFileToBuffer((path + "/manifest.json").c_str(), buffer.data(), buffer.size(),
                                                MAX_MANIFEST_BYTES - 1);
      if (n == 0 || n >= MAX_MANIFEST_BYTES - 1) continue;
      buffer.resize(n);
      xtapp::Manifest m;
      if (!xtapp::parseManifest(buffer, m, nullptr, I18N.getLanguage() == Language::RU ? "ru" : "en")) continue;
      item.title = m.displayName;
      item.description = m.description.empty() ? std::string(tr(STR_APP_XTAPP)) : m.description;
      item.dir = path;
      if (m.encrypted) {
        item.error = tr(STR_APP_XTAPP_LOCKED);
      } else if (!Storage.exists((path + "/" + m.entry).c_str())) {
        continue;
      }
    } else {
      const size_t len = strlen(name);
      if (len < 7 || strcasecmp(name + len - 6, ".xtapp") != 0) continue;
      char head[0x80 + 0x400];
      const size_t got = Storage.readFileToBuffer(path.c_str(), head, sizeof(head), sizeof(head));
      const auto info = xtapp::inspectContainer(std::string(head, got));
      xtapp::Manifest m;
      item.title = (info.valid && xtapp::parseManifest(info.manifestJson, m)) ? m.displayName : std::string(name);
      item.description = tr(STR_APP_XTAPP_LOCKED);
      item.dir = path;
      item.error = tr(STR_APP_XTAPP_LOCKED);
    }
    out.push_back(std::move(item));
  }
}
}  // namespace

void AppsActivity::loadItems() {
  items_.clear();

  auto addBuiltIn = [this](int action, const char* title, const char* description) {
    Item item;
    item.builtIn = true;
    item.action = action;
    item.title = title;
    item.description = description;
    items_.push_back(std::move(item));
  };
  addBuiltIn(ACTION_WEATHER, tr(STR_WEATHER), tr(STR_APP_BUILTIN));
  addBuiltIn(ACTION_CLOCK, tr(STR_CLOCK_DISABLED), tr(STR_APP_BUILTIN));
  addBuiltIn(ACTION_READING_STATS, tr(STR_READING_STATS), tr(STR_APP_BUILTIN));
  addBuiltIn(ACTION_SHELVES, tr(STR_SHELVES), tr(STR_APP_BUILTIN));
  addBuiltIn(ACTION_READING_CALENDAR, tr(STR_READING_CALENDAR), tr(STR_APP_BUILTIN));
  if (BookmarkStore::hasAnyBookmarks()) addBuiltIn(ACTION_BOOKMARKS, tr(STR_BOOKMARKS), tr(STR_APP_BUILTIN));
  if (OPDS_STORE.hasServers()) addBuiltIn(ACTION_OPDS, tr(STR_OPDS_BROWSER), tr(STR_APP_BUILTIN));
  if (SETTINGS.lockscreenApp[0]) addBuiltIn(ACTION_RESET_LOCKSCREEN, tr(STR_RESET_LOCKSCREEN_APP), SETTINGS.lockscreenApp);
  addBuiltIn(ACTION_FILE_TRANSFER, tr(STR_FILE_TRANSFER), tr(STR_APP_BUILTIN));
  if (!lcploans::list().empty()) addBuiltIn(ACTION_LCP_LOANS, tr(STR_LCP_LOANS), tr(STR_APP_BUILTIN));

  {
    std::vector<XtEntry> xt;
    scanXtApps(xt);
    std::sort(xt.begin(), xt.end(),
              [](const XtEntry& a, const XtEntry& b) { return strcasecmp(a.title.c_str(), b.title.c_str()) < 0; });
    for (auto& e : xt) {
      Item item;
      item.builtIn = false;
      item.isXtApp = true;
      item.title = std::move(e.title);
      item.description = std::move(e.description);
      item.xtDir = std::move(e.dir);
      item.xtError = std::move(e.error);
      items_.push_back(std::move(item));
    }
  }

  if (SETTINGS.pluginsEnabled == 0) return;  // enabling stays in Settings > System > Plugins

  std::vector<Item> plugins;
  for (auto& entry : PluginLocations::scanPlugins()) {
    Item item;
    item.builtIn = false;
    item.title = entry.name;
    ManifestInfo manifest;
    if (entry.hasManifest && readManifest(entry.dir + "/manifest.json", manifest)) {
      if (!manifest.title.empty()) item.title = manifest.title;
      item.description = manifest.description;
    }
    ManifestInfo device;
    if (entry.hasDevice && readManifest(entry.dir + "/device.json", device)) {
      if (!device.title.empty()) item.title = device.title;
      if (!device.description.empty()) item.description = device.description;
    }
    item.hasCatalog = PluginLocations::classifyDeviceManifest(device.hasBrowseUrl, device.hasEvents) ==
                      PluginLocations::DeviceKind::Catalog;
    item.hasReadme = Storage.exists((entry.dir + "/README.md").c_str());
    if (item.description.empty()) item.description = entry.hasDevice ? tr(STR_APP_PLUGIN) : tr(STR_PLUGIN_WEB_ONLY);
    if (entry.hasDevice) {
      const std::string events = pluginEventSummary(entry.dir);
      if (!events.empty()) item.description += " · " + events;
    }
    item.entry = std::move(entry);
    plugins.push_back(std::move(item));
  }
  std::sort(plugins.begin(), plugins.end(),
            [](const Item& a, const Item& b) { return strcasecmp(a.title.c_str(), b.title.c_str()) < 0; });
  for (auto& p : plugins) items_.push_back(std::move(p));
}

void AppsActivity::onEnter() {
  Activity::onEnter();
  loadItems();  // rescanned on every entry; nothing stays resident
  selected_ = 0;
  requestUpdate();
}

void AppsActivity::onExit() {
  Activity::onExit();
  items_.clear();
  items_.shrink_to_fit();
}

void AppsActivity::openSelected() {
  if (selected_ < 0 || selected_ >= static_cast<int>(items_.size())) return;
  const Item& item = items_[selected_];
  if (item.builtIn && item.action == ACTION_LCP_LOANS) {
    // Opened right here (no Home state involved); the list is rebuilt when the screen closes.
    startActivityForResult(std::make_unique<LcpLoansActivity>(renderer, mappedInput), [this](const ActivityResult&) {
      loadItems();
      selected_ = std::min(selected_, std::max(0, static_cast<int>(items_.size()) - 1));
      requestUpdate();
    });
    return;
  }
  if (item.builtIn && item.action == ACTION_RESET_LOCKSCREEN) {
    SETTINGS.lockscreenApp[0] = '\0';
    SETTINGS.saveToFile();
    loadItems();
    if (selected_ >= items_.size() && !items_.empty()) selected_ = items_.size() - 1;
    requestUpdate();
    return;
  }
  if (item.builtIn && item.action == ACTION_SHELVES) {
    startActivityForResult(std::make_unique<ShelvesActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) { requestUpdate(); });
    return;
  }
  if (item.builtIn && item.action == ACTION_READING_CALENDAR) {
    startActivityForResult(std::make_unique<ReadingCalendarActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) { requestUpdate(); });
    return;
  }
  if (item.builtIn && item.action == ACTION_CLOCK) {
    startActivityForResult(std::make_unique<ClockActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) { requestUpdate(); });
    return;
  }
  if (item.isXtApp) {
    startActivityForResult(std::make_unique<XtAppActivity>(renderer, mappedInput, item.xtDir, item.xtError),
                           [this](const ActivityResult&) { requestUpdate(); });
    return;
  }
  if (item.builtIn) {
    MenuResult result;
    result.action = item.action;
    setResult(std::move(result));
    finish();
    return;
  }
  const auto action = PluginLocations::pickerAction(
      item.hasCatalog ? PluginLocations::DeviceKind::Catalog : PluginLocations::DeviceKind::None, item.hasReadme);
  if (action == PluginLocations::PickerAction::Catalog) {
    startActivityForResult(std::make_unique<PluginCatalogActivity>(renderer, mappedInput, item.title, item.entry.dir),
                           [this](const ActivityResult&) { requestUpdate(); });
  } else if (action == PluginLocations::PickerAction::Readme) {
    startActivityForResult(
        std::make_unique<PluginReadmeActivity>(renderer, mappedInput, item.title, item.entry.dir + "/README.md"),
        [this](const ActivityResult&) { requestUpdate(); });
  }
}

void AppsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult cancelled;
    cancelled.isCancelled = true;
    setResult(std::move(cancelled));
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelected();
    return;
  }

  const int listSize = static_cast<int>(items_.size());
  if (listSize == 0) return;

  {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight =
        renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
    auto touch = TouchListNavigation::handle(mappedInput, selected_, listSize,
                                              Rect{0, contentTop, renderer.getScreenWidth(), contentHeight},
                                              metrics.listWithSubtitleRowHeight);
    if (touch.handled) {
      requestUpdate();
      if (touch.activate) openSelected();
      return;
    }
  }

  buttonNavigator_.onNextRelease([this, listSize] {
    selected_ = ButtonNavigator::nextIndex(selected_, listSize);
    requestUpdate();
  });
  buttonNavigator_.onPreviousRelease([this, listSize] {
    selected_ = ButtonNavigator::previousIndex(selected_, listSize);
    requestUpdate();
  });
}

void AppsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_APPS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawList(
      renderer, Rect{0, contentTop, width, contentHeight}, static_cast<int>(items_.size()), selected_,
      [this](int index) { return items_[index].title; }, [this](int index) { return items_[index].description; },
      nullptr, nullptr, true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
