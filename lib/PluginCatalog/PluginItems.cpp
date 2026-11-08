#include "PluginItems.h"

#include <cstdlib>

namespace plugincat {
namespace {

std::vector<std::string> splitPath(const std::string& path) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= path.size()) {
    const size_t dot = path.find('.', start);
    const size_t end = dot == std::string::npos ? path.size() : dot;
    if (end > start) parts.push_back(path.substr(start, end - start));
    if (dot == std::string::npos) break;
    start = dot + 1;
  }
  return parts;
}

bool isIndex(const std::string& s) {
  if (s.empty()) return false;
  for (const char c : s) {
    if (c < '0' || c > '9') return false;
  }
  return true;
}

std::string itemString(JsonVariantConst row, const std::string& path) {
  return path.empty() ? std::string() : variantToString(resolvePath(row, path));
}

}  // namespace

JsonVariantConst resolvePath(JsonVariantConst root, const std::string& path) {
  JsonVariantConst v = root;
  for (const auto& part : splitPath(path)) {
    if (v.isNull()) return v;
    if (v.is<JsonArrayConst>()) {
      if (!isIndex(part)) return JsonVariantConst();
      v = v[static_cast<size_t>(strtoul(part.c_str(), nullptr, 10))];
    } else if (v.is<JsonObjectConst>()) {
      v = v[part];
    } else {
      return JsonVariantConst();
    }
  }
  return v;
}

std::string variantToString(JsonVariantConst v) {
  if (v.is<const char*>()) return v.as<const char*>();
  if (v.is<bool>()) return v.as<bool>() ? "true" : "false";
  if (v.is<long long>()) return std::to_string(v.as<long long>());
  if (v.is<double>()) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%g", v.as<double>());
    return buf;
  }
  return {};
}

bool buildItemFilter(const DeviceManifest& m, JsonDocument& filter) {
  const auto itemParts = splitPath(m.itemsPath);
  for (const auto& part : itemParts) {
    if (isIndex(part)) return false;  // an index inside the items path: parse unfiltered
  }

  JsonObject elem;
  if (itemParts.empty()) {
    elem = filter.to<JsonArray>().add<JsonObject>();
  } else {
    JsonObject cur = filter.to<JsonObject>();
    for (size_t i = 0; i + 1 < itemParts.size(); i++) cur = cur[itemParts[i]].to<JsonObject>();
    elem = cur[itemParts.back()].to<JsonArray>().add<JsonObject>();
  }
  // Only the first segment of each mapped path is named: a nested value keeps its whole subtree, which is
  // small for the fields catalogs map and avoids rebuilding array/object nesting in the filter.
  for (const std::string* p : {&m.titlePath, &m.authorPath, &m.idPath, &m.urlPath, &m.versionPath,
                               &m.descriptionPath, &m.detailAuthorPath, &m.repositoryPath,
                               &m.detailsUrlPath, &m.detailsFormatPath, &m.compatFamilyPath, &m.compatApiMinPath, &m.compatRequiresPath,
                               &m.bundleBasePath, &m.bundleFilesPath}) {
    const auto parts = splitPath(*p);
    if (parts.empty()) continue;
    if (isIndex(parts[0])) return false;  // rows are arrays: keep everything
    elem[parts[0]] = true;
  }
  return true;
}

bool csvContains(const std::string& csv, const std::string& token) {
  size_t start = 0;
  while (start <= csv.size()) {
    size_t comma = csv.find(',', start);
    if (comma == std::string::npos) comma = csv.size();
    size_t b = start, e = comma;
    while (b < e && csv[b] == ' ') b++;
    while (e > b && csv[e - 1] == ' ') e--;
    if (e > b && csv.compare(b, e - b, token) == 0) return true;
    if (comma == csv.size()) break;
    start = comma + 1;
  }
  return false;
}

