// HtmlParser.h
//
// Native HTML reader (".html" / ".htm" / ".xhtml"). Fourth implementation of
// FictionBookParser next to FB2, FB3, DOCX and Markdown, and the engine behind
// the RTF and MOBI/AZW3 readers: those formats are converted once, while the
// book is prepared, into a normalised HTML body (html::kConvertedBodyName in
// the book cache) which this class then indexes and renders like any other
// HTML file.
//
// Structure
//   * <h1>-<h3> start chapters and form the table of contents (nested by
//     heading depth, like Markdown). <h4>-<h6> render as subtitles. Text before
//     the first heading becomes an untitled opening chapter.
//   * innerStartOffset of a chapter is the offset of its heading START TAG, so
//     renderSectionTitle() re-reads the heading and renderSection() skips it.
//   * <title> / <meta name=author|language|description|date|cover> and
//     <html lang> supply the metadata.
//   * Blocks: paragraphs (p, div, ...), lists with nesting, blockquote, pre,
//     hr, tables (cells flattened to text), definition lists, <br> line
//     breaks. Inline: bold, italic, underline, strike, sup/sub, small caps
//     (font-variant or a "smallcaps"/"sc" class), internal #links.
//     Scripts, styles, <head> content, svg, noscript and similar are skipped.
//
// Images are indexed as binaries whose id starts with the encoding:
//   d<hex>  <img src="data:image/...;base64,...">  payload range inside the HTML
//   f<hex>  relative file reference; the range holds the src text, resolved by
//           the ExternalImageLoader (HtmlSource.cpp supplies the SD one)
//   r<hex>  src="x-raw:START-END:mime"  raw bytes at START..END of the reader
//           handed to decodeBinary (MOBI image records)
//   h<hex>  src="x-hex:START-END:mime"  hex text at START..END (RTF pictures)
// <hex> is the offset of the <img> tag, so ids are stable across partial reads.
#pragma once

#include <functional>
#include <string>
#include <utility>

#include <FictionBookParser.h>

class HtmlParser final : public FictionBookParser {
 public:
  // Streams the bytes of a relative image reference (as written in the HTML) to
  // `out`; returns false when it cannot be found.
  using ExternalImageLoader = std::function<bool(const std::string& src, const BinaryOutputFn& out)>;

  void setExternalImageLoader(ExternalImageLoader loader) { externalLoader_ = std::move(loader); }

  bool scan(IByteReader& reader, Fb2ScanResult& outResult, size_t xmlBufferSize = 8192) override;

  bool renderCoverForFirstSection(IByteReader&, const Fb2SectionIndexEntry&, Fb2ContentSink&) override {
    return false;  // the cover is exposed through metadata.coverBinaryId
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

  bool decodeBinary(IByteReader& reader, const Fb2BinaryIndexEntry& binary, const BinaryOutputFn& out,
                    const reader::ReaderCancellationToken* cancellationToken = nullptr) override;

 private:
  ExternalImageLoader externalLoader_;
};
