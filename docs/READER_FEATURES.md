# Функции, перенесённые из Paperio-Reader

Источник идей и части кода — Paperio-Reader (MIT, Dave Allie), см. THIRD_PARTY.md.

| Функция | Где в коде | Что сделано |
|---|---|---|
| Защита от циклической перезагрузки (BootHealth) | `src/network/BootHealth*.{h,cpp}`, `OtaBootSwitch.*` | Счётчик аварийных перезапусков в NVS, откат на предыдущий слот прошивки после 3 падений, подтверждение загрузки после 60 с здорового UI. Если проверенного предыдущего слота нет, включается SD-восстановление (`/inkmod-recovery.bin`). Ручное переключение слота использует `switchToInstalled`. Нужны флаги `--wrap=esp_restart,--wrap=esp_deep_sleep_start` (добавлены в `platformio.ini`). |
| Все закладки | `src/activities/home/GlobalBookmarksListActivity.*` | Плоский список закладок всех книг, новые сверху, переход сразу к месту. Первая строка в экране «Закладки». Долгое нажатие Confirm удаляет закладку. |
| Предпросмотр сносок | `lib/Epub/Epub/FootnotePreviews.*`, `FootnotePreviewActivity.*` | Текст сноски (до 240 байт) собирается один раз на книгу в `footnotes.bin`. При открытии сноски сначала показывается текст; Confirm открывает её в книге (с возвратом), Back возвращает на страницу. |
| Печатные страницы | `lib/Epub/Epub/PrintedPages.*` | Пункт меню «Перейти на печатную страницу»: page-list из nav (EPUB 3) или `pageList` из NCX, один раз сохраняется в `pagelist.bin`, дальше читается потоком. Выбирается ближайшая печатная страница не позже введённой. |
| Профили чтения | `EpubReaderMenuActivity.cpp` | Пресеты «Как в настройках / Обычный / Крупный / Компактный» для текущей книги (шрифт + интервал). |

Новые пункты меню книги (`GO_TO_PRINTED_PAGE`, `READING_PROFILES`) добавлены в конфигурируемую раскладку меню; у существующих пользователей они появляются в конце списка.

«Избранные страницы» Paperio соответствуют закладкам InkOptimum (они уже есть: добавить/просмотреть/удалить), отдельная копия не добавлялась.

Не проверено: сборка PlatformIO и работа на X3/X4/X4 Pro. Нативные тесты есть только для BootHealth (`test/boot_health`).

## OPDS: новые форматы

Каталоги OPDS теперь могут отдавать FB3, MOBI, AZW3, RTF и DOCX (по MIME-типу ссылки acquisition).
Скачанные файлы проверяются по содержимому: ZIP-контейнеры, PalmDB `BOOKMOBI`, `{\rtf`, FB2 XML.
Расширение `.fb3.zip` обрабатывается как единое. Значения `OpdsBookFormat` только добавляются в конец —
кэш каталогов остаётся совместимым.

## Настройки отдельной книги

«Профили чтения» в меню книги теперь действуют **только на эту книгу**: размер шрифта и межстрочный интервал
сохраняются в `overrides.cfg` в кэше книги. Пункт «Как в настройках» снимает переопределение.
При открытии книги значения накладываются на глобальные настройки, при закрытии возвращаются; в файл
глобальных настроек всегда записываются именно глобальные значения. Если изменить параметры чтения через
«Параметры чтения» в книге с переопределением, изменения остаются за этой книгой.

## После «Прочитано»

Когда пользователь подтверждает «Отметить как прочитанное», а в папке книги есть следующая книга
(порядок как в файловом браузере), предлагается: остаться, уйти на главный экран или открыть следующую книгу.
Если следующей книги нет, поведение прежнее.

## Копия настроек кнопок

«Управление» → «Сохранить / Восстановить настройки кнопок». В `/.inkmod/button-settings.txt` (формат `ключ=число`)
сохраняются действия кнопки питания, Home, боковых и передних кнопок, переназначение, долгие нажатия.
При восстановлении принимаются только значения, которые можно выбрать в интерфейсе; остальные игнорируются.

## Защита от прошивки не той платы (X4 Pro / X4 Classic)

X4 Pro и X4 Classic построены на одном ESP32-S3, поэтому проверка `chip_id` их не различает.
Каждая сборка теперь содержит метку `INKMOD_BOARD=<variant>;` (`lib/BoardTag`), а
`firmware_flash::validateImageFile` перед любым стиранием/записью прогоняет образ через потоковый
сканер метки:

