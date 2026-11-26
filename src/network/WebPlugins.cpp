// Web plugins: the device side of browser-hosted plugin.js files (CrossPoint-compatible contract).
// Kept apart from InkMODWebServer.cpp, which is already very large.
//   GET  /plugins, /plugins-run      page that hosts plugin.js (interactive / headless runner)
//   GET  /api/plugins                installed plugins
//   GET  /plugin?name&file           one file of a plugin folder
//   POST /api/relay                  outbound HTTP(S) for the page (no CORS), body capped at 32 KB
//   POST /api/fetch                  download a URL straight to the card
//   POST /api/plugin-fs              write one small file (multipart), <=256 KB
//   /api/plugin-jobs...              job queue for external automation
//   POST /api/crypto                 stateless wolfSSL primitives (hash, random, AES-CBC, RSA, PKCS#12)
//   POST /api/book-key               store a protected book's content key, wrapped to this device
// Everything answers 403 while Settings > Plugins is off.
#include <ArduinoJson.h>
#include <BookKey.h>
#include <HalStorage.h>
#include <LcpUnlock.h>
#include <Logging.h>
#include <Memory.h>
#include <PluginJobs.h>
#include <ProtectedPaths.h>
#include <Util.h>
#include <WolfsslCrypto.h>
#include <ZipFile.h>
#include <base64.h>
#include <wolfssl/wolfcrypt/aes.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "InkMODSettings.h"
#include "LcpLoans.h"
#include "InkMODWebServer.h"
#include "html/PluginsPageHtml.generated.h"
#include "util/BookCacheUtils.h"
#include "util/PluginHttp.h"
#include "util/PluginLocations.h"

namespace {

// The relay forwards the body as it arrives and never holds it, so the cap only keeps big files on /api/fetch.
constexpr size_t RELAY_MAX_BODY = 64 * 1024;
constexpr size_t FETCH_MAX_SEGMENT = 4 * 1024 * 1024;
constexpr size_t PLUGIN_FS_MAX = 256 * 1024;
constexpr uint32_t MIN_HEAP_FOR_TLS = 45 * 1024;

pluginjobs::Queue jobQueue;

void sendJson(WebServer* server, int code, const JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  server->sendHeader("Cache-Control", "no-store");
  server->send(code, "application/json", out);
}

void sendError(WebServer* server, int code, const char* message) {
  JsonDocument doc;
  doc["error"] = message;
  sendJson(server, code, doc);
}

bool pluginsAllowed(WebServer* server) {
  if (SETTINGS.pluginsEnabled) return true;
  sendError(server, 403, "plugins disabled");
  return false;
}

bool singleComponent(const std::string& s) {
  return !s.empty() && s.size() <= 96 && s.find('/') == std::string::npos && s.find('\\') == std::string::npos &&
         s.find("..") == std::string::npos && s[0] != '.';
}

bool isBookFile(const std::string& path) {
  const size_t dot = path.rfind('.');
  if (dot == std::string::npos) return false;
  std::string ext = path.substr(dot);
  for (auto& c : ext) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return ext == ".epub" || ext == ".txt" || ext == ".md" || ext == ".xtc" || ext == ".fb2" || ext == ".fb3";
}

void ensureParentDirs(const std::string& path) {
  for (size_t i = 1; i < path.size(); i++) {
    if (path[i] == '/') Storage.mkdir(path.substr(0, i).c_str(), true);
  }
}

const char* contentTypeFor(const std::string& name) {
  const size_t dot = name.rfind('.');
  const std::string ext = dot == std::string::npos ? "" : name.substr(dot);
  if (ext == ".js") return "application/javascript";
  if (ext == ".json") return "application/json";
  if (ext == ".css") return "text/css";
  if (ext == ".html") return "text/html";
  if (ext == ".md" || ext == ".txt") return "text/plain; charset=utf-8";
  if (ext == ".png") return "image/png";
  if (ext == ".svg") return "image/svg+xml";
  return "application/octet-stream";
}

bool readRequestHeaders(JsonVariantConst node, plugincat::Headers& out) {
  if (node.isNull()) return true;
  if (!node.is<JsonObjectConst>()) return false;
  for (JsonPairConst kv : node.as<JsonObjectConst>()) {
    if (!kv.value().is<const char*>()) return false;
    out.emplace_back(kv.key().c_str(), kv.value().as<const char*>());
  }
  return true;
}

}  // namespace

