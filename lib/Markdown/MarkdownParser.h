// MarkdownParser.h
//
// Native Markdown reader (".md" / ".markdown"). Third implementation of
// FictionBookParser, so Markdown rides the same pipeline as FB2 and FB3:
// chapters from headings, table of contents, EPUB-engine typography and
// pagination, bookmarks, footnote-free links, statistics, sync mapping.
//
// Structure (see MarkdownParser.cpp for the block grammar):
//   * H1-H3 (ATX "#" or setext "===" / "---") start chapters and form the TOC,
//     nesting by heading depth. H4-H6 render as subtitles inside a chapter.
//     Text before the first heading becomes an untitled opening chapter.
//   * innerStartOffset of a chapter is the byte offset of its HEADING line, so
//     renderSectionTitle() can re-read the heading and renderSection() skips it.
//   * YAML front matter (--- ... ---) supplies title/author/language/date and
//     is never rendered.
//   * Inline: **bold**, *italic*, ***both***, ~~strike~~, `code`, escapes,
//     [text](url) -> text, ![alt](url) -> [alt]. Soft line breaks join into one
//     paragraph. Lists keep nesting and task-list boxes; tables keep alignment.
//
// The inline/line grammar is adapted from Paperio Reader's MdParser (MIT).
#pragma once

#include <FictionBookParser.h>

class MarkdownParser final : public FictionBookParser {
 public:
  bool scan(IByteReader& reader, Fb2ScanResult& outResult, size_t xmlBufferSize = 8192) override;

  bool renderCoverForFirstSection(IByteReader&, const Fb2SectionIndexEntry&, Fb2ContentSink&) override {
    return false;  // Markdown has no embedded cover
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

  bool decodeBinary(IByteReader&, const Fb2BinaryIndexEntry&, const BinaryOutputFn&,
                    const reader::ReaderCancellationToken* = nullptr) override {
    return false;  // no binaries
  }
};
