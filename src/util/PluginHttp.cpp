#include "PluginHttp.h"

#include <HalStorage.h>
#include <Logging.h>
#include <TlsTrust.h>

#include <algorithm>
#include <climits>
#include <cstdlib>

PluginHttp::PluginHttp() {
  TlsTrust::apply(http_);
  http_.setTimeout(20000);
  http_.setFollowRedirects(4);
  http_.setUserAgent("InkMOD-Plugins/1");
  http_.setReuse(true);
}

void PluginHttp::setBasicAuth(const std::string& user, const std::string& pass) {
  user_ = user;
  pass_ = pass;
}

std::string PluginHttp::takeRejectedHost() { return TlsTrust::takeRejectedHost(); }

PluginHttp::Result PluginHttp::run(const plugincat::Request& req, const freeink::SecureHttpClient::DataCallback& sink,
                                   const Cancel& cancel, const Progress& progress) {
  Result result;
  if (!http_.begin(req.url)) return result;
  for (const auto& header : req.headers) http_.addHeader(header.first, header.second);
  if (!user_.empty()) {
    http_.setBasicAuth(user_, pass_);
  } else {
    http_.clearBasicAuth();
  }
  if (progress) {
    http_.setProgressCallback([&progress](size_t done, size_t total) {
      progress(done, total);
      return true;
    });
  } else {
    http_.setProgressCallback(nullptr);
  }

  const bool hasBody = !req.body.empty();
  const std::string method = req.method.empty() ? "GET" : req.method;
  result.status = http_.sendRequest(method.c_str(), reinterpret_cast<const uint8_t*>(req.body.data()),
                                    hasBody ? req.body.size() : 0, sink, cancel);
  result.complete = http_.responseComplete();
  result.cancelled = http_.aborted() || (cancel && cancel());
  http_.setProgressCallback(nullptr);
  return result;
}

PluginHttp::Result PluginHttp::fetchString(const plugincat::Request& req, std::string& out, size_t maxBytes,
                                           const Cancel& cancel) {
  out.clear();
  bool tooLarge = false;
  auto result = run(
      req,
      [&](const uint8_t* data, size_t len) {
        if (out.size() + len > maxBytes) {
          tooLarge = true;
          return false;
        }
        out.append(reinterpret_cast<const char*>(data), len);
        return true;
      },
      cancel, nullptr);
  result.tooLarge = tooLarge;
  if (tooLarge) result.complete = false;
  return result;
}

int PluginHttp::fetchStream(const plugincat::Request& req, const StreamStart& onStart, const StreamData& onData,
                            bool& complete, const Cancel& cancel) {
  complete = false;
  bool started = false;
  bool stopped = false;
  const auto start = [&] {
    // A chunked body's Content-Length, if any, is not its size.
    const long length = http_.hasContentLength() && http_.getHeader("transfer-encoding").empty()
                            ? static_cast<long>(std::min<size_t>(http_.getContentLength(), LONG_MAX))
                            : -1;
    started = onStart(http_.getStatus(), http_.getHeaders(), length);
    return started;
  };
  const auto result = run(
      req,
      [&](const uint8_t* data, size_t len) {
        if ((!started && !start()) || !onData(data, len)) {
          stopped = true;
          return false;
        }
        return true;
      },
      cancel, nullptr);
  complete = result.status >= 0 && !stopped && result.complete;
  // An empty body never calls onData, so start here.
  if (complete && !started && !start()) complete = false;
  if (!complete) LOG_ERR("PLG", "Streamed request failed: status=%d started=%d stopped=%d", result.status, started, stopped);
  return started ? result.status : -1;
}

PluginHttp::Result PluginHttp::fetchToFile(const plugincat::Request& req, const std::string& path, const Cancel& cancel,
                                           const Progress& progress, size_t maxBytes) {
  Storage.remove(path.c_str());
  FsFile file;
  if (!Storage.openFileForWrite("PLG", path, file)) {
    LOG_ERR("PLG", "Cannot create %s", path.c_str());
    Result failed;
    failed.writeFailed = true;
    return failed;
  }
  bool writeFailed = false;
  bool tooLarge = false;
  size_t total = 0;
  auto result = run(
      req,
      [&](const uint8_t* data, size_t len) {
        total += len;
        if (maxBytes > 0 && total > maxBytes) {
          tooLarge = true;
          return false;
        }
        if (file.write(data, len) != len) {
          writeFailed = true;
          return false;
        }
        return true;
      },
      cancel, progress);
  file.close();
  result.writeFailed = writeFailed;
  result.tooLarge = tooLarge;
  if (tooLarge) result.complete = false;
  if (!result.ok()) Storage.remove(path.c_str());
  return result;
}

PluginHttp::Segment PluginHttp::appendToFile(plugincat::Request req, const std::string& path, size_t offset,
                                             size_t maxBytes) {
  Segment seg;
  if (offset > 0) req.headers.emplace_back("Range", "bytes=" + std::to_string(offset) + "-");
  HalFile file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND);
  if (!file) {
    seg.result.writeFailed = true;
    return seg;
  }
  bool writeFailed = false;
  size_t written = 0;
  bool checkedStatus = false;
  seg.result = run(
      req,
      [&](const uint8_t* data, size_t len) {
        if (!checkedStatus) {
          checkedStatus = true;
          if (offset > 0 && http_.getStatus() == 200) {
            // Range ignored: this response is the whole body again. Rewrite the .part from it and finish it here,
            // since another segment would only restart again.
            file.close();
            file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
            if (!file) {
              writeFailed = true;
              return false;
            }
            seg.restarted = true;
            maxBytes = 0;
          }
        }
        size_t take = len;
        if (maxBytes > 0 && written + take > maxBytes) {
          take = maxBytes - written;
          seg.segmentFull = true;
        }
        if (take > 0 && file.write(data, take) != take) {
          writeFailed = true;
          return false;
        }
        written += take;
        return !seg.segmentFull;
      },
      nullptr, nullptr);
  file.close();
  seg.bytes = written;
  seg.result.writeFailed = writeFailed;
  if (seg.segmentFull) seg.result.complete = false;

  const int status = seg.result.status;
  if (offset > 0 && status == 200 && !seg.restarted) seg.rangeUnsupported = true;
  if (status == 206) {
    for (const auto& h : http_.getHeaders()) {
      if (h.first != "content-range") continue;
      const size_t slash = h.second.rfind('/');
      if (slash != std::string::npos) seg.total = strtoul(h.second.c_str() + slash + 1, nullptr, 10);
    }
  } else if (status == 200 && http_.hasContentLength()) {
    seg.total = http_.getContentLength();
  }
  return seg;
}
