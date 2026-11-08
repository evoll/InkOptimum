#pragma once
// Text helpers shared by the Atom (OpdsParser) and JSON (Opds2) feed readers. Pure C++.
#include <cstring>
#include <string>

#include "OpdsEntry.h"

namespace opds_text {

inline bool containsIgnoreCase(const char* haystack, const char* needle) {
  if (!haystack || !needle || !*needle) return false;
  for (const char* h = haystack; *h; ++h) {
    const char* a = h;
    const char* b = needle;
    while (*a && *b) {
      char ca = *a;
      char cb = *b;
      if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
      if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
      if (ca != cb) break;
      ++a;
      ++b;
    }
    if (!*b) return true;
  }
  return false;
}

inline bool classifyAcquisitionType(const char* type, OpdsBookFormat& format) {
  if (!type) return false;

  // Be liberal with legacy OPDS catalogs: MIME parameters, case variants and
  // historical spellings are all seen in the wild.
  if (containsIgnoreCase(type, "vnd.readium.lcp.license")) {
    format = OpdsBookFormat::LCP_LICENSE;
    return true;
  }
  if (containsIgnoreCase(type, "epub+zip") || containsIgnoreCase(type, "application/epub")) {
    format = OpdsBookFormat::EPUB;
    return true;
  }
  if (containsIgnoreCase(type, "application/fb3") || containsIgnoreCase(type, "application/x-fb3") ||
      containsIgnoreCase(type, "fb3+zip")) {
    format = OpdsBookFormat::FB3;
    return true;
  }
  if (containsIgnoreCase(type, "mobi8") || containsIgnoreCase(type, "vnd.amazon.ebook") ||
      containsIgnoreCase(type, "x-azw3") || containsIgnoreCase(type, "x-kindle-ebook")) {
    format = OpdsBookFormat::AZW3;
    return true;
  }
  if (containsIgnoreCase(type, "mobipocket")) {
    format = OpdsBookFormat::MOBI;
    return true;
  }
  if (containsIgnoreCase(type, "application/rtf") || containsIgnoreCase(type, "text/rtf") ||
      containsIgnoreCase(type, "application/x-rtf")) {
    format = OpdsBookFormat::RTF;
    return true;
  }
  if (containsIgnoreCase(type, "wordprocessingml.document")) {
    format = OpdsBookFormat::DOCX;
    return true;
  }
  if (containsIgnoreCase(type, "fb2+zip")) {
    format = OpdsBookFormat::FB2_ZIP;
    return true;
  }
  if (containsIgnoreCase(type, "application/fb2") || containsIgnoreCase(type, "text/fb2") ||
      containsIgnoreCase(type, "fictionbook+xml")) {
    format = OpdsBookFormat::FB2;
    return true;
  }
  return false;
}


// The publication itself: rel="http://opds-spec.org/acquisition", or its "/open-access" form. The other
// acquisition rels (buy, borrow, subscribe, sample) mostly lead to a shop or loan page, whatever their
// address ends in.
inline bool isDirectAcquisitionRel(const char* rel) {
  if (!rel) return false;
  static constexpr char kAcquisition[] = "opds-spec.org/acquisition";
  const char* at = std::strstr(rel, kAcquisition);
  if (!at) return false;
  const char* tail = at + sizeof(kAcquisition) - 1;
  return tail[0] == '\0' || std::strcmp(tail, "/open-access") == 0;
}

// The format of a download whose link names none (no type, an empty one, or application/octet-stream),
// read from the file name its href ends in; the query and fragment are left off. Longer suffixes first.
// Ported in spirit from witchhunt-reader (8cd2007f), with this fork's format set.
inline bool classifyAcquisitionHref(const char* href, OpdsBookFormat& format) {
  if (!href) return false;
  std::string name(href);
  const size_t cut = name.find_first_of("?#");
  if (cut != std::string::npos) name.resize(cut);
  while (!name.empty() && name.back() == '/') name.pop_back();
  for (auto& c : name) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  auto endsWith = [&name](const char* suffix) {
    const size_t n = std::char_traits<char>::length(suffix);
    return name.size() > n && name.compare(name.size() - n, n, suffix) == 0;
  };
  if (endsWith(".fb2.zip")) { format = OpdsBookFormat::FB2_ZIP; return true; }
  if (endsWith(".epub") || endsWith(".kepub")) { format = OpdsBookFormat::EPUB; return true; }
  if (endsWith(".fb2")) { format = OpdsBookFormat::FB2; return true; }
  if (endsWith(".fb3")) { format = OpdsBookFormat::FB3; return true; }
  if (endsWith(".azw3")) { format = OpdsBookFormat::AZW3; return true; }
  if (endsWith(".mobi") || endsWith(".prc")) { format = OpdsBookFormat::MOBI; return true; }
  if (endsWith(".rtf")) { format = OpdsBookFormat::RTF; return true; }
  if (endsWith(".docx")) { format = OpdsBookFormat::DOCX; return true; }
  return false;
}

// classifyAcquisitionType() first; a direct download whose type names no format falls back to its file name.
inline bool classifyAcquisition(const char* type, const char* rel, const char* href, OpdsBookFormat& format) {
  if (classifyAcquisitionType(type, format)) return true;
  const bool typeNamesNoFormat = !type || type[0] == '\0' || containsIgnoreCase(type, "application/octet-stream");
  return typeNamesNoFormat && isDirectAcquisitionRel(rel) && classifyAcquisitionHref(href, format);
}

inline std::string sanitizeDescriptionText(const std::string& raw) {
  std::string out;
  out.reserve(raw.size() < 1536 ? raw.size() : 1536);
  bool inTag = false;
  bool pendingSpace = false;

  auto appendSpace = [&]() {
    if (!out.empty() && out.back() != ' ') pendingSpace = true;
  };

  for (size_t i = 0; i < raw.size();) {
    const unsigned char c = static_cast<unsigned char>(raw[i]);
    if (c == '<') {
      inTag = true;
      appendSpace();
      ++i;
      continue;
    }
    if (inTag) {
      if (c == '>') inTag = false;
      ++i;
      continue;
    }

    if (c == '&') {
      const size_t semi = raw.find(';', i + 1);
      if (semi != std::string::npos && semi - i <= 10) {
        const std::string entity = raw.substr(i, semi - i + 1);
        char decoded = 0;
        if (entity == "&nbsp;" || entity == "&#160;") decoded = ' ';
        else if (entity == "&amp;") decoded = '&';
        else if (entity == "&lt;") decoded = '<';
        else if (entity == "&gt;") decoded = '>';
        else if (entity == "&quot;") decoded = '"';
        else if (entity == "&apos;") decoded = '\'';
        if (decoded) {
          if (decoded == ' ') appendSpace();
          else {
            if (pendingSpace && !out.empty()) out.push_back(' ');
            pendingSpace = false;
            out.push_back(decoded);
          }
          i = semi + 1;
          continue;
        }
      }
    }

    if (c <= 0x20) {
      appendSpace();
      ++i;
      continue;
    }

    size_t cpLen = 1;
    if ((c & 0xE0) == 0xC0) cpLen = 2;
    else if ((c & 0xF0) == 0xE0) cpLen = 3;
    else if ((c & 0xF8) == 0xF0) cpLen = 4;
    if (i + cpLen > raw.size()) break;

    if (pendingSpace && !out.empty()) out.push_back(' ');
    pendingSpace = false;
    if (out.size() + cpLen > 1536) break;
    out.append(raw, i, cpLen);
    i += cpLen;
  }

  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}


}  // namespace opds_text
