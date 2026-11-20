#include <gtest/gtest.h>

#include <vector>

#include "Epub/hyphenation/HyphenationCommon.h"
#include "Epub/hyphenation/HyphenationPackFormat.h"
#include "Epub/hyphenation/LanguageRegistry.h"
#include "Epub/hyphenation/generated/hyph-en.trie.h"

namespace {

// Rebuilds the on-SD pack from the built-in English trie (same blob the pack script wraps).
std::vector<uint8_t> makePack(uint8_t script, uint8_t minPrefix, uint8_t minSuffix) {
  std::vector<uint8_t> pack = {'I', 'H', 'Y', 'P', script, minPrefix, minSuffix, 0};
  const uint32_t root = static_cast<uint32_t>(en_patterns.rootOffset + 4);
  pack.push_back(static_cast<uint8_t>(root >> 24));
  pack.push_back(static_cast<uint8_t>(root >> 16));
  pack.push_back(static_cast<uint8_t>(root >> 8));
  pack.push_back(static_cast<uint8_t>(root));
  pack.insert(pack.end(), en_patterns.data, en_patterns.data + en_patterns.size);
  return pack;
}

std::vector<CodepointInfo> cps(const char* w) { return collectCodepoints(w); }

}  // namespace

TEST(HyphenationPack, ParsesValidPack) {
  const auto pack = makePack(0, 3, 3);
  HyphenationPack::Parsed p;
  ASSERT_TRUE(HyphenationPack::parse(pack.data(), pack.size(), p));
  EXPECT_EQ(p.script, 0);
  EXPECT_EQ(p.minPrefix, 3);
  EXPECT_EQ(p.rootOffset, en_patterns.rootOffset);
  EXPECT_EQ(p.size, en_patterns.size);
}

TEST(HyphenationPack, RejectsBadMagicScriptAndRoot) {
  auto pack = makePack(0, 0, 0);
  HyphenationPack::Parsed p;
  auto bad = pack;
  bad[0] = 'X';
  EXPECT_FALSE(HyphenationPack::parse(bad.data(), bad.size(), p));
  bad = pack;
  bad[4] = 7;
  EXPECT_FALSE(HyphenationPack::parse(bad.data(), bad.size(), p));
  bad = pack;
  bad[8] = bad[9] = bad[10] = bad[11] = 0xFF;  // root beyond the blob
  EXPECT_FALSE(HyphenationPack::parse(bad.data(), bad.size(), p));
  EXPECT_FALSE(HyphenationPack::parse(pack.data(), 6, p));
  EXPECT_FALSE(HyphenationPack::parse(nullptr, 100, p));
}

TEST(HyphenationPack, PackedTrieBreaksLikeBuiltIn) {
  const auto pack = makePack(0, 3, 3);
  HyphenationPack::Parsed p;
  ASSERT_TRUE(HyphenationPack::parse(pack.data(), pack.size(), p));
  const SerializedHyphenationPatterns patterns{p.rootOffset, p.data, p.size};
  LanguageHyphenator fromPack(patterns, isLatinLetter, toLowerLatin, p.minPrefix, p.minSuffix);
  const auto* builtIn = getLanguageHyphenatorForPrimaryTag("en");
  ASSERT_NE(builtIn, nullptr);
  for (const char* w : {"international", "hyphenation", "typography", "extraordinary"}) {
    EXPECT_EQ(fromPack.breakIndexes(cps(w)), builtIn->breakIndexes(cps(w))) << w;
  }
}

TEST(HyphenationPack, ExternalFinderIsConsultedOnlyForUnknownLanguages) {
  static int calls = 0;
  calls = 0;
  setExternalHyphenatorFinder([](const std::string&) -> const LanguageHyphenator* {
    ++calls;
    return nullptr;
  });
  EXPECT_NE(getLanguageHyphenatorForPrimaryTag("en"), nullptr);
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(getLanguageHyphenatorForPrimaryTag("nl"), nullptr);
  EXPECT_EQ(calls, 1);
  setExternalHyphenatorFinder(nullptr);
}
