#pragma once

#include <cctype>
#include <string>

// Per-server HTTP identity for OPDS catalogues: a custom User-Agent and one extra request header.
// Some catalogues (Calibre-Web, COPS, private mirrors) only answer clients that look like KOReader or
// need an API token header. Everything here is pure C++ so the validation is covered by native tests.
namespace opds_profile {

inline constexpr size_t MAX_USER_AGENT = 120;
inline constexpr size_t MAX_HEADER_NAME = 40;
inline constexpr size_t MAX_HEADER_VALUE = 200;

// What the web UI offers as a one-click preset.
inline constexpr char KOREADER_USER_AGENT[] = "KOReader/2024.11 (https://koreader.rocks/)";

struct Header {
  std::string name;
  std::string value;
  bool empty() const { return name.empty(); }
};

namespace detail {

inline bool isTokenChar(const char c) {
  const unsigned char u = static_cast<unsigned char>(c);
  if (std::isalnum(u)) return true;
  switch (c) {
    case '!': case '#': case '$': case '%': case '&': case '\'': case '*': case '+':
    case '-': case '.': case '^': case '_': case '`': case '|': case '~':
      return true;
    default:
      return false;
  }
}

inline std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

inline std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
  return s.substr(a, b - a);
}

// Visible ASCII and UTF-8 only: control characters (CR/LF in particular) would allow header injection.
inline bool isSafeValue(const std::string& s) {
  for (const char c : s) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u < 0x20 || u == 0x7F) return false;
  }
  return true;
}

}  // namespace detail

// Headers the downloader sets itself; overriding them would break redirects, resume or auth.
inline bool isReservedHeader(const std::string& name) {
  static const char* const reserved[] = {"host",          "user-agent",  "authorization",    "range",
                                         "content-length", "connection", "transfer-encoding", "accept-encoding",
                                         "cookie",         "proxy-authorization"};
  const std::string n = detail::lower(name);
  for (const char* r : reserved) {
    if (n == r) return true;
  }
  return false;
}

// Returns the trimmed User-Agent, or an empty string when it is unusable (control characters, too long).
inline std::string sanitizeUserAgent(const std::string& raw) {
  const std::string v = detail::trim(raw);
  if (v.size() > MAX_USER_AGENT || !detail::isSafeValue(v)) return "";
  return v;
}

// Parses "Name: value". Empty input means "no custom header" and yields ok=true with an empty Header.
// Anything malformed or reserved is rejected so a typo can never produce a broken request.
inline bool parseHeaderLine(const std::string& line, Header& out) {
  out = Header{};
  const std::string trimmed = detail::trim(line);
  if (trimmed.empty()) return true;

  const size_t colon = trimmed.find(':');
  if (colon == std::string::npos || colon == 0) return false;

  const std::string name = detail::trim(trimmed.substr(0, colon));
  const std::string value = detail::trim(trimmed.substr(colon + 1));
  if (name.empty() || name.size() > MAX_HEADER_NAME || value.size() > MAX_HEADER_VALUE) return false;
  for (const char c : name) {
    if (!detail::isTokenChar(c)) return false;
  }
  if (!detail::isSafeValue(value) || isReservedHeader(name)) return false;

  out.name = name;
  out.value = value;
  return true;
}

// Normalises a per-server download folder. Returns "" (= use the firmware default) when the value is empty or
// unusable. Accepts absolute SD paths such as "/Books/OPDS"; rejects "..", control characters and over-long values.
inline std::string sanitizeDownloadFolder(const std::string& raw) {
  std::string v = detail::trim(raw);
  if (v.empty() || v.size() > 64 || !detail::isSafeValue(v)) return "";
  if (v.find('\\') != std::string::npos || v.find(':') != std::string::npos) return "";
  if (v[0] != '/') v.insert(v.begin(), '/');
  std::string out;  // collapse repeated slashes, validate each segment
  size_t i = 0;
  while (i < v.size()) {
    while (i < v.size() && v[i] == '/') ++i;
    size_t j = i;
    while (j < v.size() && v[j] != '/') ++j;
    if (j == i) break;
    const std::string seg = v.substr(i, j - i);
    if (seg == "." || seg == ".." ) return "";
    out += '/';
    out += seg;
    i = j;
  }
  return out;  // "/" alone (SD root) collapses to "" = default
}

inline std::string formatHeaderLine(const Header& h) { return h.empty() ? std::string() : h.name + ": " + h.value; }

}  // namespace opds_profile
