#include "FirmwareSignature.h"

#include <cstring>

#include "monocypher-ed25519.h"

namespace fwsig {

bool keyConfigured(const uint8_t publicKey[kPublicKeySize]) {
  uint8_t any = 0;
  for (size_t i = 0; i < kPublicKeySize; ++i) any |= publicKey[i];
  return any != 0;
}

void buildMessage(const uint8_t digest[kDigestSize], uint8_t message[kMessageSize]) {
  std::memcpy(message, kMessagePrefix, kMessagePrefixLen);
  std::memcpy(message + kMessagePrefixLen, digest, kDigestSize);
}

bool verifyMessage(const uint8_t publicKey[kPublicKeySize], const uint8_t* message, const size_t len,
                   const uint8_t signature[kSignatureSize]) {
  if (!keyConfigured(publicKey)) return false;
  return crypto_ed25519_check(signature, publicKey, message, len) == 0;
}

bool verifyDigest(const uint8_t publicKey[kPublicKeySize], const uint8_t digest[kDigestSize],
                  const uint8_t signature[kSignatureSize]) {
  uint8_t message[kMessageSize];
  buildMessage(digest, message);
  return verifyMessage(publicKey, message, sizeof(message), signature);
}

}  // namespace fwsig
