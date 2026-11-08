#pragma once

#include <string>

// "Events: reader.open, sleep.enter" for a plugin folder whose device.json subscribes to events; "" when it
// has none. Shown under the plugin in the Plugins and Apps lists so the user sees what a plugin receives.
std::string pluginEventSummary(const std::string& pluginDir);
