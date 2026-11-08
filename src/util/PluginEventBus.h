#pragma once

#include <PluginManifest.h>
#include <PluginTemplate.h>

#include <cstdint>
#include <string>

// Firmware side of plugin events (lib/PluginEvents holds the rules): emit() appends a line to the outbox of
// every plugin that subscribed in its device.json, drain() replays outboxes through the declared requests when
// the device is online. Everything is a no-op while Settings > Plugins is off.
namespace PluginEventBus {

// Reads the subscriptions of the installed plugins (at most 8, name <= 23 bytes, path <= 63 bytes).
// Rebuilt at boot only: a plugin installed later starts receiving events after the next restart.
void init();

// `name` must be on the whitelist. Cheap when nobody subscribed: nothing touches the card.
void emit(const char* name, const plugincat::Vars& vars);

// Delivers queued events through the declared handlers if Wi-Fi is up (at most 4 handlers per plugin and call).
void drainIfOnline();

// Handlers' `toast` templates are shown through this sink (a short popup) after a request succeeded.
using ToastSink = void (*)(const char* text);
void setToastSink(ToastSink sink);

// Sleep entry only: like drainIfOnline(), but when Wi-Fi is down and a queued event's handler declares
// `connect: true`, brings Wi-Fi up (battery >= 20 %, saved credential for the last network, popup shown,
// 10 s deadline) for that delivery and turns it off again. It never waits longer than the deadline.
void drainAtSleep(uint16_t batteryPercent, const char* connectingText);

}  // namespace PluginEventBus
