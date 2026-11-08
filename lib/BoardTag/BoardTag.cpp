#include "BoardTag.h"

#include <AppVersion.h>
#include <sdkconfig.h>

namespace board_tag {

// Must stay a single contiguous literal in .rodata; external linkage prevents it from being
// optimised away, and embeddedTag() gives the linker a reference to keep it.
extern const char kEmbeddedTag[];
const char kEmbeddedTag[] = "INKMOD_BOARD=" INKMOD_FIRMWARE_VARIANT ";";

const char* currentVariant() { return INKMOD_FIRMWARE_VARIANT; }

const char* embeddedTag() { return kEmbeddedTag; }

bool allowUntaggedImages() {
#if defined(CONFIG_IDF_TARGET_ESP32C3)
  return true;
#else
  return false;
#endif
}

}  // namespace board_tag
