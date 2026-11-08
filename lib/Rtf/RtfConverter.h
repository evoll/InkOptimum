// RtfConverter.h
//
// RTF reader, first half: a streaming converter from Rich Text Format to the
// normalised HTML that HtmlParser (lib/Html) indexes and renders. The converter
// runs once while the book is prepared and never builds a document tree, so it
// needs only a few KiB regardless of the file size.
//
// Supported:
//   * paragraphs, line breaks, tabs, page/section breaks (-> paragraph breaks)
//   * bold, italic, underline, strike-through, super/subscript, small caps
//   * chapters: stylesheet styles named "heading 1..3" / "Title" / "Subtitle"
//     and \outlinelevelN become <h1>..<h6>
//   * tables (\trowd / \intbl / \cell / \row), flattened to text cells
//   * text in Windows code pages (\ansicpg, \fcharsetN per font: 437, 866,
//     1250, 1251, 1252, 1253, 1254, 1257) and Unicode (\uN with \ucN fallback)
//   * internal hyperlinks (HYPERLINK \l "name" fields) and bookmarks
//   * title and author from {\info}
//   * PNG and JPEG pictures: the hex payload is NOT copied; the HTML carries
//     <img src="x-hex:START-END:mime"> pointing at the payload inside the
//     original RTF, and HtmlParser::decodeBinary() decodes it on demand.
// Ignored: headers/footers, footnotes, OLE objects, EMF/WMF pictures, lists
// formatting beyond the literal bullet/number text Word writes (\listtext).
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include <IByteReader.h>

namespace rtf {

// Receives output chunks; return false to abort (for example on a write error).
using TextSink = std::function<bool(const char* data, size_t len)>;
// Polled periodically; return true to abort.
using CancelFn = std::function<bool()>;

// True when the first bytes (after optional whitespace/BOM) start with "{\rtf".
bool looksLikeRtf(const uint8_t* head, size_t len);

// Converts the document in `in` to HTML. Image ranges refer to offsets in `in`.
bool convertToHtml(IByteReader& in, const TextSink& out, const CancelFn& cancel = CancelFn());

}  // namespace rtf
