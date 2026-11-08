// OpcNames.h
//
// Minimal Open Packaging Conventions (OPC) naming helpers shared by the OPC-based
// book formats (FB3, DOCX).
//
// An FB3 file is an OPC package: a ZIP whose entries are "parts" addressed by
// names such as "/fb3/body.xml", linked together by relationship (.rels)
// files. The rules that matter for reading a real-world package, and that a
// naive "open fb3/body.xml" would get wrong (the official FB3 test corpus has
// a deliberately nasty "Hardcore file structure" sample for exactly this):
//
//   * Part names are case-INsensitive (ASCII), so "_RELS/META.XML.RELS" and
//     "_rels/meta.xml.rels" are the same part.
//   * Part names and relationship targets are URI-ish: non-ASCII characters
//     appear percent-encoded ("%D0%BA%D0%BD...") in .rels files and are
//     stored either percent-encoded or as raw UTF-8 in the ZIP directory.
//     Both sides must be compared after percent-decoding.
//   * Relationship targets are relative to the directory of the part that
//     OWNS the .rels file (or absolute with a leading '/'), and may contain
//     "." and ".." segments ("/cover/../x/coreprop.xml").
//   * A part may have no file extension at all (the sample body is just
//     "/<cyrillic word>"), so content type can never be inferred from the
//     name alone - it comes from [Content_Types].xml.
//
// Every function here returns / takes *normalized* names: percent-decoded,
// ASCII-lowercased, '\' folded to '/', no leading '/', dot segments resolved.
#pragma once

#include <cstddef>
#include <string>

namespace opc {

inline int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// "%D0%BA" -> raw bytes. Malformed escapes are kept literally.
inline std::string percentDecode(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '%' && i + 2 < in.size() && hexValue(in[i + 1]) >= 0 && hexValue(in[i + 2]) >= 0) {
      out.push_back(static_cast<char>((hexValue(in[i + 1]) << 4) | hexValue(in[i + 2])));
      i += 2;
    } else {
      out.push_back(in[i]);
    }
  }
  return out;
}

inline char asciiLower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

inline std::string asciiLowerCopy(std::string s) {
  for (char& c : s) c = asciiLower(c);
  return s;
}

inline bool endsWithLower(const std::string& value, const char* suffixLower) {
  size_t n = 0;
  while (suffixLower[n] != '\0') ++n;
  if (value.size() < n) return false;
  const size_t off = value.size() - n;
  for (size_t i = 0; i < n; ++i) {
    if (asciiLower(value[off + i]) != suffixLower[i]) return false;
  }
  return true;
}

// Collapses "." / ".." / empty segments. Returns false if ".." climbs above
// the package root (a malicious or broken target).
inline bool collapseDots(const std::string& path, std::string& out) {
  out.clear();
  size_t i = 0;
  while (i <= path.size()) {
    size_t j = path.find('/', i);
    if (j == std::string::npos) j = path.size();
    const std::string seg = path.substr(i, j - i);
    if (seg.empty() || seg == ".") {
      // skip
    } else if (seg == "..") {
      if (out.empty()) return false;
      const size_t cut = out.find_last_of('/');
      out.erase(cut == std::string::npos ? 0 : cut);
    } else {
      if (!out.empty()) out.push_back('/');
      out += seg;
    }
    if (j == path.size()) break;
    i = j + 1;
  }
  return true;
}

// ZIP-directory name or absolute part name -> normalized part name.
inline std::string normalizePartName(const std::string& raw) {
  std::string s = percentDecode(raw);
  for (char& c : s) {
    if (c == '\\') c = '/';
    c = asciiLower(c);
  }
  std::string collapsed;
  if (!collapseDots(s, collapsed)) return std::string();
  return collapsed;
}

// Directory part of a normalized name, without trailing '/' ("" for root).
inline std::string partDirectory(const std::string& norm) {
  const size_t slash = norm.find_last_of('/');
  return slash == std::string::npos ? std::string() : norm.substr(0, slash);
}

// Resolves a relationship Target found in the .rels file that belongs to the
// source part whose normalized name is `sourceNorm` ("" = package root).
// Returns an empty string when the target escapes the package.
inline std::string resolveTarget(const std::string& sourceNorm, const std::string& target) {
  std::string t = percentDecode(target);
  for (char& c : t) {
    if (c == '\\') c = '/';
  }
  const size_t hash = t.find('#');
  if (hash != std::string::npos) t.erase(hash);
  std::string joined;
  if (!t.empty() && t[0] == '/') {
    joined = t;
  } else {
    const std::string dir = partDirectory(sourceNorm);
    joined = dir.empty() ? t : dir + "/" + t;
  }
  for (char& c : joined) c = asciiLower(c);
  std::string out;
  if (!collapseDots(joined, out)) return std::string();
  return out;
}

// Name of the relationships part that describes `partNorm`:
//   "fb3/body.xml" -> "fb3/_rels/body.xml.rels"; "book" -> "_rels/book.rels".
inline std::string relationshipsPartFor(const std::string& partNorm) {
  const size_t slash = partNorm.find_last_of('/');
  if (slash == std::string::npos) return "_rels/" + partNorm + ".rels";
  return partNorm.substr(0, slash + 1) + "_rels/" + partNorm.substr(slash + 1) + ".rels";
}

// Characters allowed in an FB2-side id/anchor produced from a package-side id. Every
// other byte becomes "_XX" so distinct FB3 ids stay distinct.
inline std::string sanitizeId(const std::string& raw, size_t maxLen = 120) {
  static const char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(raw.size());
  for (const unsigned char c : raw) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('_');
      out.push_back(kHex[c >> 4]);
      out.push_back(kHex[c & 0x0F]);
    }
    if (out.size() >= maxLen) break;
  }
  return out;
}

}  // namespace opc
