#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Client side of the FreeInk LCP service (https://lcp.freeink.org):
//   POST /enroll   {release_key, pubkey}              -> {device_id}
//   POST /fulfill  <license.lcpl>                     -> the EPUB with the license embedded
//   POST /unlock   {device_id, user_key, license}     -> {content_key:{epk,iv,ct,tag}, expires}
// The content key in /unlock is wrapped to the device's enrolled X25519 key (see LcpLicense.h,
// lcpUnwrapContentKey), so a captured response is useless to anyone else. Everything here is pure string
// work so it can be tested on the host.
namespace lcpservice {

inline constexpr const char* ENROLL_URL = "https://lcp.freeink.org/enroll";
inline constexpr const char* FULFILL_URL = "https://lcp.freeink.org/fulfill";
inline constexpr const char* UNLOCK_URL = "https://lcp.freeink.org/unlock";
inline constexpr const char* LICENSE_CONTENT_TYPE = "application/vnd.readium.lcp.license.v1.0+json";

struct WrappedKey {
  std::string epk, iv, ct, tag;  // base64
};

// Lower-case hex of bytes.
std::string toHex(const uint8_t* data, size_t len);
// Exactly 2*len hex chars -> bytes; false on any other input.
bool fromHex(const std::string& hex, uint8_t* out, size_t len);

std::string buildEnrollRequest(const std::string& releaseKey, const std::string& pubkeyB64);
// {device_id: "..."}; false when missing or implausibly long.
bool parseEnrollResponse(const std::string& json, std::string* deviceId);

// licenseText is embedded verbatim (it already parsed as JSON when it was fetched).
std::string buildUnlockRequest(const std::string& deviceId, const std::string& userKeyHex,
                               const std::string& licenseText);
// All four wrapped-key fields are required; expires is epoch seconds (0 = none).
bool parseUnlockResponse(const std::string& json, WrappedKey* wrapped, int64_t* expiresAt);

enum class UnlockOutcome { Ok, WrongPassphrase, Unsupported, NotEnrolled, Failed };
// Maps /unlock's HTTP status: 401 = device unknown (re-enroll), 403 = wrong passphrase, 422 = profile the
// service cannot serve; any other non-2xx is Failed. 2xx is Ok (the body is still validated by the caller).
UnlockOutcome classifyUnlockStatus(int httpStatus);

}  // namespace lcpservice
