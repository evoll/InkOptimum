# RTF books

`.rtf` files are converted once into normalised HTML in the book cache
(`lib/Rtf`, streaming, a few KiB of RAM) and then read by `HtmlParser`.

- Paragraphs, line/page breaks, bold/italic/underline/strike, sub/superscript, small caps.
- Chapters from styles named `heading 1..3` / `Title` / `Subtitle` and `\outlinelevelN`.
- Tables (flattened to text), bookmarks and internal hyperlinks, title/author from `{\info}`.
- Code pages `\ansicpg` / `\fcharsetN` (437, 866, 1250–1254, 1257) and Unicode (`\uN`).
- PNG and JPEG pictures stay inside the `.rtf` (hex payload decoded on demand).

Ignored: headers/footers, footnotes, OLE objects, EMF/WMF pictures, list formatting
beyond the literal bullet/number text. Tests: `test/native_rtf`.
