#include <gtest/gtest.h>

#include "SyncProfile.h"

using namespace sync_profile;

TEST(SyncProfile, Name) {
  EXPECT_EQ(sanitizeName("  Home  "), "Home");
  EXPECT_EQ(sanitizeName(""), "");
  EXPECT_EQ(sanitizeName(std::string(25, 'a')), "");
  EXPECT_EQ(sanitizeName("a\nb"), "");
}

TEST(SyncProfile, Url) {
  std::string u;
  EXPECT_TRUE(normalizeServerUrl("https://kosync.example.com//", u));
  EXPECT_EQ(u, "https://kosync.example.com");
  EXPECT_TRUE(normalizeServerUrl("  ", u));
  EXPECT_EQ(u, "");
  EXPECT_FALSE(normalizeServerUrl("ftp://x", u));
  EXPECT_FALSE(normalizeServerUrl("http://a b", u));
}

TEST(SyncProfile, ListOps) {
  std::vector<Profile> l;
  EXPECT_EQ(nextIndex(l, ""), -1);
  ASSERT_TRUE(upsert(l, {"A", "http://a", "u", "p", ""}));
  ASSERT_TRUE(upsert(l, {"B", "", "u2", "p2", "X-Token: 1"}));
  ASSERT_TRUE(upsert(l, {"A", "http://a2", "u", "p", ""}));
  ASSERT_EQ(l.size(), 2u);
  EXPECT_EQ(l[0].serverUrl, "http://a2");
  EXPECT_EQ(nextIndex(l, "A"), 1);
  EXPECT_EQ(nextIndex(l, "B"), 0);
  EXPECT_EQ(nextIndex(l, "none"), 0);
  EXPECT_TRUE(remove(l, "A"));
  EXPECT_FALSE(remove(l, "A"));
  for (size_t i = 0; i < MAX_PROFILES; ++i) upsert(l, {std::to_string(i), "", "", "", ""});
  EXPECT_EQ(l.size(), MAX_PROFILES);
  EXPECT_FALSE(upsert(l, {"overflow", "", "", "", ""}));
}
