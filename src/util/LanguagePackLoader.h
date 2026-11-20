#pragma once

#include <string>
#include <vector>

// SD-card UI language packs (/.inkmod/languages/<code>.lang, built by scripts/make_language_pack.py).
namespace LanguagePackLoader {

struct PackInfo {
  std::string code;  // file stem: letters and digits only
  std::string name;  // display name stored in the pack
};

// Packs on the card that were built for this firmware (header checked, at most 8).
std::vector<PackInfo> scan();

// Loads /.inkmod/languages/<code>.lang into I18N. False keeps the current language.
bool load(const char* code);

}  // namespace LanguagePackLoader
