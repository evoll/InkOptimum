#pragma once
// "Did you mean" without a dictionary format change: the on-device index is hash-based, so instead of scanning
// words we generate the spelling variants one edit away from the missed word and let the caller test each in the
// index. Platform-free; works on UTF-8 (Latin and Cyrillic alphabets).

#include <cstdint>
#include <string>
#include <vector>

namespace dict_suggest {

namespace detail {

inline void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

inline bool decode(const std::string& s, std::vector<uint32_t>& out) {
  out.clear();
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    uint32_t cp;
    size_t len;
    if (c < 0x80) {
      cp = c;
      len = 1;
    } else if ((c & 0xE0) == 0xC0) {
      cp = c & 0x1F;
      len = 2;
    } else if ((c & 0xF0) == 0xE0) {
      cp = c & 0x0F;
      len = 3;
    } else {
      return false;  // 4-byte and malformed sequences: no suggestions
    }
    if (i + len > s.size()) return false;
    for (size_t k = 1; k < len; ++k) {
      const unsigned char cc = static_cast<unsigned char>(s[i + k]);
      if ((cc & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (cc & 0x3F);
    }
    out.push_back(cp);
    i += len;
  }
  return true;
}

inline uint32_t lower(uint32_t cp) {
  if (cp >= 'A' && cp <= 'Z') return cp + 32;
  if (cp >= 0x0410 && cp <= 0x042F) return cp + 32;
  if (cp == 0x0401) return 0x0451;
  return cp;
}

inline bool isCyrillic(uint32_t cp) { return (cp >= 0x0430 && cp <= 0x044F) || cp == 0x0451; }
inline bool isLatinLetter(uint32_t cp) { return cp >= 'a' && cp <= 'z'; }

inline std::string encode(const std::vector<uint32_t>& cps) {
  std::string s;
  for (const uint32_t cp : cps) appendUtf8(s, cp);
  return s;
}

}  // namespace detail

// Spelling variants one edit from `word` (lowercased), most likely first: swapped neighbours, a dropped letter, a
// substituted letter, an inserted letter. The word itself is never returned, nor duplicates. Words under 3 letters,
// with digits/spaces/other scripts, or over 24 letters give nothing. At most `maxCandidates` are returned.
inline std::vector<std::string> editCandidates(const std::string& word, size_t maxCandidates = 700) {
  std::vector<std::string> result;
  std::vector<uint32_t> cps;
  if (!detail::decode(word, cps) || cps.size() < 3 || cps.size() > 24) return result;
  bool cyr = false, lat = false;
  for (auto& cp : cps) {
    cp = detail::lower(cp);
    if (detail::isCyrillic(cp)) {
      cyr = true;
    } else if (detail::isLatinLetter(cp)) {
      lat = true;
    } else if (cp != '-' && cp != '\'') {
      return result;
    }
  }
  if (cyr == lat) return result;  // mixed or no letters

  std::vector<uint32_t> alphabet;
  if (cyr) {
    for (uint32_t c = 0x0430; c <= 0x044F; ++c) alphabet.push_back(c);
    alphabet.push_back(0x0451);
  } else {
    for (uint32_t c = 'a'; c <= 'z'; ++c) alphabet.push_back(c);
  }
  const std::string original = detail::encode(cps);
  std::vector<std::string> seen;
  const auto add = [&](const std::vector<uint32_t>& v) {
    if (result.size() >= maxCandidates || v.size() < 2) return;
    std::string s = detail::encode(v);
    if (s == original) return;
    for (const auto& e : result) {
      if (e == s) return;
    }
    result.push_back(std::move(s));
  };

  const size_t n = cps.size();
  for (size_t i = 0; i + 1 < n; ++i) {  // transposition
    auto v = cps;
    std::swap(v[i], v[i + 1]);
    add(v);
  }
  for (size_t i = 0; i < n; ++i) {  // deletion
    auto v = cps;
    v.erase(v.begin() + static_cast<long>(i));
    add(v);
  }
  for (size_t i = 0; i < n; ++i) {  // substitution
    for (const uint32_t c : alphabet) {
      if (c == cps[i]) continue;
      auto v = cps;
      v[i] = c;
      add(v);
    }
  }
  for (size_t i = 0; i <= n; ++i) {  // insertion
    for (const uint32_t c : alphabet) {
      auto v = cps;
      v.insert(v.begin() + static_cast<long>(i), c);
      add(v);
    }
  }
  return result;
}

}  // namespace dict_suggest