void InkMODWebServer::handlePluginsPage() const {
  if (!pluginsAllowed(server.get())) return;
  server->sendHeader("Content-Encoding", "gzip");
  server->sendHeader("Cache-Control", "no-cache");
  server->send_P(200, "text/html", PluginsPageHtml, sizeof(PluginsPageHtml));
}

void InkMODWebServer::handleGetPlugins() const {
  if (!pluginsAllowed(server.get())) return;
  JsonDocument doc;
  JsonArray list = doc.to<JsonArray>();
  for (const auto& entry : PluginLocations::scanPlugins()) {
    JsonObject o = list.add<JsonObject>();
    o["name"] = entry.name;
    o["dir"] = entry.dir;
    o["hasPluginJs"] = entry.hasPluginJs;
    o["hasDevice"] = entry.hasDevice;
    o["title"] = entry.name;
    o["mount"] = "plugins";
    if (entry.hasManifest) {
      const String text = Storage.readFile((entry.dir + "/manifest.json").c_str());
      JsonDocument manifest;
      if (deserializeJson(manifest, text) == DeserializationError::Ok) {
        if (manifest["title"].is<const char*>()) o["title"] = manifest["title"].as<const char*>();
        if (manifest["mount"].is<const char*>()) o["mount"] = manifest["mount"].as<const char*>();
        if (manifest["version"].is<const char*>()) o["version"] = manifest["version"].as<const char*>();
        if (manifest["description"].is<const char*>()) o["description"] = manifest["description"].as<const char*>();
      }
    }
  }
  sendJson(server.get(), 200, doc);
}

void InkMODWebServer::handlePluginFile() const {
  if (!pluginsAllowed(server.get())) return;
  const std::string name = server->arg("name").c_str();
  const std::string file = server->hasArg("file") ? server->arg("file").c_str() : "plugin.js";
  if (!singleComponent(name) || !singleComponent(file)) {
    server->send(400, "text/plain", "bad name/file");
    return;
  }
  const std::string dir = PluginLocations::findPluginDir(name.c_str());
  if (dir.empty()) {
    server->send(404, "text/plain", "no such plugin");
    return;
  }
  const std::string path = dir + "/" + file;
  if (!protectedpaths::isPluginPath(path)) {
    server->send(403, "text/plain", "denied");
    return;
  }
  HalFile f;
  if (!Storage.openFileForRead("WPL", path, f) || f.isDirectory()) {
    server->send(404, "text/plain", "not found");
    return;
  }
  server->setContentLength(f.size());
  server->sendHeader("Cache-Control", "no-cache");
  server->send(200, contentTypeFor(file), "");
  NetworkClient client = server->client();
  uint8_t buffer[1024];
  while (true) {
    const int n = f.read(buffer, sizeof(buffer));
    if (n <= 0) break;
    if (client.write(buffer, static_cast<size_t>(n)) != static_cast<size_t>(n)) break;
  }
  f.close();
}

