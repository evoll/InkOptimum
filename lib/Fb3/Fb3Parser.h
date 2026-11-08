// Fb3Parser.h
//
// Native FictionBook 3 reader. Same two-phase design and the same index/sink
// contract as Fb2Parser (see FictionBookParser.h), so the document layer treats
// both formats identically:
//
//   scan()          one forward pass over the BODY part (an XML file in the
//                   book cache) -> section index with byte offsets, titles,
//                   nesting, per-section image counts. Metadata and the image
//                   table come from the package (setPackageInfo()).
//   renderSection() seeks to a section's offset and streams just that section
//                   into an Fb2ContentSink.
//   decodeBinary()  inflates one image straight from the package ZIP.
//
// Differences from FB2 that this class absorbs:
//   * Root is <fb3-body>; book metadata is a separate "description" part.
//   * Chapters are <section> (and <preamble>); endnotes are <notes>/<notebody>,
//     exposed as a second body named "notes" whose notebodies are sections, so
//     the existing footnote machinery works unchanged.
//   * Paragraph content mixes text, <note> references, <a>, <img>, <span>...
//     Images are referenced by relationship id; the id is the "binary id".
//   * Lists, preformatted blocks, <div>, <br> and tables have their own shapes
//     and are mapped to the sink's block events (list items become marked
//     paragraphs, <pre> becomes a quote block, <div> is transparent).
//
// Element handling is by local name, so a document using a namespace prefix
// (<fb3:p>) reads the same as one using the default namespace.
#pragma once

#include <string>

#include "Fb3Package.h"
#include <FictionBookParser.h>

class Fb3Parser final : public FictionBookParser {
 public:
  // Package data for scan(): image table, cover candidate, description XML.
  // The pointer must outlive the scan() call.
  void setPackageInfo(const fb3::PackageInfo* info) { info_ = info; }

  // For parser objects that only render (no package at hand): the raw
  // description XML (annotation) and the cover's binary id.
  void setRenderContext(std::string descriptionXml, std::string coverBinaryId) {
    descriptionXml_ = std::move(descriptionXml);
    coverId_ = std::move(coverBinaryId);
    haveRenderContext_ = true;
  }

  bool scan(IByteReader& reader, Fb2ScanResult& outResult, size_t xmlBufferSize = 8192) override;

  bool renderCoverForFirstSection(IByteReader& reader, const Fb2SectionIndexEntry& section,
                                  Fb2ContentSink& sink) override;

  bool renderAnnotation(IByteReader& reader, Fb2ContentSink& sink,
                        const reader::ReaderCancellationToken* cancellationToken = nullptr) override;

  bool renderBodyPreambleForFirstSection(IByteReader& reader, Fb2ContentSink& sink,
                                         const reader::ReaderCancellationToken* cancellationToken = nullptr) override;

  bool renderSectionTitle(IByteReader& reader, const Fb2SectionIndexEntry& section, Fb2ContentSink& sink,
                          uint8_t level,
                          const reader::ReaderCancellationToken* cancellationToken = nullptr) override;

  bool renderSection(IByteReader& reader, const Fb2SectionIndexEntry& section, Fb2ContentSink& sink,
                     const reader::ReaderCancellationToken* cancellationToken = nullptr) override;

  // `reader` is the PACKAGE (ZIP). binary.payloadStartOffset is the member's
  // central-directory ordinal (see Fb3Package.h).
  bool decodeBinary(IByteReader& reader, const Fb2BinaryIndexEntry& binary, const BinaryOutputFn& out,
                    const reader::ReaderCancellationToken* cancellationToken = nullptr) override;

 private:
  const std::string& descriptionXml() const { return info_ ? info_->descriptionXml : descriptionXml_; }
  const std::string& coverId() const { return info_ ? info_->coverBinaryId : coverId_; }

  const fb3::PackageInfo* info_ = nullptr;
  std::string descriptionXml_;
  std::string coverId_;
  bool haveRenderContext_ = false;
};
