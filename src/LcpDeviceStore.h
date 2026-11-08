#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Device identity for the FreeInk LCP service: an X25519 keypair generated on first use and enrolled once
// with /enroll, gated by the release key that CI injects into official builds
// (-DFREEINK_LCP_RELEASE_KEY=\"...\"). /unlock replies are wrapped to the enrolled public key, so they are
// useless to any other party. Enrollment identifies the device, it does not attest it; the private key is
// stored obfuscated with the device key, like the other credential stores.
namespace lcpdevice {

// Loads the stored identity or generates and enrolls a new one. False on failure; notEnrollable=true means
// this build carries no release key (self-compiled) and enrollment can never succeed.
bool ensureEnrolled(std::string& deviceId, bool& notEnrollable);

// True when this build carries a release key or a stored identity already exists.
bool available();

// True when this build carries a release key (enrollment possible).
bool canEnroll();
// True when this device already holds an enrolled identity.
bool enrolled();

// Drops the stored identity (the service answered 401: revoked or registry reset).
void forget();

// Unwraps a /unlock content_key (base64 epk/iv/ct/tag) with the stored private key.
bool unwrapContentKey(const std::string& epkB64, const std::string& ivB64, const std::string& ctB64,
                      const std::string& tagB64, uint8_t out[32]);

}  // namespace lcpdevice

// Per-provider LCP user-key hashes (hex SHA-256 of the passphrase), keyed by the license's `provider` URI, so
// the next borrow from the same library skips the prompt: the saved hash is tried first and only a wrong-
// passphrase answer asks again. The hash is the basic-profile user key, so it is stored obfuscated with the
// device key and never in the clear.
class LcpPassphraseStore {
 public:
  static LcpPassphraseStore& getInstance();

  bool loadFromFile();
  std::string get(const std::string& provider) const;
  // Upserts; an empty hash removes the entry. No SD write when unchanged.
  bool put(const std::string& provider, const std::string& userKeyHex);

 private:
  struct Entry {
    std::string provider;
    std::string userKeyHex;
  };
  static constexpr size_t MAX_PROVIDERS = 8;
  std::vector<Entry> entries;
  bool loaded = false;

  bool saveToFile() const;
  void ensureLoaded() const;
};

#define LCP_PASSPHRASES LcpPassphraseStore::getInstance()