void InkMODWebServer::handlePluginRelay() {
  if (!pluginsAllowed(server.get())) return;
  JsonDocument in;
  if (deserializeJson(in, server->arg("plain")) != DeserializationError::Ok || !in["url"].is<const char*>() ||
      !in["plugin"].is<const char*>()) {
    sendError(server.get(), 400, "missing plugin/url");
    return;
  }
  plugincat::Request req;
  req.url = in["url"].as<const char*>();
  req.method = in["method"] | "GET";
  req.body = in["body"] | "";
  if (!readRequestHeaders(in["headers"], req.headers) || req.url.compare(0, 4, "http") != 0) {
    sendError(server.get(), 400, "bad headers/url");
    return;
  }
  if (ESP.getMaxAllocHeap() < MIN_HEAP_FOR_TLS) {
    sendError(server.get(), 502, "relay failed; low memory");
    return;
  }

  PluginHttp http;
  http.setFollowRedirects(0);
  size_t sent = 0;
  bool chunked = false;
  bool complete = false;
  const int status = http.fetchStream(
      req,
      [&](const int upstreamStatus, const auto& responseHeaders, const long contentLength) {
        if (contentLength > static_cast<long>(RELAY_MAX_BODY)) {
          LOG_ERR("WPL", "relay body of %ld bytes is over the %u byte cap", contentLength,
                  static_cast<unsigned>(RELAY_MAX_BODY));
          return false;
        }
        String headerJson;
        {  // TLS is open: free the document before the header copies below
          JsonDocument headers;
          JsonArray arr = headers.to<JsonArray>();
          for (const auto& h : responseHeaders) {
            JsonArray pair = arr.add<JsonArray>();
            pair.add(h.first);
            pair.add(h.second);
          }
          serializeJson(headers, headerJson);
        }
        server->sendHeader("X-Relay-Status", String(upstreamStatus));
        server->sendHeader("X-Relay-Headers", headerJson);
        server->sendHeader("Cache-Control", "no-store");
        chunked = contentLength < 0;
        server->setContentLength(chunked ? CONTENT_LENGTH_UNKNOWN : static_cast<size_t>(contentLength));
        server->send(200, "application/octet-stream", "");
        return true;
      },
      [&](const uint8_t* data, const size_t len) {
        if (len > RELAY_MAX_BODY - sent) {
          LOG_ERR("WPL", "relay body is over the %u byte cap", static_cast<unsigned>(RELAY_MAX_BODY));
          return false;
        }
        server->sendContent(reinterpret_cast<const char*>(data), len);
        sent += len;
        return server->client().connected() != 0;
      },
      complete);
  // Nothing sent yet: a transport failure or a body over the cap (the device log names which).
  if (status < 0) {
    sendError(server.get(), 502, "relay failed; large bodies need /api/fetch");
    return;
  }
  // The 200 is already out. Closing early makes the browser's read fail instead of returning a short body.
  if (!complete) {
    server->client().stop();
    return;
  }
  if (chunked) server->sendContent("");
}

void InkMODWebServer::handlePluginFetch() {
  if (!pluginsAllowed(server.get())) return;
  JsonDocument in;
  if (deserializeJson(in, server->arg("plain")) != DeserializationError::Ok || !in["url"].is<const char*>() ||
      !in["dest"].is<const char*>()) {
    sendError(server.get(), 400, "bad url/dest");
    return;
  }
  plugincat::Request req;
  req.url = in["url"].as<const char*>();
  const std::string dest = in["dest"].as<const char*>();
  if (req.url.compare(0, 4, "http") != 0 || !protectedpaths::isPluginPath(dest) || dest.back() == '/') {
    sendError(server.get(), 400, "bad url/dest");
    return;
  }
  if (!readRequestHeaders(in["headers"], req.headers)) {
    sendError(server.get(), 400, "bad headers");
    return;
  }
  const size_t offset = in["offset"] | 0u;
  size_t maxBytes = in["maxBytes"] | 0u;
  if (maxBytes > FETCH_MAX_SEGMENT) maxBytes = FETCH_MAX_SEGMENT;
  if (ESP.getMaxAllocHeap() < MIN_HEAP_FOR_TLS) {
    sendError(server.get(), 502, "low memory");
    return;
  }
  ensureParentDirs(dest);
  const std::string part = dest + ".part";

  // The .part file is the transfer's state: a continuation must start exactly where it ends.
  size_t have = 0;
  if (Storage.exists(part.c_str())) {
    HalFile f;
    if (Storage.openFileForRead("WPL", part, f)) {
      have = f.size();
      f.close();
    }
  }
  if (offset == 0) {
    Storage.remove(part.c_str());
    have = 0;
  } else if (offset != have) {
    JsonDocument out;
    out["error"] = "offset mismatch";
    out["bytes"] = have;
    sendJson(server.get(), 409, out);
    return;
  }

  PluginHttp http;
  http.setFollowRedirects(5);
  const auto seg = http.appendToFile(req, part, offset, maxBytes);
  const size_t bytes = seg.restarted ? seg.bytes : offset + seg.bytes;
  JsonDocument out;
  out["status"] = seg.result.status;
  out["bytes"] = bytes;
  if (seg.total > 0) out["total"] = seg.total;

  const int status = seg.result.status;
  const bool statusOk = (offset == 0 && status == 200) || (offset > 0 && (status == 206 || (status == 200 && seg.restarted)));
  if (seg.result.writeFailed || seg.rangeUnsupported || !statusOk) {
    out["complete"] = false;
    out["error"] = seg.result.writeFailed ? "sd write failed"
                   : seg.rangeUnsupported  ? "range unsupported"
                   : status > 0            ? "http status"
                                           : "transport failure";
    if (!statusOk && status > 0 && !seg.rangeUnsupported) Storage.remove(part.c_str());
    sendJson(server.get(), 502, out);
    return;
  }
  if (seg.segmentFull) {  // more to come: the client sends the next segment with offset = bytes
    out["complete"] = false;
    sendJson(server.get(), 200, out);
    return;
  }
  if (!seg.result.complete) {  // cut short: keep the .part so the client can resume from `bytes`
    out["complete"] = false;
    out["error"] = "download truncated";
    sendJson(server.get(), 502, out);
    return;
  }
  Storage.remove(dest.c_str());
  if (!Storage.rename(part.c_str(), dest.c_str())) {
    Storage.remove(part.c_str());
    out["complete"] = false;
    out["error"] = "sd write failed";
    sendJson(server.get(), 502, out);
    return;
  }
  if (isBookFile(dest)) clearBookCache(dest);
  out["complete"] = true;
  sendJson(server.get(), 200, out);
}

