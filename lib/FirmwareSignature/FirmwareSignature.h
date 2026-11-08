#pragma once

#include <cstddef>
#include <cstdint>

// Ed25519 signatures over firmware images, so an update can be checked against a key that is not the
// download channel's: a digest published next to the image proves the download is intact, not who
// made it. The signed message is "INKOPT-FW-v1\n" followed by the 32-byte SHA-256 of the whole .bin
// (45 bytes); the 64-byte raw signature lives beside the image as "<image>.bin.sig". Sign with
// tools/sign_firmware.sh. The check is Monocypher's RFC 8032 Ed25519 (lib/Monocypher).
//
// This library holds no key. The caller passes the public key it trusts; an all-zero key means "no
// key configured" and keyConfigured() says so, so a build that ships without a signing key keeps
// today's behaviour instead of rejecting every image.
namespace fwsig {

constexpr size_t kPublicKeySize = 32;
constexpr size_t kDigestSize = 32;
constexpr size_t kSignatureSize = 64;

// The domain prefix of the signed message. Part of the format: changing it invalidates old signatures.
constexpr char kMessagePrefix[] = "INKOPT-FW-v1\n";
constexpr size_t kMessagePrefixLen = sizeof(kMessagePrefix) - 1;
constexpr size_t kMessageSize = kMessagePrefixLen + kDigestSize;

// False for the all-zero placeholder key.
bool keyConfigured(const uint8_t publicKey[kPublicKeySize]);

// Builds the 45-byte message that is signed for an image digest.
void buildMessage(const uint8_t digest[kDigestSize], uint8_t message[kMessageSize]);

// True when `signature` is `publicKey`'s signature over the message for `digest`. A key that is not
// configured never verifies.
bool verifyDigest(const uint8_t publicKey[kPublicKeySize], const uint8_t digest[kDigestSize],
                  const uint8_t signature[kSignatureSize]);

// Any other message signed with the same key; the caller's message carries its own domain prefix.
bool verifyMessage(const uint8_t publicKey[kPublicKeySize], const uint8_t* message, size_t len,
                   const uint8_t signature[kSignatureSize]);

}  // namespace fwsig
