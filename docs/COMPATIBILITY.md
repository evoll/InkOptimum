# Compatibility with the reference firmwares

Goal: a card (or a device) that has been used with CrossPoint, CrossInk, Paperio-Reader, witchhunt-reader or Papyrix
should work with InkOptimum without redoing everything, and InkOptimum must not damage the other firmwares' data.

Rule of the data directory: InkOptimum keeps its own state in `/.inkmod/` (never in `/.crosspoint/`), so two
firmwares never overwrite each other. Files that are shared on purpose live in `/.crosspoint/` (see "Shared").

## What is read or imported from other firmwares

| What | Where it is found | How InkOptimum uses it |
|---|---|---|
| Saved Wi-Fi networks, KOReader account, OPDS servers, LCP device/passphrases, recent books, all-time statistics | `/.crosspoint/{wifi,koreader,opds,lcp_device,lcp_passphrases,recent}.json`, `global_stats.bin` | Settings > System > Import from other firmware (`src/util/LegacyDataImport.cpp`, rules in `lib/LegacyImport`): copied when we have nothing of our own |
| Per-book reading position and statistics | `/.crosspoint/{epub,txt,xtc}_<hash>/{progress,stats}.bin` | Same action; the directory name is the same on every board (hash of the book path), the files are copied when missing |
| Bookmarks, clippings | `/.crosspoint/bookmarks/`, `/.crosspoint/clippings/` | Same action, file by file, never overwriting |
| SD fonts (`.cpfont`) | `/.fonts/`, `/fonts/` (CrossPoint, CrossInk) and `/.crosspoint/fonts/` (Paperio, witchhunt) | All three are scanned; a family in `/.fonts` wins by name |
| Hyphenation packs | our `/.inkmod/hyphenation/hyph-<tag>.trie`, and CPHY v1 `hyph-<tag>.cphyph` in `/.inkmod/`, `/.crosspoint/` or `/.crossink/hyphenation/` (CrossPoint #3706, CrossInk, witchhunt) | CRC-checked, accepted only for the language asked for (`HyphenationPackFormat.h`) |
| Dictionaries (StarDict) | `/.dictionaries/` | Shared as is |
| Sleep images | `/sleep/`, `/.sleep/` | Shared as is |
| Plugins | `/.inkmod/plugins`, `/.crosspoint/plugins`, `/plugins`, `/.plugins` | All four are read (`src/util/PluginLocations.h`) |

## Shared on purpose (same path, same format)

`/.crosspoint/shelves.txt`, `reading_sessions.jsonl` and `reading_sessions_sync.json`, `vocab.jsonl`,
`companion_tokens.txt`, `sleep_sync.json` (the crosspoint #4531 layer).

## Deliberately not shared

| What | Why |
|---|---|
| Section / layout caches (`sections`, `spines`, `book.bin`, `css`) | Every firmware lays text out differently and versions its cache; ours would be rejected or, worse, accepted wrongly |
| Settings (`settings.json`, `crossink-settings.json`) | Keys and meanings differ (line spacing, fonts, button maps); copying them would misconfigure the reader |
| Library index (`library.*`, `fileindex`, `library/`) | Internal formats of each firmware |
| Paperio games (`sokoban.bin`, ...), Papyrix `/.papyrix/` | Different applications and formats; Papyrix keeps its own cache tree |
| witchhunt `reading-stats.bin`, Paperio `reading-stats.json` | Another statistics format; only CrossInk's `global_stats.bin` layout is ours |
| `/.crosspoint/lang/*.cplang` (witchhunt language packs) | Different pack format from our `.lang` |
| Official XTOS data / encrypted `.xtapp` | Never decrypted; see docs/XTAPPS.md |

## Boards

The importer and the format readers are board-neutral. Anything that needs PSRAM (publisher fonts, spare book
font on S3) is gated on the board and checked in the X4 Classic build (`x4classic-release`) as well as X4 Pro and
X3/X4 (`x3x4-release`).

## Open ideas

- Read witchhunt's `reading-stats.bin` / Paperio's `reading-stats.json` into our all-time statistics.
- `.cplang` language packs.
- Export in the other direction (write our progress into `/.crosspoint/`) is not planned: it would mean writing into
  another firmware's data.
