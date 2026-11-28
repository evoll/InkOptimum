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

// Our own pack first, then the CPHY packs other firmware installs (CrossPoint, CrossInk, witchhunt-reader).
constexpr const char* kCphyDirs[] = {"/.inkmod/hyphenation", "/.crosspoint/hyphenation", "/.crossink/hyphenation"};

// Reads a whole file of at most maxSize bytes into a new buffer; null when absent, too big or short.
std::unique_ptr<uint8_t[]> readWhole(const std::string& path, const size_t minSize, const size_t maxSize,
                                     size_t& sizeOut) {
  if (!Storage.exists(path.c_str())) return nullptr;
  FsFile file;
  if (!Storage.openFileForRead("HYPH", path, file)) return nullptr;
  const size_t size = file.size();
  if (size < minSize || size > maxSize) {
    file.close();
    return nullptr;
  }
  std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[size]);
  if (!buf) {
    file.close();
    return nullptr;
  }
  const bool readOk = file.read(buf.get(), size) == static_cast<int>(size);
  file.close();
  if (!readOk) return nullptr;
  sizeOut = size;
  return buf;
}

bool loadPack(const std::string& tag, Loaded& out) {
  std::unique_ptr<uint8_t[]> buf;
  HyphenationPack::Parsed parsed;
  size_t size = 0;
  const std::string ownPath = std::string(kDir) + "/hyph-" + tag + ".trie";
  buf = readWhole(ownPath, HyphenationPack::kHeaderSize + 5, HyphenationPack::kMaxFileSize, size);
  if (buf && !HyphenationPack::parse(buf.get(), size, parsed)) {
    LOG_ERR("HYPH", "Invalid hyphenation pack %s", ownPath.c_str());
    buf.reset();
  }
  if (!buf) {
    for (const char* dir : kCphyDirs) {
      const std::string path = std::string(dir) + "/hyph-" + tag + ".cphyph";
      buf = readWhole(path, HyphenationPack::kCphyHeaderSize + 1,
                      HyphenationPack::kCphyHeaderSize + HyphenationPack::kCphyMaxPayload, size);
      if (!buf) continue;
      if (HyphenationPack::parseCphy(buf.get(), size, tag.c_str(), parsed)) break;
      LOG_ERR("HYPH", "Invalid CPHY hyphenation pack %s", path.c_str());
      buf.reset();
    }
  }
  if (!buf) return false;
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
