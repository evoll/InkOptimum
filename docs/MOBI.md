# MOBI / AZW / AZW3 books

`.mobi`, `.azw`, `.azw3`, `.prc` open as books (`lib/Mobi`). Supported: PalmDOC and
Mobipocket 6/7, KF8 (AZW3) and combined MOBI7+KF8 files (the KF8 half is used);
PalmDOC (LZ77), uncompressed and HUFF/CDIC compression.

How it works: the text is decompressed into a scratch file, converted once into
normalised HTML in the book cache and then read by `HtmlParser`. Memory use is flat:
the HUFF/CDIC phrase dictionary is read from the book on demand. Images stay in the
original file and are decoded from their records when a page needs them.

- KF8: skeleton + fragment tables are applied, `kindle:pos` links and `kindle:embed`
  images (including SVG-wrapped covers) are resolved; CSS/SVG flows are ignored.
- Mobipocket 6/7: `filepos` links, `recindex` images.
- Books without `<h1>`–`<h3>` get chapters from their NCX table of contents.
- Text in code page 1252 (and the other shared pages) or UTF-8; cover from EXTH.

Not supported: DRM-protected books (Kindle/Mobipocket DRM), print replicas, fonts,
embedded CSS, dictionaries.

Validation status: tests (`test/native_mobi`) use synthetic files written from the
format documentation. Real books with unusual index layouts or HUFF/CDIC data have
not been verified; report any book that fails to open.
