#pragma once
// Pure-logic helpers for the Library screen (tabs Books / Recent / New / Authors).
// No Arduino/SD dependencies so everything is covered by native tests.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_set>
#include <vector>

namespace library {

enum class Tab : uint8_t { Books = 0, Recent = 1, New = 2, Authors = 3 };
enum class View : uint8_t { Names = 0, Details = 1, Covers = 2 };

constexpr int TAB_COUNT = 4;
constexpr int VIEW_COUNT = 3;

inline Tab nextTab(Tab t, int dir) {
  return static_cast<Tab>((static_cast<int>(t) + dir + TAB_COUNT) % TAB_COUNT);
}
inline View nextView(View v) { return static_cast<View>((static_cast<int>(v) + 1) % VIEW_COUNT); }

struct Entry {
  std::string path;
  std::string title;
  std::string author;
  uint32_t size = 0;  // bytes
};

struct AuthorGroup {
  std::string author;
  int count = 0;
};

inline std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

inline std::string trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) b++;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
  return s.substr(b, e - b);
}

inline std::string baseName(const std::string& path) {
  const auto p = path.find_last_of('/');
  return p == std::string::npos ? path : path.substr(p + 1);
}

inline std::string stripExtension(const std::string& name) {
  const auto dot = name.find_last_of('.');
  if (dot == std::string::npos || dot == 0 || name.size() - dot > 8) return name;
  return name.substr(0, dot);
}

inline bool hasExtension(const std::string& name, const std::string& ext) {
  if (name.size() < ext.size()) return false;
  return lower(name.substr(name.size() - ext.size())) == ext;
}

// Formats shown in the library (images/firmware are not books).
inline bool isBookFileName(const std::string& name) {
  static const char* const exts[] = {".epub", ".fb2", ".fb3", ".docx", ".txt", ".md", ".xtc", ".xtch", ".mobi",
                                     ".azw", ".azw3", ".prc", ".rtf", ".html", ".htm", ".xhtml", ".zip"};
  for (const char* e : exts) {
    if (hasExtension(name, e)) return true;
  }
  return false;
}

inline std::string formatLabel(const std::string& name) {
  const auto dot = name.find_last_of('.');
  if (dot == std::string::npos) return "";
  std::string e = name.substr(dot + 1);
  for (auto& c : e) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return e;
}

// "Author - Title.ext" or "Title (Author).ext"; otherwise the whole stem is the title.
inline void parseFileName(const std::string& fileName, std::string& title, std::string& author) {
  const std::string stem = trim(stripExtension(baseName(fileName)));
  author.clear();
  title = stem;
  const auto dash = stem.find(" - ");
  if (dash != std::string::npos && dash > 0 && dash + 3 < stem.size()) {
    author = trim(stem.substr(0, dash));
    title = trim(stem.substr(dash + 3));
    return;
  }
  if (!stem.empty() && stem.back() == ')') {
    const auto open = stem.rfind(" (");
    if (open != std::string::npos && open > 0 && open + 2 < stem.size() - 1) {
      author = trim(stem.substr(open + 2, stem.size() - open - 3));
      title = trim(stem.substr(0, open));
    }
  }
}

inline std::string humanSize(uint32_t bytes) {
  char buf[24];
  if (bytes >= 1048576U) {
    std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / 1048576.0);
  } else {
    std::snprintf(buf, sizeof(buf), "%u KB", static_cast<unsigned>(bytes / 1024U));
  }
  return buf;
}

inline void sortByTitle(std::vector<Entry>& v) {
  std::sort(v.begin(), v.end(), [](const Entry& a, const Entry& b) {
    const std::string la = lower(a.title), lb = lower(b.title);
    return la != lb ? la < lb : a.path < b.path;
  });
}

// Books that are not in the recent list (never opened through the reader).
inline std::vector<Entry> newBooks(const std::vector<Entry>& all, const std::vector<std::string>& recentPaths) {
  std::unordered_set<std::string> seen(recentPaths.begin(), recentPaths.end());
  std::vector<Entry> out;
  for (const auto& e : all) {
    if (!seen.count(e.path)) out.push_back(e);
  }
  return out;
}