bool entryCompatible(const std::string& families, long apiMin, const std::string& requires_) {
  if (!families.empty()) {
    bool match = false;
    size_t start = 0;
    while (start <= families.size() && !match) {
      size_t comma = families.find(',', start);
      if (comma == std::string::npos) comma = families.size();
      size_t b = start, e = comma;
      while (b < e && families[b] == ' ') b++;
      while (e > b && families[e - 1] == ' ') e--;
      if (e > b && csvContains(identity::COMPATIBLE_FAMILIES, families.substr(b, e - b))) match = true;
      start = comma + 1;
    }
    if (!match) return false;
  }
  if (apiMin > identity::PLUGIN_API) return false;
  size_t start = 0;
  while (start <= requires_.size()) {
    size_t comma = requires_.find(',', start);
    if (comma == std::string::npos) comma = requires_.size();
    size_t b = start, e = comma;
    while (b < e && requires_[b] == ' ') b++;
    while (e > b && requires_[e - 1] == ' ') e--;
    if (e > b && !csvContains(identity::CAPABILITIES, requires_.substr(b, e - b))) return false;
    if (comma == requires_.size()) break;
    start = comma + 1;
  }
  return true;
}

Paging pagingOf(const Request& browse) {
  const auto uses = [&](const char* name) {
    if (browse.url.find(name) != std::string::npos || browse.body.find(name) != std::string::npos) return true;
    for (const auto& h : browse.headers) {
      if (h.second.find(name) != std::string::npos) return true;
    }
    return false;
  };
  if (uses("{offset}")) return Paging::Offset;
  if (uses("{page}")) return uses("{limit}") ? Paging::PageLimit : Paging::Page;
  return Paging::Static;
}

int limitFor(Paging paging, int pageSize) { return paging == Paging::PageLimit ? pageSize : pageSize + 1; }

void extractItems(JsonVariantConst response, const DeviceManifest& m, std::vector<Item>& out, bool& more, size_t skip,
                  bool fullPageMeansMore) {
  more = false;
  size_t skipped = 0;
  JsonVariantConst rows = m.itemsPath.empty() ? response : resolvePath(response, m.itemsPath);
  if (!rows.is<JsonArrayConst>()) return;

  for (JsonVariantConst row : rows.as<JsonArrayConst>()) {
    Item item;
    item.title = itemString(row, m.titlePath);
    if (item.title.empty()) continue;

    const std::string apiMin = itemString(row, m.compatApiMinPath);
    if (!entryCompatible(itemString(row, m.compatFamilyPath), apiMin.empty() ? 0 : strtol(apiMin.c_str(), nullptr, 10),
                         itemString(row, m.compatRequiresPath))) {
      continue;  // filtered rows never take a page slot
    }

    if (skipped < skip) {  // rows of earlier pages of a static list
      skipped++;
      continue;
    }

    // The request asks for pageSize + 1 rows; the extra one only proves that another page exists.
    if (out.size() >= static_cast<size_t>(m.pageSize)) {
      more = true;
      break;
    }

    item.id = itemString(row, m.idPath);
    item.author = itemString(row, m.authorPath);
    item.url = itemString(row, m.urlPath);
    item.version = itemString(row, m.versionPath);
    item.description = itemString(row, m.descriptionPath);
    item.detailAuthor = itemString(row, m.detailAuthorPath);
    item.repository = itemString(row, m.repositoryPath);
    item.detailsUrl = itemString(row, m.detailsUrlPath);
    item.detailsFormat = itemString(row, m.detailsFormatPath);
    if (m.isBundle()) {
      item.base = itemString(row, m.bundleBasePath);
      JsonVariantConst files = resolvePath(row, m.bundleFilesPath);
      if (files.is<JsonArrayConst>()) {
        for (JsonVariantConst f : files.as<JsonArrayConst>()) {
          if (f.is<const char*>()) item.files.emplace_back(f.as<const char*>());
        }
      }
    }
    out.push_back(std::move(item));
  }
  if (fullPageMeansMore && out.size() >= static_cast<size_t>(m.pageSize)) more = true;
}

}  // namespace plugincat
