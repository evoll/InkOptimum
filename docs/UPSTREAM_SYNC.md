# Upstream reconciliation — 2026-11-09/10

Checked against the working repositories (shallow clones, commits since 2026-10-19 unless noted). "Port" = adapt to this code, "skip" = not applicable or already covered.

| Repository | HEAD | Newer than our last check | Verdict |
|---|---|---|---|
| alpzoloto-sudo/inkmod (= evoll/InkOptimum upstream) | `9af70eb` (v1.1.11 republished, see WORK_HISTORY 2026-11-12/13) | 34 | **merged now**: FB2 footnote boundary (#56: `<` instead of `<=` in `addLineToPage`), footnote list button hints (#58). Earlier: 5bba3d3 navigation/Wi-Fi regressions (#55) merged on 11-08. |
| jpirnay/witchhunt-reader | `89ef347` 2026-11-09 | 858 | see below — Arabic/Myanmar shaping, glyph-ID records, RTL page turns, perf; mostly large, separate work |
| crosspoint-reader/crosspoint-reader | `a4f64a3b` 2026-11-14 | 115 | see below |
| Kapfilm/Paperio-Reader | `1537aaf` 2026-11-08 | 22 | releases 2.2.3–2.2.7: compact headings, full inline notes, Dropbox, Lyra covers, quotes, power actions |
| bigbag/papyrix-reader | `a40c17d` 2026-11-07 | 33 | new: sleep image picker, Clock power modes, EPUB CSS rule cache, continuous chapters, mini-apps (2048, Go) |
| uxjulia/CrossInk | `9914146` 2026-10-04 | 0 | nothing new since v1.6.1 |
| Free-Ink/freeink-sdk | `995d9bd` 2026-11-21 | 142 | pin updated 2026-11-13 from `1e1cec2` to `e637713`, 2026-11-14 to `3f3e4c1`, 2026-11-22 to `995d9bd`, local patches re-applied; see WORK_HISTORY 2026-11-13 and 2026-11-22 |

## witchhunt-reader — what is there that we lack (grouped)
* **Arabic**: shaper (`feat(shaper)` Arabic joining/plan, RTL runs), Arabic runs in the run scan, bidi fallback pieces, "an Arabic-script book is right to left, shaped, installed Arabic family" (`de38b57`), RTL page turns (`864333b`), RTL lists and tables on the right (`94c7bf1`), UI lines in visual order (`c3baa21`, `3899325`), bracket pair resolves only brackets (deviation 13, `f9c85f5`).
* **Indic/Myanmar**: paragraph store for shaped tokens, `IndicReorder` without allocation, glyph-ID drawing with compact records (`775f11b`, `007c1bc`), CPac only for fonts whose layout names a shaped script (`a456640`), Myanmar shaper and syllable line breaks (`bb5e5e9`, `37a412e`), Hebrew/Arabic/Myanmar families in the Font Manager.
* **Perf/heap**: paragraph arrays grow to the parser's split capacity, bidi tokens in the pass's arena, shaped store 16 B/entry, glyph store remembers recent answers (`44b779c`), TTF kerning memo (`4731c81`).
* **Reader**: Auto Turn 5–120 s (`eb762a9`), reader menu names the chapter (`56ec4d8`), One-Pass anti-aliasing on X3/X4/X4 Pro/M3, long touch on a library row opens options (`b001ba0`), `<center>`/`<hr>`/dash layout fixes, a book tagged `cn` laid out as Chinese (`6e0ba0e`), stats past midnight (`2988e51`), bookmarks survive damaged files (`4ea6eb1`).
* **Web/net**: hotspot captive portal on phones, relay cut short, one wrong POST no longer crashes (ours: fixed earlier), URL escaping and header injection (`0bc985c`), TLS fallback rules (`0350e97`).
* **Dictionary**: duplicate/synonym headwords, `.ifo` read as lines, inline entry types, hyphenation by the open book's language.
* **Verdict for us**: the Arabic/RTL-UI/Myanmar line is a separate multi-week port (it needs the CPac/glyph-ID drawing path); the reader/web/dictionary fixes above are small and portable one by one. Already ours: bidi markup (`dir`, `<bdi>`, `<bdo>`, `unicode-bidi`), L4 bracket mirroring, Indic OpenType shaping (PR #603), word spacing.

## crosspoint-reader — new items
Ported/covered: bracket mirroring L4 (#4295), plugins with updates first (#4319), Wi-Fi power. Not yet: SD card size/used/free in About (#4331), dictionary vocabulary history (#4294), dictionary link selection reuse (#4379), swipe in dictionary definitions (#4363), "Orient Side Buttons" (#45986f2), special root folders skipped when building the library (#818a154), shared FtFont across sizes (#4308), plugin catalog capability filter / headers (#4260, #4197), `end_percent` in reading events (#4201), sleep images once each in random order (#3841), ruby closer to browsers (#4204), `.notdef` for replacement characters in TTF (#4256), latin-ext combining marks (#4358).

## Paperio-Reader 2.2.3–2.2.7
Not yet in our code (checked by search): compact FB2/EPUB headings, complete inline notes, large Lyra covers, quote sleep screen, power-button assignments, transparent sleep screens, Dropbox (ours is a plugin), "stop clock maintenance when disabled", SD font metadata safe rendering.

## papyrix-reader — new items worth a look
Sleep image picker with saved selection (#157), Clock power modes, front-light timeout, EPUB CSS rule cache, recovery for missing EPUB TOC, stop saves after failed loads (keeps reading history), progressive JPEG on S3.

## Order of work proposed
1. Small portable fixes from crosspoint/witchhunt/paperio/papyrix (SD size in About, sleep-image order, special folders, `.notdef`, Auto Turn range, chapter name in the reader menu).
2. Paperio features the user asked for (compact headings, full inline notes, Lyra large covers, quote and transparent sleep screens, power-button assignments).
3. UI fallback font, language/hyphenation packs from SD, drag selection.
4. Arabic/RTL-UI/Myanmar from witchhunt as a separate project; SDK pin update with per-board builds.

## Re-check 2026-11-15
* inkmod: no new commits (still `9af70eb`, merged).
* crosspoint `36b9430e`: one new commit (TTF perf, #4433) — only relevant once a TTF rasterizer exists; nothing taken.
* paperio `d0eb9317` (2.3.0): independent top/bottom margins — **taken**; HTTP long-header fix — our SDK `SecureHttpClient.h` already skips an over-long Content-Security-Policy line (the GitHub case), Paperio skips any unused long header; not ported; expandable TOC (already ours), per-book override tabs Font/Size/Layout/Style, sleep screens calendar / calendar+weather, bookmarks bound to a text position, bottom oval button hints in Mini/Lyra — **open**, in order of interest: override tabs, bookmark anchors, calendar sleep screens.
* papyrix `61816c4`: Wi-Fi transfer connection / HTTP parsing patch for the Arduino WebServer (#177, patch file 458 lines) — not examined in depth; games (Sokoban, Lights Out) out of scope.
* crossink `9914146e`, witchhunt `90d04f08`: crossink unchanged; witchhunt 461 new commits not reviewed this round.
* Follow-up same day: bookmarks bound to text — already covered (we store paragraph index + progress + snippet); sleep calendar + weather — taken in the cached-forecast form (see WORK_HISTORY 2026-11-15); per-book override tabs — not taken as UI, record extended instead.

## Re-check 2026-11-16

* crosspoint `490813ff` (11 commits since `a4f64a3b`): `3c42dfde` misc fixes — taken in part (see WORK_HISTORY, "upstream re-check"); `867a1210` power-button gestures across soft restarts — open, depends on the power input path and the SDK pin; `36b9430e` TTF perf — not needed; CI/test commits — nothing to take.
* witchhunt `a4078b57` (87 commits since `90d04f08`, release 2.52): in-book link resolution fix `1b5ee33a` — taken (without the NCX part); bookmark anchors for older bookmarks / star text offsets, cold-open perf (PSRAM chapter build region, kept page on screen during build), lazy SD font registry, Picco two-finger gestures — open / not examined in depth.
* inkmod `9af70eb`, paperio `d0eb9317`, papyrix `61816c4`, crossink `9914146e`: unchanged since the last check.

Follow-up 2026-11-17 (examined, nothing to take):
* witchhunt "bookmarks anchored to text" — our bookmarks already store spine + progress + paragraph number + snippet (`BookmarkStore`), so they survive a font or layout change; only bookmarks made before the paragraph number was stored stay page-based.
* witchhunt `2cefc2b5`/`19cb56f0`/`b67ade13` (certificate dates on a software clock) — our default TLS mode is trust-on-first-use (chain and dates not checked, pinned); only Strict mode checks dates and could fail on a stale X4 clock. Not ported.
* witchhunt `5fd1f6c1` (reader-refresh hint on every page) — only for panels with a reading waveform (Picco); none of our boards.
* witchhunt `99726dfc` (lazy SD font list) — a heap-tuned refactor across 8 files measured on an X3; left until it can be measured on a device.

## Re-check 2026-11-18

Heads: inkmod 9af70eb, paperio d0eb9317, papyrix 61816c4, crossink 9914146e — unchanged.
crosspoint 490813ff → b6b7a6c1; witchhunt a4078b57 → 5d903ca1.

Reviewed, nothing ported (no code changed in this pass):
- crosspoint #4512 (side buttons in dictionary pages): in our DictionaryActivity the side buttons arrive as Up/Down
  (article = switch dictionary, select = vertical move). Adding PageBack/PageForward would double-fire on layouts where
  both map to the same physical key; needs device testing. Deferred.
- #4469 (release font caches after chapter builds): our Section.cpp already frees the SD font before inflate and the
  reader has releaseReaderSdFontCachesForLowMemory; no equivalent change without heap measurements. Deferred.
- #4487 (OPDS redirect message): our OPDS browser has no lastAnswerRedirected/STR_FEED_REDIRECTED; n/a.
- #4509 (Recent tab: only opened books): belongs to crosspoint's library-index feature, absent here; n/a.
- #4411 (thin "1" in 8 pt UI font), About UID: font/hardware specific; n/a.
- witchhunt: 0d6f14b3 (empty bordered block after spaced paragraph), heading-only spine join, protected-content
  uncompressed entries, section version bump — candidates for a later pass (layout cache format impact); gray-plane
  build-page work is C3-performance/hardware dependent. Picco-only items n/a.
Reference pins updated to crosspoint b6b7a6c1, witchhunt 5d903ca1.

## 2026-11-19 — new references for the Lua mini-app host
* omer-faruq-xteink/touch-licorice-apps (open Lua apps + `docs/firmware-notes.md`) and kycube/x4pro-emu
  (`docs/lua-apps.md`, `docs/licorice-apps.md`): API description and test corpus for `lib/XtApp`. Not merged; used as docs.
* lua/lua v5.4.7 vendored in `lib/Lua`.

## Re-check 2026-11-20

Pins fetched: inkmod 9af70eb (unchanged), crossink 9914146e (unchanged), crosspoint 3331d189 (was fa17b9ca fetched / 490813ff reconciled; 18 commits since the last reconcile), paperio a1ba1059 (X4 Classic sleep clock with quick resume), papyrix 12b7e6b (USB install/update commands, changelog), witchhunt 8d1541bf (was 5d903ca1; 287 commits since a4078b57 reviewed).

Looked at in detail (witchhunt):
- 0d6f14b3 (empty bordered block after a spaced paragraph): **not applicable** — our ChapterHtmlSlimParser has no `extraGapEnd_` / box layout, and no `test/epub_pipeline`.
- e4fe80b5 (Adobe `aes128-cbc-uncompressed` in encryption.xml): **not applicable** — we have no Adobe DRM entry handling.
- join heading-only spine items, C3 gray-plane / heading-join perf: depend on witchhunt's section pipeline and on-device measurement; **not ported**.

Only noted by subject (NOT reviewed in depth, nothing ported): crosspoint #4542 (sleep image replaced by device), #3666 (shared streamed BMP output), #4540 (clipping handles), #3787 (Indic without HarfBuzz — large, deferred), #4538 (blocked activity time not idle), #4533 (Sync during BT shutdown), #4524 (full-tone EPUB images, eego/picco — other hardware); paperio a1ba1059 and papyrix changes; the remaining ~280 witchhunt commits (e.g. #845–#850 table-row growth, inline anchor page, anchor spill removal) — candidates for the next pass.

### Re-check 2026-11-20 (2): witchhunt #845–#850 in detail
- 3d43b909 / 9215761a / 3fb0a986 (checked reserves for table rows, cells and per-element stacks), 1544be1d (cell lines allocated once), 4c789ec5, 7c177c9f / eb9e9694 (anchor spill file, arena harness): **not applicable** — our parser has no `pushOrStop`, no anchor spill and no allocation-census harness; these fix witchhunt's own memory-engineering layer.
- 920ea6ca (CSS resolved against a chapter's linked stylesheets), b04ac243 (link scanner): not checked against our CSS parser; candidate.
- c67fb6b7 + 0f1116b0 (inline `id` recorded at the page of its own line; from crosspoint #4328): **applicable in principle**. Our `anchorData.push_back` sites (ChapterHtmlSlimParser.cpp ~535, ~855, ~3136) record an id mid-paragraph at the *next block's* page, and a second inline id overwrites the first. Not ported: ~85 lines in the parser, a section-version bump, and no pipeline test harness here to verify page numbers — would be unverified. Candidate for a dedicated step.

### Re-check 2026-11-20 (3): inline anchors ported (simplified)
- witchhunt c67fb6b7 / 0f1116b0 (crosspoint #4328): an `id` inside a paragraph was recorded at the next block's page and a second one overwrote the first. Ported **in a simplified form**: ids met while the current text block already has words are queued (`inlineAnchorIds`) and recorded when the block is laid out (`makePages`) at the page where its remaining text starts. Witchhunt's exact per-line recording (word index → line) is not ported; for a paragraph that spans pages the id can land on the page where layout of the rest began rather than the exact line. `SECTION_FILE_VERSION` 82. No native test covers the parser (not built in test/); not tested on a device.

### Re-check 2026-11-20 (4): witchhunt 920ea6ca (CSS scoped to the sheets a chapter links)
**Not ported.** It is a pipeline-level feature (sheet table in the CSS cache, `readSheetLinks`, `scopeToSheets` over @import edges, `Cascade::apply` skip, new cache validation, section version bump) and only changes the result for omnibus books / sheets no chapter links (matcha-reader #453). Our CSS cache has no sheet table; porting would touch the cache format without a test harness to verify. b04ac243 (link scanner) is part of the same design. Revisit only if a real book shows a cross-sheet styling bug.

## Re-check 2026-11-21
Pins fetched: crosspoint 08d9d68d (4 commits since 3331d189), witchhunt c699a618 (~600 more commits since 8d1541bf, mostly docs/arena memory work), paperio a1ba1059 / papyrix 12b7e6b / crossink 9914146e / inkmod 9af70eb unchanged.
- crosspoint #4547 (web file manager: renaming selects only the base name, so the extension survives): **ported** into `web/pages/files.js` (folders still select everything); `src/network/html/FilesPageHtml.generated.h` regenerated with `scripts/build_web.py`.
- #4549 (reading progress on the Lyra home card): **already present** — our LyraTheme draws `progressPercent`.
- #4545 (selected filename in the file-browser action popup): **not applicable** — our file browser has no option popup.
- #4546 (host-test temp files): test-only. #4549's BookMetadataCache `try*` hardening: candidate (our `tryReadString` has no length limit argument), not ported.
- witchhunt: the new commits are plugin crypto (api.crypto, protected-content), X3 panel/power work (bbf4db6f, 3266abf4), arena memory for page lines (7c6d6f18 etc.) — all in witchhunt-specific subsystems; not reviewed for porting. `ctx.lockscreen` for XtApp checked: it is a sleep-screen session (separate lockscreen.lua run on timer wakes) tied to the firmware's sleep/wake flow, not a plain API call — deferred.

## Re-check 2026-11-21 (2)
Pins: crosspoint 428ce8ce (2 commits since 08d9d68d); witchhunt c699a618, paperio a1ba1059, papyrix 12b7e6b, crossink 9914146e, inkmod 9af70eb unchanged.
- crosspoint #4551 (clock and battery fixed in Lyra's status strip across built-in themes, `headerBandTopShift`, `HEADER_STATUS_*`): **not applicable** — it rewrites crosspoint's `applyHeaderStatus` / freeink::ui header props; our themes draw their own headers (`drawHeader`) and keep their per-theme clock placement.
- crosspoint #4479 (navigate installed plugins in the WebUI, plugin-host.js): **not applicable** — our plugins page is our own (`WebPlugins.cpp`, `/plugins`, `/plugins-run`); crosspoint's JS host and endpoints differ.

## Re-check 2026-11-21 (3)
Pins: crosspoint ca1a8c1e (2 commits since 428ce8ce), witchhunt 79d09ee0 (plugin-event docs/commits), paperio a1ba1059, papyrix 12b7e6b, crossink 9914146e, inkmod 9af70eb unchanged.
- crosspoint #4531 (Sync On Sleep, reading-session log `/.crosspoint/reading_sessions.jsonl`, bookmarks sync; ~1000 lines in main.cpp, `src/sync/*`, KOReaderSyncActivity): **not ported** — built around crosspoint's own "CrossPoint Sync server" API and its plugin-event machinery; we have KOReader progress sync only, no such server to test against, and a Wi-Fi join on every sleep is a power/timing risk that cannot be checked here. Revisit only if a sync server is chosen as a target.
- crosspoint ca1a8c1e (restores theme-specific header positions, partly reverting #4551): not applicable, same reason as #4551.
- witchhunt: plugin device events (edfb2409, b170fa5e) and plugin-event docs — crosspoint plugin API, not ours.

### Correction 2026-11-21 (4) — plugin events
The previous section said witchhunt's plugin-event commits "belong to crosspoint's plugin API, which we do not have". That was wrong: we have the events (lib/PluginEvents, src/util/PluginEventBus, reader.open/exit/session, sleep.enter, `connect`, drainAtSleep). Re-reviewed:
- edfb2409 device part: the **clean-boot send on ESP32-C3 is ported** (see WORK_HISTORY 2026-11-21 (6)); the redraw of the sleep screen after a download is not.
- b170fa5e (web: mark community plugins whose events run): no counterpart — our Plugins page has no community catalog judgement.
- ca1a8c1e / #4551: touch crosspoint's `BaseTheme::applyHeaderStatus`; we never had #4551's header rewrite, so there is nothing to follow.
- #4531 (Sync on Sleep, reading sessions, bookmarks sync): depends on a stack we lack (KOReaderServerType, SyncRequest, delta sync, SyncEngine, clipping sync, "CrossPoint Sync server" endpoints `/api/v1/stats/sessions`, `/api/v1/bookmarks/<doc>`). Waiting for a decision on which server to target.

### 2026-11-21 — crosspoint #4531 ported (partly)
Supersedes the "not ported" notes above. Ported: reading-session log + upload to `/api/v1/stats/sessions`, Sync On Sleep (setting, snapshot, clean-boot/in-place send, server-ahead guard). Not ported: crosspoint's own KOReaderServerType/SyncRequest/SyncEngine layering (our existing CrossPointExtendedSync stays the transport), bookmark delta/cursor protocol (see next entry once checked), FB2 books at sleep. Needs device + server verification.
Bookmarks (same day): ids already match crosspoint's; delta probe, cursor paging, `accepted` check and the probe gate for reading sessions added (WORK_HISTORY 2026-11-21 (8)). Name labels / visible-text offsets are not ported.

### 2026-11-21 (evening) — witchhunt 79d09ee0 → 4f1eb18c re-check
18 non-merge commits, 289 files; nothing ported:
- Float-free refactors (CSS lengths in fixed point, text scale, book progress in billionths, weather units, UI formatting, image ratios, clock drift model; #863–#868): behaviour-preserving, spread over the layout, progress and weather code that we have changed independently (progress is a float 0..1 in EpubReaderActivity/ProgressMapper/KOReaderSync). A partial port would only create merge conflicts; revisit only if a C3 profile shows soft-float time in layout.
- Gray-level plans for sleep pictures (296351ab, 9eeb1fb8, 3d839d1a, SDK pins faae308c/ff719d02/8663b2cb): compiled out on every board upstream (`grayLevelPlan()` returns nullptr) and needs a display driver with calibrated multi-pass waveforms that our SDK pin lacks. Nothing to run.
- 2b4c797a (XTC percent on the sleep overlay shown 100x): we have no sleep-screen book overlay, so the bug cannot occur here.
- Flash tooling (`bin/devices.local`, 035e927c, d632e95b) and serial lab commands (97b7527f, ac5d71cf): host/dev tooling, not firmware features.
Pin: witchhunt 4f1eb18c. crosspoint ca1a8c1e, paperio a1ba1059, crossink 9914146e, inkmod 9af70eb, papyrix 12b7e6b unchanged.

### 2026-11-22 — re-check
crosspoint 79ae14ff: only "remove conditional half refresh for EEGO A4" — not our hardware. SDK moved to 995d9bd and was taken (WORK_HISTORY 2026-11-22 (10)). paperio, crossink, inkmod, papyrix, witchhunt (4f1eb18c) unchanged.


### 2026-11-22 — re-check 2
crosspoint 177d0694: Paper Mono board only — N/A. Others unchanged.
