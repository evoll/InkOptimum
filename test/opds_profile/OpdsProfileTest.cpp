#include <gtest/gtest.h>

#include "OpdsProfile.h"

using namespace opds_profile;

TEST(OpdsProfile, EmptyLineMeansNoHeader) {
  Header h;
  EXPECT_TRUE(parseHeaderLine("", h));
  EXPECT_TRUE(h.empty());
  EXPECT_TRUE(parseHeaderLine("   \t ", h));
  EXPECT_TRUE(h.empty());
}

TEST(OpdsProfile, ParsesAndTrims) {
  Header h;
  ASSERT_TRUE(parseHeaderLine("  X-Api-Token :  abc123  ", h));
  EXPECT_EQ(h.name, "X-Api-Token");
  EXPECT_EQ(h.value, "abc123");
  EXPECT_EQ(formatHeaderLine(h), "X-Api-Token: abc123");
}

TEST(OpdsProfile, ValueMayContainColons) {
  Header h;
  ASSERT_TRUE(parseHeaderLine("X-Url: https://example.org:8080/x", h));
  EXPECT_EQ(h.value, "https://example.org:8080/x");
}

TEST(OpdsProfile, EmptyValueAllowed) {
  Header h;
  ASSERT_TRUE(parseHeaderLine("X-Flag:", h));
  EXPECT_EQ(h.name, "X-Flag");
  EXPECT_EQ(h.value, "");
}

TEST(OpdsProfile, RejectsMalformed) {
  Header h;
  EXPECT_FALSE(parseHeaderLine("no colon here", h));
  EXPECT_FALSE(parseHeaderLine(": value", h));
  EXPECT_FALSE(parseHeaderLine("Bad Name: v", h));
  EXPECT_FALSE(parseHeaderLine("Bad/Name: v", h));
  EXPECT_TRUE(h.empty());
}

TEST(OpdsProfile, RejectsHeaderInjection) {
  Header h;
  EXPECT_FALSE(parseHeaderLine("X-A: v\r\nX-B: w", h));
  EXPECT_FALSE(parseHeaderLine("X-A: v\nX-B: w", h));
  EXPECT_FALSE(parseHeaderLine(std::string("X-A: v") + '\0' + "w", h));
}

TEST(OpdsProfile, RejectsReservedHeaders) {
  Header h;
  EXPECT_FALSE(parseHeaderLine("Host: evil.example", h));
  EXPECT_FALSE(parseHeaderLine("authorization: Basic xx", h));
  EXPECT_FALSE(parseHeaderLine("USER-AGENT: x", h));
  EXPECT_FALSE(parseHeaderLine("Range: bytes=0-", h));
  EXPECT_FALSE(parseHeaderLine("Cookie: a=b", h));
}

TEST(OpdsProfile, RejectsOversize) {
  Header h;
  EXPECT_FALSE(parseHeaderLine("X-A: " + std::string(MAX_HEADER_VALUE + 1, 'a'), h));
  EXPECT_FALSE(parseHeaderLine(std::string(MAX_HEADER_NAME + 1, 'a') + ": v", h));
  EXPECT_TRUE(parseHeaderLine("X-A: " + std::string(MAX_HEADER_VALUE, 'a'), h));
}

TEST(OpdsProfile, UserAgentSanitising) {
  EXPECT_EQ(sanitizeUserAgent("  KOReader/1.0  "), "KOReader/1.0");
  EXPECT_EQ(sanitizeUserAgent(""), "");
  EXPECT_EQ(sanitizeUserAgent("a\r\nHost: x"), "");
  EXPECT_EQ(sanitizeUserAgent(std::string(MAX_USER_AGENT + 1, 'a')), "");
  EXPECT_EQ(sanitizeUserAgent(KOREADER_USER_AGENT), KOREADER_USER_AGENT);
}

TEST(OpdsProfile, Utf8ValueAccepted) {
  Header h;
  ASSERT_TRUE(parseHeaderLine("X-Name: Андрей", h));
  EXPECT_EQ(h.value, "Андрей");
}

TEST(OpdsProfileDownloadFolder, NormalisesAndRejects) {
  using opds_profile::sanitizeDownloadFolder;
  EXPECT_EQ(sanitizeDownloadFolder("/Books/OPDS"), "/Books/OPDS");
  EXPECT_EQ(sanitizeDownloadFolder("  Books//OPDS/ "), "/Books/OPDS");
  EXPECT_EQ(sanitizeDownloadFolder(""), "");
  EXPECT_EQ(sanitizeDownloadFolder("/"), "");
  EXPECT_EQ(sanitizeDownloadFolder("/a/../b"), "");
  EXPECT_EQ(sanitizeDownloadFolder("/a/./b"), "");
  EXPECT_EQ(sanitizeDownloadFolder("C:\\x"), "");
  EXPECT_EQ(sanitizeDownloadFolder("/a\nb"), "");
  EXPECT_EQ(sanitizeDownloadFolder(std::string(80, 'x')), "");
  EXPECT_EQ(sanitizeDownloadFolder("/Книги"), "/Книги");
}