void InkMODWebServer::handlePluginFsData() {
  if (!SETTINGS.pluginsEnabled) return;
  HTTPUpload& up = server->upload();
  switch (up.status) {
    case UPLOAD_FILE_START: {
      pluginFs = PluginFsState{};
      const std::string path = server->arg("path").c_str();
      if (!protectedpaths::isPluginPath(path) || path.back() == '/') break;
      ensureParentDirs(path);
      pluginFs.path = path;
      pluginFs.tempPath = path + ".tmp";
      Storage.remove(pluginFs.tempPath.c_str());
      pluginFs.started = Storage.openFileForWrite("WPL", pluginFs.tempPath, pluginFs.file);
      pluginFs.ok = pluginFs.started;
      break;
    }
    case UPLOAD_FILE_WRITE:
      if (!pluginFs.ok) break;
      if (pluginFs.bytes + up.currentSize > PLUGIN_FS_MAX) {
        pluginFs.tooLarge = true;
        pluginFs.ok = false;
        pluginFs.file.close();
        Storage.remove(pluginFs.tempPath.c_str());
        break;
      }
      if (pluginFs.file.write(up.buf, up.currentSize) != up.currentSize) {
        pluginFs.ok = false;
        pluginFs.file.close();
        Storage.remove(pluginFs.tempPath.c_str());
        break;
      }
      pluginFs.bytes += up.currentSize;
      break;
    case UPLOAD_FILE_END:
      if (pluginFs.started) pluginFs.file.close();
      break;
    case UPLOAD_FILE_ABORTED:
      if (pluginFs.started) pluginFs.file.close();
      if (!pluginFs.tempPath.empty()) Storage.remove(pluginFs.tempPath.c_str());
      pluginFs.ok = false;
      break;
  }
}

