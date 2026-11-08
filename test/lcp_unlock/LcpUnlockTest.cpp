#include <Crypto.h>
#include <LcpUnlock.h>
#include <Util.h>
#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <cstring>
#include <string>
#include <vector>

using namespace freeink::content;

namespace {

// Host crypto: only the primitives LCP unlock uses; the rest is unreachable.
class HostCrypto : public Crypto {
 public:
  int32_t rsaPrivateRaw(const uint8_t*, size_t, const uint8_t*, size_t, uint8_t*, size_t) override { return -1; }
  bool aes128CbcDecrypt(const uint8_t*, const uint8_t*, const uint8_t*, size_t, uint8_t*) override { return false; }
  bool aes256CbcDecrypt(const uint8_t key[32], const uint8_t iv[16], const uint8_t* in, size_t len,
                        uint8_t* out) override {
    EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
    int n = 0, m = 0;
    EVP_DecryptInit_ex(c, EVP_aes_256_cbc(), nullptr, key, iv);
    EVP_CIPHER_CTX_set_padding(c, 0);
    const bool ok = EVP_DecryptUpdate(c, out, &n, in, static_cast<int>(len)) == 1 &&
                    EVP_DecryptFinal_ex(c, out + n, &m) == 1;
    EVP_CIPHER_CTX_free(c);
    return ok;
  }
  void sha1(const uint8_t* d, size_t l, uint8_t out[20]) override { SHA1(d, l, out); }
  void sha256(const uint8_t* d, size_t l, uint8_t out[32]) override { SHA256(d, l, out); }
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
};

// base64(IV || AES-256-CBC(key, plain padded to a block with a count byte)).
std::string encryptBlob(const uint8_t key[32], const std::string& plain) {
  uint8_t iv[16];
  for (int i = 0; i < 16; i++) iv[i] = static_cast<uint8_t>(i * 7 + 1);
  const size_t pad = 16 - plain.size() % 16;
  std::string padded = plain + std::string(pad, static_cast<char>(pad));
  std::vector<uint8_t> blob(16 + padded.size() + 16);
  memcpy(blob.data(), iv, 16);
  EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
  int n = 0;
  EVP_EncryptInit_ex(c, EVP_aes_256_cbc(), nullptr, key, iv);
  EVP_CIPHER_CTX_set_padding(c, 0);
  EVP_EncryptUpdate(c, blob.data() + 16, &n, reinterpret_cast<const uint8_t*>(padded.data()),
                    static_cast<int>(padded.size()));
  EVP_CIPHER_CTX_free(c);
  blob.resize(16 + static_cast<size_t>(n));
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (size_t i = 0; i < blob.size(); i += 3) {
    const uint32_t v = (blob[i] << 16) | (i + 1 < blob.size() ? blob[i + 1] << 8 : 0) | (i + 2 < blob.size() ? blob[i + 2] : 0);
    out += T[v >> 18 & 63];
    out += T[v >> 12 & 63];
    out += i + 1 < blob.size() ? T[v >> 6 & 63] : '=';
    out += i + 2 < blob.size() ? T[v & 63] : '=';
  }
  return out;
}

struct Fixture {
  uint8_t contentKey[32];
  uint8_t userKey[32];
  std::string json;
  Fixture(const char* pass, const char* profile, const char* rightsEnd = nullptr) {
    for (int i = 0; i < 32; i++) contentKey[i] = static_cast<uint8_t>(0xA0 + i);
    SHA256(reinterpret_cast<const uint8_t*>(pass), strlen(pass), userKey);
    const std::string id = "urn:uuid:11111111-2222-3333-4444-555555555555";
    json = "{\"id\":\"" + id + "\",\"provider\":\"https://p.example\",\"encryption\":{\"profile\":\"" + profile +
           "\",\"content_key\":{\"algorithm\":\"x\",\"encrypted_value\":\"" +
           encryptBlob(userKey, std::string(reinterpret_cast<char*>(contentKey), 32)) +
           "\"},\"user_key\":{\"text_hint\":\"favourite colour\",\"key_check\":\"" + encryptBlob(userKey, id) +
           "\"}},\"links\":[{\"rel\":\"hint\",\"href\":\"x\"}]";
    if (rightsEnd) json += std::string(",\"rights\":{\"end\":\"") + rightsEnd + "\"}";
    json += "}";
  }
};

const char* BASIC = "http://readium.org/lcp/basic-profile";

}  // namespace

