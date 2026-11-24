#include <gtest/gtest.h>

#include "CompanionAuth.h"

using namespace companion;

TEST(CompanionPairing, CodeIsSixDigitsAndConfirmsOnce) {
  Pairing p;
  ASSERT_TRUE(p.begin(42, 1000));
  EXPECT_EQ(p.code(), "000042");
  EXPECT_EQ(p.confirm("000042", 2000), Pairing::Result::Ok);
  EXPECT_EQ(p.confirm("000042", 2001), Pairing::Result::NoSession);
}

TEST(CompanionPairing, ExpiresAfterTwoMinutes) {
  Pairing p;
  p.begin(123456, 0);
  EXPECT_TRUE(p.active(119999));
  EXPECT_EQ(p.confirm("123456", Pairing::kCodeLifetimeMs), Pairing::Result::NoSession);
}

TEST(CompanionPairing, ThreeWrongCodesLockForFiveMinutes) {
  Pairing p;
  p.begin(111111, 0);
  EXPECT_EQ(p.confirm("000000", 10), Pairing::Result::Wrong);
  EXPECT_EQ(p.confirm("000001", 20), Pairing::Result::Wrong);
  EXPECT_EQ(p.confirm("000002", 30), Pairing::Result::Locked);
  EXPECT_FALSE(p.begin(111111, 40));
  EXPECT_EQ(p.confirm("111111", 40), Pairing::Result::Locked);
  EXPECT_FALSE(p.begin(5, 30 + Pairing::kLockoutMs - 1));
  EXPECT_TRUE(p.begin(5, 30 + Pairing::kLockoutMs));
}

TEST(CompanionPairing, ClockWrapIsHandled) {
  Pairing p;
  const uint32_t t = 0xFFFFFFFFu - 1000;
  p.begin(7, t);
  EXPECT_TRUE(p.active(t + 5000));  // wrapped past zero
  EXPECT_FALSE(p.active(t + Pairing::kCodeLifetimeMs + 1));
}

TEST(CompanionPairing, LengthMismatchIsWrong) {
  Pairing p;
  p.begin(123456, 0);
  EXPECT_EQ(p.confirm("12345", 1), Pairing::Result::Wrong);
  EXPECT_EQ(p.confirm("1234567", 2), Pairing::Result::Wrong);
}

TEST(CompanionTokens, IssueVerifyRevoke) {
  TokenStore s;
  const std::string raw(32, '\x07');
  const std::string tok = s.issue(raw, "Pixel\n 8");
  ASSERT_EQ(tok.size(), 64u);
  EXPECT_TRUE(s.verify(tok));
  EXPECT_FALSE(s.verify(std::string(64, '0')));
  EXPECT_FALSE(s.verify("zz"));
  EXPECT_EQ(s.tokens()[0].name, "Pixel 8");
  EXPECT_TRUE(s.revoke(s.tokens()[0].id));
  EXPECT_FALSE(s.verify(tok));
  EXPECT_EQ(s.issue("short", "x"), "");
}

TEST(CompanionTokens, OnlyTheHashIsStoredAndSurvivesSerialize) {
  TokenStore s;
  const std::string tok = s.issue(std::string(32, '\x11'), "Laptop");
  const std::string text = s.serialize();
  EXPECT_EQ(text.find(tok), std::string::npos);
  TokenStore t;
  EXPECT_EQ(t.load(text + "garbage line\nbad\tname\t00\n"), 1u);
  EXPECT_TRUE(t.verify(tok));
}

TEST(CompanionTokens, OldestIsDroppedWhenFull) {
  TokenStore s;
  std::string first;
  for (int i = 0; i < 9; i++) {
    const std::string tok = s.issue(std::string(32, static_cast<char>(i + 1)), "d");
    if (i == 0) first = tok;
  }
  EXPECT_EQ(s.tokens().size(), TokenStore::kMaxTokens);
  EXPECT_FALSE(s.verify(first));
}

TEST(CompanionTokens, BearerHeader) {
  EXPECT_EQ(bearerFrom("Bearer abc"), "abc");
  EXPECT_EQ(bearerFrom("Basic abc"), "");
  EXPECT_EQ(bearerFrom("Bearer "), "");
}
