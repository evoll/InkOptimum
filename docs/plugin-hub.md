# Плагины: структура, свой сервер (Plugin Hub) и TLS

Контракт совместим с CrossPoint (`docs/sd-plugins.md`). Здесь — то, что нужно, чтобы **выложить плагины на свой
сервер** и ставить их с книги.

## 1. Структура плагина

Плагин — папка в `/.inkmod/plugins/<имя>/` на SD (читаются также `/.crosspoint/plugins`, `/plugins`, `/.plugins`).

```
<имя>/
  manifest.json   карточка: title, version (MAJOR.MINOR.PATCH), author, description, mount
  plugin.js       браузерная часть (необязательно), выполняется на веб-странице /plugins
  device.json     экранный каталог на книге (необязательно)
  README.md       описание; открывается на книге, если нет device.json
```

Подойдёт любое сочетание: только `plugin.js` (веб-плагин), только `device.json` (каталог на книге), оба.
Папка с файлом `.installing` считается недоустановленной и не показывается.

### manifest.json
```json
{ "title": "Hello plugin", "version": "1.0.0", "author": "me", "description": "…", "mount": "plugins" }
```
`version` строго `X.Y.Z` — по нему каталог показывает «Установлен» / «Обновить».

### plugin.js
Исполняется в браузере как тело функции с аргументами `api` и `JSZip`:

| API | Что делает |
|---|---|
| `api.root` | DOM-элемент карточки плагина |
| `api.name`, `api.dir`, `api.version` | имя, папка на SD, версия |
| `api.relay(method, url, headers, body)` | HTTP(S)-запрос через книгу, без CORS; тело ответа до 64 КБ (пересылается по мере поступления, в памяти не хранится) → `{status, headers, body, bytes}` |
| `api.fetchToSd(url, dest, headers)` | скачать файл сразу на SD частями по 1 МБ (`offset`/`maxBytes`, докачка Range, `dest.part` → `dest`) |
| `api.writeFile(path, base64 \| Uint8Array \| Blob, {text})` | записать небольшой файл (≤256 КБ) |
| `api.mkdir(path)` | создать папку |
| `api.registerAction(name, async (args) => result)` | действие для очереди заданий |
| `api.log(...)` | строка в журнал карточки |

Пути только абсолютные, без `..`, не в хранилищах паролей (`wifi.json`, `opds.json`, `koreader*.json`).
`api.crypto(op, fields)` → `POST /api/crypto`: random, sha1, sha256, aesenc/aesdec (AES-128-CBC), keygen, pubencrypt,
sign, rsadec, pkcs12 (бинарные поля — base64). `api.bookKey(path, key, expires)` → `POST /api/book-key`: ключ
защищённой книги (16 или 32 байта) сохраняется в `<книга>.key`, обёрнутый AES-256-GCM ключом из секрета устройства
(NVS); дата возврата входит в аутентифицируемый заголовок. Читалка открывает такую книгу, расшифровывая записи на
лету (на SD ничего расшифрованного не пишется), и сверяет срок с монотонными часами `TrustedTime`: без достоверного
времени книга со сроком не открывается, пока не будет одного подключения к Wi-Fi.

Очередь заданий (внешняя автоматизация): `POST /api/plugin-jobs {plugin, action, args}` → `{id}`,
`GET /api/plugin-jobs/status?id=`, а выполняет задания открытая страница `/plugins-run`.

Всё веб-API отвечает `403`, пока выключено **Настройки → Система → Плагины**.

### device.json (каталог на книге)

```jsonc
{
  "title": "My Plugin Hub",
  "token":  { "file": "token.json", "path": "token" },      // файл внутри папки плагина → {token}
  "config": { "file": "config.json" },                       // плоский JSON → {cfg.KEY}
  "browse": {
    "url": "https://hub.example.com/catalog.json?page={page}&limit={limit}",
    "method": "GET", "headers": { "Authorization": "Bearer {token}" }, "body": "",
    "lists_url": "https://hub.example.com/lists.json",       // или статический "lists": [{title,url,body,notice}]
    "items": "items",                                         // путь к массиву в ответе
    "fields": { "id": "id", "title": "title", "author": "author",
                "version": "version", "description": "description", "url": "url",
                "detail_author": "…", "repository": "…", "details_url": "…", "details_format": "html|text" },
    "compatibility": { "firmware_family": "families", "plugin_api_min": "api_min", "requires": "requires" },
    "page_size": 8                                            // 1..16
  },
  "download": {
    "url": "{url}", "headers": {…}, "username": "", "password": "",
    "dest_dir": "/Books", "filename": "{title}.epub",         // одиночный файл
    "url_path": "link",                                       // «прыжок»: первый ответ содержит ссылку на файл
    "bundle": { "base": "base", "files": "files", "subdir": "{id}" }  // набор файлов (установка плагина)
  }
}
```