- метка другой платы → `INCOMPATIBLE_BOARD`, флеш не затрагивается;
- метки нет на S3 (X4 Pro / X4 Classic) → `UNTAGGED_IMAGE`, образ отклоняется;
- метки нет на C3 (X3/X4) → разрешено (стоковые и сторонние образы);
- слот отката проверяется мягко: только явное несовпадение блокирует переключение.

Действует для всех путей: обновление с SD (экран с сообщением), OTA через staging и аварийный
`/inkmod-recovery.bin` / `/update.bin` (файл переименовывается в `.incompatible.bin`).
Тест: `test/board_tag/BoardTagTest.cpp`.

**Прошивка без метки (сток / сторонние сборки) на X4 Pro и X4 Classic.** В меню «Обновление с SD»
образ без метки не отклоняется наглухо: показывается предупреждение «Непроверенная прошивка …
продолжить на свою ответственность?», и только после подтверждения образ шьётся (остальные проверки —
magic, chip_id, SHA, checksum — выполняются как обычно). Метка *другой* платы по-прежнему блокирует
прошивку без возможности обойти. Аварийные `/inkmod-recovery.bin` и `/update.bin` без UI образы без метки
на S3 отклоняют (переименование в `.incompatible.bin`) — для них используйте меню.

**Аварийные файлы для конкретной платы (без метки, без подтверждения).** В корне SD при загрузке/рестарте
автоматически шьются без вопросов (образ без метки допускается, метка чужой платы — нет):

| Плата | Имена файлов |
|---|---|
| X4 Pro | `inkoptimum-x4pro-recovery.bin`, `x4pro-update.bin` |
| X4 Classic | `inkoptimum-x4c-recovery.bin`, `x4c-update.bin` |

Файл другой платы игнорируется. Они проверяются раньше `/inkmod-recovery.bin` и `/update.bin`. После
прошивки файл переименовывается в `.applied.bin` (при ошибке — `.failed.bin`, при метке чужой платы —
`.incompatible.bin`), цикла не будет. Целостность (magic, chip_id, checksum, SHA256) проверяется всегда.

## Мелкие улучшения из crosspoint-reader / witchhunt-reader

