#include "PinLock.h"

#include <CompanionAuth.h>
#include <XtAppUtil.h>

namespace pinlock {

namespace {
constexpr int kRounds = 1000;

std::string derive(const std::string& salt, const std::string& pin) {
  std::string h = xtapp::sha256(salt + pin);
  for (int i = 1; i < kRounds; ++i) h = xtapp::sha256(h + salt + pin);
  return h;
}

bool constantTimeEq(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  unsigned char diff = 0;
  for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
  return diff == 0;
}

bool split(const std::string& record, std::string& salt, std::string& hash) {
  if (record.compare(0, 3, "v1:") != 0) return false;
  const size_t sep = record.find(':', 3);
  if (sep == std::string::npos) return false;
  return companion::fromHex(record.substr(3, sep - 3), salt) && salt.size() == 16 &&
         companion::fromHex(record.substr(sep + 1), hash) && hash.size() == 32;
}
}  // namespace

bool validPin(const std::string& pin) {
  if (pin.size() < kMinDigits || pin.size() > kMaxDigits) return false;
  for (const char c : pin) {
    if (c < '0' || c > '9') return false;
  }
  return true;
}

std::string makeRecord(const std::string& pin, const std::string& salt) {
  if (!validPin(pin) || salt.size() != 16) return "";
  return "v1:" + companion::toHex(salt) + ":" + companion::toHex(derive(salt, pin));
}

bool wellFormed(const std::string& record) {
  std::string s, h;
  return split(record, s, h);
}

bool verify(const std::string& record, const std::string& pin) {
  std::string salt, hash;
  if (!split(record, salt, hash) || !validPin(pin)) return false;
  return constantTimeEq(derive(salt, pin), hash);
}

uint32_t delaySeconds(const uint32_t failCount) {
  if (failCount < kFreeAttempts) return 0;
  const uint32_t shift = failCount - kFreeAttempts;
  if (shift >= 7) return 3600;  // 30 << 7 = 3840 > 3600
  const uint32_t s = 30u << shift;
  return s > 3600 ? 3600 : s;
}

}  // namespace pinlock
