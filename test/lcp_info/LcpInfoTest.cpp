#include <LcpInfo.h>
#include <gtest/gtest.h>

#include <cstring>

using namespace lcpinfo;

namespace {
const char* LICENSE =
    "{\"id\":\"urn:uuid:1\",\"provider\":\"https://lib.example\",\"encryption\":{\"profile\":\"http://readium.org/lcp/"
    "basic-profile\",\"user_key\":{\"text_hint\":\"colour?\"}},\"rights\":{\"end\":\"2026-03-01T10:00:00Z\"},"
    "\"links\":[{\"rel\":\"hint\",\"href\":\"h\"},{\"rel\":[\"x\",\"status\"],\"href\":\"https://lib.example/lsd/1\"}]}";
const char* STATUS =
    "{\"id\":\"urn:uuid:1\",\"status\":\"active\",\"potential_rights\":{\"end\":\"2026-04-01T00:00:00Z\"},"
    "\"links\":[{\"rel\":\"license\",\"href\":\"/lic/1\"},{\"rel\":\"return\",\"href\":\"https://lib.example/"
    "return/1{?end,id,name}\",\"templated\":true},{\"rel\":\"renew\",\"href\":\"https://lib.example/renew/1\"}]}";
}  // namespace

TEST(LcpInfo, ParsesLicense) {
  LicenseInfo l;
  ASSERT_TRUE(parseLicense(LICENSE, strlen(LICENSE), &l));
  EXPECT_EQ(l.id, "urn:uuid:1");
  EXPECT_EQ(l.provider, "https://lib.example");
  EXPECT_EQ(l.hint, "colour?");
  EXPECT_EQ(l.statusHref, "https://lib.example/lsd/1");
  EXPECT_EQ(l.rightsEnd, 1772359200);
}

TEST(LcpInfo, RejectsGarbage) {
  LicenseInfo l;
  EXPECT_FALSE(parseLicense("nope", 4, &l));
  EXPECT_FALSE(parseLicense("{}", 2, &l));
  StatusInfo s;
  EXPECT_FALSE(parseStatus("{}", 2, &s));
}

TEST(LcpInfo, ParsesStatusLinks) {
  StatusInfo s;
  ASSERT_TRUE(parseStatus(STATUS, strlen(STATUS), &s));
  EXPECT_EQ(s.status, "active");
  EXPECT_EQ(s.licenseHref, "/lic/1");
  EXPECT_TRUE(s.returnTemplated);
  EXPECT_EQ(s.renewHref, "https://lib.example/renew/1");
  EXPECT_EQ(s.potentialEnd, 1775001600);
}

TEST(LcpInfo, ExpandsReturnTemplate) {
  EXPECT_EQ(expandReturnUrl("https://x/r/1{?end,id,name}", true, "dev 1", "InkOptimum"),
            "https://x/r/1?id=dev%201&name=InkOptimum");
  EXPECT_EQ(expandReturnUrl("https://x/r/1?a=b{?id}", true, "d", ""), "https://x/r/1?a=b&id=d");
  EXPECT_EQ(expandReturnUrl("https://x/r/1", false, "d", "n"), "https://x/r/1");
  EXPECT_EQ(expandReturnUrl("https://x/r/1{?end}", true, "d", "n"), "https://x/r/1");
}

TEST(LcpInfo, ResolvesLinks) {
  EXPECT_EQ(resolveLink("https://a.example/x/y.json?q=1", "/lic/1"), "https://a.example/lic/1");
  EXPECT_EQ(resolveLink("https://a.example/x/y.json", "z"), "https://a.example/x/z");
  EXPECT_EQ(resolveLink("https://a.example/x/y.json", "https://b.example/q"), "https://b.example/q");
}

TEST(LcpInfo, LoanStateFailsClosedWithoutClock) {
  EXPECT_EQ(loanState(0, 5), LoanState::NoExpiry);
  EXPECT_EQ(loanState(100, 50), LoanState::Active);
  EXPECT_EQ(loanState(100, 150), LoanState::Expired);
  EXPECT_EQ(loanState(100, 0), LoanState::ClockUnknown);
}

TEST(LcpInfo, FormatsDates) {
  EXPECT_EQ(formatDate(1772359200), "2026-03-01");
  EXPECT_EQ(formatDate(951782400), "2000-02-29");
  EXPECT_EQ(formatDate(0), "");
}
