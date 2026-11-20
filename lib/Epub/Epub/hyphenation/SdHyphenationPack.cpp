#include "SdHyphenationPack.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "HyphenationCommon.h"
#include "HyphenationPackFormat.h"
#include "LanguageRegistry.h"

namespace {
constexpr char kDir[] = "/.inkmod/hyphenation";
constexpr size_t kMaxLoaded = 2;  // books rarely mix more than two languages

struct Loaded {
  std::string tag;  // empty slot = unused; `hyphenator` null = remembered miss
  std::unique_ptr<uint8_t[]> buffer;
  SerializedHyphenationPatterns patterns{};
  std::unique_ptr<LanguageHyphenator> hyphenator;
};

std::vector<Loaded>& cache() {
  static std::vector<Loaded> c;
  return c;
}
std::vector<std::string>& misses() {
  static std::vector<std::string> m;
  return m;
}

bool loadPack(const std::string& tag, Loaded& out) {
  const std::string path = std::string(kDir) + "/hyph-" + tag + ".trie";
  if (!Storage.exists(path.c_str())) return false;
  FsFile file;
  if (!Storage.openFileForRead("HYPH", path, file)) return false;
  const size_t size = file.size();
  if (size < HyphenationPack::kHeaderSize + 5 || size > HyphenationPack::kMaxFileSize) {
    file.close();
    return false;
  }
  std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[size]);
  if (!buf) {
    file.close();
    return false;
  }
  const bool readOk = file.read(buf.get(), size) == static_cast<int>(size);
  file.close();
  HyphenationPack::Parsed parsed;
  if (!readOk || !HyphenationPack::parse(buf.get(), size, parsed)) {
    LOG_ERR("HYPH", "Invalid hyphenation pack %s", path.c_str());
    return false;
  }
  out.tag = tag;
  out.buffer = std::move(buf);
  out.patterns = {parsed.rootOffset, parsed.data, parsed.size};
  const size_t minPrefix = parsed.minPrefix ? parsed.minPrefix : LiangWordConfig::kDefaultMinPrefix;
  const size_t minSuffix = parsed.minSuffix ? parsed.minSuffix : LiangWordConfig::kDefaultMinSuffix;
  out.hyphenator.reset(parsed.script == 1 ? new LanguageHyphenator(out.patterns, isCyrillicLetter, toLowerCyrillic,
                                                                    minPrefix, minSuffix)
                                          : new LanguageHyphenator(out.patterns, isLatinLetter, toLowerLatin,
                                                                   minPrefix, minSuffix));
  return true;
}
}  // namespace

namespace SdHyphenationPack {

const LanguageHyphenator* find(const std::string& primaryTag) {
  if (primaryTag.empty() || primaryTag.size() > 8) return nullptr;
  for (const char c : primaryTag) {
    if (c < 'a' || c > 'z') return nullptr;  // tags are lower-cased by the caller; refuse path tricks
  }
  auto& c = cache();
  for (const auto& e : c) {
    if (e.tag == primaryTag) return e.hyphenator.get();
  }
  for (const auto& m : misses()) {
    if (m == primaryTag) return nullptr;
  }
  if (!Storage.ready()) return nullptr;
  Loaded fresh;
  if (!loadPack(primaryTag, fresh)) {
    misses().push_back(primaryTag);
    return nullptr;
  }
  if (c.size() >= kMaxLoaded) c.erase(c.begin());  // oldest pack out; frees its trie
  c.push_back(std::move(fresh));
  LOG_INF("HYPH", "Loaded SD hyphenation pack: %s", primaryTag.c_str());
  return c.back().hyphenator.get();
}

void install() { setExternalHyphenatorFinder(&find); }

}  // namespace SdHyphenationPack
