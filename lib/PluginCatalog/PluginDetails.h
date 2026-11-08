#pragma once

#include <cstdint>
#include <string>

// Details pages of catalog items (CrossPoint's catalog.item_details): a small, bounded HTML-to-text reader.
namespace plugincat {

// Largest details_url body that is fetched and shown.
inline constexpr size_t MAX_DETAILS_BYTES = 24 * 1024;

namespace detail {

inline bool iequalsPrefix(const std::string& s, size_t pos, const char* word) {
  size_t i = 0;
  for (; word[i]; ++i) {
    if (pos + i >= s.size()) return false;
    const char a = static_cast<char>(tolower(static_cast<unsigned char>(s[pos + i])));
    if (a != word[i]) return false;
  }
  return true;
}

inline void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp <= 0x10FFFF) {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// Decodes one entity starting at s[pos] == '&'. Returns the number of bytes consumed (0: not an entity).
inline size_t decodeEntity(const std::string& s, size_t pos, std::string& out) {
  const size_t semi = s.find(';', pos + 1);
  if (semi == std::string::npos || semi - pos > 10) return 0;
  const std::string name = s.substr(pos + 1, semi - pos - 1);
  if (name == "amp") out += '&';
  else if (name == "lt") out += '<';
  else if (name == "gt") out += '>';
  else if (name == "quot") out += '"';
  else if (name == "apos") out += '\'';
  else if (name == "nbsp") out += ' ';
  else if (name.size() > 1 && name[0] == '#') {
    uint32_t cp = 0;
    const bool hex = name[1] == 'x' || name[1] == 'X';
    for (size_t i = hex ? 2 : 1; i < name.size(); ++i) {
      const char c = name[i];
      int d;
      if (c >= '0' && c <= '9') d = c - '0';
      else if (hex && c >= 'a' && c <= 'f') d = c - 'a' + 10;
      else if (hex && c >= 'A' && c <= 'F') d = c - 'A' + 10;
      else return 0;
      cp = cp * (hex ? 16 : 10) + static_cast<uint32_t>(d);
      if (cp > 0x10FFFF) return 0;
    }
    if (name.size() == (hex ? 2u : 1u) || cp == 0) return 0;
    appendUtf8(out, cp);
  } else {
    return 0;
  }
  return semi - pos + 1;
}

}  // namespace detail

// Semantic HTML to plain text: block tags break lines, <li> becomes "- ", <script>/<style> are dropped,
// entities are decoded, whitespace is collapsed. Output never exceeds maxBytes.
inline std::string htmlToText(const std::string& html, size_t maxBytes = MAX_DETAILS_BYTES) {
  std::string out;
  out.reserve(std::min(html.size(), maxBytes));
  bool pendingSpace = false;
  auto newline = [&](int count) {
    while (!out.empty() && out.back() == ' ') out.pop_back();
    if (out.empty()) return;
    int have = 0;
    for (size_t i = out.size(); i > 0 && out[i - 1] == '\n' && have < 2; --i) ++have;
    for (int i = have; i < count; ++i) out += '\n';
    pendingSpace = false;
  };

  size_t i = 0;
  while (i < html.size() && out.size() < maxBytes) {
    const char c = html[i];
    if (c == '<') {
      if (html.compare(i, 4, "<!--") == 0) {
        const size_t end = html.find("-->", i + 4);
        i = end == std::string::npos ? html.size() : end + 3;
        continue;
      }
      const size_t close = html.find('>', i + 1);
      if (close == std::string::npos) break;
      size_t n = i + 1;
      const bool closing = n < html.size() && html[n] == '/';
      if (closing) ++n;
      if (!closing && (detail::iequalsPrefix(html, n, "script") || detail::iequalsPrefix(html, n, "style"))) {
        const char* endTag = detail::iequalsPrefix(html, n, "script") ? "</script" : "</style";
        size_t j = close;
        while ((j = html.find('<', j + 1)) != std::string::npos && !detail::iequalsPrefix(html, j, endTag)) {
        }
        const size_t end = j == std::string::npos ? std::string::npos : html.find('>', j);
        i = end == std::string::npos ? html.size() : end + 1;
        continue;
      }
      if (detail::iequalsPrefix(html, n, "br")) {
        newline(1);
      } else if (detail::iequalsPrefix(html, n, "li")) {
        if (!closing) {
          newline(1);
          out += "- ";
        }
      } else if (detail::iequalsPrefix(html, n, "h1") || detail::iequalsPrefix(html, n, "h2") ||
                 detail::iequalsPrefix(html, n, "h3") || detail::iequalsPrefix(html, n, "h4") ||
                 detail::iequalsPrefix(html, n, "p") || detail::iequalsPrefix(html, n, "div") ||
                 detail::iequalsPrefix(html, n, "ul") || detail::iequalsPrefix(html, n, "ol") ||
                 detail::iequalsPrefix(html, n, "table") || detail::iequalsPrefix(html, n, "tr") ||
                 detail::iequalsPrefix(html, n, "blockquote") || detail::iequalsPrefix(html, n, "pre")) {
        newline(2);
      }
      i = close + 1;
      continue;
    }
    if (c == '&') {
      std::string decoded;
      const size_t used = detail::decodeEntity(html, i, decoded);
      if (used > 0) {
        if (decoded == " ") {
          pendingSpace = true;
        } else {
          if (pendingSpace && !out.empty() && out.back() != '\n') out += ' ';
          pendingSpace = false;
          out += decoded;
        }
        i += used;
        continue;
      }
    }
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      pendingSpace = true;
      ++i;
      continue;
    }
    if (pendingSpace && !out.empty() && out.back() != '\n') out += ' ';
    pendingSpace = false;
    out += c;
    ++i;
  }
  while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
  if (out.size() > maxBytes) out.resize(maxBytes);
  // never leave a UTF-8 sequence cut in half
  size_t start = out.size();
  while (start > 0 && (static_cast<unsigned char>(out[start - 1]) & 0xC0) == 0x80) --start;
  if (start > 0) {
    const unsigned char lead = static_cast<unsigned char>(out[start - 1]);
    const size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    if (out.size() - (start - 1) < need) out.resize(start - 1);
  }
  return out;
}

// "text"/"" details come as they are (whitespace kept, CRLF folded); "html" goes through htmlToText.
inline std::string detailsToText(const std::string& body, const std::string& format, size_t maxBytes = MAX_DETAILS_BYTES) {
  if (format == "html") return htmlToText(body, maxBytes);
  std::string out;
  out.reserve(std::min(body.size(), maxBytes));
  for (size_t i = 0; i < body.size() && out.size() < maxBytes; ++i) {
    if (body[i] == '\r') continue;
    out += body[i];
  }
  while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
  return out;
}

}  // namespace plugincat