void InkMODWebServer::handlePluginFsDone() {
  if (!pluginsAllowed(server.get())) return;
  const std::string path = server->arg("path").c_str();
  if (!protectedpaths::isPluginPath(path)) {
    sendError(server.get(), 400, "bad path");
    return;
  }
  if (pluginFs.tooLarge) {
    sendError(server.get(), 413, "too large");
  } else if (pluginFs.path.empty() || !pluginFs.started) {
    sendError(server.get(), pluginFs.path.empty() ? 400 : 500, pluginFs.path.empty() ? "missing file part" : "cannot write");
  } else if (!pluginFs.ok) {
    sendError(server.get(), 500, "sd write failed");
  } else if (pluginFs.bytes == 0) {
    Storage.remove(pluginFs.tempPath.c_str());
    sendError(server.get(), 400, "empty body");
  } else {
    Storage.remove(pluginFs.path.c_str());
    if (!Storage.rename(pluginFs.tempPath.c_str(), pluginFs.path.c_str())) {
      Storage.remove(pluginFs.tempPath.c_str());
      sendError(server.get(), 500, "sd write failed");
    } else {
      if (isBookFile(pluginFs.path)) clearBookCache(pluginFs.path);
      JsonDocument out;
      out["ok"] = true;
      out["bytes"] = pluginFs.bytes;
      sendJson(server.get(), 200, out);
    }
  }
  pluginFs = PluginFsState{};
}

void InkMODWebServer::handlePluginJobEnqueue() {
  if (!pluginsAllowed(server.get())) return;
  JsonDocument in;
  if (deserializeJson(in, server->arg("plain")) != DeserializationError::Ok || !in["plugin"].is<const char*>() ||
      !in["action"].is<const char*>()) {
    sendError(server.get(), 400, "missing plugin/action");
    return;
  }
  std::string args = "{}";
  if (!in["args"].isNull()) {
    String text;
    serializeJson(in["args"], text);
    args = text.c_str();
  }
  const uint32_t id = jobQueue.enqueue(in["plugin"].as<const char*>(), in["action"].as<const char*>(), args, millis());
  if (id == 0) {
    sendError(server.get(), 503, "job queue full");
    return;
  }
  JsonDocument out;
  out["id"] = id;
  sendJson(server.get(), 200, out);
}

void InkMODWebServer::handlePluginJobClaim() {
  if (!pluginsAllowed(server.get())) return;
  const auto job = jobQueue.claim(server->arg("plugin").c_str(), millis());
  JsonDocument out;
  out["id"] = job.id;
  if (job.id != 0) {
    out["claim"] = job.claim;
    out["action"] = job.action;
    JsonDocument args;
    deserializeJson(args, job.args);
    out["args"] = args;
  }
  sendJson(server.get(), 200, out);
}

void InkMODWebServer::handlePluginJobComplete() {
  if (!pluginsAllowed(server.get())) return;
  JsonDocument in;
  if (deserializeJson(in, server->arg("plain")) != DeserializationError::Ok) {
    sendError(server.get(), 400, "bad json");
    return;
  }
  String result;
  serializeJson(in["result"], result);
  if (!jobQueue.complete(in["id"] | 0u, in["claim"] | 0u, in["ok"] | false, std::string(result.c_str()))) {
    sendError(server.get(), 409, "stale claim");
    return;
  }
  JsonDocument out;
  out["ok"] = true;
  sendJson(server.get(), 200, out);
}

void InkMODWebServer::handlePluginJobStatus() const {
  if (!pluginsAllowed(server.get())) return;
  const uint32_t id = static_cast<uint32_t>(server->arg("id").toInt());
  const auto st = jobQueue.status(id);
  static const char* const names[] = {"unknown", "pending", "running", "done", "error"};
  JsonDocument out;
  out["id"] = id;
  out["state"] = names[static_cast<int>(st.state)];
  if (!st.result.empty()) {
    JsonDocument result;
    if (deserializeJson(result, st.result) == DeserializationError::Ok) out["result"] = result;
  }
  sendJson(server.get(), 200, out);
}

