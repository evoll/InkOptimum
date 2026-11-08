#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// What the device needs from a Readium LCP license document (the .lcpl embedded in the book as
// META-INF/license.lcpl) and from its License Status Document (LSD, linked as rel="status"): loan dates,
// the status/return/renew links, and the small helpers for them. Pure string work, tested on the host.
namespace lcpinfo {

struct LicenseInfo {
  std::string id;
  std::string provider;
  std::string hint;
  std::string profile;
  std::string statusHref;  // rel="status"
  int64_t rightsEnd = 0;   // rights.end as epoch seconds, 0 = none
  bool valid = false;
};

// False on malformed JSON or a document without an id.
bool parseLicense(const char* json, size_t len, LicenseInfo* out);

struct StatusInfo {
  std::string status;       // ready / active / revoked / returned / cancelled / expired
  std::string licenseHref;  // rel="license": the current license (renewals extend it)
  std::string returnHref;   // rel="return"
  bool returnTemplated = false;
  std::string renewHref;    // rel="renew"
  int64_t potentialEnd = 0;  // potential_rights.end
  bool valid = false;
};

bool parseStatus(const char* json, size_t len, StatusInfo* out);

// LCP return links are templated ("...{?end,id,name}"): the reader's id and name go in the query, "end" is
// left out (the provider uses the current date). Any other {...} expression is dropped.
std::string expandReturnUrl(const std::string& href, bool templated, const std::string& deviceId,
                            const std::string& deviceName);

// Resolves a possibly relative link against the URL it was found at.
std::string resolveLink(const std::string& base, const std::string& href);

enum class LoanState { NoExpiry, Active, Expired, ClockUnknown };
// expiresAt 0 = no expiry; now 0 = no trustworthy clock (a dated loan then fails closed, like the reader).
LoanState loanState(int64_t expiresAt, int64_t now);

// "2026-03-01" (UTC) from epoch seconds; empty for 0 or negative values.
std::string formatDate(int64_t epoch);

}  // namespace lcpinfo
