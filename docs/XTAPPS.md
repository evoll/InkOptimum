# Mini apps (Lua, XtApp host)

InkOptimum can run small Lua apps written for the mini-app host of the official Xteink "Licorice" firmware.
Source of the API description: community documentation (omer-faruq-xteink/touch-licorice-apps `docs/firmware-notes.md`,
kycube/x4pro-emu `docs/lua-apps.md`, `docs/licorice-apps.md`). Nothing here was tested on a device.

## Installing

Copy an app **folder** to the SD card as `/XTApps/<name>/` with at least `manifest.json` and `index.lua`
(plus any `src/*.lua` it `require`s and `data/*`). It appears in Home > Apps. Per-app data lives in
`/XTApps/<name>/files/`, the saved `ctx.state` in `/XTApps/<name>/state.json`.

## What does NOT work

* **Official `.xtapp` packages.** They are encrypted to a key held by the vendor firmware
  (`dek_wrap_alg: P256-ECDH-HKDF-SHA256-AESKW`). They are listed in Apps as "encrypted — not supported" and cannot be
  run. Use the app's source folder instead (most community apps publish one).
* Apps declaring the `net` permission (the network API is not implemented) — refused at start with a message.
* `ctx.layers:create(w,h)` -> offscreen layer (`width`, `height`, `draw(fn(g))`, `release`), drawn with `g:image`.
  Limits: 1-bit shapes only (**text drawn into a layer is skipped**), no nesting, shares the 1.5 MB image budget.
* Not provided yet: `ctx.lockscreen`, `ctx.system`, `ctx.net:websocket`, BLE,
  TTF/pattern capabilities, image transforms.

## What is implemented

* Lifecycle: `on_load`/`on_enter`, `on_draw(ctx, g)`, `on_input(ctx, ev)` (return `true` = handled; an unhandled
  `back` exits), `on_tick`, `on_leave`. State saved from `ctx.state` (JSON) on leave.
* `g`: `clear rect round_rect line circle arc text text_box size measure measure_text polygon polyline pattern_rect
  fill_pattern`. `rect/circle/polygon/round_rect` are outlines unless the last argument is `true`/`{fill=true}`.
  Three fonts: `ctx.fonts.small/medium/large` (SMALL / UI 10 / UI 12).
* `ctx`: `invalidate('full')`, `quit`, `set_tick`, `set_tick_rate`, `after`/`cancel`, `save_state`, `screen`, `fonts`,
  `clock`, `sys`, `device:battery`, `display:full_refresh`, `storage`/`files`, `package`/`data` (read-only app files,
  `open_text`), `json`, `base64`, `hex`, `url`, `random`, `log`, `gc`, `budget:extend`, `dialog:toast/confirm`,
  `i18n`, `capabilities`.
* `ctx.input:text({title,text,max_chars,max_bytes}, fn)` and `ctx.input:edit({text,on_change,on_end})` open the
  firmware's on-screen keyboard (edit is a full-screen modal; on_change then on_end(...,'enter') fire on confirm).
* `ctx.net:request({url, method, body, headers, timeout_ms, save={to='files',name=}}, fn)` -> job (`job:cancel()`);
  `fn(ctx, resp)` gets `status`, `ok`, `body` (or `saved=true`), lower-cased `headers`, `error` (`wifi`, `timeout`,
  `connect`, `too_large`, `io_error`, `cancelled`). GET/POST/PUT/DELETE/PATCH/HEAD, http and https (certificate bundle),
  redirects followed, at most 4 jobs per app and 2 at once, body <= 512 KiB in memory / 4 MiB when saved. If Wi-Fi is
  down the firmware's network picker opens first; cancelling it answers every waiting job with `error='wifi'`.
  `ctx.net:keep_wifi(ms)` is accepted; Wi-Fi started by the app is switched off when the app exits.
* `ctx.image:decode(bytes)` (JPEG/PNG -> gray, decimated to <= 480x800, <= 1.5 MB of live images), `g:image(img,x,y,{w,h})`
  (nearest scaling, 4x4 ordered dither, white pixels paint white), `img:release()`, `ctx.qr:encode(text)` (1 px per
  module), `ctx.zlib:decompress(bytes[,max])`, `ctx.hash:sha1/sha256` (raw bytes; use `ctx.hex`).
* Input: buttons Up/Down/Left/Right/Confirm/Back become `key` events (`down` then `up`); on touch devices taps and
  swipes become `pointer` events (API >= 2) or `touch` events (API 0.8). Swipe events carry the start point only.
* Sandbox: base, coroutine, table, string, math, utf8 only (no io/os/debug/package, no `dofile/loadfile`,
  no `string.dump`). Lua heap capped (<= 768 KiB, half of the largest free block), 8 s per-callback watchdog.
* The renderer is forced to portrait while an app runs.

## Verification

`test/xtapp` (native): manifest/container parsing, JSON, sandbox, state persistence, timers, storage, clock,
watchdog, memory limit, dialogs. With `XTAPP_CORPUS=<dir>` it also loads every community app in the folder
(all 57 in touch-licorice-apps), draws them, feeds keys, taps and swipes and answers their network requests with canned replies and failures,
failing on any Lua error. Timing, memory and the real display behaviour are untested.


## ctx.reader (manifest permission `"reader"`)

Without the permission `ctx.reader` is `nil`. Methods (call with `:`): `summary()` → `{total_sec, today_sec, streak_days, best_streak_days}`;
`books()` / `all_books()` → recent books `{id (path), title, author, format, read_sec, finished}` (`percent` is omitted: not available here);
`current()` → the most recent book or `nil`; `days('YYYY-MM-DD', 'YYYY-MM-DD')` → `{{date, sec}}` ascending, inclusive, **only days with recorded seconds**;
`book_days(a, b)` → an **empty table** (per-book per-day rows are not stored); `cover(id)` → `nil, "not_found"`.
Field names follow the official host docs (tla `firmware-notes.md`) and what the community Bookworm app reads (its test stand-in shows the `days` row format).
`today_sec` and `days()` come from `/.inkmod/daily_seconds.bin`, which is written at the end of each EPUB/FB2/DOCX reading session (reading before this
file existed is not back-filled; TXT/XTC sessions are not recorded by the firmware at all). Totals and streaks come from `GlobalReadingStats`, books from
`RecentBooksStore` + `BookReadingStats`. `capabilities.reader` is true. Not tested on a device.