// POST /api/crypto {op, ...base64 fields...} -> {data|public|private|key|cert, ...}
// Generic wolfSSL primitives for a plugin page; stateless, keys travel as base64.
void InkMODWebServer::handlePluginCrypto() {
  if (!pluginsAllowed(server.get())) return;
  using namespace freeink::content;
  JsonDocument req;
  if (deserializeJson(req, server->arg("plain")) != DeserializationError::Ok) {
    sendError(server.get(), 400, "bad json");
    return;
  }
  const std::string_view op = req["op"] | "";
  const auto sendOom = [this](const char* what) {
    LOG_ERR("WEB", "OOM: crypto %s", what);
    sendError(server.get(), 503, "out of memory");
  };

  struct Bytes {
    std::unique_ptr<uint8_t[]> data;
    size_t size = 0;
    const uint8_t* ptr() const {
      static constexpr uint8_t EMPTY = 0;
      return data ? data.get() : &EMPTY;
    }
  };
  // A decode that cannot allocate fails the operation instead of aborting (-fno-exceptions); the cap keeps a
  // LAN client from posting a multi-megabyte value (keys, certificates and bundles are a few KB).
  bool decodeOom = false;
  auto dec = [&](const char* field) {
    static constexpr size_t MAX_CRYPTO_FIELD = 64 * 1024;
    Bytes out;
    const char* v = req[field].as<const char*>();
    const size_t encodedLen = v ? strlen(v) : 0;
    if (encodedLen == 0 || encodedLen > MAX_CRYPTO_FIELD) return out;
    const size_t cap = encodedLen * 3 / 4 + 3;
    out.data = makeUniqueNoThrow<uint8_t[]>(cap);
    if (!out.data) decodeOom = true;
    const int32_t n = out.data ? base64Decode(v, encodedLen, out.data.get(), cap) : -1;
    out.size = n < 0 ? 0 : static_cast<size_t>(n);
    return out;
  };

  JsonDocument resp;
  bool encodeFailed = false;
  const auto setEncoded = [&](const char* name, const uint8_t* data, size_t size) {
    const uint8_t empty = 0;
    if (size == 0) data = &empty;
    const String encoded = base64::encode(data, size);
    if ((size && encoded.isEmpty()) || encoded == "-FAIL-") {
      encodeFailed = true;
      return;
    }
    resp[name] = encoded;
  };
  const auto setEncodedVector = [&](const char* name, const std::vector<uint8_t>& data) {
    setEncoded(name, data.data(), data.size());
  };

  WolfsslCrypto c;

  if (op == "random") {
    static constexpr int MAX_RANDOM_BYTES = 4096;
    const int n = std::clamp(static_cast<int>(req["len"] | 16), 0, MAX_RANDOM_BYTES);
    auto out = makeUniqueNoThrow<uint8_t[]>(n);
    if (n && !out) return sendOom("random output");
    if (n) c.randomBytes(out.get(), n);
    const uint8_t empty = 0;
    setEncoded("data", n ? out.get() : &empty, n);
  } else if (op == "sha1") {
    const Bytes d = dec("data");
    if (decodeOom) return sendOom("input");
    uint8_t h[20];
    c.sha1(d.ptr(), d.size, h);
    setEncoded("data", h, 20);
  } else if (op == "sha256") {
    const Bytes d = dec("data");
    if (decodeOom) return sendOom("input");
    uint8_t h[32];
    c.sha256(d.ptr(), d.size, h);
    setEncoded("data", h, 32);
  } else if (op == "aesenc" || op == "aesdec") {
    const Bytes k = dec("key"), iv = dec("iv"), d = dec("data");
    if (decodeOom) return sendOom("input");
    if (k.size != 16 || iv.size != 16) {
      resp["error"] = "key/iv must be 16 bytes";
    } else if (op == "aesenc") {
      const size_t outSize = ((d.size / 16) + 1) * 16;
      auto out = makeUniqueNoThrow<uint8_t[]>(outSize);
      auto aes = makeUniqueNoThrow<Aes>();
      if (!out || !aes) return sendOom("AES output");
      // PKCS#7 padding in place: the SDK helper would allocate a second copy.
      memcpy(out.get(), d.ptr(), d.size);
      memset(out.get() + d.size, static_cast<int>(outSize - d.size), outSize - d.size);
      if (wc_AesSetKey(aes.get(), k.ptr(), 16, iv.ptr(), AES_ENCRYPTION) == 0 &&
          wc_AesCbcEncrypt(aes.get(), out.get(), out.get(), outSize) == 0) {
        setEncoded("data", out.get(), outSize);
      } else {
        resp["error"] = "aesenc failed";
      }
    } else if (d.size % 16 != 0) {
      resp["error"] = "data not block-aligned";
    } else {
      auto out = makeUniqueNoThrow<uint8_t[]>(d.size);
      if (d.size && !out) return sendOom("AES output");
      uint8_t empty = 0;
      if (c.aes128CbcDecrypt(k.ptr(), iv.ptr(), d.ptr(), d.size, d.size ? out.get() : &empty)) {
        setEncoded("data", d.size ? out.get() : &empty, d.size);
      } else {
        resp["error"] = "aesdec failed";
      }
    }
  } else if (op == "rsadec") {
    // Raw private-key operation with a caller-supplied PKCS#8 key; the caller removes the padding.
    const Bytes priv = dec("private"), d = dec("data");
    if (decodeOom) return sendOom("input");
    static constexpr size_t MAX_MODULUS = 512;
    auto out = makeUniqueNoThrow<uint8_t[]>(MAX_MODULUS);
    if (!out) return sendOom("RSA output");
    const int32_t n = c.rsaPrivateRaw(priv.ptr(), priv.size, d.ptr(), d.size, out.get(), MAX_MODULUS);
    if (n > 0) {
      setEncoded("data", out.get(), static_cast<size_t>(n));
    } else {
      resp["error"] = "rsadec failed";
    }
  } else if (op == "keygen") {
    RsaKeyPairDer kp;
    if (c.rsaGenerate(&kp)) {
      setEncodedVector("public", kp.spki);
      setEncodedVector("private", kp.pkcs8);
    } else {
      resp["error"] = "keygen failed: " + c.lastError;
    }
  } else if (op == "pubencrypt") {
    const Bytes cert = dec("cert"), d = dec("data");
    if (decodeOom) return sendOom("input");
    auto out = makeUniqueNoThrow<uint8_t[]>(512);  // RSA output up to 4096-bit, off the stack
    if (!out) return sendOom("RSA output");
    size_t olen = 0;
    if (c.rsaPublicEncrypt(cert.ptr(), cert.size, d.ptr(), d.size, out.get(), 512, &olen)) {
      setEncoded("data", out.get(), olen);
    } else {
      resp["error"] = "pubencrypt failed: " + c.lastError;
    }
  } else if (op == "sign") {
    const Bytes priv = dec("private"), h = dec("hash");
    if (decodeOom) return sendOom("input");
    uint8_t sig[128];
    if (h.size != 20) {
      resp["error"] = "hash must be 20 bytes";
    } else if (c.rsaPrivateSignRaw(priv.ptr(), priv.size, h.ptr(), sig)) {
      setEncoded("data", sig, 128);
    } else {
      resp["error"] = "sign failed";
    }
  } else if (op == "pkcs12") {
    const Bytes p12 = dec("data");
    if (decodeOom) return sendOom("input");
    const std::string pw = req["password"] | "";
    req.clear();  // the bundle is decoded; free the large base64 string before the KDF runs
    std::vector<uint8_t> key, cert;
    if (p12.size == 0) {
      resp["error"] = "pkcs12 failed: bundle missing, invalid, or out of memory";
    } else if (c.pkcs12Extract(p12.ptr(), p12.size, pw, &key, &cert)) {
      setEncodedVector("key", key);
      setEncodedVector("cert", cert);
    } else {
      resp["error"] = "pkcs12 failed: " + c.lastError;
    }
  } else {
    resp["error"] = "unknown op";
  }

  if (encodeFailed || resp.overflowed()) return sendOom("response");
  sendJson(server.get(), 200, resp);
}

