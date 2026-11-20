// The generated string blobs address their strings with 2-byte-unit offsets (15 bits + "use English"
// flag), same decoding as I18n::get(). Every offset must land on the start of a NUL-terminated string.
#include <gtest/gtest.h>

#include <cstring>

#include "I18nKeys.h"
#include "I18nStrings.h"

using namespace i18n_strings;

static const char* decode(const Language lang, const size_t index) {
  const LangStrings ls = getLanguageStrings(lang);
  const uint16_t off = ls.offsets[index];
  if (off & 0x8000) return STRINGS_EN_DATA + (static_cast<size_t>(off & 0x7FFF) << 1);
  return ls.data + (static_cast<size_t>(off) << 1);
}

TEST(I18nBlob, EveryOffsetIsAStringStart) {
  for (uint8_t l = 0; l < static_cast<uint8_t>(Language::_COUNT); ++l) {
    for (size_t i = 0; i < static_cast<size_t>(StrId::_COUNT); ++i) {
      const char* s = decode(static_cast<Language>(l), i);
      const LangStrings ls = getLanguageStrings(static_cast<Language>(l));
      const char* base = (ls.offsets[i] & 0x8000) ? STRINGS_EN_DATA : ls.data;
      if (s != base) {
        EXPECT_EQ(*(s - 1), '\0') << "lang " << int(l) << " index " << i;
        EXPECT_EQ(static_cast<size_t>(s - base) % 2, 0u);
      }
    }
  }
}

TEST(I18nBlob, KnownStrings) {
  EXPECT_STREQ(decode(Language::EN, static_cast<size_t>(StrId::STR_NIGHT_MODE)), "Night Mode");
  EXPECT_STREQ(decode(Language::RU, static_cast<size_t>(StrId::STR_SEARCH_BOOK)), "Поиск по книге");
  EXPECT_STREQ(decode(Language::EN, static_cast<size_t>(StrId::STR_SEARCH_BOOK)), "Search in book");
  // Untranslated entries fall back to English.
  EXPECT_NE(std::strlen(decode(Language::JA, static_cast<size_t>(StrId::STR_SEARCH_BOOK))), 0u);
}
