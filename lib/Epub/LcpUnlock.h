#pragma once

#include <LcpLicense.h>

#include <cstddef>
#include <cstdint>
#include <string>

// Readium LCP unlock: license.lcpl + passphrase (or a ready user key) -> the
// AES-256 content key, which the caller stores via bookkey::write().
//
// Basic profile: user key = SHA-256(passphrase), checked against the license's
// key_check. The production profile transforms the passphrase with a
// confidential, certified step that is not implemented here; for such licenses
// the caller (a plugin or an unlock service) supplies the finished 32-byte user
// key, and the same key_check / content-key decryption runs on it.
namespace lcp {

enum class Status : uint8_t {
  Ok,
  BadLicense,           // not an LCP license document
  NeedPassphrase,       // neither passphrase nor user key supplied; hint is filled
  UnsupportedProfile,   // production profile and no user key supplied
  WrongPassphrase,      // key_check did not decrypt to the license id
  DecryptFailed,        // content key blob malformed
};

struct Result {
  Status status = Status::BadLicense;
  uint8_t contentKey[32] = {0};
  int64_t expiresAt = 0;  // epoch seconds from rights.end, 0 = no expiry
  std::string hint;
  std::string licenseId;
};

// passphrase may be null; userKey (32 bytes) may be null. userKey wins if both are given.
Result unlock(freeink::content::Crypto& crypto, const char* licenseJson, size_t licenseLen, const char* passphrase,
              const uint8_t* userKey);

const char* statusName(Status s);

}  // namespace lcp
