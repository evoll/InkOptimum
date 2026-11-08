// MobiBook.h
//
// Mobipocket / Kindle reader (".mobi", ".azw", ".azw3", ".prc"): PalmDOC,
// Mobipocket 6/7 and KF8 (AZW3) books, including combined MOBI7+KF8 files, for
// which the KF8 half is used.
//
// The book is converted in two streaming steps, both bounded in memory:
//   1. extractText()   decompresses the text records into the raw markup. The
//                      caller stores it in a scratch file on the SD card.
//   2. convertToHtml() reads that raw markup and writes the normalised HTML body
//                      that HtmlParser (lib/Html) indexes and renders.
// Images are NOT copied: the HTML refers to them as
//   <img src="x-raw:START-END:mime">
// where START-END is the byte range of the image record inside the original
// file; HtmlParser::decodeBinary() reads it from there on demand.
//
// Conversion rules
//   * Mobipocket 6/7: <a filepos=N> links become #pN anchors, <img recindex=N>
//     references become x-raw images; the <head> and <mbp:*> markup is dropped.
//   * KF8: the skeleton + fragment tables are applied to rebuild the XHTML,
//     kindle:pos:fid links become #pN anchors and kindle:embed images x-raw
//     images (also those wrapped in an <svg><image> cover page). CSS and SVG
//     flows are ignored.
//   * Books without <h1>-<h3> headings get chapter headings from their NCX
//     table of contents, so they still have a usable chapter list.
//   * Text in code page 1252 (and the other pages of fictionbook::Codepage) or
//     UTF-8 is converted to UTF-8.
// Not supported: DRM-protected books (reported as Error::Drm), print replicas
// (%MOP), dictionaries' inflection tables, fonts, embedded CSS.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include <IByteReader.h>

#include "HuffCdic.h"
#include "RecordSource.h"

namespace mobi {

using TextSink = std::function<bool(const char* data, size_t len)>;
using CancelFn = std::function<bool()>;

enum class Error {
  None,
  NotMobi,
  Truncated,
  Drm,
  Unsupported,  // unknown compression, print replica, ...
  Corrupt,
  Io,
  Cancelled,
};

struct Metadata {
  std::string title;
  std::string author;
  std::string publisher;
  std::string description;
  std::string language;  // BCP-47-ish, e.g. "en", "ru"
  std::string published;
};

class HtmlConverter;

class Book final : public RecordSource {
 public:
  // Parses the Palm database and MOBI headers. `file` must outlive the Book.
  bool open(IByteReader& file);

  Error error() const { return error_; }
  const Metadata& metadata() const { return metadata_; }
  bool isKf8() const { return kf8_; }
  // Uncompressed size of the raw markup, as recorded in the header.
  uint64_t textLength() const { return header_.textLength; }

  // Step 1: raw markup (trailing entries removed, decompressed).
  bool extractText(const TextSink& out, const CancelFn& cancel = CancelFn());

  // Step 2: `rawText` is the output of extractText() stored somewhere seekable.
  bool convertToHtml(IByteReader& rawText, const TextSink& out, const CancelFn& cancel = CancelFn());

  // RecordSource
  uint32_t recordCount() const override { return numRecords_; }
  bool recordSize(uint32_t index, uint32_t& size) override;
  bool readRecord(uint32_t index, std::string& out, size_t maxSize) override;
  bool readRecordPart(uint32_t index, uint32_t offset, void* buf, size_t len) override;

  // Appends `n` bytes of book text, converted from the book's code page to UTF-8.
  void appendText(std::string& out, const char* s, size_t n) const;

  // Byte range of an image resource (1-based, as written in recindex= and
  // kindle:embed:) and its MIME type; false for non-image or unknown records.
  bool imageRange(uint32_t number, uint64_t& start, uint64_t& end, const char*& mime);

 private:
  friend class HtmlConverter;

  struct Header {
    uint32_t start = 0;  // record index of this header (0, or the KF8 half)
    uint16_t compression = 1;
    uint32_t textLength = 0;
    uint16_t textRecords = 0;
    uint16_t crypto = 0;
    bool mobi = false;  // has the "MOBI" magic (not plain PalmDOC)
    uint32_t headerLen = 0;
    uint32_t version = 0;
    uint32_t minVersion = 0;
    uint32_t codepage = 1252;
    uint32_t langCode = 0;
    uint32_t firstImage = 0xFFFFFFFFu;  // absolute record index
    uint32_t huffRecord = 0xFFFFFFFFu;  // absolute
    uint32_t huffCount = 0;
    uint32_t ncx = 0xFFFFFFFFu;   // absolute
    uint32_t frag = 0xFFFFFFFFu;  // absolute
    uint32_t skel = 0xFFFFFFFFu;  // absolute
    uint32_t fdst = 0xFFFFFFFFu;  // absolute
    uint32_t fdstCount = 0;
    uint16_t trailFlags = 0;
    uint32_t coverOffset = 0xFFFFFFFFu;  // EXTH 201, relative to the MOBI7 firstImage
    // Combined files: firstImage of the MOBI7 half, the base EXTH 201 refers to (kNone otherwise).
    uint32_t coverBase = 0xFFFFFFFFu;
  };

  bool recordOffset(uint32_t index, uint32_t& offset);
  bool parseHeader(uint32_t startRecord, Header& h, Metadata& md);
  void stripTrailingEntries(std::string& rec) const;

  IByteReader* file_ = nullptr;
  Error error_ = Error::None;
  uint32_t numRecords_ = 0;
  uint64_t fileSize_ = 0;
  Header header_;
  bool kf8_ = false;
  Metadata metadata_;
  HuffCdic huff_;
};

}  // namespace mobi
