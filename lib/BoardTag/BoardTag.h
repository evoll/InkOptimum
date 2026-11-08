#pragma once

#include "BoardTagScanner.h"

namespace board_tag {

// The board variant this firmware was built for (INKMOD_FIRMWARE_VARIANT).
const char* currentVariant();

// Full embedded marker, e.g. "INKMOD_BOARD=x4pro;". Referencing it from code keeps the linker
// from discarding the marker, which is what the flasher of *other* builds looks for.
const char* embeddedTag();

// Untagged candidate images are acceptable only on the ESP32-C3 (X3/X4) build.
bool allowUntaggedImages();

}  // namespace board_tag
