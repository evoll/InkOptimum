// FictionBookParser.h
//
// Common interface of the native FictionBook readers (FB2 and FB3).
//
// Fb2 (the document class that builds the EPUB-shaped package, caches and
// renders chapters on demand) only needs the seven operations below. Both
// formats produce the same Fb2ScanResult index and drive the same
// Fb2ContentSink, so everything downstream - spine, TOC, footnotes, image
// cache, virtual chapter slicing, pagination - is shared. What differs is the
// markup the parser understands and where image bytes live:
//
//   FB2: one XML file; images are base64 <binary> elements inside it.
//   FB3: an OPC/ZIP package; the book body is one XML part and images are
//        separate ZIP entries (see Fb3Parser.h).
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include <FictionBookTypes.h>
#include "IByteReader.h"

namespace reader {
class ReaderCancellationToken;
}

class FictionBookParser {
 public:
  virtual ~FictionBookParser() = default;

  using BinaryOutputFn = std::function<void(const uint8_t* data, size_t len)>;

  // One forward pass over the book XML: metadata, section index, image index.
  virtual bool scan(IByteReader& reader, Fb2ScanResult& outResult, size_t xmlBufferSize = 8192) = 0;

  virtual bool renderCoverForFirstSection(IByteReader& reader, const Fb2SectionIndexEntry& section,
                                          Fb2ContentSink& sink) = 0;

  virtual bool renderAnnotation(IByteReader& reader, Fb2ContentSink& sink,
                                const reader::ReaderCancellationToken* cancellationToken = nullptr) = 0;

  virtual bool renderBodyPreambleForFirstSection(
      IByteReader& reader, Fb2ContentSink& sink,
      const reader::ReaderCancellationToken* cancellationToken = nullptr) = 0;

  virtual bool renderSectionTitle(IByteReader& reader, const Fb2SectionIndexEntry& section, Fb2ContentSink& sink,
                                  uint8_t level,
                                  const reader::ReaderCancellationToken* cancellationToken = nullptr) = 0;

  virtual bool renderSection(IByteReader& reader, const Fb2SectionIndexEntry& section, Fb2ContentSink& sink,
                             const reader::ReaderCancellationToken* cancellationToken = nullptr) = 0;

  // FB2: `reader` is the book XML and the entry is a base64 payload range.
  // FB3: `reader` is the PACKAGE (ZIP) and the entry names a ZIP member.
  virtual bool decodeBinary(IByteReader& reader, const Fb2BinaryIndexEntry& binary, const BinaryOutputFn& out,
                            const reader::ReaderCancellationToken* cancellationToken = nullptr) = 0;
};
