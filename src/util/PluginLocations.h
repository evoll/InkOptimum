#pragma once
#include <cstdint>
#include <string>
#include <vector>

// SD-card plugins: a plugin is a folder under one of these roots; earlier roots win on name collisions.
// /.inkmod/plugins is the native home, /.crosspoint/plugins is read for plugins written for CrossPoint, and
// /plugins and /.plugins are friendlier for copying folders onto the card from a computer.
// Layout follows CrossPoint's SD plugin system (docs/sd-plugins.md there): manifest.json, plugin.js,
// device.json, README.md.
namespace PluginLocations {
inline constexpr const char* ROOTS[] = {"/.inkmod/plugins", "/.crosspoint/plugins", "/plugins", "/.plugins"};
inline constexpr size_t ROOT_COUNT = sizeof(ROOTS) / sizeof(ROOTS[0]);
inline constexpr const char* INSTALL_MARKER = ".installing";

inline bool isPluginRoot(const std::string& dir) {
  for (size_t i = 0; i < ROOT_COUNT; i++) {
    if (dir == ROOTS[i]) return true;
  }
  return false;
}

// A bundle targeting one direct child of a plugin root may update an existing same-named plugin in place
// instead of creating a duplicate in another root.
inline bool shouldReuseInstalledBundleDir(const std::string& destDir, const std::string& subdir) {
  return !subdir.empty() && subdir.find('/') == std::string::npos && isPluginRoot(destDir);
}

enum class DeviceKind { None, Catalog, Background };
enum class PickerAction { None, Catalog, Readme };

constexpr bool isDiscoverableEntry(const bool quarantined, const bool hasPluginJs, const bool hasDevice,
                                   const bool hasManifest) {
  return !quarantined && (hasPluginJs || hasDevice || hasManifest);
}

constexpr DeviceKind classifyDeviceManifest(const bool hasBrowseUrl, const bool hasEvents) {
  if (hasBrowseUrl) return DeviceKind::Catalog;
  if (hasEvents) return DeviceKind::Background;
  return DeviceKind::None;
}

// A catalog opens; any other plugin with a README shows it (web-only plugins included).
constexpr PickerAction pickerAction(const DeviceKind kind, const bool hasReadme) {
  if (kind == DeviceKind::Catalog) return PickerAction::Catalog;
  return hasReadme ? PickerAction::Readme : PickerAction::None;
}

// Exactly MAJOR.MINOR.PATCH: three digit-only components, nothing else, each fitting in 32 bits.
inline bool parseVersion(const std::string& v, uint32_t (&out)[3]) {
  size_t pos = 0;
  for (int k = 0; k < 3; k++) {
    if (k > 0 && (pos >= v.size() || v[pos++] != '.')) return false;
    if (pos >= v.size() || v[pos] < '0' || v[pos] > '9') return false;
    uint64_t n = 0;
    for (; pos < v.size() && v[pos] >= '0' && v[pos] <= '9'; pos++) {
      n = n * 10 + static_cast<uint64_t>(v[pos] - '0');
      if (n > UINT32_MAX) return false;
    }
    out[k] = static_cast<uint32_t>(n);
  }
  return pos == v.size();
}

// True when `catalog` is newer than `installed`. An installed copy without a valid version is offered the
// update; a malformed catalog version never is.
inline bool isNewerVersion(const std::string& catalog, const std::string& installed) {
  uint32_t c[3], i[3];
  if (!parseVersion(catalog, c)) return false;
  if (!parseVersion(installed, i)) return true;
  for (int k = 0; k < 3; k++) {
    if (c[k] != i[k]) return c[k] > i[k];
  }
  return false;
}

// One SD plugin folder, classified by the marker files it carries.
struct Entry {
  std::string name;          // folder name
  std::string dir;           // "<root>/<name>"
  bool hasPluginJs = false;  // browser-side plugin (plugin.js)
  bool hasDevice = false;    // on-device catalog manifest (device.json)
  bool hasManifest = false;  // web UI card metadata (manifest.json)
};

// Installed plugins, only while the global Plugins setting is on. Root priority, direct child folders only.
std::vector<Entry> scanPlugins();

// Raw probes that ignore the setting and installation quarantine (migration, interrupted installs).
std::vector<Entry> scanPluginsOnDisk();
bool anyPluginInstalledOnDisk();
std::string findPluginDirOnDisk(const char* name);
bool isPluginQuarantined(const std::string& dir);

// "<root>/<name>" of the named plugin, or "" when absent, disabled or quarantined.
std::string findPluginDir(const char* name);
}  // namespace PluginLocations
