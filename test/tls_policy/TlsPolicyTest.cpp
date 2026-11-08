#include <gtest/gtest.h>

#include "TlsPolicy.h"

using namespace tlspolicy;

namespace {
std::string fp(char c) { return std::string(64, c); }
}  // namespace

TEST(TlsPolicy, HostKeyIsCaseInsensitiveAndKeepsThePort) {
  EXPECT_EQ(hostKey("Sync.Example.COM", 443), "sync.example.com:443");
  EXPECT_NE(hostKey("a", 443), hostKey("a", 8443));
}

TEST(TlsPolicy, HexRoundTrip) {
  uint8_t raw[32];
  for (int i = 0; i < 32; i++) raw[i] = static_cast<uint8_t>(i * 7);
  const std::string hex = toHex(raw, 32);
  ASSERT_EQ(hex.size(), 64u);
  uint8_t back[32];
  ASSERT_TRUE(fromHex(hex, back, 32));
  EXPECT_EQ(memcmp(raw, back, 32), 0);
  EXPECT_FALSE(fromHex(hex.substr(1), back, 32));
  EXPECT_FALSE(fromHex(std::string(64, 'g'), back, 32));
}

TEST(TlsPolicy, FirstContactPinsAndLaterContactsMustMatch) {
  PinStore store;
  EXPECT_EQ(decideTofu(store, "h:443", fp('a')), Decision::AcceptAndSave);
  EXPECT_TRUE(store.changed());
  EXPECT_EQ(decideTofu(store, "h:443", fp('a')), Decision::Accept);
  EXPECT_EQ(decideTofu(store, "h:443", fp('b')), Decision::Reject);
  // A refused certificate must not replace the pin.
  EXPECT_EQ(store.check("h:443", fp('a')), Verdict::Match);
  EXPECT_EQ(decideTofu(store, "other:443", fp('b')), Decision::AcceptAndSave);
}

TEST(TlsPolicy, AnUnreadableCertificateIsAcceptedWithoutPinning) {
  PinStore store;
  EXPECT_EQ(decideTofu(store, "h:443", ""), Decision::Accept);
  EXPECT_EQ(store.size(), 0u);
}

TEST(TlsPolicy, ForgettingAllowsANewCertificate) {
  PinStore store;
  decideTofu(store, "h:443", fp('a'));
  EXPECT_TRUE(store.forget("h:443"));
  EXPECT_FALSE(store.forget("h:443"));
  EXPECT_EQ(decideTofu(store, "h:443", fp('b')), Decision::AcceptAndSave);
}

TEST(TlsPolicy, StoreSurvivesSerialisationAndDropsBadEntries) {
  PinStore store;
  store.set("a:443", fp('1'));
  store.set("b:443", fp('2'));
  PinStore copy;
  ASSERT_TRUE(copy.load(store.toJson()));
  EXPECT_EQ(copy.size(), 2u);
  EXPECT_EQ(copy.check("b:443", fp('2')), Verdict::Match);
  EXPECT_FALSE(copy.changed());

  PinStore messy;
  ASSERT_TRUE(messy.load(R"({"pins":[{"h":"ok:1","fp":")" + std::string(64, 'A') + R"("},{"h":"bad:1","fp":"zz"},{"fp":")" +
                         fp('c') + R"("}]})"));
  EXPECT_EQ(messy.size(), 1u);
  EXPECT_EQ(messy.check("ok:1", fp('a')), Verdict::Match);  // upper case input is normalised
  EXPECT_FALSE(messy.load("not json"));
  EXPECT_EQ(messy.size(), 0u);
}

TEST(TlsPolicy, TheOldestPinGoesWhenFull) {
  PinStore store;
  for (size_t i = 0; i < MAX_PINS + 5; i++) store.set("h" + std::to_string(i) + ":1", fp('a'));
  EXPECT_EQ(store.size(), MAX_PINS);
  EXPECT_EQ(store.check("h0:1", fp('a')), Verdict::NewHost);
  EXPECT_EQ(store.check("h" + std::to_string(MAX_PINS + 4) + ":1", fp('a')), Verdict::Match);
}