// Authors sorted case-insensitively; books without an author are collected under "" (shown last).
inline std::vector<AuthorGroup> groupByAuthor(const std::vector<Entry>& all) {
  std::vector<AuthorGroup> groups;
  for (const auto& e : all) {
    const std::string key = lower(e.author);
    auto it = std::find_if(groups.begin(), groups.end(), [&](const AuthorGroup& g) { return lower(g.author) == key; });
    if (it == groups.end()) {
      groups.push_back({e.author, 1});
    } else {
      it->count++;
    }
  }
  std::sort(groups.begin(), groups.end(), [](const AuthorGroup& a, const AuthorGroup& b) {
    if (a.author.empty() != b.author.empty()) return !a.author.empty();
    return lower(a.author) < lower(b.author);
  });
  return groups;
}

inline std::vector<Entry> booksOfAuthor(const std::vector<Entry>& all, const std::string& author) {
  const std::string key = lower(author);
  std::vector<Entry> out;
  for (const auto& e : all) {
    if (lower(e.author) == key) out.push_back(e);
  }
  sortByTitle(out);
  return out;
}

// ---- Index (cache) serialization: one book per line, tab-separated: size \t path \t title \t author ----

inline std::string sanitizeField(const std::string& s) {
  std::string o = s;
  for (auto& c : o) {
    if (c == '\t' || c == '\n' || c == '\r') c = ' ';
  }
  return o;
}

inline std::string indexHeader(uint32_t signature) { return "INKLIB1\t" + std::to_string(signature) + "\n"; }

inline std::string indexLine(const Entry& e) {
  return std::to_string(e.size) + "\t" + sanitizeField(e.path) + "\t" + sanitizeField(e.title) + "\t" +
         sanitizeField(e.author) + "\n";
}

// Header line without the trailing newline. False when it is not an index header.
inline bool parseIndexHeader(const std::string& line, uint32_t& signature) {
  if (line.compare(0, 8, "INKLIB1\t") != 0) return false;
  signature = static_cast<uint32_t>(std::strtoul(line.substr(8).c_str(), nullptr, 10));
  return true;
}

// One book line (without newline). False for malformed lines (they are skipped).
inline bool parseIndexLine(const std::string& line, Entry& e) {
  std::vector<std::string> f;
  size_t b = 0;
  while (true) {
    const size_t t = line.find('\t', b);
    if (t == std::string::npos) {
      f.push_back(line.substr(b));
      break;
    }
    f.push_back(line.substr(b, t - b));
    b = t + 1;
  }
  if (f.size() != 4 || f[1].empty()) return false;
  e.size = static_cast<uint32_t>(std::strtoul(f[0].c_str(), nullptr, 10));
  e.path = f[1];
  e.title = f[2];
  e.author = f[3];
  return true;
}

inline std::string serializeIndex(const std::vector<Entry>& all, uint32_t signature) {
  std::string out = indexHeader(signature);
  for (const auto& e : all) out += indexLine(e);
  return out;
}

inline bool parseIndex(const std::string& text, std::vector<Entry>& out, uint32_t& signature) {
  out.clear();
  size_t pos = 0;
  bool first = true;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(pos, end - pos);
    pos = end + 1;
    if (first) {
      if (!parseIndexHeader(line, signature)) return false;
      first = false;
      continue;
    }
    Entry e;
    if (parseIndexLine(line, e)) out.push_back(std::move(e));
  }
  return !first;
}

// Cheap rolling signature of a folder listing (names+sizes) so a cached count/index can be validated.
inline uint32_t mixSignature(uint32_t sig, const std::string& name, uint32_t size) {
  for (unsigned char c : name) sig = sig * 16777619u ^ c;
  return sig * 16777619u ^ size;
}

}  // namespace library
