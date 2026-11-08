# HTML books

`.html`, `.htm` and `.xhtml` files open as books (`lib/Html`, `HtmlParser`).

- Chapters start at `<h1>`–`<h3>` (the TOC nests by heading depth); `<h4>`–`<h6>` are
  subtitles. Text before the first heading becomes an untitled opening chapter.
- Paragraphs, lists (nested), quotes, `<pre>`, `<hr>`, `<br>`, tables (cells are
  flattened to text), bold/italic/underline/strike, sub/superscript, small caps
  (`font-variant` or class `smallcaps`/`sc`), `#fragment` links.
- Skipped: scripts, styles, svg, math, forms controls, iframes, media.
- Metadata from `<title>`, `<meta name=author|language|description|date|cover>`.
- Images: `data:` URIs (png/jpeg/gif) and relative files next to the book. Remote
  images, SVG and WebP are not shown.
- Encoding: UTF-8 is read in place. A declared legacy charset (windows-125x, cp866,
  koi8-r, ISO-8859-1/2/7/9/13) or UTF-16 is converted once into the book cache.

The same parser reads the converted bodies of RTF and MOBI/AZW3 books.
Tests: `test/native_html`.