Подстановки: `{token} {cfg.KEY} {firmware_family} {firmware_version} {plugin_api} {plugin_capabilities}
{page} {limit} {offset} {id} {title} {author} {url}` (и `{id_enc} {title_enc} {author_enc} {url_enc}`). Значения в URL кодируются, в заголовках и теле — как есть, повторно не
разбираются. Неизвестные `{имена}` остаются как есть.

* Листание задают переменные запроса (как у CrossPoint). `{offset}` (= `(page-1)*page_size`; приоритетнее `{page}`):
  `{limit}` = `page_size + 1`, лишняя строка лишь доказывает, что страница не последняя, сервер сдвигает на
  `page_size`. `{page}` вместе с `{limit}` (`?page={page}&per_page={limit}`): `{limit}` = `page_size`, полная страница
  показывает «Следующая страница» (если последняя страница ровно полная, следующая будет пустой). `{page}` без
  `{limit}` (файл на страницу): следующая страница есть, если в ответе больше `page_size` строк. Нет ни `{page}`, ни
  `{offset}`: ответ — один статический список, читалка листает его сама.
* Строки, несовместимые по `compatibility`, скрываются и не занимают место на странице. Семейство прошивки —
  `crosspoint` (прошивка сохраняет контракт плагинов CrossPoint, как разрешают его документы); записи с
  `inkmod` тоже видны. Возможности: `catalog.lists, notices, bundle_install, version_tracking, compatibility,
  item_details, encoded_item_fields`. `{id_enc} {title_enc} {author_enc} {url_enc}` — поля элемента с
  процентным кодированием (как у CrossPoint; у нас значения элемента и так кодируются внутри URL, а в теле и
  заголовках `_enc` даёт закодированную форму). Перед отправкой в адресе кодируются пробел, управляющие и не-ASCII
  байты; уже допустимые адреса не меняются.
* Установка набора файлов: каждый файл качается в `<файл>.new`, и только когда скачано всё — переименовывается.
  `dest_dir` обязан лежать в одной из папок плагинов. Подпапка `subdir` без `/`, совпадающая с уже установленным
  плагином, обновляет его на месте.
* Страница «Детали» (`catalog.item_details`): Confirm на строке открывает название, автора (`detail_author`),
  версию, `repository`, описание и — если есть `details_url` — текст по этой ссылке (только для выбранного элемента,
  не больше 24 КБ; `details_format: "html"` — читалка семантического HTML, иначе простой текст). Если страница
  недоступна, остаются поля строки; Install/Update от неё не зависят. В каталогах с версиями (`tracksInstalls`)
  Детали открываются перед установкой/обновлением.
* В sidecar доступны `{dest}` (путь файла на SD) и `{md5}` (MD5 пути назначения).

### XML-каталоги
`"browse": {"format":"xml", "url":…, "method":"PROPFIND", "item":"response", "container_element":"collection",
"skip_self":true, "resolve_urls":true, "extensions":[".epub"], "fields":{"title":"displayname","url":"href"}}`.
Повторяющийся элемент `item` — одна строка (имена сравниваются без префикса `D:`). Селекторы полей: `elem` (текст
первого такого элемента внутри строки), `elem@attr`, `@attr` (атрибут самого элемента строки). Строка с
`container_element` внутри — папка: Confirm заходит, Back поднимается. Без страниц, не больше 200 строк. Подойдёт
WebDAV (PROPFIND), Atom/OPDS и любой XML-список.

### Вход
`"auth": {"type":"password", "request":{…}, "token_path":"access_token"}` — токен выпускается молча по данным из
`config` и перевыпускается после 401/403. `"type":"device_code"` — экран с кодом и QR, опрос `poll`, поля ответа
`code_path`, `verify_url_path`, `device_code_path`, `interval_path`, `expires_path`, `token_path`, `error_path`
(`authorization_pending`, `slow_down`, `expired_token`, `access_denied`). Токен пишется в файл `token.file` по
пути `token.path`.

### Sidecar и папка
`download.sidecar: {"path":"{dest}.meta.json","body":"{\"book_id\":{id}}"}` пишется после скачивания файла
(≤2 КБ). `dest_dir` в файле `config` перекрывает `download.dest_dir`. `username`/`password` в `download`
понимают `{cfg.*}`.

