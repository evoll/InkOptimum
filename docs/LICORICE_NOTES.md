# Notes on the Licorice report (XTOS Licorice 26112401, static analysis, 2026-11-27)

Source: the reverse-engineering report supplied by the owner (`revengreport.zip`, ru/en). It is a static analysis
of an official firmware image; no code or format is copied from it and encrypted official apps are not touched
(docs/XTAPPS.md). Only the ideas below are compared with what InkOptimum does.

| Licorice mechanism (report) | InkOptimum today | Verdict |
|---|---|---|
| `.ezix`: persistent ZIP central-directory index, FNV-1a, 256 buckets | `ZipFile` scans the central directory in 4 KB-class windows with a sequential cursor (`ZipCentralDirWindow.h`), batch size lookup by FNV-64 (`getInflatedFileSizes`), per-open stat cache; `book.bin` already stores spine data | Not taken. A scan of a typical EPUB (50-300 entries) is tens of ms once; an on-card index adds a second file to keep valid for no measurable gain. Revisit only for books with thousands of entries |
| `.xtss`: structural EPUB cache checked on open | `book.bin` (BookMetadataCache) with a version constant | Same idea, already there |
| Per-chapter unpacked XHTML reused (`.tmp` then publish) | Chapters are inflated from the ZIP on demand | Not taken: inflate is a small part of a chapter build next to parsing and layout |
| `.xtcm`/`.xtpi`: layout numbers, two RAM slots for neighbour data | Section files on SD (`section_*.bin`), look-ahead of the next section, sibling builds, next-page pre-render | Same idea; our unit is a page-offset table per section |
| Time-budgeted calculation, budget 4 / 6 / 45 by state, resumes between calls | Section builds carry a cancellation token and run in memory modes (Normal/Safe/Survival), idle work is cancelled by input (`reader::ScopedReaderWork`) | Equivalent; no change |
| Readiness test for one position before the whole calculation ends | Reader opens at the page of the saved position once its section exists; sibling sections are built for the page map only | Partly: no partial section yet (see below) |
| Output records buffered before writing (fewer small writes) | `BufferedWriter`, section files written in blocks; stats now skip identical rewrites (entry 38); SdFat folder zeroing batched (entry 41) | Done |

## The one idea that would change first-open time

The report's open question is the same as ours: how long from "open" to the first readable page with a cold cache.
InkOptimum lays out a whole section before showing its first page. Showing the first pages while the rest of the
section is still being laid out (a partial section file that grows) is the only change in this family that would
shorten the cold open of a long chapter. It touches the section file format, page counting and the progress bar,
so it is not done blind. A measurement comes first: the build already logs section build times (`LOG_DBG` in
`Section.cpp`); collect them for a cold open of a 1 MB chapter on each board before deciding.

## Measurement plan the report suggests, for the device checks

Cold open to first page, time to the end of preparation, re-open, far jump, font-size change; plain text, heavy CSS,
images, one huge chapter; several repeats; clear only the test book's cache between runs (Settings > System >
Clear reading cache clears all books; delete `/.inkmod/epub_<hash>/` for one).