// POST /api/book-key {path, key (base64, 16 or 32 bytes), expires?} -> {ok}
// Stores a protected book's content key, wrapped to this device, as "<path>.key". The header (with the
// loan date) is authenticated, so the file opens only on this reader and the date cannot be edited.
void InkMODWebServer::handlePluginBookKey() {
  if (!pluginsAllowed(server.get())) return;
  JsonDocument req;
  if (deserializeJson(req, server->arg("plain")) != DeserializationError::Ok) {
    sendError(server.get(), 400, "bad json");
    return;
  }
  const std::string path = req["path"] | "";
  const char* keyB64 = req["key"] | "";
  const int64_t expires = req["expires"] | static_cast<int64_t>(0);
  uint8_t key[bookkey::MAX_KEY_LEN];
  const int32_t n = freeink::content::base64Decode(keyB64, strlen(keyB64), key, sizeof(key));
  if (!protectedpaths::isPluginPath(path) || (n != 16 && n != 32) || expires < 0) {
    sendError(server.get(), 400, "bad path/key");
    return;
  }
  const bool stored = bookkey::write(path, key, static_cast<size_t>(n), expires);
  memset(key, 0, sizeof(key));
  if (stored) lcploans::add(path);
  if (!stored) {
    sendError(server.get(), 500, "cannot store key");
    return;
  }
  server->send(200, "application/json", "{\"ok\":true}");
}

