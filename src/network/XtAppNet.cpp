#include "XtAppNet.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>

#include <algorithm>

#include "AppVersion.h"
#include "network/WifiPowerSaveGuard.h"

namespace xtapp_net {
namespace {
constexpr size_t kMaxMemBody = 512 * 1024;
constexpr size_t kMaxFileBody = 4 * 1024 * 1024;

struct Ctx {
  xtapp::NetResponse* out;
  const std::atomic<bool>* cancel;
  HalFile file;
  bool toFile = false;
  size_t received = 0;
  bool tooLarge = false;
  bool ioError = false;
};

esp_err_t onEvent(esp_http_client_event_t* e) {
  auto* c = static_cast<Ctx*>(e->user_data);
  switch (e->event_id) {
    case HTTP_EVENT_ON_HEADER: {
      std::string k = e->header_key ? e->header_key : "";
      for (auto& ch : k) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
      if (c->out->headers.size() < 48) c->out->headers.emplace_back(std::move(k), e->header_value ? e->header_value : "");
      break;
    }
    case HTTP_EVENT_ON_DATA: {
      if (c->cancel->load()) return ESP_FAIL;
      if (esp_http_client_get_status_code(e->client) >= 300 && esp_http_client_get_status_code(e->client) < 400) break;  // redirect body
      c->received += static_cast<size_t>(e->data_len);
      if (c->toFile) {
        if (c->received > kMaxFileBody) {
          c->tooLarge = true;
          return ESP_FAIL;
        }
        if (c->file.write(static_cast<const uint8_t*>(e->data), static_cast<size_t>(e->data_len)) !=
            static_cast<size_t>(e->data_len)) {
          c->ioError = true;
          return ESP_FAIL;
        }
      } else {
        if (c->received > kMaxMemBody) {
          c->tooLarge = true;
          return ESP_FAIL;
        }
        c->out->body.append(static_cast<const char*>(e->data), static_cast<size_t>(e->data_len));
      }
      break;
    }
    case HTTP_EVENT_ON_CONNECTED:
      // A redirect restarts the response: drop headers and body of the previous hop.
      c->out->headers.clear();
      if (!c->toFile) c->out->body.clear();
      c->received = 0;
      break;
    default: break;
  }
  return ESP_OK;
}
}  // namespace

bool request(const xtapp::NetRequest& req, const std::string& savePath, xtapp::NetResponse& out,
             const std::atomic<bool>& cancel) {
  WifiPowerSaveGuard guard;
  (void)guard;
  out = {};
  Ctx ctx;
  ctx.out = &out;
  ctx.cancel = &cancel;
  if (!req.saveName.empty()) {
    const std::string tmp = savePath + ".tmp";
    if (!Storage.openFileForWrite("XTNET", tmp, ctx.file)) {
      out.error = "io_error";
      return false;
    }
    ctx.toFile = true;
  }

  esp_http_client_config_t cfg = {};
  cfg.url = req.url.c_str();
  cfg.event_handler = onEvent;
  cfg.user_data = &ctx;
  cfg.timeout_ms = static_cast<int>(req.timeoutMs);
  cfg.buffer_size = 4096;
  cfg.buffer_size_tx = 2048;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.keep_alive_enable = false;
  cfg.max_redirection_count = 5;
  const std::string& m = req.method;
  cfg.method = m == "POST" ? HTTP_METHOD_POST : m == "PUT" ? HTTP_METHOD_PUT : m == "DELETE" ? HTTP_METHOD_DELETE
             : m == "PATCH" ? HTTP_METHOD_PATCH : m == "HEAD" ? HTTP_METHOD_HEAD : HTTP_METHOD_GET;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    if (ctx.toFile) ctx.file.close();
    out.error = "out_of_memory";
    return false;
  }
  esp_http_client_set_header(client, "User-Agent", "InkOptimum-XtApp");
  esp_http_client_set_header(client, "Connection", "close");
  for (const auto& h : req.headers) esp_http_client_set_header(client, h.first.c_str(), h.second.c_str());
  if (!req.body.empty()) esp_http_client_set_post_field(client, req.body.data(), static_cast<int>(req.body.size()));

  const esp_err_t err = esp_http_client_perform(client);
  out.status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  if (ctx.toFile) {
    ctx.file.flush();
    ctx.file.close();
  }
  bool ok = err == ESP_OK;
  if (cancel.load()) {
    out.error = "cancelled";
    ok = false;
  } else if (ctx.tooLarge) {
    out.error = "too_large";
    ok = false;
  } else if (ctx.ioError) {
    out.error = "io_error";
    ok = false;
  } else if (err != ESP_OK) {
    out.error = err == ESP_ERR_HTTP_EAGAIN || err == ESP_ERR_TIMEOUT ? "timeout" : "connect";
    if (out.status <= 0) out.status = 0;
  }
  if (ctx.toFile) {
    const std::string tmp = savePath + ".tmp";
    if (ok && out.status >= 200 && out.status < 300) {
      Storage.remove(savePath.c_str());
      if (Storage.rename(tmp.c_str(), savePath.c_str())) out.saved = true;
      else out.error = "io_error";
    } else {
      Storage.remove(tmp.c_str());
    }
  }
  return ok;
}
}  // namespace xtapp_net
