#pragma once

#include <cstdint>
#include <string>

// Checks of detached Ed25519 firmware signatures against the key in FirmwareSigningKey.h (lib/FirmwareSignature
// does the maths). With no key configured nothing is required and every check says NotRequired.
namespace firmware_signing {

enum class Verdict { NotRequired, Valid, Missing, Invalid };

bool required();

// The 64-byte signature stored in `sigPath`; false when the file is absent or not exactly 64 bytes.
bool readSignatureFile(const char* sigPath, uint8_t out[64]);

// SHA-256 of a whole file; false on a read error.
bool sha256File(const char* path, uint8_t out[32]);

// Verifies `binPath` against the signature file `sigPath`.
Verdict verifyFile(const char* binPath, const char* sigPath);

// Verifies an image whose SHA-256 is already known (OTA staging) against `sigPath`.
Verdict verifyDigest(const uint8_t digest[32], const char* sigPath);

}  // namespace firmware_signing
