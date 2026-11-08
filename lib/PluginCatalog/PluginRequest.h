#pragma once

#include "PluginManifest.h"
#include "PluginTemplate.h"

namespace plugincat {

// Fills a request template. `raw` (token, cfg.*, firmware_*) goes in as it is; `item` values (page, id,
// title, event.*, ...) are percent-encoded inside the URL so they cannot add query parameters, and as they
// are in headers and body. A URL that is exactly "{url}" is already a URL and takes the raw value.
inline Request substituteRequest(const Request& tpl, const Vars& raw, const Vars& item) {
  Vars all = raw;
  all.insert(all.end(), item.begin(), item.end());
  Vars urlVars = raw;
  for (const auto& v : item) urlVars.emplace_back(v.first, urlEncode(v.second));
  // CrossPoint's {id_enc} {title_enc} {author_enc} {url_enc} (catalog.encoded_item_fields): the item field
  // percent-encoded, wherever it is used. Built only when the request uses it (a field can be several KB).
  static const char* const encodedFields[] = {"id", "title", "author", "url"};
  for (const char* field : encodedFields) {
    const std::string key = std::string("{") + field + "_enc}";
    if (tpl.url.find(key) == std::string::npos && tpl.body.find(key) == std::string::npos) {
      bool inHeader = false;
      for (const auto& h : tpl.headers) inHeader = inHeader || h.second.find(key) != std::string::npos;
      if (!inHeader) continue;
    }
    for (const auto& v : item) {
      if (v.first != field) continue;
      const std::string enc = urlEncode(v.second);
      all.emplace_back(std::string(field) + "_enc", enc);
      urlVars.emplace_back(std::string(field) + "_enc", enc);
    }
  }

  Request out;
  out.method = tpl.method.empty() ? "GET" : tpl.method;
  // Spaces, control and non-ASCII bytes cannot stay in a request line (a raw {url} or static text may carry them).
  out.url = escapeUrl(substitute(tpl.url, tpl.url == "{url}" ? all : urlVars));
  out.body = substitute(tpl.body, all);
  for (const auto& h : tpl.headers) out.headers.emplace_back(h.first, substitute(h.second, all));
  return out;
}

}  // namespace plugincat
