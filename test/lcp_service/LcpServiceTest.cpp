#include <Crypto.h>
#include <LcpLicense.h>
#include <LcpService.h>
#include <Util.h>
#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

#include <cstring>

using namespace freeink::content;

namespace {

// Host crypto with the three primitives the key unwrap uses (RFC 7748 raw keys, HKDF-SHA256 with no salt,
// AES-256-GCM), matching WolfsslCrypto on the device.
class HostCrypto : public Crypto {
 public:
  int32_t rsaPrivateRaw(const uint8_t*, size_t, const uint8_t*, size_t, uint8_t*, size_t) override { return -1; }
  bool aes128CbcDecrypt(const uint8_t*, const uint8_t*, const uint8_t*, size_t, uint8_t*) override { return false; }
  void sha1(const uint8_t*, size_t, uint8_t[20]) override {}
  void sha256(const uint8_t*, size_t, uint8_t[32]) override {}
  bool rsaGenerate(RsaKeyPairDer*) override { return false; }
  bool rsaPublicEncrypt(const uint8_t*, size_t, const uint8_t*, size_t, uint8_t*, size_t, size_t*) override {
    return false;
  }
  bool rsaPrivateSignRaw(const uint8_t*, size_t, const uint8_t[20], uint8_t[128]) override { return false; }
  bool aes128CbcEncrypt(const uint8_t*, const uint8_t*, const uint8_t*, size_t, uint8_t*) override { return false; }
  bool pkcs12Extract(const uint8_t*, size_t, const std::string&, std::vector<uint8_t>*,
                     std::vector<uint8_t>*) override {
    return false;
  }
  void randomBytes(uint8_t* out, size_t len) override { RAND_bytes(out, static_cast<int>(len)); }

  bool x25519SharedSecret(const uint8_t priv[32], const uint8_t peerPub[32], uint8_t out[32]) override {
    EVP_PKEY* a = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, priv, 32);
    EVP_PKEY* b = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, peerPub, 32);
    EVP_PKEY_CTX* c = EVP_PKEY_CTX_new(a, nullptr);
    size_t n = 32;
    const bool ok = EVP_PKEY_derive_init(c) == 1 && EVP_PKEY_derive_set_peer(c, b) == 1 &&
                    EVP_PKEY_derive(c, out, &n) == 1 && n == 32;
    EVP_PKEY_CTX_free(c);
    EVP_PKEY_free(a);
    EVP_PKEY_free(b);
    return ok;
  }
  bool hkdfSha256(const uint8_t* ikm, size_t ikmLen, const uint8_t* info, size_t infoLen, uint8_t* out,
                  size_t outLen) override {
    EVP_PKEY_CTX* c = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    size_t n = outLen;
    const uint8_t zeroSalt[32] = {0};
    const bool ok = EVP_PKEY_derive_init(c) == 1 && EVP_PKEY_CTX_set_hkdf_md(c, EVP_sha256()) == 1 &&
                    EVP_PKEY_CTX_set1_hkdf_salt(c, zeroSalt, 32) == 1 &&
                    EVP_PKEY_CTX_set1_hkdf_key(c, ikm, static_cast<int>(ikmLen)) == 1 &&
                    EVP_PKEY_CTX_add1_hkdf_info(c, info, static_cast<int>(infoLen)) == 1 &&
                    EVP_PKEY_derive(c, out, &n) == 1;
    EVP_PKEY_CTX_free(c);
    return ok;
  }
  bool aes256GcmDecrypt(const uint8_t key[32], const uint8_t iv[12], const uint8_t* in, size_t len,
                        const uint8_t tag[16], uint8_t* out) override {
    EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
    int n = 0, m = 0;
    const bool ok = EVP_DecryptInit_ex(c, EVP_aes_256_gcm(), nullptr, key, iv) == 1 &&
                    EVP_DecryptUpdate(c, out, &n, in, static_cast<int>(len)) == 1 &&
                    EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, 16, const_cast<uint8_t*>(tag)) == 1 &&
                    EVP_DecryptFinal_ex(c, out + n, &m) == 1;
    EVP_CIPHER_CTX_free(c);
    return ok;
  }
};

