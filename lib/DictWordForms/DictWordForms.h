#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Fallback spellings to try when a dictionary has no entry for a word exactly as it was selected.
// Reader side only: the dictionary index and its hashes are untouched, so dictionaries already
// prepared on the card keep working.
//
//  1. Lowercase of letters the index fold does not cover (Latin Extended-A/B, the rest of
//     Cyrillic): "Łódź" and "Ґанок" at the start of a sentence find their lowercase headword.
//  2. French elision: "l'histoire", "qu’après", "d'histoires" are looked up by the word after the
//     apostrophe. The whole word is always tried first, so "aujourd'hui" and "d'accord" keep
//     their own entries, and other prefixes ("O'Brien") are left alone.
namespace dict_word_forms {

// Simple one-codepoint lowercase for the scripts the reader's fonts and dictionaries cover.
// Anything else, and any letter whose lowercase is not a single codepoint, comes back unchanged.
inline uint32_t lowerCodepoint(const uint32_t cp) {
  if (cp < 0x80) return (cp >= 'A' && cp <= 'Z') ? cp + 0x20 : cp;
  if ((cp >= 0x00C0 && cp <= 0x00DE) && cp != 0x00D7) return cp + 0x20;
  if (cp == 0x0130) return 'i';  // İ -> i, not the dotless ı after it
  if (cp == 0x0178) return 0x00FF;
  // Latin Extended-A pairs: even capitals U+0100..0137 and U+014A..0177, odd U+0139..0148 and U+0179..017E.
  if ((cp >= 0x0100 && cp <= 0x0137 && cp % 2 == 0) || (cp >= 0x0139 && cp <= 0x0148 && cp % 2 == 1) ||
      (cp >= 0x014A && cp <= 0x0177 && cp % 2 == 0) || (cp >= 0x0179 && cp <= 0x017E && cp % 2 == 1)) {
    return cp + 1;
  }
  switch (cp) {  // Vietnamese and Romanian letters of Latin Extended-B
    case 0x01A0:
    case 0x01AF:
    case 0x0218:
    case 0x021A:
      return cp + 1;
    default:
      break;
  }
  // Cyrillic: Ѐ..Џ sit 0x50 below their lowercase, А..Я 0x20; the extended blocks pair capital and
  // small letter in adjacent slots.
  if (cp >= 0x0400 && cp <= 0x040F) return cp + 0x50;
  if (cp >= 0x0410 && cp <= 0x042F) return cp + 0x20;
  if (((cp >= 0x0460 && cp <= 0x0481) || (cp >= 0x048A && cp <= 0x04BF) || (cp >= 0x04D0 && cp <= 0x052F)) &&
      cp % 2 == 0) {
    return cp + 1;
  }
  if (cp >= 0x04C1 && cp <= 0x04CE && cp % 2 == 1) return cp + 1;
  if (cp == 0x04C0) return 0x04CF;
  return cp;
}

namespace detail {

// Decodes one codepoint; a malformed byte comes back as itself so the text round-trips.
inline uint32_t decode(const std::string& s, size_t& pos) {
  const auto b0 = static_cast<uint8_t>(s[pos]);
  size_t extra = 0;
  uint32_t cp = b0;
  if (b0 >= 0xF0 && b0 < 0xF8) {
    extra = 3;
    cp = b0 & 0x07;
  } else if (b0 >= 0xE0) {
    extra = 2;
    cp = b0 & 0x0F;
  } else if (b0 >= 0xC0) {
    extra = 1;
    cp = b0 & 0x1F;
  }
  if (b0 >= 0xF8 || (b0 >= 0x80 && b0 < 0xC0) || pos + extra >= s.size() + (extra ? 0 : 1)) {
    ++pos;
    return b0;
  }
  for (size_t i = 1; i <= extra; ++i) {
    const auto b = static_cast<uint8_t>(s[pos + i]);
    if ((b & 0xC0) != 0x80) {
      ++pos;
      return b0;
    }
    cp = (cp << 6) | (b & 0x3F);
  }
  pos += extra + 1;
  return cp;
}

inline void append(std::string& out, const uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

}  // namespace detail

inline std::string lowerUtf8(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  size_t pos = 0;
  while (pos < s.size()) {
    const size_t start = pos;
    const uint32_t cp = detail::decode(s, pos);
    if (pos - start == 1 && cp >= 0x80) {
      out += s[start];  // malformed byte: keep it as it was
    } else {
      detail::append(out, lowerCodepoint(cp));
    }
  }
  return out;
}

// The word after a leading c' d' j' l' m' n' s' t' or qu' (apostrophe ' ’ or ʼ), matched without
// regard to case; empty when the word has no such prefix or nothing follows it.
inline std::string stripElision(const std::string& word) {
  size_t pos = 0;
  std::string prefix;
  while (pos < word.size() && prefix.size() < 3) {
    const size_t before = pos;
    const uint32_t cp = detail::decode(word, pos);
    if (cp == '\'' || cp == 0x2019 || cp == 0x02BC) {
      static const char* kPrefixes[] = {"c", "d", "j", "l", "m", "n", "s", "t", "qu"};
      for (const char* p : kPrefixes) {
        if (prefix == p) return pos < word.size() ? word.substr(pos) : std::string();
      }
      return std::string();
    }
    if (cp >= 0x80 || before == pos) return std::string();
    prefix += static_cast<char>(lowerCodepoint(cp));
  }
  return std::string();
}

// Spellings to try after `word` itself missed, in order, without duplicates.
inline std::vector<std::string> fallbackForms(const std::string& word) {
  std::vector<std::string> forms;
  const auto add = [&](const std::string& form) {
    if (form.empty() || form == word) return;
    for (const std::string& known : forms) {
      if (known == form) return;
    }
    forms.push_back(form);
  };
  const std::string lower = lowerUtf8(word);
  add(lower);
  const std::string bare = stripElision(word);
  add(bare);
  add(lowerUtf8(bare));
  return forms;
}

}  // namespace dict_word_forms
