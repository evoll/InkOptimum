# Markdown support

`.md` and `.markdown` files are read natively on the same pipeline as FB2 and FB3
(`MarkdownParser` implements `FictionBookParser`; see [FB3.md](FB3.md) for the architecture).
Previously `.md` opened in the plain-text reader and showed raw markup.

Because Markdown rides the EPUB-style reading engine you get the same things FB2/FB3 get:
real chapters and a table of contents, the normal typography and pagination, bookmarks,
reading statistics and progress. The user's file is the source (no extraction or conversion).

## What is understood

| Markdown | Result |
|---|---|
| `#`, `##`, `###` and setext `===` / `---` headings | chapters and TOC, nested by depth |
| `####`–`######` | bold subtitle inside the chapter |
| text before the first heading | untitled opening chapter |
| `---` front matter (`title`, `author`, `lang`, `date`) | book metadata, never rendered |
| consecutive lines | one paragraph (soft breaks join) |
| `*italic*` `**bold**` `***both***` `~~strike~~` | inline styles (intraword `_` stays literal) |
| `` `code` `` | plain text (no monospace face on the device) |
| `[text](url)` / `![alt](url)` / `<https://…>` | text / `[alt]` / the address |
| `-` `*` `+` / `1.` lists, nesting by real indent width, `- [ ]` / `- [x]` | marked paragraphs, ☐ ☑ boxes |
| `>` quotes | one quote block per run |
| fenced code (```` ``` ```` / `~~~`) | quote block, one line per paragraph, indentation kept |
| GFM pipe tables with `:--` / `:-:` / `--:` | table with header row and column alignment |
| `---`, `***` rules | empty line |
| inline HTML tags | dropped (`<br>` becomes a space) |

Not supported (rendered as ordinary text): indented (4-space) code blocks, reference-style links,
footnotes, raw HTML blocks, embedded images (shown as `[alt]`).

## Robustness

Lines longer than 32 KiB and paragraphs longer than 64 KiB are split at a space, never truncated
(a test round-trips a 300 KiB single line word for word). Chapter boundaries come from one block
reader shared by indexing and rendering, so the table of contents can never disagree with the pages.
Fuzzed with 1500 random documents under ASan/UBSan: no crash or hang.

## Credits

The inline grammar is adapted from Paperio Reader's `MdParser` (MIT, <https://github.com/Kapfilm/Paperio-Reader>);
block structure, chapters, tables and the reader integration are new.

## Tests

`test/native_markdown` (17 tests).

## Code layout

The parser lives in its own library, `lib/Markdown/` (`MarkdownParser`, `MarkdownFormat.h`). It depends only on the
shared contract in `lib/FictionBookCore/`; the `Fb2` document class in `lib/Fb2/` selects it by file extension.

