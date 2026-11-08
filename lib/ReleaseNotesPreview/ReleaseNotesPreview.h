#pragma once

#include <string>
#include <string_view>
#include <vector>

// Turns the beginning of a GitHub release body (Markdown) into a few short plain-text lines that
// fit an e-ink dialog. Platform-free so it can be unit-tested natively.
namespace release_notes {

inline std::string trim(std::string_view s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
  return std::string(s.substr(b, e - b));
}

// Removes Markdown decoration from one line. Returns an empty string for lines without text
// (blank lines, horizontal rules, code fences, "Full Changelog" link lines).
inline std::string plainLine(std::string_view raw) {
  std::string line = trim(raw);
  if (line.empty()) return {};
  if (line.rfind("```", 0) == 0 || line.rfind("---", 0) == 0 || line.rfind("***", 0) == 0 ||
      line.rfind("___", 0) == 0) {
    return {};
  }
  bool bullet = false;
  size_t i = 0;
  while (i < line.size() && line[i] == '#') ++i;  // heading marks
  if (i == line.size()) return {};                // only '#' characters
  if (i > 0 && i < line.size() && line[i] == ' ') {
    line = trim(std::string_view(line).substr(i));
  } else if ((line[0] == '-' || line[0] == '*' || line[0] == '+') && line.size() > 1 && line[1] == ' ') {
    bullet = true;
    line = trim(std::string_view(line).substr(2));
  }
  std::string out;
  out.reserve(line.size());
  for (size_t k = 0; k < line.size(); ++k) {
    const char c = line[k];
    if (c == '*' || c == '`' || (c == '_' && (k == 0 || k + 1 == line.size()))) continue;
    out.push_back(c);
  }
  out = trim(out);
  if (out.empty()) return {};
  return bullet ? "- " + out : out;
}

// Up to maxLines non-empty lines from the start of the notes. Lines are separated by '\n'.
inline std::vector<std::string> previewLines(std::string_view notes, size_t maxLines) {
  std::vector<std::string> lines;
  size_t pos = 0;
  while (pos <= notes.size() && lines.size() < maxLines) {
    size_t nl = notes.find('\n', pos);
    if (nl == std::string_view::npos) nl = notes.size();
    std::string line = plainLine(notes.substr(pos, nl - pos));
    if (!line.empty()) lines.push_back(std::move(line));
    if (nl == notes.size()) break;
    pos = nl + 1;
  }
  return lines;
}

}  // namespace release_notes