std::string b64(const uint8_t* d, size_t n) {
  std::string out(((n + 2) / 3) * 4 + 1, '\0');
  const int len = EVP_EncodeBlock(reinterpret_cast<uint8_t*>(out.data()), d, static_cast<int>(n));
  out.resize(static_cast<size_t>(len));
  return out;
}

// Plays the service: wraps contentKey to devicePub with an ephemeral key.
std::string serverUnlockResponse(const uint8_t devicePub[32], const uint8_t contentKey[32], int64_t expires) {
  EVP_PKEY* eph = nullptr;
  EVP_PKEY_CTX* kg = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
  EVP_PKEY_keygen_init(kg);
  EVP_PKEY_keygen(kg, &eph);
  EVP_PKEY_CTX_free(kg);
  uint8_t ephPriv[32], ephPub[32];
  size_t n = 32;
  EVP_PKEY_get_raw_private_key(eph, ephPriv, &n);
  n = 32;
  EVP_PKEY_get_raw_public_key(eph, ephPub, &n);
  EVP_PKEY_free(eph);

  HostCrypto c;
  uint8_t shared[32], kek[32];
  EXPECT_TRUE(c.x25519SharedSecret(ephPriv, devicePub, shared));
  EXPECT_TRUE(c.hkdfSha256(shared, 32, reinterpret_cast<const uint8_t*>("lcp-key-wrap"), 12, kek, 32));
  uint8_t iv[12], ct[32], tag[16];
  RAND_bytes(iv, 12);
  EVP_CIPHER_CTX* e = EVP_CIPHER_CTX_new();
  int outl = 0, fin = 0;
  EVP_EncryptInit_ex(e, EVP_aes_256_gcm(), nullptr, kek, iv);
  EVP_EncryptUpdate(e, ct, &outl, contentKey, 32);
  EVP_EncryptFinal_ex(e, ct + outl, &fin);
  EVP_CIPHER_CTX_ctrl(e, EVP_CTRL_GCM_GET_TAG, 16, tag);
  EVP_CIPHER_CTX_free(e);
  return "{\"content_key\":{\"epk\":\"" + b64(ephPub, 32) + "\",\"iv\":\"" + b64(iv, 12) + "\",\"ct\":\"" +
         b64(ct, 32) + "\",\"tag\":\"" + b64(tag, 16) + "\"},\"expires\":" + std::to_string(expires) + "}";
}

}  // namespace

TEST(LcpService, HexRoundTrip) {
  const uint8_t d[4] = {0x00, 0xab, 0x7f, 0xff};
  EXPECT_EQ(lcpservice::toHex(d, 4), "00ab7fff");
  uint8_t back[4];
  ASSERT_TRUE(lcpservice::fromHex("00AB7fff", back, 4));
  EXPECT_EQ(0, memcmp(d, back, 4));
  EXPECT_FALSE(lcpservice::fromHex("00ab7f", back, 4));
  EXPECT_FALSE(lcpservice::fromHex("zzab7fff", back, 4));
}

TEST(LcpService, EnrollRequestAndResponse) {
  const std::string req = lcpservice::buildEnrollRequest("rk\"1", "cHVi");
  EXPECT_EQ(req, "{\"release_key\":\"rk\\\"1\",\"pubkey\":\"cHVi\"}");
  std::string id;
  ASSERT_TRUE(lcpservice::parseEnrollResponse("{\"device_id\":\"dev-42\"}", &id));
  EXPECT_EQ(id, "dev-42");
  EXPECT_FALSE(lcpservice::parseEnrollResponse("{}", &id));
  EXPECT_FALSE(lcpservice::parseEnrollResponse("nope", &id));
}

