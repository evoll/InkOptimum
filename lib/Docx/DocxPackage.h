// DocxPackage.h
//
// The OPC package of a .docx: relationship walk, metadata, image table, and the
// reduction of styles.xml / numbering.xml into DocxInfo. Platform-free; it sits
// on the shared container layer in lib/FictionBookCore (OpcPackage.h).
//
// Large parts (styles.xml, numbering.xml) must never be loaded as a whole on the
// device, so they are handed to a PartStager that makes them readable through an
// IByteReader (firmware: a short-lived file in the book cache; tests: memory).
#pragma once

#include <cstdint>
#include <functional>

#include <IByteReader.h>

#include "DocxInfo.h"

namespace docx {

using PartConsumer = std::function<bool(IByteReader& part)>;
// Makes ZIP member `ordinal` readable and calls `consume` with a reader over its
// inflated bytes. Returns false if the part could not be staged or consumed.
using PartStager = std::function<bool(IByteReader& zip, uint32_t ordinal, const PartConsumer& consume)>;

using ByteSink = std::function<void(const uint8_t* data, size_t len)>;

// True if `zip` is an OPC package whose main part is WordprocessingML.
bool isDocxPackage(IByteReader& zip);

// Resolves parts, metadata, images; reduces styles and numbering through
// `stage`. Returns false for a structurally broken package.
bool readPackageInfo(IByteReader& zip, const PartStager& stage, PackageInfo& out);

// The body source the parser reads: one XML stream
//   <docx-book> document.xml [footnotes.xml] [endnotes.xml] </docx-book>
// (XML declarations of the parts removed). Footnotes and endnotes are appended
// so a chapter and its notes live in one seekable file with plain byte offsets.
bool writeBodySource(IByteReader& zip, const PackageInfo& info, const ByteSink& out);

// Reducers, exposed for tests and for PartStager consumers.
bool parseStyles(IByteReader& xmlPart, PackageInfo& info);
bool parseNumbering(IByteReader& xmlPart, PackageInfo& info);

}  // namespace docx
