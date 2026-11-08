#include <DownloadSniff.h>
#include <gtest/gtest.h>

#include <cstring>

namespace {
DownloadedBookKind sniff(const char* s) { return sniffBookBytes(reinterpret_cast<const uint8_t*>(s), static_cast<int>(strlen(s))); }
}  // namespace

TEST(DownloadSniff, RecognisesZipSignatures) {
  const uint8_t local[4] = {'P', 'K', 3, 4};
  const uint8_t empty[4] = {'P', 'K', 5, 6};
  const uint8_t spanned[4] = {'P', 'K', 7, 8};
  EXPECT_EQ(sniffBookBytes(local, 4), DownloadedBookKind::ZIP);
  EXPECT_EQ(sniffBookBytes(empty, 4), DownloadedBookKind::ZIP);
  EXPECT_EQ(sniffBookBytes(spanned, 4), DownloadedBookKind::ZIP);
}

// A fulfillment service that answers 200 with an error page must not pass for an EPUB.
TEST(DownloadSniff, ErrorBodiesAreNotBooks) {
  EXPECT_EQ(sniff("<html><body>502 Bad Gateway</body></html>"), DownloadedBookKind::UNKNOWN);
  EXPECT_EQ(sniff("{\"error\":\"unsupported profile\"}"), DownloadedBookKind::UNKNOWN);
  EXPECT_EQ(sniff("PK"), DownloadedBookKind::UNKNOWN);
  EXPECT_EQ(sniffBookBytes(nullptr, 100), DownloadedBookKind::UNKNOWN);
}

TEST(DownloadSniff, OtherContainers) {
  EXPECT_EQ(sniff("{\\rtf1\\ansi"), DownloadedBookKind::RTF);
  EXPECT_EQ(sniff("\xEF\xBB\xBF<?xml version=\"1.0\"?>\n<FictionBook xmlns=\"x\">"), DownloadedBookKind::FB2_XML);
  uint8_t mobi[80] = {};
  memcpy(mobi + 60, "BOOKMOBI", 8);
  EXPECT_EQ(sniffBookBytes(mobi, 80), DownloadedBookKind::MOBI);
}
