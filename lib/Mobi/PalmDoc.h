// PalmDoc.h - PalmDOC (LZ77) record decompression.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace mobi {

// Appends the decompressed form of one record to `out`. Stops quietly at
// malformed back-references (keeping what was decoded) and returns false only
// when more than `maxOut` bytes would be produced.
bool palmDocDecompress(const uint8_t* in, size_t len, std::string& out, size_t maxOut);

}  // namespace mobi