TEST(LcpUnlock, BasicProfileRoundTrip) {
  HostCrypto c;
  Fixture f("secret", BASIC);
  const auto r = lcp::unlock(c, f.json.data(), f.json.size(), "secret", nullptr);
  ASSERT_EQ(r.status, lcp::Status::Ok);
  EXPECT_EQ(0, memcmp(r.contentKey, f.contentKey, 32));
  EXPECT_EQ(r.expiresAt, 0);
}

TEST(LcpUnlock, WrongPassphrase) {
  HostCrypto c;
  Fixture f("secret", BASIC);
  const auto r = lcp::unlock(c, f.json.data(), f.json.size(), "nope", nullptr);
  EXPECT_EQ(r.status, lcp::Status::WrongPassphrase);
  EXPECT_EQ(r.hint, "favourite colour");
}

TEST(LcpUnlock, NeedsPassphraseReturnsHint) {
  HostCrypto c;
  Fixture f("secret", BASIC);
  const auto r = lcp::unlock(c, f.json.data(), f.json.size(), nullptr, nullptr);
  EXPECT_EQ(r.status, lcp::Status::NeedPassphrase);
  EXPECT_EQ(r.hint, "favourite colour");
}

TEST(LcpUnlock, ProductionProfileNeedsUserKey) {
  HostCrypto c;
  Fixture f("secret", "http://readium.org/lcp/profile-1.0");
  EXPECT_EQ(lcp::unlock(c, f.json.data(), f.json.size(), "secret", nullptr).status, lcp::Status::UnsupportedProfile);
  const auto r = lcp::unlock(c, f.json.data(), f.json.size(), nullptr, f.userKey);
  ASSERT_EQ(r.status, lcp::Status::Ok);
  EXPECT_EQ(0, memcmp(r.contentKey, f.contentKey, 32));
}

TEST(LcpUnlock, WrongUserKeyRejected) {
  HostCrypto c;
  Fixture f("secret", "http://readium.org/lcp/profile-1.0");
  uint8_t bad[32] = {1};
  EXPECT_EQ(lcp::unlock(c, f.json.data(), f.json.size(), nullptr, bad).status, lcp::Status::WrongPassphrase);
}

TEST(LcpUnlock, RightsEndBecomesExpiry) {
  HostCrypto c;
  Fixture f("secret", BASIC, "2026-03-01T10:00:00Z");
  const auto r = lcp::unlock(c, f.json.data(), f.json.size(), "secret", nullptr);
  ASSERT_EQ(r.status, lcp::Status::Ok);
  EXPECT_EQ(r.expiresAt, 1772359200);
}

TEST(LcpUnlock, GarbageIsBadLicense) {
  HostCrypto c;
  const char* j = "{\"foo\":1}";
  EXPECT_EQ(lcp::unlock(c, j, strlen(j), "x", nullptr).status, lcp::Status::BadLicense);
  EXPECT_EQ(lcp::unlock(c, "not json", 8, "x", nullptr).status, lcp::Status::BadLicense);
}

TEST(LcpUnlock, Iso8601Offset) {
  EXPECT_EQ(lcpParseIso8601("2026-03-01T12:00:00+02:00"), 1772359200);
  EXPECT_EQ(lcpParseIso8601("2026-03-01T10:00:00.250Z"), 1772359200);
  EXPECT_EQ(lcpParseIso8601("bad"), 0);
}
