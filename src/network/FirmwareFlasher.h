#pragma once

#include <esp_partition.h>

#include <cstddef>
#include <cstdint>

// Flash a firmware image from an SD-card path into the next OTA app
// partition, then switch otadata so the X3/X4 stock bootloader picks it up
// on next boot. Mirrors the web flasher: raw esp_partition_erase_range +
// esp_partition_write + ota_boot::switchTo (no Arduino Update class, no
// esp_image_verify — those reject our patched image on X4 silicon).
//
// Both the SD update activity and the OTA path land here. OTA first
// downloads the firmware to an SD-card cache file, then calls this.

namespace firmware_flash {

enum class Result {
  OK,
  OPEN_FAIL,
  TOO_SMALL,
  TOO_LARGE,
  BAD_MAGIC,
  INCOMPATIBLE_CHIP,  // ESP image chip_id at offset 0x0C differs from the running firmware
  INCOMPATIBLE_BOARD,  // image carries a board marker for another device (e.g. X4 Classic on X4 Pro)
  UNTAGGED_IMAGE,      // no board marker on a chip shared by several boards (ESP32-S3)
  BAD_SEGMENTS,  // segment table malformed or runs past EOF
  BAD_CHECKSUM,  // ESP image XOR checksum mismatch
  BAD_SHA,       // SHA256 trailer mismatch (hash_appended images)
  BAD_SIZE,      // body+pad+sha length doesn't match file size
  NO_PARTITION,
  OOM,
  READ_FAIL,
  ERASE_FAIL,
  WRITE_FAIL,
  OTADATA_FAIL,
};

// Progress callback: called after every chunk write. `written`/`total` are bytes.
using ProgressCb = void (*)(size_t written, size_t total, void* ctx);

// Open `sdPath`, validate it looks like an ESP32 image, then stream it into the
// next OTA app partition with interleaved 64 KiB erase + sector writes. On
// success switches otadata via ota_boot::switchTo. Caller is responsible for
// ESP.restart() afterwards.
//
// `alreadyValidated` lets callers that have just run `validateImageFile()`
// themselves (e.g. SdFirmwareUpdateActivity, which validates before showing
// the user the confirmation prompt) skip the redundant second pass. Defaults
// to false so callers without prior validation (any future entry point) keep
// the defense-in-depth check.
//
// `allowUntagged` is the user's explicit acknowledgement that an image without a board marker
// (stock / third-party firmware) may be flashed on a chip shared by several boards (X4 Pro /
// X4 Classic). It never overrides a marker that names a *different* board.
Result flashFromSdPath(const char* sdPath, ProgressCb onProgress, void* ctx, bool alreadyValidated = false,
                       bool allowUntagged = false);

// Full-image integrity check that mirrors the bootloader's verification:
// header magic, chip-family compatibility (ESP image uint16_t chip_id at offset 0x0C),
// board-variant marker (X4 Pro vs X4 Classic share the ESP32-S3, so chip_id alone cannot tell them
// apart), segment table walk, XOR checksum, and SHA256 trailer (when
// hash_appended == 1). Run this before flashing a candidate firmware so a
// truncated/corrupted .bin never reaches otadata.
//
// `partitionSize` is the size of the destination OTA partition; pass 0 to
// skip the size-fits-partition check (e.g. when validating ahead of partition
// lookup). Streams the file in CHUNK-sized reads; the file is rewound on
// success so the caller can immediately reread it for flashing.
Result validateImageFile(const char* sdPath, size_t partitionSize, bool allowUntagged = false);

// Validates an already-written OTA partition without esp_image_verify (which
// rejects the patched X3/X4 images despite the stock bootloader accepting them).
Result validateImagePartition(const esp_partition_t* partition, size_t* imageSize = nullptr);

const char* resultName(Result r);

}  // namespace firmware_flash
