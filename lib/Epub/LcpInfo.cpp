#include "LcpInfo.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

namespace lcpinfo {

namespace {

// Hinnant's days-from-civil, then the clock. Same arithmetic as lcpParseIso8601.
int64_t parseIso(const char* s) {
  if (!s || !*s) return 0;
  int y, mo, d, h, mi, sec;
  if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &sec) != 6 || y < 1970 || mo < 1 || mo > 12 ||
      d < 1 || d > 31) {
    return 0;
  }
  const int yAdj = y - (mo <= 2);
  const int era = yAdj / 400;
  const unsigned yoe = static_cast<unsigned>(yAdj - era * 400);
  const unsigned doy = (153u * static_cast<unsigned>(mo + (mo > 2 ? -3 : 9)) + 2u) / 5u + static_cast<unsigned>(d) - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  int64_t epoch = (static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468) * 86400 + h * 3600 +
                  mi * 60 + sec;
  if (strlen(s) >= 19) {
    const char* p = s + 19;
    if (*p == '.') {
      p++;
      while (*p >= '0' && *p <= '9') p++;
    }
    if (*p == '+' || *p == '-') {
      int oh = 0, om = 0;
      if (sscanf(p + 1, "%2d:%2d", &oh, &om) >= 1) epoch -= (*p == '+' ? 1 : -1) * (oh * 3600 + om * 60);
    }
  }
  return epoch > 0 ? epoch : 0;
}

bool relIs(JsonVariantConst rel, const char* want) {
  if (rel.is<const char*>()) return strcmp(rel.as<const char*>(), want) == 0;
  for (JsonVariantConst r : rel.as<JsonArrayConst>()) {
    if (r.is<const char*>() && strcmp(r.as<const char*>(), want) == 0) return true;
  }
  return false;
}

std::string pctEncode(const std::string& s) {
  static const char* H = "0123456789ABCDEF";
  std::string out;
  for (const unsigned char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += H[c >> 4];
      out += H[c & 15];
    }
  }
  return out;
}

}  // namespace

bool parseLicense(const char* json, const size_t len, LicenseInfo* out) {
  *out = LicenseInfo{};
  if (!json) return false;
  JsonDocument doc;
  if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
  out->id = doc["id"] | "";
  out->provider = doc["provider"] | "";
  out->hint = doc["encryption"]["user_key"]["text_hint"] | "";
  out->profile = doc["encryption"]["profile"] | "";
  out->rightsEnd = parseIso(doc["rights"]["end"] | "");
  for (JsonVariantConst link : doc["links"].as<JsonArrayConst>()) {
    if (relIs(link["rel"], "status")) {
      out->statusHref = link["href"] | "";
      break;
    }
  }
  out->valid = !out->id.empty();
  return out->valid;
}

bool parseStatus(const char* json, const size_t len, StatusInfo* out) {
  *out = StatusInfo{};
  if (!json) return false;
  JsonDocument doc;
  if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
  out->status = doc["status"] | "";
  out->potentialEnd = parseIso(doc["potential_rights"]["end"] | "");
  for (JsonVariantConst link : doc["links"].as<JsonArrayConst>()) {
    const char* href = link["href"] | "";
    if (!*href) continue;
    if (out->licenseHref.empty() && relIs(link["rel"], "license")) out->licenseHref = href;
    if (out->returnHref.empty() && relIs(link["rel"], "return")) {
      out->returnHref = href;
      out->returnTemplated = link["templated"] | false;
    }
    if (out->renewHref.empty() && relIs(link["rel"], "renew")) out->renewHref = href;
  }
  out->valid = !out->status.empty();
  return out->valid;
}

std::string expandReturnUrl(const std::string& href, const bool templated, const std::string& deviceId,
                            const std::string& deviceName) {
  if (!templated) return href;
  std::string out;
  std::string query;
  for (size_t i = 0; i < href.size();) {
    if (href[i] != '{') {
      out += href[i++];
      continue;
    }
    const size_t close = href.find('}', i);
    if (close == std::string::npos) break;
    const std::string expr = href.substr(i + 1, close - i - 1);
    i = close + 1;
    if (expr.empty() || expr[0] != '?') continue;
    size_t p = 1;
    while (p <= expr.size()) {
      size_t comma = expr.find(',', p);
      if (comma == std::string::npos) comma = expr.size();
      const std::string var = expr.substr(p, comma - p);
      p = comma + 1;
      const std::string* value = var == "id" ? &deviceId : var == "name" ? &deviceName : nullptr;
      if (!value || value->empty()) continue;
      if (!query.empty()) query += '&';
      query += var + "=" + pctEncode(*value);
    }
  }
  if (!query.empty()) out += (out.find('?') == std::string::npos ? "?" : "&") + query;
  return out;
}

std::string resolveLink(const std::string& base, const std::string& href) {
  if (href.rfind("http://", 0) == 0 || href.rfind("https://", 0) == 0) return href;
  const size_t scheme = base.find("://");
  if (scheme == std::string::npos) return href;
  const size_t hostEnd = base.find('/', scheme + 3);
  const std::string origin = hostEnd == std::string::npos ? base : base.substr(0, hostEnd);
  if (!href.empty() && href[0] == '/') return origin + href;
  size_t cut = base.find_first_of("?#");
  std::string dir = base.substr(0, cut == std::string::npos ? base.size() : cut);
  const size_t slash = dir.rfind('/');
  if (slash == std::string::npos || slash < scheme + 3) return origin + "/" + href;
  return dir.substr(0, slash + 1) + href;
}

LoanState loanState(const int64_t expiresAt, const int64_t now) {
  if (expiresAt == 0) return LoanState::NoExpiry;
  if (now == 0) return LoanState::ClockUnknown;
  return now > expiresAt ? LoanState::Expired : LoanState::Active;
}

std::string formatDate(const int64_t epoch) {
  if (epoch <= 0) return {};
  const int64_t days = epoch / 86400;
  const int64_t z = days + 719468;
  const int64_t era = z / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t y = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  char buf[40];
  snprintf(buf, sizeof(buf), "%04d-%02u-%02u", static_cast<int>(y + (m <= 2)), m, d);
  return buf;
}

}  // namespace lcpinfo
