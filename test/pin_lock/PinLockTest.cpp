#include <PinLock.h>

#include "gtest/gtest.h"

namespace {
const std::string kSalt("0123456789abcdef");
}

TEST(PinLock, ValidPin) {
  EXPECT_TRUE(pinlock::validPin("1234"));
  EXPECT_TRUE(pinlock::validPin("12345678"));
  EXPECT_FALSE(pinlock::validPin("123"));
  EXPECT_FALSE(pinlock::validPin("123456789"));
  EXPECT_FALSE(pinlock::validPin("12a4"));
  EXPECT_FALSE(pinlock::validPin(""));
}

TEST(PinLock, RoundTrip) {
  const std::string rec = pinlock::makeRecord("4821", kSalt);
  ASSERT_FALSE(rec.empty());
  EXPECT_TRUE(pinlock::wellFormed(rec));
  EXPECT_TRUE(pinlock::verify(rec, "4821"));
  EXPECT_FALSE(pinlock::verify(rec, "4822"));
  EXPECT_FALSE(pinlock::verify(rec, "482"));
  EXPECT_EQ(rec.find("4821"), std::string::npos);  // the PIN itself is not in the record
}

TEST(PinLock, SaltChangesRecord) {
  EXPECT_NE(pinlock::makeRecord("4821", kSalt), pinlock::makeRecord("4821", std::string("fedcba9876543210")));
}

TEST(PinLock, RejectsBadInputs) {
  EXPECT_TRUE(pinlock::makeRecord("12", kSalt).empty());
  EXPECT_TRUE(pinlock::makeRecord("1234", "short").empty());
  EXPECT_FALSE(pinlock::wellFormed(""));
  EXPECT_FALSE(pinlock::wellFormed("v2:00:00"));
  EXPECT_FALSE(pinlock::wellFormed("v1:zz:zz"));
  EXPECT_FALSE(pinlock::verify("garbage", "1234"));
}

TEST(PinLock, Backoff) {
  EXPECT_EQ(pinlock::delaySeconds(0), 0u);
  EXPECT_EQ(pinlock::delaySeconds(4), 0u);
  EXPECT_EQ(pinlock::delaySeconds(5), 30u);
  EXPECT_EQ(pinlock::delaySeconds(6), 60u);
  EXPECT_EQ(pinlock::delaySeconds(9), 480u);
  EXPECT_EQ(pinlock::delaySeconds(11), 1920u);
  EXPECT_EQ(pinlock::delaySeconds(12), 3600u);
  EXPECT_EQ(pinlock::delaySeconds(1000), 3600u);
}
