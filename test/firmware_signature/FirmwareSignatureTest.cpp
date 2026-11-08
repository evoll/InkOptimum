#include <FirmwareSignature.h>
#include <gtest/gtest.h>

#include <cstring>

// Vectors made with Python's `cryptography` (RFC 8032 Ed25519), seed = bytes 1..32, over the image
// "inkoptimum-test-image" x100, message "INKOPT-FW-v1\n" + SHA-256(image).
namespace {
const uint8_t kPub[32] = {0x79, 0xb5, 0x56, 0x2e, 0x8f, 0xe6, 0x54, 0xf9, 0x40, 0x78, 0xb1, 0x12, 0xe8, 0xa9, 0x8b, 0xa7, 0x90, 0x1f, 0x85, 0x3a, 0xe6, 0x95, 0xbe, 0xd7, 0xe0, 0xe3, 0x91, 0x0b, 0xad, 0x04, 0x96, 0x64};
const uint8_t kDigest[32] = {0x8a, 0xf5, 0xbd, 0x46, 0x0f, 0x23, 0xd3, 0xfc, 0x42, 0xf3, 0xb6, 0x5e, 0x47, 0x3e, 0xf7, 0x8e, 0x19, 0x9c, 0x2d, 0x5c, 0xf8, 0xbc, 0x7f, 0xf6, 0x74, 0x83, 0xb9, 0xab, 0xce, 0x60, 0xac, 0x11};
const uint8_t kSig[64] = {0x00, 0x1f, 0x1f, 0xb6, 0x6d, 0xa6, 0x14, 0x62, 0x1d, 0x46, 0xff, 0x5c, 0x91, 0xda, 0x04, 0xdc, 0x46, 0xc3, 0xc1, 0x5e, 0x89, 0xf6, 0xe7, 0xf3, 0x33, 0xef, 0x7a, 0x59, 0x75, 0xb9, 0xc2, 0x23, 0xb7, 0xae, 0xe9, 0xfa, 0x9b, 0xdf, 0xad, 0xc6, 0xaa, 0x6a, 0xea, 0xd9, 0x0d, 0x39, 0x65, 0xe5, 0x8d, 0xa2, 0x6a, 0x3a, 0xb1, 0x02, 0x16, 0x55, 0x6c, 0xce, 0x65, 0xc0, 0x4a, 0x8d, 0x5e, 0x0f};
}  // namespace

TEST(FirmwareSignature, AcceptsAGoodSignature) { EXPECT_TRUE(fwsig::verifyDigest(kPub, kDigest, kSig)); }

TEST(FirmwareSignature, RejectsAChangedDigest) {
  uint8_t d[32];
  std::memcpy(d, kDigest, 32);
  d[31] ^= 1;
  EXPECT_FALSE(fwsig::verifyDigest(kPub, d, kSig));
}

TEST(FirmwareSignature, RejectsAChangedSignature) {
  for (size_t i : {0u, 31u, 32u, 63u}) {
    uint8_t s[64];
    std::memcpy(s, kSig, 64);
    s[i] ^= 0x80;
    EXPECT_FALSE(fwsig::verifyDigest(kPub, kDigest, s)) << i;
  }
}

TEST(FirmwareSignature, RejectsAnotherKey) {
  uint8_t other[32];
  std::memcpy(other, kPub, 32);
  other[0] ^= 1;
  EXPECT_FALSE(fwsig::verifyDigest(other, kDigest, kSig));
}

TEST(FirmwareSignature, AnUnconfiguredKeyNeverVerifies) {
  const uint8_t zero[32] = {};
  EXPECT_FALSE(fwsig::keyConfigured(zero));
  EXPECT_TRUE(fwsig::keyConfigured(kPub));
  EXPECT_FALSE(fwsig::verifyDigest(zero, kDigest, kSig));
}

TEST(FirmwareSignature, MessageIsThePrefixThenTheDigest) {
  uint8_t m[fwsig::kMessageSize];
  fwsig::buildMessage(kDigest, m);
  EXPECT_EQ(fwsig::kMessageSize, 45u);
  EXPECT_EQ(std::memcmp(m, "INKOPT-FW-v1\n", 13), 0);
  EXPECT_EQ(std::memcmp(m + 13, kDigest, 32), 0);
}

TEST(FirmwareSignature, ADifferentDomainPrefixDoesNotVerify) {
  // The same digest signed under another project's prefix must not pass.
  uint8_t m[fwsig::kMessageSize];
  fwsig::buildMessage(kDigest, m);
  m[0] = 'X';
  EXPECT_FALSE(fwsig::verifyMessage(kPub, m, sizeof(m), kSig));
}
