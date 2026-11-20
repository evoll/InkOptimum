#pragma once

#include <cstddef>
#include <string>

#include "LanguageHyphenator.h"

struct LanguageEntry {
  const char* cliName;
  const char* primaryTag;
  const LanguageHyphenator* hyphenator;
};

struct LanguageEntryView {
  const LanguageEntry* data;
  size_t size;

  const LanguageEntry* begin() const { return data; }
  const LanguageEntry* end() const { return data + size; }
};

// Returns the Liang-backed hyphenator for a given primary language tag (e.g., "en", "fr").
const LanguageHyphenator* getLanguageHyphenatorForPrimaryTag(const std::string& primaryTag);

// Optional fallback consulted when a language has no built-in patterns (SD-card packs).
using ExternalHyphenatorFinder = const LanguageHyphenator* (*)(const std::string& primaryTag);
void setExternalHyphenatorFinder(ExternalHyphenatorFinder finder);

// Exposes the list of supported languages primarily for tooling/tests.
LanguageEntryView getLanguageEntries();
