#include "FirmwareSigning.h"

#include <FirmwareSignature.h>
#include <HalStorage.h>
#include <Logging.h>
#include <mbedtls/sha256.h>

#include "FirmwareSigningKey.h"

namespace firmware_signing {

bool required() { return fwsig::keyConfigured(kPublicKey); }

bool readSignatureFile(const char* sigPath, uint8_t out[64]) {
  HalFile file;
  if (!Storage.openFileForRead("FWS", sigPath, file) || !file) return false;
  const bool sizeOk = file.fileSize() == fwsig::kSignatureSize;
  const bool readOk = sizeOk && file.read(out, fwsig::kSignatureSize) == static_cast<int>(fwsig::kSignatureSize);
  file.close();
  return readOk;
}

bool sha256File(const char* path, uint8_t out[32]) {
  HalFile file;
  if (!Storage.openFileForRead("FWS", path, file) || !file) return false;
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  uint8_t buffer[4096];
  bool ok = true;
  while (true) {
    const int n = file.read(buffer, sizeof(buffer));
    if (n < 0) {
      ok = false;
      break;
    }
    if (n == 0) break;
    mbedtls_sha256_update(&sha, buffer, static_cast<size_t>(n));
  }
  file.close();
  if (ok) mbedtls_sha256_finish(&sha, out);
  mbedtls_sha256_free(&sha);
  return ok;
}

Verdict verifyDigest(const uint8_t digest[32], const char* sigPath) {
  if (!required()) return Verdict::NotRequired;
  uint8_t signature[fwsig::kSignatureSize];
  if (!readSignatureFile(sigPath, signature)) {
    LOG_ERR("FWS", "Signature file missing or malformed: %s", sigPath);
    return Verdict::Missing;
  }
  if (!fwsig::verifyDigest(kPublicKey, digest, signature)) {
    LOG_ERR("FWS", "Signature does not match the image");
    return Verdict::Invalid;
  }
  return Verdict::Valid;
}

Verdict verifyFile(const char* binPath, const char* sigPath) {
  if (!required()) return Verdict::NotRequired;
  uint8_t digest[32];
  if (!sha256File(binPath, digest)) return Verdict::Invalid;
  return verifyDigest(digest, sigPath);
}

}  // namespace firmware_signing
