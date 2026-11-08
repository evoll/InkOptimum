#pragma once

#include <LcpLicense.h>

#include <string>

// LCP acquisition glue shared by the OPDS browser: passphrase -> user key, then the key exchange with the
// FreeInk LCP service (or, when this build cannot enroll, a local unlock of basic-profile licenses), ending
// with the content key stored as the book's device-wrapped "<book>.key" so the standard protected-read path
// opens it. The reader itself knows nothing about LCP.
namespace lcpclient {

enum class Result {
  Ok,
  WrongPassphrase,
  Unsupported,             // profile neither the service nor this build can serve
  OfficialBuildRequired,   // self-compiled firmware and a non-basic profile: service enrollment impossible
  Failed,                  // storage or crypto error
  Offline,                 // the service could not be reached at all
  DeviceRejected,          // the service refuses this device even after a fresh enrollment (401)
  ServiceError,            // the service answered with a server error (5xx) or an unusable reply
  NeedPassphrase,          // refresh: no saved passphrase hash for this provider
};

// Hex SHA-256 of the passphrase: the basic-profile user key, and what the service expects.
std::string userKeyHexFromPassphrase(const std::string& passphrase);

// bookPath: the fulfilled EPUB. licenseText: the .lcpl as fetched (sent verbatim to /unlock).
Result unlock(const std::string& bookPath, const std::string& licenseText, const freeink::content::LcpLicense& license,
              const std::string& userKeyHex);

// ---- loans -------------------------------------------------------------------------------------------

// The license embedded in the book (META-INF/license.lcpl); empty when missing or too large.
std::string readEmbeddedLicense(const std::string& bookPath);

struct LoanInfo {
  bool hasKey = false;
  int64_t expiresAt = 0;  // 0 = no expiry
  int state = 0;          // lcpinfo::LoanState
};
LoanInfo loanInfo(const std::string& bookPath);

// Gives the loan back through the license's status document (return link, PUT), then removes the key.
Result returnBook(const std::string& bookPath);

// Fetches the current license via the status document (a renewal extends it), unlocks again with the saved
// passphrase hash for the provider, and stores the new key and due date.
Result refreshKey(const std::string& bookPath);

struct ServiceStatus {
  bool reachable = false;
  int httpStatus = 0;
  bool enrolled = false;     // this device holds an identity
  bool canEnroll = false;    // this build carries a release key
};
// One request to the service root: tells "no network" from "service down" from "all fine".
ServiceStatus checkService();

}  // namespace lcpclient
