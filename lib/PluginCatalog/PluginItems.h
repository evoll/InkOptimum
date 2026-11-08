#pragma once

#include <ArduinoJson.h>

#include <string>
#include <vector>

#include "PluginManifest.h"

namespace plugincat {

// What this firmware advertises to catalogs (and exposes as {firmware_family} etc. in templates).
// inkMOD keeps CrossPoint's plugin contract, so it identifies as the "crosspoint" family (the docs allow forks
// that keep the contract to do so); "inkmod" stays accepted for catalogs that name this fork explicitly.
namespace identity {
inline constexpr const char* FAMILY = "crosspoint";
inline constexpr const char* COMPATIBLE_FAMILIES = "inkmod,crosspoint";
inline constexpr int PLUGIN_API = 1;
inline constexpr const char* CAPABILITIES =
    "catalog.lists,catalog.notices,catalog.bundle_install,catalog.version_tracking,catalog.compatibility,"
    "catalog.item_details,catalog.encoded_item_fields,catalog.server_paths";
}  // namespace identity

struct Item {
  std::string id;
  std::string title;
  std::string author;
  std::string url;  // direct file URL, when the row carries one
  std::string version;
  std::string description;
  std::string detailAuthor, repository, detailsUrl, detailsFormat;  // details page fields
  // Bundle rows: files to fetch below `base`.
  std::string base;
  std::vector<std::string> files;
};

// Dotted path into a document; a numeric segment indexes an array. Null variant when absent.
JsonVariantConst resolvePath(JsonVariantConst root, const std::string& path);

// A scalar as text ("" for null/absent/containers); numbers and booleans are rendered.
std::string variantToString(JsonVariantConst v);

// ArduinoJson filter keeping only the fields the manifest maps (plus the bundle file list), so a page of
// heavy metadata parses into a few KB.
// Returns false when the manifest's paths cannot be expressed as a filter: parse unfiltered then.
bool buildItemFilter(const DeviceManifest& manifest, JsonDocument& filter);

// Comma list helpers for compatibility checks.
bool csvContains(const std::string& csv, const std::string& token);

// True when an entry declaring (family allow-list, minimum API, required capabilities) can run on this
// firmware. Empty fields mean "no restriction".
bool entryCompatible(const std::string& families, long apiMin, const std::string& requires_);

// Reads the item array out of an already parsed (and possibly filtered) response. Rows without a title and
// incompatible rows are dropped. `more` is set when the response held more than manifest.pageSize rows (the
// request asks for pageSize + 1 to detect it); at most pageSize items are returned.
// How a browse request pages (CrossPoint's rules): the variables the template uses decide.
//   Offset    {offset} (precedence over {page}): the server skips; {limit} = page_size + 1, the extra row proves "more".
//   PageLimit {page} with {limit}: {limit} = page_size, a full page shows "Next page".
//   Page      {page} without {limit}: the response has page_size rows, more than that means "Next page".
//   Static    neither: one list for every page, sliced locally.
enum class Paging { Static, Page, PageLimit, Offset };

Paging pagingOf(const Request& browse);
int limitFor(Paging paging, int pageSize);

// `skip` eligible rows are passed over first (a static list shows page N from row (N-1)*pageSize on);
// `fullPageMeansMore` is the PageLimit rule.
void extractItems(JsonVariantConst response, const DeviceManifest& manifest, std::vector<Item>& out, bool& more,
                  size_t skip = 0, bool fullPageMeansMore = false);

}  // namespace plugincat
