# FB3 support

InkOptimum reads **FictionBook 3** natively, the same way it reads FB2: its own parser
indexes the book into sections and renders them one at a time. FB3 is **not** converted to
FB2 or EPUB on the way in.

| File | Handling |
|---|---|
| `book.fb3` | OPC/ZIP package, read in place |
| `book.fb3.zip` | ZIP wrapping a `.fb3`; the inner file is staged once as `.package.fb3` in the book cache |
| `book.zip` (any name) | a ZIP with no `.fb2` inside is probed as an FB3 package |

Real `.fb2.zip` behaviour is unchanged: a `.zip` is only tried as FB3 when it contains no `.fb2`.

## Architecture

```
FictionBookParser (interface)         scan · renderSection · renderSectionTitle
 ├─ Fb2Parser   (FB2 XML, base64)     renderAnnotation · renderBodyPreamble · renderCover · decodeBinary
 └─ Fb3Parser   (FB3 body XML + OPC package)
Fb2 (document class) ── Fb2ScanResult index ── StreamSink ── chapter XHTML ── EPUB renderer
```

Both parsers produce the same `Fb2ScanResult` and drive the same `Fb2ContentSink`, so the spine,
TOC, footnotes, image cache, virtual chapter slicing, bookmarks and caches are shared.

Preparing an FB3 (`fb3::prepareSource`):

1. read the package structure (`Fb3Package`): root relationships → description part → body part,
   body relationships → images, `[Content_Types].xml` → image types;
2. inflate the **body part once** into `<cache>/.source.fb3body` (a ZIP member cannot be seeked
   inside; FB2.ZIP has the same extract-once step) — the parser then works on that file by byte offset;
3. write two small sidecars: the description XML (annotation page) and the cover's binary id.

Images are never copied into the XML. Each stays a ZIP member; the image index stores its
central-directory ordinal and the bytes are inflated straight from the package when a page needs them.

Package rules honoured (the official "Hardcore file structure" sample tests them): part names are
ASCII-case-insensitive and percent-decoded, relationship targets resolve relative to the owning part
(`.`/`..`, absolute `/…`), the body may have no extension, and an image's type comes from
`[Content_Types].xml`, not its file name.

### Element mapping (FB3 → reader events)

| FB3 | Reader |
|---|---|
| `section`, `preamble` | chapters (nested sections are separate chapters) |
| `notes` / `notebody` | second body `notes`; each `notebody` is a section, so footnote pop-ups work as in FB2 |
| `title` (direct child) | chapter title (multi-paragraph/`br` → line breaks); other titles read as subtitles |
| `p` | paragraph (opened on first visible content; a picture alone stays a block image) |
| `note`, `a` | link (ids are used raw, matching section ids) |
| `epigraph`, `blockquote`, `pre` | epigraph / quote block |
| `poem`/`stanza`/`p` | poem, stanza, verse |
| `subscription` | text-author line |
| `ul`/`ol`/`li` | paragraphs with bullet / number marker, indented per nesting level |
| `table`, `tr`, `td`, `th` | table with colspan/rowspan/align |
| `strong` `em` `underline` `strikethrough` `sub` `sup` `smallcaps` | inline styles |
| `br`, `div`, `span`, `marker` | empty line / transparent |

Scan and render agree on the image count per section (the document layer slices illustration-heavy
sections by it); a test checks this on the official samples.

### Known limits

- Images: JPEG and PNG only (what the firmware decodes). SVG/GIF parts are not indexed.
- Not representable and dropped: `marker`/`paper-page-break` anchors, letter-spacing, floats, image sizing.
  Links to those targets (and to the root element) do not resolve.
- Caps for RAM safety: 512 images, 128 KiB description, 28 KiB section string pool.
- Cover: a body image relationship whose id starts with `cover`, else the package thumbnail.

## Tests

`test/native_fb3` (22 tests, clean under ASan/UBSan, no expat dependency). To also run the official corpus:

```
git clone https://github.com/gribuser/fb3 /tmp/fb3
FB3_SAMPLES_DIR=/tmp/fb3/Examples ./NativeFb3Test
```

The corpus is intentionally not vendored (its schemas have a no-modification licence).

## Code layout

| Directory | Contents |
|---|---|
| `lib/FictionBookCore/` | shared parser contract (`FictionBookParser.h`), index/sink types (`FictionBookTypes.h`), `XmlPullReader`, `ZipReader`, `Inflate`, `IByteReader`, `FsFileReader`, `BufferedFileWriter`, `PathUtil` |
| `lib/Fb3/` | OPC helpers (`OpcNames.h`), package layer (`Fb3Package`), parser (`Fb3Parser`), storage side (`Fb3Source`: `fb3::prepareSource`, `fb3::packagePath`, `fb3::loadPackageInfo`, `fb3::makeRenderParser`) |
| `lib/Fb2/` | FB2 parser (`native/`) and the `Fb2` document class that dispatches to FB2/FB3/Markdown parsers |

`lib/Fb3` depends only on `lib/FictionBookCore`; it contains no FB2 code and does not use expat.

