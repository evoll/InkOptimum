#pragma once

#include <ArduinoJson.h>

#include <string>

namespace plugincat {

// JSON text holding `token` at the dotted `path` ("token" -> {"token":"..."}, "auth.access" -> nested), the
// shape the manifest's token section reads back. "" when the path is empty or has an empty segment.
inline std::string tokenFileJson(const std::string& path, const std::string& token) {
  if (path.empty()) return {};
  JsonDocument doc;
  JsonVariant node = doc.to<JsonVariant>();
  size_t pos = 0;
  while (true) {
    const size_t dot = path.find('.', pos);
    const std::string part = path.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos);
    if (part.empty()) return {};
    if (dot == std::string::npos) {
      node[part] = token;
      break;
    }
    node = node[part].to<JsonObject>();
    pos = dot + 1;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

}  // namespace plugincat
