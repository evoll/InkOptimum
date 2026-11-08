#pragma once

#include <string>
#include <vector>

// Multi-word dictionary lookup ("ice cream", "give up", "по сравнению с").
// Given the selected word followed by the words after it on the page, produces the phrases to try,
// longest first, so a phrase entry wins over the first word's own article.
namespace dict_phrase {

inline constexpr size_t MAX_PHRASE_WORDS = 3;

// words[0] is the selected word; the rest are the following words in reading order.
// Returns phrases of 2..maxWords words, longest first. Empty when there is nothing to combine.
inline std::vector<std::string> buildCandidates(const std::vector<std::string>& words,
                                                const size_t maxWords = MAX_PHRASE_WORDS) {
  std::vector<std::string> candidates;
  if (words.size() < 2 || words[0].empty()) return candidates;

  size_t usable = 1;
  while (usable < words.size() && !words[usable].empty()) ++usable;
  const size_t limit = usable < maxWords ? usable : maxWords;

  for (size_t count = limit; count >= 2; --count) {
    std::string phrase = words[0];
    for (size_t i = 1; i < count; ++i) {
      phrase += ' ';
      phrase += words[i];
    }
    candidates.push_back(std::move(phrase));
  }
  return candidates;
}

}  // namespace dict_phrase
