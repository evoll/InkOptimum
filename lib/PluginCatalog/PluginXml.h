#pragma once

#include <string>
#include <vector>

// XML catalogs (device.json browse.format "xml"): a repeating element becomes one row. Covers a WebDAV
// PROPFIND multistatus, an Atom/OPDS feed or any XML list; the firmware knows no protocol, the manifest names
// the elements. Field selectors: "elem" (text of the first such element inside the item), "elem@attr"
// (attribute of it) or "@attr" (attribute of the item element itself). Element names match by local name.
namespace plugincat {

struct XmlFields {
  std::string title, author, id, url, version, description;
};

struct XmlOptions {
  std::string itemName;       // local name of the repeating element
  std::string containerName;  // an item holding this element is a folder ("" = flat list)
  XmlFields fields;
  std::vector<std::string> extensions;  // allowed file extensions incl. dot, lower case; empty = all
  bool skipSelf = false;                // drop the entry equal to the request URL
  bool resolveUrls = false;             // resolve url values against the request URL
  std::string requestUrl;
  size_t maxEntries = 200;
};

struct XmlEntry {
  std::string title, author, id, url, version, description;
  bool folder = false;
};

// Resolves `href` against `base` (absolute URL, "/path" or relative path). "" when base has no scheme.
std::string resolveAgainst(const std::string& base, const std::string& href);

class XmlCatalogParser {
 public:
  explicit XmlCatalogParser(const XmlOptions& options);
  ~XmlCatalogParser();
  XmlCatalogParser(const XmlCatalogParser&) = delete;
  XmlCatalogParser& operator=(const XmlCatalogParser&) = delete;

  // False when the document is malformed (entries parsed so far stay available).
  bool feed(const char* data, size_t len, bool last);
  const std::vector<XmlEntry>& entries() const { return entries_; }
  bool truncated() const { return truncated_; }  // stopped at maxEntries

 private:
  struct Impl;
  Impl* impl_;
  std::vector<XmlEntry> entries_;
  bool truncated_ = false;
};

}  // namespace plugincat
