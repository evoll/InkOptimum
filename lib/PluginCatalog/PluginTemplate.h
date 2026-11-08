#pragma once

#include <string>
#include <utility>
#include <vector>

// {name} substitution for device.json request templates (URLs, bodies, header values, file names).
namespace plugincat {

using Vars = std::vector<std::pair<std::string, std::string>>;

// Replaces every {name} that has an entry in `vars`. One pass over the template: substituted values are
// never scanned again, so a title containing "{token}" cannot pull the token into a request. Unknown
// {names} (and lone braces, as in a JSON body) are left untouched.
inline std::string substitute(const std::string& tpl, const Vars& vars) {
  std::string out;
  out.reserve(tpl.size() + 32);
  size_t i = 0;
  while (i < tpl.size()) {
    if (tpl[i] == '{') {
      const size_t close = tpl.find('}', i + 1);
      if (close != std::string::npos && close - i - 1 <= 48) {
        const std::string name = tpl.substr(i + 1, close - i - 1);
        bool replaced = false;
        for (const auto& var : vars) {
          if (var.first == name) {
            out += var.second;
            replaced = true;
            break;
          }
        }
        if (replaced) {
          i = close + 1;
          continue;
        }
      }
    }
    out += tpl[i++];
  }
  return out;
}

// Percent-encodes everything outside the RFC 3986 unreserved set.
inline std::string urlEncode(const std::string& in) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(in.size() * 3 / 2);
  for (const unsigned char c : in) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

// Makes a URL valid for an HTTP request line: control characters, space and non-ASCII (UTF-8) bytes are
// percent-encoded; every other character (including existing %XX escapes, '&', '?', '#', '/') is kept, so a
// URL that was already valid does not change.
inline std::string escapeUrl(const std::string& url) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(url.size());
  for (const unsigned char c : url) {
    if (c > 0x20 && c < 0x7F) {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

// A name that is safe as one file name on the card: no separators or reserved characters, bounded length
// (the extension of `name` is kept when it is short and plain).
inline std::string sanitizeFileName(const std::string& name, size_t maxBytes = 100) {
  std::string out;
  out.reserve(name.size());
  for (const unsigned char c : name) {
    if (c < 0x20 || c == 0x7f || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
        c == '>' || c == '|') {
      out += '_';
    } else {
      out += static_cast<char>(c);
    }
  }
  while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
  while (!out.empty() && out.front() == ' ') out.erase(out.begin());
  if (out.empty() || out == "." || out == "..") return "download";
  if (out.size() <= maxBytes) return out;

  std::string ext;
  const size_t dot = out.rfind('.');
  if (dot != std::string::npos && out.size() - dot <= 8) ext = out.substr(dot);
  size_t keep = maxBytes - ext.size();
  // Never cut inside a UTF-8 sequence.
  while (keep > 0 && (static_cast<unsigned char>(out[keep]) & 0xC0) == 0x80) keep--;
  return out.substr(0, keep) + ext;
}

// A server-relative path from a response ("/x", not "//host/x") gets the origin (scheme, host and port) of
// requestUrl, the request that returned it (catalog.server_paths). Returns true when path was changed.
inline bool resolveServerPath(std::string& path, const std::string& requestUrl) {
  if (path.empty() || path[0] != '/' || (path.size() > 1 && path[1] == '/')) return false;
  const size_t scheme = requestUrl.find("://");
  if (scheme == std::string::npos) return false;
  const size_t hostEnd = requestUrl.find_first_of("/?#", scheme + 3);
  path.insert(0, requestUrl, 0, hostEnd == std::string::npos ? requestUrl.size() : hostEnd);
  return true;
}

}  // namespace plugincat
