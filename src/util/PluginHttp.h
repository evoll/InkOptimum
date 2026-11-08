#pragma once

#include <PluginManifest.h>
#include <SecureHttpClient.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// HTTP(S) transport for on-device plugin catalogs: one kept-alive session, trust mode from Settings
// (TlsTrust), requests described by plugincat::Request, bodies streamed into RAM (bounded) or to a file.
class PluginHttp {
 public:
  using Cancel = std::function<bool()>;
  using Progress = std::function<void(size_t done, size_t total)>;

  struct Result {
    int status = -1;       // HTTP status, -1 on transport failure
    bool complete = false;  // whole body delivered
    bool cancelled = false;
    bool tooLarge = false;
    bool writeFailed = false;
    bool ok() const { return status >= 200 && status < 300 && complete && !tooLarge && !writeFailed; }
  };

  PluginHttp();

  void setFollowRedirects(int hops) { http_.setFollowRedirects(hops); }

  // Headers of the last response, in receive order, duplicates kept.
  std::vector<std::pair<std::string, std::string>> responseHeaders() const { return http_.getHeaders(); }

  // Basic credentials for the following requests ("" user: none).
  void setBasicAuth(const std::string& user, const std::string& pass);

  // Whole response into `out` (at most maxBytes; more sets tooLarge).
  Result fetchString(const plugincat::Request& req, std::string& out, size_t maxBytes, const Cancel& cancel = nullptr);

  // The response forwarded as it arrives, nothing buffered. onStart gets the status, the headers and the body
  // length (-1 when unknown or chunked) once, before the first body bytes; onData gets each piece. Either
  // returning false stops the transfer. Returns the status, or -1 when the request failed or onStart refused
  // it. `complete` is false when the body was cut short or a callback stopped it.
  using StreamStart = std::function<bool(int status, const std::vector<std::pair<std::string, std::string>>& headers,
                                         long contentLength)>;
  using StreamData = std::function<bool(const uint8_t* data, size_t len)>;
  int fetchStream(const plugincat::Request& req, const StreamStart& onStart, const StreamData& onData, bool& complete,
                  const Cancel& cancel = nullptr);

  // Response into a file on the card (replaced). A failed or cancelled transfer removes the partial file.
  // maxBytes > 0 stops a longer body (tooLarge).
  Result fetchToFile(const plugincat::Request& req, const std::string& path, const Cancel& cancel = nullptr,
                     const Progress& progress = nullptr, size_t maxBytes = 0);

  // One segment of a resumable download: appends the body to `path` (kept on failure; the caller owns the
  // .part file). Stops after maxBytes (> 0) with `segmentFull` set. `total` is the full size when the server
  // reported it (Content-Length, or Content-Range on a 206), else 0. Sends "Range: bytes=offset-" when
  // offset > 0 and needs a 206 then (`rangeUnsupported` otherwise).
  struct Segment {
    Result result;
    size_t bytes = 0;  // appended by this call
    size_t total = 0;
    bool segmentFull = false;
    bool rangeUnsupported = false;
  };
  Segment appendToFile(plugincat::Request req, const std::string& path, size_t offset, size_t maxBytes);

  // Host:port of a server refused by the certificate check since the last call ("" when none).
  static std::string takeRejectedHost();

 private:
  Result run(const plugincat::Request& req, const freeink::SecureHttpClient::DataCallback& sink, const Cancel& cancel,
             const Progress& progress);

  freeink::SecureHttpClient http_;
  std::string user_, pass_;
};
