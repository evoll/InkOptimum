#include "PluginManifest.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cctype>

#include "ProtectedPaths.h"

namespace plugincat {
namespace {

void readHeaders(JsonVariantConst obj, Headers& out) {
  out.clear();
  if (!obj.is<JsonObjectConst>()) return;
  for (JsonPairConst kv : obj.as<JsonObjectConst>()) {
    if (kv.value().is<const char*>()) out.emplace_back(kv.key().c_str(), kv.value().as<const char*>());
  }
}

void readRequest(JsonVariantConst node, const char* defaultMethod, Request& out) {
  out.url = node["url"] | "";
  out.method = node["method"] | defaultMethod;
  out.body = node["body"] | "";
  readHeaders(node["headers"], out.headers);
}

Notice readNotice(JsonVariantConst node) {
  Notice n;
  n.title = node["title"] | "";
  n.message = node["message"] | "";
  n.confirm = node["confirm"] | "";
  n.cancel = node["cancel"] | "";
  return n;
}

}  // namespace

std::string inPluginDir(const std::string& pluginDir, const std::string& path) {
  if (path.empty()) return {};
  if (path.find("..") != std::string::npos || path.find('\\') != std::string::npos) return {};
  std::string full = path[0] == '/' ? path : pluginDir + "/" + path;
  if (!protectedpaths::isPluginPath(full)) return {};
  return full;
}

bool parseDeviceManifest(const std::string& json, const std::string& pluginDir, DeviceManifest& m) {
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok || !doc.is<JsonObject>()) return false;

  m = DeviceManifest{};
  m.title = doc["title"] | "";
  m.description = doc["description"] | "";

  m.tokenFile = inPluginDir(pluginDir, doc["token"]["file"] | "");
  m.tokenPath = doc["token"]["path"] | "token";
  m.configFile = inPluginDir(pluginDir, doc["config"]["file"] | "");

  JsonVariantConst browse = doc["browse"];
  readRequest(browse, "GET", m.browse);
  m.itemsPath = browse["items"] | "";
  m.titlePath = browse["fields"]["title"] | "title";
  m.authorPath = browse["fields"]["author"] | "";
  m.idPath = browse["fields"]["id"] | "";
  m.urlPath = browse["fields"]["url"] | "";
  m.versionPath = browse["fields"]["version"] | "";
  m.descriptionPath = browse["fields"]["description"] | "";
  m.detailAuthorPath = browse["fields"]["detail_author"] | "";
  m.repositoryPath = browse["fields"]["repository"] | "";
  m.detailsUrlPath = browse["fields"]["details_url"] | "";
  m.detailsFormatPath = browse["fields"]["details_format"] | "";
  m.compatFamilyPath = browse["compatibility"]["firmware_family"] | "";
  m.compatApiMinPath = browse["compatibility"]["plugin_api_min"] | "";
  m.compatRequiresPath = browse["compatibility"]["requires"] | "";
  m.pageSize = std::clamp(static_cast<int>(browse["page_size"] | 8), 1, MAX_PAGE_SIZE);
  m.xml = std::string(browse["format"] | "json") == "xml";
  if (m.xml) {
    m.xmlItem = browse["item"] | "";
    m.xmlContainer = browse["container_element"] | "";
    m.xmlSkipSelf = browse["skip_self"] | false;
    m.xmlResolveUrls = browse["resolve_urls"] | false;
    for (JsonVariantConst e : browse["extensions"].as<JsonArrayConst>()) {
      std::string ext = e | "";
      for (auto& c : ext) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
      if (!ext.empty()) m.xmlExtensions.push_back(ext[0] == '.' ? ext : "." + ext);
    }
    if (m.xmlItem.empty()) return false;  // an XML catalog without an item element cannot list anything
  }
  m.listsUrl = browse["lists_url"] | "";
  for (JsonVariantConst l : browse["lists"].as<JsonArrayConst>()) {
    BrowseList entry;
    entry.title = l["title"] | "";
    entry.url = l["url"] | "";
    entry.body = l["body"] | "";
    entry.notice = readNotice(l["notice"]);
    if (!entry.title.empty()) m.lists.push_back(std::move(entry));
  }

  JsonVariantConst dl = doc["download"];
  readRequest(dl, "GET", m.download);
  m.dlUrlPath = dl["url_path"] | "";
  m.dlUser = dl["username"] | "";
  m.dlPass = dl["password"] | "";
  const char* dest = dl["dest_dir"] | "";
  if (dest[0] != '\0') {
    m.destDir = inPluginDir(pluginDir, dest);
    if (m.destDir.empty()) return false;  // outside plugin space: refuse the whole manifest
    while (m.destDir.size() > 1 && m.destDir.back() == '/') m.destDir.pop_back();
  }
  m.filenameTemplate = dl["filename"] | "{title}.epub";
  m.bundleBasePath = dl["bundle"]["base"] | "";
  m.bundleFilesPath = dl["bundle"]["files"] | "";
  m.bundleSubdir = dl["bundle"]["subdir"] | "{id}";
  m.sidecarPath = dl["sidecar"]["path"] | "";
  m.sidecarBody = dl["sidecar"]["body"] | "";

  JsonVariantConst auth = doc["auth"];
  const std::string authType = auth["type"] | "";
  if (authType == "device_code" || authType == "password") {
    m.auth.type = authType == "password" ? Auth::Type::Password : Auth::Type::DeviceCode;
    readRequest(auth["request"], "POST", m.auth.request);
    readRequest(auth["poll"], "POST", m.auth.poll);
    auto path = [&](const char* key, std::string& field) {
      const char* v = auth[key] | "";
      if (v[0] != '\0') field = v;
    };
    path("code_path", m.auth.codePath);
    path("verify_url_path", m.auth.verifyUrlPath);
    path("device_code_path", m.auth.deviceCodePath);
    path("interval_path", m.auth.intervalPath);
    path("expires_path", m.auth.expiresPath);
    path("token_path", m.auth.tokenPath);
    path("error_path", m.auth.errorPath);
    if (m.auth.request.url.empty() || (m.auth.type == Auth::Type::DeviceCode && m.auth.poll.url.empty())) {
      m.auth = Auth{};  // unusable sign-in block: behave as if there were none
    }
  }
  return true;
}

}  // namespace plugincat
