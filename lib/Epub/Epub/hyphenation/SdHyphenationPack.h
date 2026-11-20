#pragma once

#include <string>

#include "LanguageHyphenator.h"

// Loads hyphenation packs for languages that are not built into the firmware.
// Call once at start-up; afterwards getLanguageHyphenatorForPrimaryTag() falls back
// to /.inkmod/hyphenation/hyph-<tag>.trie when a language has no built-in patterns.
namespace SdHyphenationPack {
void install();
// Returns nullptr when no valid pack exists (the miss is remembered for the session).
const LanguageHyphenator* find(const std::string& primaryTag);
}  // namespace SdHyphenationPack
