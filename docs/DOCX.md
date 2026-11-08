# DOCX support

`.docx` (Word) files are read natively on the same pipeline as FB2, FB3 and Markdown: their own parser
produces the shared section index and drives the shared content sink, so TOC, footnotes, bookmarks, image
cache and pagination work unchanged. Nothing is converted to FB2 or EPUB text.

## What is understood

| Word | Reader |
|---|---|
| Heading 1–3 (style name, inherited style, or outline level) | chapters / TOC (levels 1–3) |
| Heading 4–9, Title, Subtitle | subtitle lines |
| Quote, Intense Quote, Block Text | quotation block |
| Numbered / bulleted lists (direct or style-bound), nested levels | marker per paragraph, numbers counted per list and level |
| Bold, italic, underline, strikethrough, small caps, super/subscript (direct, or via character/paragraph style) | inline styles |
| Footnotes and endnotes | notes body; references are links with a superscript number |
| Internal hyperlinks (bookmarks) | links; external URLs keep their text only |
| Tables | rows, header rows, column spans, vertical alignment; nested tables are flattened |
| Images (JPEG, PNG; inline, floating, legacy VML) | images; GIF/EMF/WMF/SVG are skipped |
| Core properties | title, author, language, date |

Content before the first heading becomes an untitled first section; a document without headings is a single
section (long ones are sliced into virtual chapters like any other book). Skipped on purpose: text boxes,
tracked deletions, field codes, table-of-contents paragraphs (`TOC 1–9`), headers/footers, comments.

## Code layout

| Directory | Contents |
|---|---|
| `lib/Docx/` | `DocxPackage` (OPC walk, styles/numbering reduction, body stream), `DocxInfo` (reduced package data + text sidecar), `DocxParser`, `DocxSource` (storage side), `DocxFormat.h` |
| `lib/FictionBookCore/` | shared contract and building blocks, including the OPC container layer (`OpcPackage`, `OpcNames.h`) also used by `lib/Fb3` |

`lib/Docx` depends only on `lib/FictionBookCore`.

## How a book is opened

`docx::prepareSource` (once per book) writes into the book cache:

- `.source.docxbody` — `<docx-book>` stream: `document.xml` + `footnotes.xml` + `endnotes.xml` (inflated once;
  a ZIP member cannot be seeked inside), so a chapter and its notes are addressed by plain byte offsets;
- `.docx_info` — metadata, the styles that matter to a reader, list kinds and the image table.

`styles.xml` (often 100+ KiB) is never held in RAM: it is inflated to a short-lived cache file, reduced by
streaming, and deleted. Images are not copied; each is inflated from the user's `.docx` by ZIP ordinal when
shown. Chapter rendering reads only the body stream and the small sidecar.

## Limits and notes

- Maximum 512 images and 512 styles of interest are indexed.
- Paragraph-level formatting (alignment, indents, spacing, fonts, colours) is intentionally ignored; the
  reader applies its own typography.
- Password-protected and legacy `.doc` files are not supported.
- Tests: `test/native_docx` (33 tests, clean under ASan/UBSan, including a mutation robustness run).
  Firmware build and on-device behaviour have not been verified here.
