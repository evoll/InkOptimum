#include "PluginEventSummary.h"

#include <HalStorage.h>
#include <I18n.h>
#include <PluginEvents.h>

#include <algorithm>

std::string pluginEventSummary(const std::string& pluginDir) {
  const std::string path = pluginDir + "/device.json";
  if (!Storage.exists(path.c_str())) return "";
  const String text = Storage.readFile(path.c_str());
  if (text.length() == 0 || text.length() > 8 * 1024) return "";

  std::string names;
  std::vector<std::string> seen;
  for (const auto& handler : pluginevents::parseHandlers(std::string(text.c_str(), text.length()))) {
    if (std::find(seen.begin(), seen.end(), handler.event) != seen.end()) continue;
    seen.push_back(handler.event);
    if (!names.empty()) names += ", ";
    names += handler.event;
  }
  return names.empty() ? "" : std::string(tr(STR_PLUGIN_EVENTS)) + names;
}
