#pragma once

#include <string>
#include <utility>
#include <vector>

// device.json: the declarative on-device catalog manifest of an SD plugin (CrossPoint-compatible subset).
// Supported: JSON catalogs with paging, named lists with notices, version-tracked plugin catalogs with
// compatibility filtering, token and config files, direct and "one hop" downloads, multi-file bundles.
// Also: XML catalogs, on-device sign-in, item details pages, sidecars, events.
namespace plugincat {

using Headers = std::vector<std::pair<std::string, std::string>>;

struct Request {
  std::string url;
  std::string method = "GET";
  std::string body;
  Headers headers;
};

struct Notice {
  std::string title, message, confirm, cancel;
  bool present() const { return !message.empty(); }
};

struct BrowseList {
  std::string title;
  std::string url;   // empty: the browse URL
  std::string body;  // empty: the browse body
  Notice notice;
};

// On-device sign-in. "password" mints a token silently from the config credentials; "device_code" shows a code
// and a QR and polls until the user authorizes.
struct Auth {
  enum class Type { None, DeviceCode, Password };
  Type type = Type::None;
  Request request, poll;
  std::string codePath = "user_code", verifyUrlPath = "verification_uri", deviceCodePath = "device_code",
              intervalPath = "interval", expiresPath = "expires_in", tokenPath = "access_token",
              errorPath = "error";
};

struct DeviceManifest {
  std::string title;
  std::string description;

  std::string tokenFile;  // absolute path or empty
  std::string tokenPath = "token";
  std::string configFile;

  Request browse;
  std::string listsUrl;  // dynamic list index ("lists" array in the response)
  std::vector<BrowseList> lists;
  std::string itemsPath;
  std::string titlePath = "title";
  std::string authorPath;
  std::string idPath;
  std::string urlPath;
  std::string versionPath;
  std::string descriptionPath;
  std::string compatFamilyPath, compatApiMinPath, compatRequiresPath;
  // Details page (catalog.item_details): extra row fields; details_url is fetched only for the selected item.
  std::string detailAuthorPath, repositoryPath, detailsUrlPath, detailsFormatPath;
  int pageSize = 8;
  // browse.format "xml": a repeating element is one row (see PluginXml.h); no paging.
  bool xml = false;
  std::string xmlItem, xmlContainer;
  bool xmlSkipSelf = false, xmlResolveUrls = false;
  std::vector<std::string> xmlExtensions;

  Request download;
  std::string dlUrlPath;  // response field holding the file URL when download is a "hop"
  std::string dlUser, dlPass;
  std::string destDir;  // absolute, may be empty (SD root)
  std::string filenameTemplate = "{title}.epub";
  std::string bundleBasePath, bundleFilesPath;
  std::string bundleSubdir = "{id}";
  std::string sidecarPath, sidecarBody;  // "<dest>.meta.json" convention; written after a file download
  std::string destDirConfigKey = "dest_dir";  // a "dest_dir" in the config file overrides destDir
  Auth auth;

  bool isCatalog() const { return !browse.url.empty(); }
  bool isBundle() const { return !bundleBasePath.empty() && !bundleFilesPath.empty(); }
  // Rows of a catalog of plugins are badged Installed / Update by comparing versions.
  bool tracksInstalls() const { return !versionPath.empty() && isBundle(); }
};

inline constexpr int MAX_PAGE_SIZE = 16;

// A path from the manifest: absolute stays, a relative one resolves inside pluginDir. "" when it climbs out.
std::string inPluginDir(const std::string& pluginDir, const std::string& path);

// Parses device.json text. `pluginDir` is "<root>/<name>". False when the JSON is invalid or unusable.
// A manifest without browse.url parses fine (a background/events plugin): isCatalog() is then false.
bool parseDeviceManifest(const std::string& json, const std::string& pluginDir, DeviceManifest& out);

}  // namespace plugincat
