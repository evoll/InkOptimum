#include "LcpClient.h"

#include <BookKey.h>
#include <LcpInfo.h>
#include <LcpService.h>
#include <TrustedTime.h>
#include <ZipFile.h>
#include <LcpUnlock.h>
#include <Logging.h>
#include <WolfsslCrypto.h>

#include <cstring>

#include "InkMODSettings.h"
#include "LcpDeviceStore.h"
#include "LcpLoans.h"
#include "network/HttpDownloader.h"

namespace lcpclient {

std::string userKeyHexFromPassphrase(const std::string& passphrase) {
  freeink::content::WolfsslCrypto crypto;
  uint8_t key[32];
  freeink::content::lcpUserKey(crypto, passphrase.data(), passphrase.size(), key);
  const std::string hex = lcpservice::toHex(key, sizeof(key));
  memset(key, 0, sizeof(key));
  return hex;
}

namespace {

// Basic-profile license, no service: the user key is the passphrase hash, so the device can finish alone.
Result unlockLocally(const std::string& bookPath, const std::string& licenseText, const std::string& userKeyHex) {
  uint8_t userKey[32];
  if (!lcpservice::fromHex(userKeyHex, userKey, sizeof(userKey))) return Result::Failed;
  freeink::content::WolfsslCrypto crypto;
  lcp::Result r = lcp::unlock(crypto, licenseText.data(), licenseText.size(), nullptr, userKey);
  memset(userKey, 0, sizeof(userKey));
  Result out = Result::Failed;
  if (r.status == lcp::Status::WrongPassphrase) {
    out = Result::WrongPassphrase;
  } else if (r.status == lcp::Status::Ok) {
    out = bookkey::write(bookPath, r.contentKey, sizeof(r.contentKey), r.expiresAt) ? Result::Ok : Result::Failed;
    if (out == Result::Ok) lcploans::add(bookPath);
  }
  memset(r.contentKey, 0, sizeof(r.contentKey));
  return out;
}

Result unlockViaService(const std::string& bookPath, const std::string& licenseText, const std::string& userKeyHex) {
  std::string deviceId;
  bool notEnrollable = false;
  if (!lcpdevice::ensureEnrolled(deviceId, notEnrollable)) return notEnrollable ? Result::OfficialBuildRequired : Result::Offline;

  // Two attempts: a 401 means the registry no longer knows this device (revoked or reset), so enroll fresh once.
  for (int attempt = 0; attempt < 2; ++attempt) {
    const std::string request = lcpservice::buildUnlockRequest(deviceId, userKeyHex, licenseText);
    std::string response;
    int status = 0;
    const bool ok = HttpDownloader::postForm(lcpservice::UNLOCK_URL, request, response, &status);
    switch (lcpservice::classifyUnlockStatus(status)) {
      case lcpservice::UnlockOutcome::NotEnrolled:
        if (attempt == 0) {
          lcpdevice::forget();
          if (!lcpdevice::ensureEnrolled(deviceId, notEnrollable)) return Result::Offline;
          continue;
        }
        return Result::DeviceRejected;
      case lcpservice::UnlockOutcome::WrongPassphrase:
        return Result::WrongPassphrase;
      case lcpservice::UnlockOutcome::Unsupported:
        return Result::Unsupported;
      case lcpservice::UnlockOutcome::Failed:
        return status == 0 ? Result::Offline : Result::ServiceError;
      case lcpservice::UnlockOutcome::Ok:
        break;
    }
    lcpservice::WrappedKey wrapped;
    int64_t expiresAt = 0;
    uint8_t contentKey[32];
    if (!ok || !lcpservice::parseUnlockResponse(response, &wrapped, &expiresAt) ||
        !lcpdevice::unwrapContentKey(wrapped.epk, wrapped.iv, wrapped.ct, wrapped.tag, contentKey)) {
      LOG_ERR("LCP", "Unlock reply unusable (status %d)", status);
      return Result::ServiceError;
    }
    const bool stored = bookkey::write(bookPath, contentKey, sizeof(contentKey), expiresAt);
    memset(contentKey, 0, sizeof(contentKey));
    if (stored) lcploans::add(bookPath);
    return stored ? Result::Ok : Result::Failed;
  }
  return Result::Failed;
}

}  // namespace

Result unlock(const std::string& bookPath, const std::string& licenseText, const freeink::content::LcpLicense& license,
              const std::string& userKeyHex) {
  // The service holds the key derivation for every profile (including the confidential production
  // transform), so it is the primary path whenever this build can enroll.
  if (lcpdevice::available()) return unlockViaService(bookPath, licenseText, userKeyHex);
  if (license.isBasicProfile()) return unlockLocally(bookPath, licenseText, userKeyHex);
  return Result::OfficialBuildRequired;
}


// ---- loans -------------------------------------------------------------------------------------------

namespace {
constexpr size_t MAX_LICENSE_BYTES = 16 * 1024;

std::string deviceName() { return SETTINGS.deviceName[0] ? SETTINGS.deviceName : "InkOptimum"; }

std::string deviceIdOrName() {
  std::string id;
  bool notEnrollable = false;
  if (lcpdevice::available() && lcpdevice::ensureEnrolled(id, notEnrollable)) return id;
  return deviceName();
}

// GET a small document; status 0 = transport failure.
bool getText(const std::string& url, std::string& out, int* status) {
  return HttpDownloader::sendRequest("GET", url, "", out, status);
}
}  // namespace

std::string readEmbeddedLicense(const std::string& bookPath) {
  ZipFile zip(bookPath.c_str());
  size_t size = 0;
  if (!zip.getInflatedFileSize("META-INF/license.lcpl", &size) || size == 0 || size > MAX_LICENSE_BYTES) return {};
  uint8_t* buf = zip.readFileToMemory("META-INF/license.lcpl", &size, false);
  if (!buf) return {};
  std::string text(reinterpret_cast<const char*>(buf), size);
  free(buf);
  return text;
}

LoanInfo loanInfo(const std::string& bookPath) {
  LoanInfo info;
  uint8_t key[bookkey::MAX_KEY_LEN];
  int64_t expires = 0;
  info.hasKey = bookkey::read(bookPath, key, &expires) != 0;
  memset(key, 0, sizeof(key));
  info.expiresAt = expires;
  info.state = static_cast<int>(lcpinfo::loanState(expires, trustedtime::trustedNow()));
  return info;
}

Result returnBook(const std::string& bookPath) {
  const std::string licenseText = readEmbeddedLicense(bookPath);
  lcpinfo::LicenseInfo license;
  if (licenseText.empty() || !lcpinfo::parseLicense(licenseText.data(), licenseText.size(), &license) ||
      license.statusHref.empty()) {
    return Result::Unsupported;  // no status document: this loan cannot be returned from here
  }
  std::string body;
  int status = 0;
  if (!getText(license.statusHref, body, &status)) return status == 0 ? Result::Offline : Result::ServiceError;
  lcpinfo::StatusInfo st;
  if (!lcpinfo::parseStatus(body.data(), body.size(), &st)) return Result::ServiceError;
  if (st.returnHref.empty()) return Result::Unsupported;

  const std::string url = lcpinfo::resolveLink(
      license.statusHref, lcpinfo::expandReturnUrl(st.returnHref, st.returnTemplated, deviceIdOrName(), deviceName()));
  std::string reply;
  if (!HttpDownloader::sendRequest("PUT", url, "", reply, &status)) {
    return status == 0 ? Result::Offline : Result::ServiceError;
  }
  bookkey::remove(bookPath);
  lcploans::remove(bookPath);
  return Result::Ok;
}

Result refreshKey(const std::string& bookPath) {
  const std::string embedded = readEmbeddedLicense(bookPath);
  lcpinfo::LicenseInfo info;
  if (embedded.empty() || !lcpinfo::parseLicense(embedded.data(), embedded.size(), &info)) return Result::Unsupported;

  // The current license: via the status document when there is one (a renewal updates it), else the embedded one.
  std::string licenseText = embedded;
  if (!info.statusHref.empty()) {
    std::string body;
    int status = 0;
    if (!getText(info.statusHref, body, &status)) return status == 0 ? Result::Offline : Result::ServiceError;
    lcpinfo::StatusInfo st;
    if (lcpinfo::parseStatus(body.data(), body.size(), &st) && !st.licenseHref.empty()) {
      std::string fresh;
      if (getText(lcpinfo::resolveLink(info.statusHref, st.licenseHref), fresh, &status)) {
        lcpinfo::LicenseInfo freshInfo;
        if (lcpinfo::parseLicense(fresh.data(), fresh.size(), &freshInfo) && freshInfo.id == info.id) {
          licenseText = std::move(fresh);
        }
      } else if (status == 0) {
        return Result::Offline;
      }
    }
  }

  freeink::content::LcpLicense license;
  if (!freeink::content::parseLcpLicense(licenseText.data(), licenseText.size(), &license)) return Result::Failed;
  const std::string hash = license.provider.empty() ? std::string() : LCP_PASSPHRASES.get(license.provider);
  if (hash.empty()) return Result::NeedPassphrase;
  return unlock(bookPath, licenseText, license, hash);
}

ServiceStatus checkService() {
  ServiceStatus s;
  s.canEnroll = lcpdevice::canEnroll();
  s.enrolled = lcpdevice::enrolled();
  std::string body;
  int status = 0;
  HttpDownloader::sendRequest("GET", "https://lcp.freeink.org/", "", body, &status);
  s.httpStatus = status;
  s.reachable = status >= 200 && status < 500;
  return s;
}

}  // namespace lcpclient