## События (`device.json → events`)
`percent` и `end_percent` — прогресс по книге, 0–100: `percent` считается по верху видимой страницы, `end_percent` — по низу (округляется вниз и равен 100 только на последней странице и на экране конца книги; книга дочитана, когда `end_percent` = 100).
Белый список: `reader.open`, `reader.exit` (`book`, `percent`, `end_percent`), `reader.session` (`book`, `document`, `start_time`,
`end_time`, `duration_seconds`, `start_progress_bp`, `end_progress_bp`, `progress_scale`), `book.downloaded`
(`path`, `title`, `plugin`), `sleep.enter` (`book`, `percent`, `end_percent` при сне из читалки). Обработчик — `request` (`url`, `method`=POST, `headers`, `body`;
любой 2xx = доставлено) или `download` (`url`, `dest`; до 1 МБ). Переменные: `{token} {cfg.KEY} {event.NAME}
{event.ts} {event.id} {meta.KEY}` (из `<книга>.meta.json`). События пишутся в `<плагин>/events.jsonl` (4 КБ, затем
очередь сбрасывается целиком) и уходят при любом подключении к Wi-Fi и при входе в сон, если Wi-Fi уже поднят;
доставка «хотя бы раз», по порядку, останавливается на первой ошибке, не более 4 обработчиков за раз. Подписки
читаются при загрузке, плагин ≤8, имя ≤23 байт, путь ≤63. `toast` (шаблон, подставляются те же переменные) показывается всплывающим окном после успешного `request`.
`"require_meta": "ключ"` ставит событие в очередь обработчика только для книги, у чьего `.meta.json` этот ключ — непустая строка или целое число (другие типы не подходят; обработчик с нестроковым `require_meta` игнорируется). События без книги не ставятся.

`"connect": true` разрешает на входе в сон поднять Wi-Fi ради доставки: заряд ≥20 %, сохранённая сеть, окно
«Подключение…», не дольше 10 с, сон не задерживается; после доставки Wi-Fi выключается. В списках «Плагины» и
«Приложения» под плагином видно, какие события он получает. `reader.session`: страница считается, если на ней
провели 2..1800 с; страницы, открытые обратным листанием или переходом (глава, процент, закладка), не считаются;
нужно ≥10 учтённых секунд и достоверное время начала.

## 2. Свой сервер

Каталог `tools/plugin-hub/`, только стандартная библиотека Python 3.8+.

```
plugins/<id>/{manifest.json, plugin.js, …}   сами плагины; hub.json (необязательно) — служебные поля каталога
server.py        динамический сервер (HTTP/HTTPS, токен, листы, поиск)
build_catalog.py статический вариант для nginx / GitHub Pages / S3
sd-plugin-pluginhub/   готовый плагин «каталог» для SD книги (положить в /.inkmod/plugins/)
```

`hub.json` внутри папки плагина: `{"families":"inkmod","api_min":1,"requires":"","lists":["games","tools"]}`.
В каталог попадают только папки, где есть `manifest.json` (по нему книга определяет установленную версию).

### Запуск

```
cd tools/plugin-hub
HUB_TOKEN=секрет python3 server.py --dir plugins --port 8443 \
    --cert fullchain.pem --key privkey.pem --public-url https://hub.example.com
```

Без `--cert/--key` — обычный HTTP (только для домашней сети). Рекомендуется держать `server.py` за nginx/Caddy с
HTTPS и пустить `--host 127.0.0.1`. Токен в `HUB_TOKEN` проверяется на всём, кроме `/health`; тот же токен кладёте
в `token.json` плагина-каталога на книге.

Эндпоинты: `/catalog.json?page&limit[&list][&q]`, `/lists.json`, `/plugins/<id>/<файл>`, `/health`.

### Статика без сервера

```
python3 build_catalog.py --dir plugins --out public --base-url https://user.github.io/hub --page-size 8
```
В `device.json` — `"url": "https://user.github.io/hub/catalog/page-{page}.json"`, `"page_size": 8` (то же число),
без `lists_url`. Каждая страница содержит `page_size + 1` строк.

### Выпуск обновления
Поднимите `version` в `manifest.json` плагина → на книге в каталоге появится «Обновить».

## 3. TLS (Настройки → Система → Сертификаты серверов)

| Режим | Поведение |
|---|---|
| Запомнить при первом входе (по умолчанию) | отпечаток сертификата сервера запоминается при первом подключении (`/.inkmod/tls_pins.json`); изменился — соединение отклоняется |
| Строгий | проверка цепочки и имени по вашему `/.inkmod/ca.pem` (можно несколько сертификатов подряд); нет файла — всё отклоняется |
| Выкл | без проверки (как раньше) |

«Забыть сертификаты серверов» стирает запомненные отпечатки. Отпечаток — SHA-256 листового сертификата, поэтому
после продления Let's Encrypt (новый лист) в режиме «запомнить» сервер будет отклонён, пока вы не нажмёте «Забыть
…», не положите CA в `ca.pem` и не перейдёте на «Строгий». Для своего хаба проще всего «Строгий» с корнем
вашего CA (для Let's Encrypt — ISRG Root X1) в `/.inkmod/ca.pem`.

`api.lcpUnlock(path, {passphrase, userKey})` → `POST /api/lcp-unlock`: берёт `META-INF/license.lcpl` из книги, проверяет пароль по `key_check` и сохраняет ключ контента так же, как `/api/book-key` (срок — из `rights.end`). Basic-профиль Readium LCP открывается паролем. Для production-профиля нужен готовый `userKey` (base64, 32 байта), который считает плагин или сервис: секретное преобразование профиля в прошивке не реализовано. Ответ при ошибке: `need_passphrase`, `wrong_passphrase`, `unsupported_profile`, `no license.lcpl`; `hint` — подсказка из лицензии.
