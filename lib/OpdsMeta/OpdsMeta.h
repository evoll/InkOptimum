#pragma once
// Extended book metadata found in OPDS/Dublin Core feeds: ISBN, ASIN and series.
// Pure C++ so the parsing rules are covered by native tests.
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace opds_meta {

enum class IdKind { None, Isbn, Asin };

struct Identifier {
  IdKind kind = IdKind::None;
  std::string value;  // ISBN: digits (+X) without hyphens; ASIN: 10 upper-case chars
};

inline std::string lowerAscii(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

inline bool isbn10Valid(const std::string& d) {
  if (d.size() != 10) return false;
  int sum = 0;
  for (int i = 0; i < 10; ++i) {
    int v;
    if (d[i] == 'X' && i == 9) v = 10;
    else if (std::isdigit(static_cast<unsigned char>(d[i]))) v = d[i] - '0';
    else return false;
    sum += v * (10 - i);
  }
  return sum % 11 == 0;
}

inline bool isbn13Valid(const std::string& d) {
  if (d.size() != 13) return false;
  int sum = 0;
  for (int i = 0; i < 13; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(d[i]))) return false;
    sum += (d[i] - '0') * (i % 2 == 0 ? 1 : 3);
  }
  return sum % 10 == 0;
}

// ASIN: 10 chars; for books it is the ISBN-10, otherwise "B0" + 8 alphanumerics.
inline bool asinValid(const std::string& a) {
  if (a.size() != 10) return false;
  for (const char c : a) {
    if (!std::isalnum(static_cast<unsigned char>(c))) return false;
  }
  return a[0] == 'B' || isbn10Valid(a);
}

// Accepts "urn:isbn:978-...", "isbn:...", "urn:asin:B00...", "amazon:B00...", or a bare ISBN.
inline Identifier parseIdentifier(const std::string& raw) {
  std::string s;
  for (const char c : raw) {
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r') s += c;
  }
  const std::string low = lowerAscii(s);
  auto after = [&](const char* prefix) -> std::string {
    const std::string p(prefix);
    return low.compare(0, p.size(), p) == 0 ? s.substr(p.size()) : std::string();
  };

  std::string body = after("urn:isbn:");
  if (body.empty()) body = after("isbn:");
  bool isbnHint = !body.empty();

  std::string asinBody = after("urn:asin:");
  if (asinBody.empty()) asinBody = after("asin:");
  if (asinBody.empty()) asinBody = after("urn:amazon:");
  if (asinBody.empty()) asinBody = after("amazon:");
  if (!asinBody.empty()) {
    for (auto& c : asinBody) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (asinValid(asinBody)) return {IdKind::Asin, asinBody};
    return {};
  }

  if (!isbnHint) {
    // Bare value: only accept something that is clearly an ISBN.
    body = s;
  }
  std::string digits;
  for (const char c : body) {
    if (c == '-') continue;
    digits += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  if (isbn13Valid(digits) || isbn10Valid(digits)) return {IdKind::Isbn, digits};
  return {};
}

// "Name" + optional index text ("3", "3.0", "2.5") -> "Name #3" / "Name #2.5".
inline std::string formatSeries(const std::string& name, const std::string& index) {
  std::string n = name;
  while (!n.empty() && std::isspace(static_cast<unsigned char>(n.back()))) n.pop_back();
  size_t b = 0;
  while (b < n.size() && std::isspace(static_cast<unsigned char>(n[b]))) ++b;
  n = n.substr(b);
  if (n.empty()) return "";
  if (index.empty()) return n;
  char* end = nullptr;
  const double v = std::strtod(index.c_str(), &end);
  if (end == index.c_str() || v <= 0) return n;
  char buf[24];
  if (v == static_cast<long>(v)) snprintf(buf, sizeof(buf), " #%ld", static_cast<long>(v));
  else snprintf(buf, sizeof(buf), " #%.1f", v);
  return n + buf;
}

}  // namespace opds_meta
