#pragma once

#include <cstddef>
#include <cstdint>

namespace Dictionary {

constexpr char ROOT_PATH[] = "/.dictionaries";
constexpr char LEGACY_ROOT_PATH[] = "/Dictionaries";
constexpr uint8_t MAX_DICTIONARIES = 8;
constexpr size_t MAX_DICTIONARY_NAME_BYTES = 64;
// Longest root ("/.dictionaries", "/Dictionaries") + '/' + a 255-character FAT long file name + NUL.
constexpr size_t MAX_DICTIONARY_PATH_BYTES = 272;
// Folder levels searched below a dictionary root: 2 finds /.dictionaries/Oxford and /.dictionaries/English/Oxford.
constexpr uint8_t MAX_SCAN_DEPTH = 2;

struct CatalogEntry {
  char name[MAX_DICTIONARY_NAME_BYTES] = {};
  char path[MAX_DICTIONARY_PATH_BYTES] = {};
  uint32_t entryCount = 0;
};

// Reads browser-prepared InkMOD dictionaries. The on-device side never
// decompresses or builds an index: a lookup is a binary search over fixed
// fixed-size records followed by one bounded article read from the data file.
// Format v2 uses a second hash so browser-prepared StarDict synonyms can point
// directly to the canonical article without duplicating its text on the SD card.
class Store {
 public:
  bool scan();
  uint8_t count() const { return count_; }
  const CatalogEntry* entry(uint8_t index) const;

  bool lookup(uint8_t dictionaryIndex, const char* word, char* matchedWord, size_t matchedWordSize, char* article,
              size_t articleSize) const;

  static size_t normalizeWord(const char* input, char* output, size_t outputSize);

 private:
  enum class FolderKind : uint8_t { Added, NotADictionary, Rejected };
  FolderKind addDictionaryFolder(const char* rootPath, const char* folderPath, const char* folderName);
  void scanFolder(const char* rootPath, const char* folderPath, uint8_t depth);
  bool lookupExact(uint8_t dictionaryIndex, const char* word, char* matchedWord, size_t matchedWordSize, char* article,
                   size_t articleSize) const;

  CatalogEntry entries_[MAX_DICTIONARIES] = {};
  uint8_t count_ = 0;
};

}  // namespace Dictionary