TEST(LcpService, UnlockRequestEmbedsLicenseVerbatim) {
  const std::string lic = "{\"id\":\"x\",\"encryption\":{\"profile\":\"p\"}}";
  const std::string req = lcpservice::buildUnlockRequest("dev-42", "ab12", lic);
  EXPECT_EQ(req, "{\"device_id\":\"dev-42\",\"user_key\":\"ab12\",\"license\":" + lic + "}");
}

TEST(LcpService, StatusClassification) {
  using O = lcpservice::UnlockOutcome;
  EXPECT_EQ(lcpservice::classifyUnlockStatus(200), O::Ok);
  EXPECT_EQ(lcpservice::classifyUnlockStatus(401), O::NotEnrolled);
  EXPECT_EQ(lcpservice::classifyUnlockStatus(403), O::WrongPassphrase);
  EXPECT_EQ(lcpservice::classifyUnlockStatus(422), O::Unsupported);
  EXPECT_EQ(lcpservice::classifyUnlockStatus(500), O::Failed);
  EXPECT_EQ(lcpservice::classifyUnlockStatus(0), O::Failed);
}

TEST(LcpService, WrappedKeyRoundTripThroughDeviceKey) {
  HostCrypto c;
  // Device identity: X25519 pair, raw little-endian like the firmware stores it.
  EVP_PKEY* dev = nullptr;
  EVP_PKEY_CTX* kg = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
  EVP_PKEY_keygen_init(kg);
  EVP_PKEY_keygen(kg, &dev);
  EVP_PKEY_CTX_free(kg);
  uint8_t priv[32], pub[32];
  size_t n = 32;
  EVP_PKEY_get_raw_private_key(dev, priv, &n);
  n = 32;
  EVP_PKEY_get_raw_public_key(dev, pub, &n);
  EVP_PKEY_free(dev);

  uint8_t contentKey[32];
  for (int i = 0; i < 32; i++) contentKey[i] = static_cast<uint8_t>(i * 5 + 3);

  const std::string reply = serverUnlockResponse(pub, contentKey, 1790000000);
  lcpservice::WrappedKey w;
  int64_t expires = 0;
  ASSERT_TRUE(lcpservice::parseUnlockResponse(reply, &w, &expires));
  EXPECT_EQ(expires, 1790000000);

  uint8_t epk[32], iv[12], ct[32], tag[16], out[32];
  ASSERT_EQ(base64Decode(w.epk.data(), w.epk.size(), epk, 32), 32);
  ASSERT_EQ(base64Decode(w.iv.data(), w.iv.size(), iv, 12), 12);
  ASSERT_EQ(base64Decode(w.ct.data(), w.ct.size(), ct, 32), 32);
  ASSERT_EQ(base64Decode(w.tag.data(), w.tag.size(), tag, 16), 16);
  ASSERT_TRUE(lcpUnwrapContentKey(c, priv, epk, iv, ct, tag, out));
  EXPECT_EQ(0, memcmp(out, contentKey, 32));

  tag[0] ^= 1;  // tampered tag must be rejected
  EXPECT_FALSE(lcpUnwrapContentKey(c, priv, epk, iv, ct, tag, out));
}

TEST(LcpService, UnlockResponseNeedsAllFields) {
  lcpservice::WrappedKey w;
  int64_t e = 0;
  EXPECT_FALSE(lcpservice::parseUnlockResponse("{\"content_key\":{\"epk\":\"a\",\"iv\":\"b\",\"ct\":\"c\"}}", &w, &e));
  EXPECT_FALSE(lcpservice::parseUnlockResponse("{}", &w, &e));
  ASSERT_TRUE(lcpservice::parseUnlockResponse(
      "{\"content_key\":{\"epk\":\"a\",\"iv\":\"b\",\"ct\":\"c\",\"tag\":\"d\"}}", &w, &e));
  EXPECT_EQ(e, 0);
}
