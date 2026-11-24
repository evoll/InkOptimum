#pragma once
// Companion-app pairing and bearer tokens (docs/COMPANION_APP.md, stage 0). Pure logic: the caller supplies
// entropy and the clock, so it runs the same on the device and in native tests.

#include <cstdint>
#include <string>
#include <vector>

namespace companion {

std::string toHex(const std::string& raw);
bool fromHex(const std::string& hex, std::string& raw);

// One pairing attempt at a time: the reader shows a 6-digit code for kCodeLifetimeMs; three wrong codes lock
// pairing for kLockoutMs.
class Pairing {
 public:
  static constexpr uint32_t kCodeLifetimeMs = 120000;
  static constexpr uint32_t kLockoutMs = 300000;
  static constexpr int kMaxWrong = 3;
  enum class Result { Ok, NoSession, Wrong, Locked };

  // Starts a session; false (and no code) while locked out.
  bool begin(uint32_t entropy, uint32_t nowMs);
  bool active(uint32_t nowMs) const;
  bool locked(uint32_t nowMs) const;
  const std::string& code() const { return code_; }
  Result confirm(const std::string& code, uint32_t nowMs);
  void cancel();

 private:
  std::string code_;
  uint32_t expiresAt_ = 0;
  uint32_t lockedUntil_ = 0;
  bool hasSession_ = false;
  bool hasLock_ = false;
  int wrong_ = 0;
};

struct TokenInfo {
  std::string id;    // first 8 hex chars of the hash, safe to show
  std::string name;  // device name given by the app (printable, <= 40 bytes)
  std::string hash;  // 32 raw bytes: sha256(token)
};

class TokenStore {
 public:
  static constexpr size_t kMaxTokens = 8;
  // Stores sha256 of `token32` (32 raw random bytes) and returns the bearer token as 64 hex chars; the oldest entry
  // is dropped when full. Empty string when token32 has the wrong size.
  std::string issue(const std::string& token32, const std::string& name);
  bool verify(const std::string& bearerHex) const;  // constant-time on the hash compare
  bool revoke(const std::string& id);
  const std::vector<TokenInfo>& tokens() const { return tokens_; }
  // "id\tname\thashhex\n" per line; load() skips malformed lines and returns how many were kept.
  std::string serialize() const;
  size_t load(const std::string& text);

 private:
  std::vector<TokenInfo> tokens_;
};

// "Authorization: Bearer <hex>" header value -> hex part (empty when the scheme is wrong).
std::string bearerFrom(const std::string& headerValue);

}  // namespace companion
