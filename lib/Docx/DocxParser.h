// DocxParser.h
//
// Native Word (.docx) reader behind the common FictionBookParser interface, so
// the document layer treats it like FB2, FB3 and Markdown: same section index,
// same content sink, same downstream TOC / footnotes / image cache / pagination.
//
// Source: the staged body stream written by docx::writeBodySource()
//   <docx-book> <w:document>...</w:document> [<w:footnotes>..] [<w:endnotes>..] </docx-book>
// Everything else the parser needs (styles, list kinds, image table, metadata)
// comes from DocxInfo, reduced once when the book was prepared.
//
// Mapping:
//   * Chapters: every Heading 1-3 paragraph (style name or outline level) starts
//     a section. Content before the first heading forms an untitled first
//     section. A document with no headings is one section (the document layer
//     slices long sections into virtual chapters).
//   * Heading 4-9, Title and Subtitle paragraphs render as subtitles; Quote
//     styles render as quotation blocks.
//   * Lists: numbered / bulleted paragraphs get a marker; numbers are counted
//     per list and level.
//   * Footnotes and endnotes become a second body named "notes" whose notes are
//     sections (ids "fn<N>" / "en<N>"), so the existing footnote machinery
//     works; references are links with a superscript number.
//   * Tables keep rows, header rows, column spans; nested tables are flattened.
//   * Images: JPEG/PNG (inline and floating DrawingML, legacy VML). Other image
//     types, text boxes, tracked deletions and field codes are skipped.
#pragma once

#include <memory>

#include <FictionBookParser.h>

#include "DocxInfo.h"

class DocxParser final : public FictionBookParser {
 public:
  // The info must outlive scan()/render calls. (setOwnedInfo keeps its own copy.)
  void setInfo(const docx::PackageInfo* info) { info_ = info; }
  void setOwnedInfo(docx::PackageInfo info) {
    owned_.reset(new docx::PackageInfo(std::move(info)));
    info_ = owned_.get();
  }

  bool scan(IByteReader& reader, Fb2ScanResult& outResult, size_t xmlBufferSize = 8192) override;

  bool renderCoverForFirstSection(IByteReader&, const Fb2SectionIndexEntry&, Fb2ContentSink&) override {
    return false;  // a .docx has no cover
  }
  bool renderAnnotation(IByteReader&, Fb2ContentSink&, const reader::ReaderCancellationToken* = nullptr) override {
    return true;
  }
  bool renderBodyPreambleForFirstSection(IByteReader&, Fb2ContentSink&,
                                         const reader::ReaderCancellationToken* = nullptr) override {
    return true;
  }

  bool renderSectionTitle(IByteReader& reader, const Fb2SectionIndexEntry& section, Fb2ContentSink& sink,
                          uint8_t level,
                          const reader::ReaderCancellationToken* cancellationToken = nullptr) override;

  bool renderSection(IByteReader& reader, const Fb2SectionIndexEntry& section, Fb2ContentSink& sink,
                     const reader::ReaderCancellationToken* cancellationToken = nullptr) override;

  // `reader` is the PACKAGE (ZIP); binary.payloadStartOffset is the member ordinal.
  bool decodeBinary(IByteReader& reader, const Fb2BinaryIndexEntry& binary, const BinaryOutputFn& out,
                    const reader::ReaderCancellationToken* cancellationToken = nullptr) override;

 private:
  const docx::PackageInfo* info_ = nullptr;
  std::unique_ptr<docx::PackageInfo> owned_;
};
