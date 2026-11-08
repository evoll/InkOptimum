// ZipReader.h
//
// Minimal, dependency-free ZIP container reader — just enough to find a
// single named entry (the .fb2 inside an .fb2.zip) and stream its
// decompressed bytes out. Not a general-purpose zip library: no writing,
// no ZIP64 (>4GB entries — irrelevant for ebooks), no multi-disk archives,
// no encryption. Reads the Central Directory (authoritative entry sizes,
// even for streaming-written zips using the data-descriptor bit) rather
// than trusting local file headers for anything but the exact filename/
// extra-field lengths needed to locate each entry's data.

#pragma once
#include <cstdint>
#include <string>
#include <functional>
#include "IByteReader.h"

struct ZipEntryInfo {
    std::string filename;
    uint16_t method = 0;            // 0 = stored, 8 = DEFLATE
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    uint32_t localHeaderOffset = 0;
};

// Scans the archive's Central Directory for the first entry whose filename
// ends in ".fb2" (case-insensitive) — the normal shape of an .fb2.zip
// (exactly one entry). Returns false if `zip` isn't a valid ZIP or has no
// such entry.
bool findFb2EntryInZip(IByteReader& zip, ZipEntryInfo& outEntry);

using ZipOutputFn = std::function<void(const uint8_t* data, size_t len)>;

// Streams the decompressed bytes of a previously-located entry to `out`.
// Supports method 0 (stored) and method 8 (DEFLATE, the near-universal
// default for zip tools). Any other method (rare: bzip2/LZMA/etc.) returns
// false — not worth a dependency for a case real .fb2.zip files don't hit.
bool extractZipEntry(IByteReader& zip, const ZipEntryInfo& entry, const ZipOutputFn& out);

// Visits every Central Directory entry in archive order, handing the callback
// the entry's full filename and location. The callback returns false to stop
// early. The ZipEntryInfo reference (and its filename string) is reused
// between calls, so copy anything that must outlive the callback.
//
// This is the general-purpose companion to findFb2EntryInZip(): OPC-style
// packages (FB3, and anything else that is "a ZIP of named parts") need
// lookups by name, but must never hold the whole directory in RAM - a book
// with hundreds of images would cost tens of KiB on a 380 KB device. Callers
// instead re-scan the directory with a small bounded set of wanted names.
//
// Returns false if `zip` is not a valid ZIP or its directory is malformed
// (even if the callback already saw some entries); true if the walk finished
// or the callback stopped it deliberately.
using ZipEntryVisitor = std::function<bool(const ZipEntryInfo& entry)>;
bool forEachZipEntry(IByteReader& zip, const ZipEntryVisitor& visit);