- **Файлы с паролями недоступны через веб всегда** (`lib/WebAccessGuard`): `/.inkmod/wifi*`, `koreader*`,
  `kosync*`, `opds*`, `settings*`, `inkmod-settings*` закрыты для списка/скачивания/загрузки/переименования/
  удаления в веб-интерфейсе и WebDAV даже при включённом «Показывать скрытые файлы». Учитываются регистр,
  `..`, `\`, хвостовые точки/пробелы и короткие имена FAT (`WIFI~1.JSO`).
- **Подключение к самой сильной точке сети** (`src/util/WifiRoaming.h`): полное сканирование каналов и выбор
  по сигналу — для mesh/репитеров.
- **Что нового в обновлении**: диалог обновления показывает название релиза и первые строки заметок
  (`lib/ReleaseNotesPreview`, `ReleaseJsonParser::getReleaseName/getReleaseNotes`).
- **Диагностика в вебе**: `GET /api/diagnostics` (текст), `?download=1` — скачать файлом.
- **Авто-пояс**: настройка «Авто-пояс при синхронизации» (по умолчанию выкл.). При ручной синхронизации
  часов смещение UTC берётся по IP (`ip-api.com`, `lib/TimezoneDetect`). Это снимок текущего смещения,
  учитывающий летнее время на момент синхронизации; автоматического перехода на DST нет.

## Избранные страницы, свайп-удаление, просмотр JPEG

- **Именованные закладки («избранные страницы»).** У закладки появилось необязательное имя (до 39 байт,
  UTF-8). Долгое нажатие (кнопка Confirm 1 с или долгое касание) в списке закладок открывает меню
  «Переименовать / Удалить»; имя вводится на экранной клавиатуре, пустое имя снимает название. В списке
  закладок книги и в «Все закладки» имя показывается вместо цитаты/главы. Повторная постановка закладки на
  ту же страницу сохраняет имя. Формат файла закладок поднят до v6 (имя добавлено в конец записи); файлы
  v2–v5 читаются и переписываются в v6 автоматически. **Файл v6 старыми прошивками не читается** — при
  откате на старую прошивку закладки, сохранённые после обновления, будут проигнорированы. Правила
  нормализации имени — `lib/BookmarkName` (тесты `test/bookmark_name`).
- **Свайп-удаление (X4 Pro, тач).** Свайп влево по строке: в списке закладок книги и в списке вырезок —
  запрос подтверждения удаления; в «Все закладки» — меню «Переименовать / Удалить». Жест левого края
  (системный «Назад») не затрагивается. «Недавние книги» свайпом не удаляются — там удаляется файл с SD.
  Общий код: `TouchListNavigation::swipeDeleteRow`, `MappedInputManager::wasSwipeFrom`.
- **Просмотр JPEG.** `.jpg`/`.jpeg` показываются в файловом браузере и поиске и открываются тем же
  просмотрщиком, что BMP/PNG (вписывание в экран, листание соседних картинок). Декодер — существующий
  `JpegToFramebufferConverter` с его защитой по размеру/памяти. JPEG нельзя назначить обложкой сна
  (экран блокировки поддерживает только BMP/PNG), поэтому кнопка «обложка» для них скрыта.

## Погода, веб-портал, профили OPDS, фразовый поиск, разбиение глав

- **Погода (Open-Meteo, без ключа).** Настройки → Система → «Погода»; опционально пункт на главном экране
  («Погода на главном экране»). Прогноз 48 ч + 1–5 дней, город ищется через геокодер Open-Meteo, единицы
  (°C/°F, км/ч · м/с · mph · уз, мм/дюймы). Кэш `/.inkmod/weather_cache.txt` (30 мин, показывается и устаревший),
  настройки `/.inkmod/weather.cfg`. Разбор — `lib/Weather` (тесты `test/weather`). Иконка на главном — временно
  «Chart» (отдельная иконка требует битмапов под каждую тему).
- **Веб-страница «Погода».** Браузер сам скачивает прогноз (TLS на устройстве не нужен) и кладёт сырой ответ в кэш
  читалки (`POST /api/weather/cache`) — читалка потом показывает его офлайн. График 48 ч, по дням, поиск города,
  единицы. API: `GET /api/weather`, `POST /api/weather/settings`.
- **Веб-страница «Заметки».** Закладки (с именами) и цитаты всех книг, фильтр, переименование/удаление,
  экспорт в Markdown и JSON (по книге или всё). API: `/api/notes`, `/api/notes/book`, `/api/notes/bookmark/{rename,delete}`,
  `/api/notes/clipping/delete` (ответ по книге стримится — цитат может быть сотни).
- **Веб-страница «Диагностика»**: состояние устройства + текст отчёта (копировать/скачать).
- **«О книге» в файловом менеджере** (кнопка ℹ️ у EPUB): обложка, название, автор, язык, издатель, глав, изображений.
- **EPUB Kit: «Делить большие главы»** (выкл./64–512 КБ). Режет огромные XHTML по границам верхнеуровневых блоков,
  добавляет части в manifest/spine и перенаправляет ссылки (контент, nav, NCX, guide). Проверка:
  `NODE_PATH=<jsdom> node test/web/epubkit_split_test.js`. По умолчанию выключено.
- **Профили OPDS.** У сервера OPDS (веб → Настройки) появились «User-Agent» (с пресетом KOReader) и «Свой заголовок»
  (`Имя: значение`, например токен API). Служебные заголовки (Host, Authorization, Range, Cookie…) запрещены, CR/LF
  отклоняются — `lib/OpdsProfile` (тесты `test/opds_profile`). Применяется ко всем запросам на время работы OPDS-браузера.
- **Словарь: поиск фраз.** При выборе слова сначала пробуются фразы из 3 и 2 слов («ice cream», «give up»),
  затем одно слово. Знак препинания после слова обрывает фразу; слова, разорванные переносом, не расширяются —
  `lib/DictPhrase` (тесты `test/dict_phrase`).
- **TLS/SecureNet/Range (№3)**: в форке уже есть wolfSSL-запасной путь для публичных загрузок и докачка по Range,
  поэтому отдельных изменений не вносилось (трогать TLS-слой без проверки на устройстве рискованно: затрагивает OTA).

## Дополнение: каталог шрифтов, словарь в вебе, иконка погоды

- **Каталог шрифтов в вебе** (страница «Шрифты»): браузер сам скачивает манифест и файлы .cpfont, проверяет CRC32 и
  загружает их через `/api/fonts/upload` — читалке интернет не нужен (работает и в режиме точки доступа). Адрес каталога
  отдаёт `GET /api/fonts/catalog`. Если хост каталога не разрешает запросы из браузера (CORS), страница покажет
  подсказку использовать «Настройки → Шрифты» на читалке. Ограничение диапазона размеров прошивки веб не применяет.
- **Страница «Словарь»**: список подготовленных словарей, удаление (только папки, найденные сканером),
  проверка поиска слова или фразы. API: `/api/dictionaries`, `/api/dictionaries/lookup`, `/api/dictionaries/delete`.
- **Иконка погоды** (`components/icons/weather.h`, `UIIcon::Weather`) для темы Lyra; остальные темы показывают пункт без иконки.

## Library, server profiles, "About book" editor, statistics (addendum)

**Library screen** (`src/activities/library`, `lib/Library`, `src/LibraryIndex.*`). One screen with tabs *Books / Recent / New / Authors* and three views (file names, details, covers). Hold Confirm cycles the view (the choice is remembered, Settings → System → "View"); Up from the first row focuses the tab bar, Left/Right (or a horizontal swipe) switch tabs; tapping the sub-header also cycles the view. "New" = indexed books that are not in the recent list; "Authors" groups by author (case-insensitive) and opens that author's books. The home entry is opt-in: Settings → System → "Library on home screen". Books are indexed by scanning folders (depth ≤ 5, ≤ 2000 books, hidden/system folders skipped) **without opening them**; titles/authors come from the file name (`Author - Title` or `Title (Author)`) and are upgraded from the recent-books store. The index is cached in `/.inkmod/library.idx` (streamed, never one big String) and reused while the root listing is unchanged, so entering the screen and the per-folder counts are instant; a stale entry is dropped when opened. Covers view shows cached thumbnails for books opened before and a format placeholder for the rest (no EPUB parsing during browsing).

**KOReader sync profiles** (`lib/SyncProfile`, `src/KOReaderProfileStore.*`). Up to 8 named profiles (server URL, account, optional extra header) in `/.inkmod/koreader_profiles.json` (passwords obfuscated like the main store). Web: Settings → "KOReader sync profiles" (create / save / activate / delete). Device: KOReader Sync → "Server profile" cycles through them. Activating copies the profile into the live KOReader settings. The extra header (`Name: value`, validated like the OPDS one) is now also sent with every sync request.

**OPDS extended metadata** (`lib/OpdsMeta`). The OPDS parser reads `dc:identifier` (ISBN-10/13 with checksum check, ASIN) and `calibre:series` + `series_index`; the book details page shows "Series #N · ISBN … · ASIN …". The feed cache format version was bumped (old caches are rebuilt automatically).

**About book editor** (Files page → ℹ️ → "Edit metadata"). Edits title, authors, series/number (calibre meta), language, publisher, date, ISBN, description and optionally replaces/adds the cover (re-encoded in the format the book already declares, ≤ 1600 px). Only changed fields are written, the result is re-opened and validated in the browser, an optional backup of the original is downloaded first, then the EPUB is uploaded over the original (reading progress is preserved by the existing upload logic). Test: `NODE_PATH=<jsdom> node test/web/opfedit_test.js`.

**Web statistics page** (`/stats`, `/api/stats`): totals, streaks, 26-week activity grid, day-of-week and time-of-day bars from the global stats (including synced devices).

## Repository re-check (2026-10-19)

* **evoll/InkOptimum** (upstream): 61 new commits (separate X3 no-flash refresh action, YACP-matching cleanup cadence, global power action) merged by a clean 3-way merge on top of our changes; i18n regenerated (865 keys).
* **crosspoint-reader**: ported `#4030` (text running into a heading/block is flushed into its own paragraph; section cache version bumped to 65). `#4032` (low-heap CSS guard): our `resolveStyle` turned out not to allocate, so the 16 KB guard was removed instead (section cache version 66; CSS is no longer silently dropped on low heap). `#4023` targets crosspoint's own library builder, which this fork does not have.
* **witchhunt-reader**: ported the self-contained fixes `39a7ab82` (ZIP entry claiming 4 GB → heap overflow from a crafted EPUB), `3ab92b1f` (XTC bit depth read from pages), `fb380660` (KOReader document id fails on a short read instead of hashing a wrong file), `27ff2c4e` (log line truncation keeps its newline), `ec292f03` (WebDAV MOVE/COPY between two spellings of one file). The crash-report fix `ed244c89` is ported too (new `--wrap=panic_arch_fill_info,--wrap=panic_soc_fill_info` in platformio.ini; the report now names the CPU fault, PC and fault address). It needs a real build to confirm the wraps link.
* **Dependencies**: ArduinoJson 7.4.2 → 7.4.3 (buffer overrun fix), PNGdec 1.1.6 → 1.1.7 (palette/tRNS overflow guards). JPEGDEC pin is already the upstream HEAD; Arduino-wolfSSL 5.7.2 → 5.8.4 (fixes the TLS 1.3 signature-algorithm and PSK downgrade CVE-2025-11934/11935; patch_wolfssl.py overrides and our -D flags are unchanged and compatible with the 5.8.4 user_settings.h). Not build-tested: check flash/RAM size and a TLS 1.3 handshake (OPDS over HTTPS, KOReader sync, a release download) on device; revert the pin in platformio.ini if anything regresses.
* **Adapted to our code (second pass)**: `lib/FileReplace` (new, tested) swaps a finished temp file over its target through a `.old` backup and restores it on failure — used by web uploads (written as `<name>.upl` first, so a broken upload keeps the old file), prepared-cache installs and WebDAV PUT (witchhunt `faf154eb`/`27e15fa5`/`0a2ca308` idea). `SecureHttpClient` now keeps Basic auth and custom headers (KOSync `x-auth-*`) on the origin a request started at; a redirect to another origin is followed without them (`182ec2f7`; http→https on the same host keeps them). The web file manager sends upload, folder and rename names in NFC (`0d86c694`). CSS guard: upstream `#4032` removed the same heap guard as obsolete, rule loading stays bounded by `MAX_RULES` and the selector pool, so no guard was re-added.
* **Ported in the third pass (from witchhunt/crosspoint, adapted to this code)**:
  * Dictionary (`lib/DictWordForms`, 5 tests): a miss is retried with the word lowercased for letters the index fold does not cover (Ł, Ґ, Ђ, ...) and with the French elision stripped (`l'histoire`, `qu’après`); the index format is untouched. Based on `742567b2`/`9ff4b890`/`e89419d6`.
  * Watermark text (`394cabf6`): `font-size` under a tenth of the body (1px/1pt spans) drops the element's text only; CSS cache v16 (also stores `small-caps`, which the cache used to lose), section v67.
  * Covers (`ff57c2e8`, `CoverImageRefScanner`, 7 tests): an SVG/XHTML wrapper named as the cover is followed to its JPEG/PNG; the guide cover page uses the same streaming scanner. Older book caches holding the wrapper path are resolved on the fly (book.bin version stays 7: browser-prepared caches depend on it).
  * TOC (`896b1e60`, `16a3d361`): a nav whose mapped entries cover less than half the spine gives way to a richer NCX; TOC links that match a spine item only by file name open their chapter (unique match only, folder hint, read budget).
  * `<img>` without an extension (`4e6b7a70`, `623e1158`): laid out as an image, format taken from its bytes; section v68.
  * Nested blocks (`1232163d` idea): a block's bold/italic/underline reach the blocks nested in it and are restored on its end tag; section v69.
  * PNG IHDR bit depth is checked against the colour type (`a35e1a48`, 4 tests) in the BMP converter.
  * KOSync (`189f2e3c`, 4 tests, needs ArduinoJson via FetchContent): a percentage-only record is read, empty replies are "not found" instead of 0% / "null", GET asks `?position_kinds=locator,percentage`.
  * Serial (`5a88698b`, 5 tests): command lines are assembled without waiting, so a stray byte no longer stalls the loop for a second.
  * File browser: a list that ran the heap out is released (capacity too), not just cleared.
* **Already fine here**: footnote links to `<span id>` (`5d773b20`: ids are recorded on any element), file-browser heap abort (`15a92d54`: our list has its own heap guard), CSS lookups.
* **Still not ported** (code this fork lacks, or large separate features): TTF kerning/ligatures, CJK line-break classes, serial test commands, `FileBrowserModel`; from crosspoint: AES-256 manifests, WiFi TX power limit, OPDS 2.0, BLE page turner, plugin hub.


* **High-priority list (2026-10-22)**: web upload nothrow guard, font upload via side file, settings save error, long dictionary paths, bounded folder-delete walk, guide fragment, meta/id cover fallbacks are done; see docs/PROJECT_REVIEW.md for what remains (nav cover landmark, first-open indexing speed-up, FAT FSInfo hint, book TTF fonts, CJK).
