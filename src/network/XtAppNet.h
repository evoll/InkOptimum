#pragma once

#include <XtApp.h>

#include <atomic>
#include <string>

// One blocking HTTP(S) request for a Lua mini app (ctx.net:request). Runs on a worker task, never on the UI task.
// Response bodies are capped at 512 KiB in memory; with req.saveName the body streams to savePath (cap 4 MiB).
// `cancel` is polled while data flows. Redirects are followed by esp_http_client; TLS uses the certificate bundle.
namespace xtapp_net {
bool request(const xtapp::NetRequest& req, const std::string& savePath, xtapp::NetResponse& out,
             const std::atomic<bool>& cancel);
}
