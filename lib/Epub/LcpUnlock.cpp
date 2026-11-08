#include "LcpUnlock.h"

#include <Crypto.h>

#include <cstring>

namespace lcp {

using namespace freeink::content;

Result unlock(Crypto& crypto, const char* licenseJson, const size_t licenseLen, const char* passphrase,
              const uint8_t* userKey) {
  Result r;
  LcpLicense lic;
  if (!licenseJson || !parseLcpLicense(licenseJson, licenseLen, &lic)) return r;
  r.hint = lic.hint;
  r.licenseId = lic.id;
  r.expiresAt = lic.rightsEnd.empty() ? 0 : lcpParseIso8601(lic.rightsEnd.c_str());

  uint8_t key[32];
  if (userKey) {
    memcpy(key, userKey, sizeof(key));
  } else if (passphrase && *passphrase) {
    if (!lic.isBasicProfile()) {
      r.status = Status::UnsupportedProfile;
      return r;
    }
    lcpUserKey(crypto, passphrase, strlen(passphrase), key);
  } else {
    r.status = lic.isBasicProfile() ? Status::NeedPassphrase : Status::UnsupportedProfile;
    return r;
  }

  if (!lcpCheckUserKey(crypto, lic, key)) {
    memset(key, 0, sizeof(key));
    r.status = Status::WrongPassphrase;
    return r;
  }
  const bool ok = lcpContentKey(crypto, lic, key, r.contentKey);
  memset(key, 0, sizeof(key));
  r.status = ok ? Status::Ok : Status::DecryptFailed;
  return r;
}

const char* statusName(const Status s) {
  switch (s) {
    case Status::Ok: return "ok";
    case Status::BadLicense: return "bad_license";
    case Status::NeedPassphrase: return "need_passphrase";
    case Status::UnsupportedProfile: return "unsupported_profile";
    case Status::WrongPassphrase: return "wrong_passphrase";
    case Status::DecryptFailed: return "decrypt_failed";
  }
  return "bad_license";
}

}  // namespace lcp