// POST /api/lcp-unlock {path, passphrase? | userKey? (base64, 32 bytes)} -> {ok, expires?} or {error, hint?}
// Reads META-INF/license.lcpl from the book, checks the passphrase against the license, and stores the
// resulting content key exactly as /api/book-key would. Basic-profile licenses unlock with the passphrase;
// production-profile licenses need the finished user key from a plugin or service.
void InkMODWebServer::handlePluginLcpUnlock() {
  if (!pluginsAllowed(server.get())) return;
  JsonDocument req;
  if (deserializeJson(req, server->arg("plain")) != DeserializationError::Ok) {
    sendError(server.get(), 400, "bad json");
    return;
  }
  const std::string path = req["path"] | "";
  const char* passphrase = req["passphrase"] | "";
  const char* userKeyB64 = req["userKey"] | "";
  if (!protectedpaths::isPluginPath(path)) {
    sendError(server.get(), 400, "bad path");
    return;
  }
  uint8_t userKey[32];
  bool haveUserKey = false;
  if (*userKeyB64) {
    haveUserKey = freeink::content::base64Decode(userKeyB64, strlen(userKeyB64), userKey, sizeof(userKey)) == 32;
    if (!haveUserKey) {
      sendError(server.get(), 400, "bad userKey");
      return;
    }
  }

  static constexpr size_t MAX_LICENSE_BYTES = 16 * 1024;
  size_t licSize = 0;
  ZipFile zip(path.c_str());
  if (!zip.getInflatedFileSize("META-INF/license.lcpl", &licSize) || licSize == 0 || licSize > MAX_LICENSE_BYTES) {
    sendError(server.get(), 404, "no license.lcpl");
    return;
  }
  uint8_t* lic = zip.readFileToMemory("META-INF/license.lcpl", &licSize, true);
  if (!lic) {
    sendError(server.get(), 500, "cannot read license");
    return;
  }
  freeink::content::WolfsslCrypto crypto;
  lcp::Result r = lcp::unlock(crypto, reinterpret_cast<const char*>(lic), licSize, passphrase,
                              haveUserKey ? userKey : nullptr);
  free(lic);
  memset(userKey, 0, sizeof(userKey));

  JsonDocument resp;
  if (r.status != lcp::Status::Ok) {
    resp["error"] = lcp::statusName(r.status);
    if (!r.hint.empty()) resp["hint"] = r.hint;
    memset(r.contentKey, 0, sizeof(r.contentKey));
    String out;
    serializeJson(resp, out);
    server->send(r.status == lcp::Status::BadLicense ? 400 : 403, "application/json", out);
    return;
  }
  const bool stored = bookkey::write(path, r.contentKey, sizeof(r.contentKey), r.expiresAt);
  if (stored) lcploans::add(path);
  memset(r.contentKey, 0, sizeof(r.contentKey));
  if (!stored) {
    sendError(server.get(), 500, "cannot store key");
    return;
  }
  resp["ok"] = true;
  if (r.expiresAt > 0) resp["expires"] = r.expiresAt;
  String out;
  serializeJson(resp, out);
  server->send(200, "application/json", out);
}
