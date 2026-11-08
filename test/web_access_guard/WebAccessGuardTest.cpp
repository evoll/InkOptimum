#include <gtest/gtest.h>

#include "WebAccessGuard/WebAccessGuard.h"

using web_access::isSecretPath;

TEST(WebAccessGuard, BlocksCredentialFiles) {
  for (const char* p : {"/.inkmod/wifi.json", "/.inkmod/wifi.bin", "/.inkmod/wifi.bin.bak", "/.inkmod/koreader.json",
                        "/.inkmod/koreader.bin", "/.inkmod/kosync_ext", "/.inkmod/opds.json", "/.inkmod/opds/x.json",
                        "/.inkmod/settings.json", "/.inkmod/inkmod-settings.json", "/.inkmod/lcp_device.json",
                        "/.inkmod/lcp_passphrases.json"}) {
    EXPECT_TRUE(isSecretPath(p)) << p;
  }
}

TEST(WebAccessGuard, CaseAndSeparatorVariantsAreBlocked) {
  EXPECT_TRUE(isSecretPath("/.INKMOD/WIFI.JSON"));
  EXPECT_TRUE(isSecretPath("\\.inkmod\\wifi.json"));
  EXPECT_TRUE(isSecretPath("//.inkmod//wifi.json"));
  EXPECT_TRUE(isSecretPath("/./.inkmod/./wifi.json"));
}

TEST(WebAccessGuard, TraversalIsResolved) {
  EXPECT_TRUE(isSecretPath("/books/../.inkmod/wifi.json"));
  EXPECT_TRUE(isSecretPath("/a/b/../../.inkmod/koreader.json"));
}

TEST(WebAccessGuard, TrailingDotsAndShortNamesAreBlocked) {
  EXPECT_TRUE(isSecretPath("/.inkmod./wifi.json"));
  EXPECT_TRUE(isSecretPath("/.inkmod/wifi.json."));
  EXPECT_TRUE(isSecretPath("/.inkmod/wifi.json "));
  EXPECT_TRUE(isSecretPath("/.inkmod/WIFI~1.JSO"));
}

TEST(WebAccessGuard, OrdinaryPathsAreNotSecret) {
  for (const char* p : {"/", "/books/wifi.epub", "/books/.inkmod/readme.txt/../x", "/.inkmod", "/.inkmod/bookmarks/a.bin",
                        "/.inkmod/recent.json", "/.dictionaries/x.ifo", "/Wifi guide.pdf", "/.inkmodx/wifi.json"}) {
    EXPECT_FALSE(isSecretPath(p)) << p;
  }
}
