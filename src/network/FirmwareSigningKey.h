#pragma once

#include <cstdint>

// The Ed25519 public key firmware updates must be signed with. All zeros = no key configured: updates are
// accepted as before (integrity only). To enforce signatures, paste the output of
//   tools/sign_firmware.sh --public-key
// here and sign every release image with tools/sign_firmware.sh (upload <image>.sig beside the .bin).
namespace firmware_signing {
inline constexpr uint8_t kPublicKey[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
}  // namespace firmware_signing
