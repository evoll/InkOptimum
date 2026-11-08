#include "PluginsActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <memory>

#include "MappedInputManager.h"
#include "PluginCatalogActivity.h"
#include "PluginReadmeActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/PluginEventSummary.h"
#include "util/TouchListNavigation.h"

namespace {
constexpr size_t MAX_MANIFEST_BYTES = 8 * 1024;

// Reads title/description (and browse.url presence for device.json) from one manifest. Oversized or
// malformed files are ignored, so one bad plugin cannot hide the others.
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
}  // namespace

void PluginsActivity::loadItems() {
  items_.clear();
  for (auto& entry : PluginLocations::scanPlugins()) {
    Item item;
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
    if (item.description.empty() && !entry.hasDevice) item.description = tr(STR_PLUGIN_WEB_ONLY);
    if (entry.hasDevice) {
      const std::string events = pluginEventSummary(entry.dir);
      if (!events.empty()) item.description += (item.description.empty() ? "" : " · ") + events;
    }
    item.entry = std::move(entry);
    items_.push_back(std::move(item));
  }
  std::sort(items_.begin(), items_.end(),
            [](const Item& a, const Item& b) { return strcasecmp(a.title.c_str(), b.title.c_str()) < 0; });
}

void PluginsActivity::onEnter() {
  Activity::onEnter();
  loadItems();  // rescanned on every entry; nothing stays resident
  selected_ = 0;
  requestUpdate();
}

void PluginsActivity::onExit() {
  Activity::onExit();
  items_.clear();
  items_.shrink_to_fit();
}

void PluginsActivity::openSelected() {
  if (selected_ < 0 || selected_ >= static_cast<int>(items_.size())) return;
  const Item& item = items_[selected_];
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

void PluginsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    notice_.clear();
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

void PluginsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_PLUGINS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (items_.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_PLUGINS_NONE));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, width, contentHeight}, static_cast<int>(items_.size()), selected_,
        [this](int index) { return items_[index].title; },
        [this](int index) { return items_[index].description; }, nullptr, nullptr, true);
  }

  if (!notice_.empty()) {
    const int noticeY = renderer.getScreenHeight() - metrics.buttonHintsHeight - renderer.getLineHeight(SMALL_FONT_ID) - 8;
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, noticeY,
                      renderer.truncatedText(SMALL_FONT_ID, notice_.c_str(), width - 2 * metrics.contentSidePadding).c_str());
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
