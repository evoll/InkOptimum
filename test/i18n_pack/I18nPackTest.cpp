#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "I18nPack.h"

namespace {
constexpr uint32_t kHash = 0xCDC06FDEu;

void putU32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}

// Three strings, the middle one untranslated.
std::vector<uint8_t> makePack(uint32_t hash = kHash, uint16_t count = 3) {
  std::vector<uint8_t> v = {'I', 'L', 'N', 'G', 1, 0};
  v.push_back(count & 0xFF);
  v.push_back(count >> 8);
  putU32(v, hash);
  const std::string name = "Deutsch";
  v.push_back(static_cast<uint8_t>(name.size()));
  v.push_back(0);
  v.push_back(0);
  v.push_back(0);
  v.insert(v.end(), name.begin(), name.end());
  putU32(v, 0);
  putU32(v, I18nPack::kMissing);
  putU32(v, 6);
  const char data[] = "Hallo\0Welt";
  v.insert(v.end(), data, data + sizeof(data));  // includes the final NUL
  return v;
}
}  // namespace

TEST(I18nPack, ParsesAndLooksUpStrings) {
  const auto pack = makePack();
  I18nPack::Parsed p;
  ASSERT_TRUE(I18nPack::parse(pack.data(), pack.size(), 3, kHash, p));
  EXPECT_EQ(std::string(p.name, p.nameLength), "Deutsch");
  EXPECT_STREQ(I18nPack::get(p, 0), "Hallo");
  EXPECT_EQ(I18nPack::get(p, 1), nullptr);  // falls back to English
  EXPECT_STREQ(I18nPack::get(p, 2), "Welt");
  EXPECT_EQ(I18nPack::get(p, 3), nullptr);
}

TEST(I18nPack, RejectsWrongFirmwareOrCount) {
  I18nPack::Parsed p;
  auto pack = makePack(kHash + 1);
  EXPECT_FALSE(I18nPack::parse(pack.data(), pack.size(), 3, kHash, p));
  pack = makePack();
  EXPECT_FALSE(I18nPack::parse(pack.data(), pack.size(), 4, kHash, p));
}

TEST(I18nPack, RejectsDamagedFiles) {
  I18nPack::Parsed p;
  auto pack = makePack();
  auto bad = pack;
  bad[0] = 'X';
  EXPECT_FALSE(I18nPack::parse(bad.data(), bad.size(), 3, kHash, p));
  bad = pack;
  bad[4] = 2;  // version
  EXPECT_FALSE(I18nPack::parse(bad.data(), bad.size(), 3, kHash, p));
  bad = pack;
  bad.back() = 'x';  // data not NUL-terminated
  EXPECT_FALSE(I18nPack::parse(bad.data(), bad.size(), 3, kHash, p));
  bad = pack;
  const size_t offsetsStart = I18nPack::kHeaderSize + 7;
  bad[offsetsStart + 8] = 0xFE;  // offset beyond the data
  EXPECT_FALSE(I18nPack::parse(bad.data(), bad.size(), 3, kHash, p));
  EXPECT_FALSE(I18nPack::parse(pack.data(), 10, 3, kHash, p));
  EXPECT_FALSE(I18nPack::parse(nullptr, 100, 3, kHash, p));
}
