#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Trust decisions for HTTPS servers that the device talks to (KOReader sync, plugin catalogs, ...).
//
// The device has no public CA store for its wolfSSL transport, so a server's certificate cannot be checked
// against the usual roots. Three modes:
//   TrustOnFirstUse - the SHA-256 of the server's certificate is remembered the first time and must match
//                     afterwards (a changed certificate is refused until the pin is forgotten);
//   Strict          - the chain must verify against the CA file the user put on the card, and the host name
//                     must match (hostname check done by the transport);
//   Off             - nothing is checked (the old behaviour).
namespace tlspolicy {

enum class Mode : uint8_t { TrustOnFirstUse = 0, Strict = 1, Off = 2 };

inline constexpr size_t FINGERPRINT_BYTES = 32;
inline constexpr size_t MAX_PINS = 48;

// "host:port", host lower-cased.
std::string hostKey(const std::string& host, uint16_t port);

std::string toHex(const uint8_t* bytes, size_t len);
// Exactly 2*len hex digits (either case); false otherwise.
bool fromHex(const std::string& hex, uint8_t* out, size_t len);

enum class Verdict { Match, NewHost, Mismatch };

class PinStore {
 public:
  // {"pins":[{"h":"host:443","fp":"<64 hex>"}, ...]}; entries that do not parse are dropped.
  bool load(const std::string& json);
  std::string toJson() const;

  Verdict check(const std::string& key, const std::string& fingerprintHex) const;
  // Adds or replaces the pin; the oldest entry goes when the store is full.
  void set(const std::string& key, const std::string& fingerprintHex);
  bool forget(const std::string& key);
  void clear() { pins_.clear(); }
  size_t size() const { return pins_.size(); }
  bool changed() const { return dirty_; }
  void markSaved() { dirty_ = false; }

 private:
  struct Pin {
    std::string key;
    std::string fp;
  };
  std::vector<Pin> pins_;
  bool dirty_ = false;
};

enum class Decision { Accept, AcceptAndSave, Reject };

// What to do with a handshake whose certificate has fingerprint `fingerprintHex` (empty: the build could not
// read it). Only meaningful for TrustOnFirstUse; the other modes never reach this.
//   - no fingerprint available: accepted (pinning impossible) so the connection still works;
//   - known host, same pin: Accept; known host, other pin: Reject; unknown host: AcceptAndSave.
Decision decideTofu(PinStore& store, const std::string& key, const std::string& fingerprintHex);

}  // namespace tlspolicy
