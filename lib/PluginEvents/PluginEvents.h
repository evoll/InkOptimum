#pragma once

#include <PluginManifest.h>
#include <PluginTemplate.h>

#include <functional>
#include <string>
#include <vector>

// Plugin events (CrossPoint-compatible): the firmware appends small event lines to a per-plugin outbox on the
// card and later replays them through the request/download templates declared in device.json "events".
// This library is the pure part: whitelist, handler parsing, line format, outbox rules and the drain loop.
namespace pluginevents {

inline constexpr size_t MAX_LINE = 512;
inline constexpr size_t MAX_OUTBOX = 4096;
inline constexpr size_t MAX_PLUGIN_NAME = 23;
inline constexpr size_t MAX_PLUGIN_PATH = 63;
inline constexpr int MAX_SUBSCRIBED = 8;
inline constexpr int MAX_HANDLERS_PER_DRAIN = 4;
inline constexpr size_t MAX_ACK_BODY = 8 * 1024;
inline constexpr size_t MAX_DOWNLOAD = 1024 * 1024;

bool isKnownEvent(const std::string& name);

struct Handler {
  std::string event;
  bool isDownload = false;
  plugincat::Request request;  // method defaults to POST for "request", GET for "download"
  std::string dest;            // download target template
  std::string toast;
  bool connect = false;  // may bring Wi-Fi up at sleep entry
  std::string requireMeta;  // queue the event only for books whose sidecar has this key ("" = every book)
};

// Reads device.json "events". Unknown event names and malformed handlers are skipped. `dest` must resolve
// to an absolute plugin-safe path at delivery time, which the caller checks after substitution.
std::vector<Handler> parseHandlers(const std::string& deviceJson);

// require_meta: true when the sidecar JSON holds `key` as a non-empty string or a whole number (decimals,
// booleans, null and nested values do not match).
bool sidecarHasKey(const std::string& sidecarJson, const std::string& key);

struct Event {
  std::string id;  // unique, stable across retries
  std::string name;
  long ts = 0;
  plugincat::Vars vars;  // event variables (book, percent, ...)
};

// One outbox line (no trailing newline); "" when it would exceed MAX_LINE.
std::string encodeLine(const Event& event);
bool decodeLine(const std::string& line, Event& event);

// Appends `line` to an outbox. When the outbox would pass MAX_OUTBOX it is dropped wholesale first.
void appendToOutbox(std::string& outbox, const std::string& line);

// Template variables of an event: event.NAME, event.ts, event.id (plus `extra`, e.g. token, cfg.*, meta.*).
plugincat::Vars eventVars(const Event& event, const plugincat::Vars& extra);

// Outcome of one delivery attempt.
enum class Delivery { Delivered, Failed };
using Deliver = std::function<Delivery(const Handler&, const Event&)>;

// Replays an outbox in order. A line that is corrupt or whose event has no handler counts as delivered and is
// removed. Stops at the first failed delivery (nothing later overtakes it) or after `maxHandlers` deliveries.
// `outbox` is rewritten to the lines that remain. Returns the number of handlers run.
int drain(std::string& outbox, const std::vector<Handler>& handlers, const Deliver& deliver,
          int maxHandlers = MAX_HANDLERS_PER_DRAIN);

}  // namespace pluginevents
