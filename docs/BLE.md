# Bluetooth-пульт (BLE) — состояние порта из CrossPoint

Источник: CrossPoint, коммит `fecee610` (#3785).

## Подключено к прошивке (собирается: x4pro-developer, Flash 83,8%)
- `lib/BlePageTurner` (радио, политика, привязка кнопок, JSON) с тестами; `freeink-sdk/.../BleKeyboardHost` обновлён до SDK main.
- Настройки: `InkMODSettings::ble`, сохраняется в settings.json теми же ключами, что в CrossPoint.
- Ввод пульта: `MappedInputManager::injectRemoteClick` (Подтвердить, Назад, вперёд, назад), `Activity::acceptsRemoteInput/remoteTurn/remoteShortcut/remoteInputBlocked`.
- `ActivityManager`: `remoteTurn/remoteClick/remoteShortcut`, счётчик экранов `visit()`, `isForegroundHomeShown()`, остановка радио перед сменой экрана (`beforeScreenChange`), `RenderLock::Mode::Try`.
- `main.cpp`: хост-интерфейс модуля, `bleturner::begin/tick/beforeSleep`; пульт считается активностью (таймер сна).
- Читалка EPUB: приём пульта, остановка радио на время сборки главы (`makeRoomForChapterBuild`, ожидание до 5 с, иначе «Bluetooth занят»), фоновая подготовка следующей главы не идёт рядом с радио, заметка «Подключение Bluetooth…» вместо названия в строке состояния; пропуск главы вынесен в `skipToChapter`.
- Экран Настройки → Система → «Bluetooth-пульт»: включение, поиск, подключённые пульты, назначение кнопок (удержание Подтвердить сбрасывает).
- Строки: 22 ключа `STR_BT_*` на английском, русском, украинском. Лимит блока строк (15 бит) был превышен, поэтому `scripts/gen_i18n.py` запускается с `--strip-unused`: 74 строки, которых нет в коде, в сборку не попадают (в yaml они остались).

## Не сделано
1. Само радио выключено: в `platformio.ini` нет `FREEINK_CAP_BLE_HID_HOST`, NimBLE-Arduino и `custom_sdkconfig` (секция `[ble_page_turner]` из CrossPoint) — собирается заглушка `NoRadio`. Для включения нужна пересборка ядра Arduino с другим sdkconfig; в песочнице проверить не удалось, и на устройстве тоже.
2. Читалки TXT и XTC пульт не принимают (только EPUB).
3. Экранные подсказки и меню читалки (пункт «Bluetooth» в меню книги) не портированы.
4. `scripts/patch_arduino_ble_controller.py` и `s3_nimble_psram.h` скопированы, но не подключены.
5. `test/test_ble_learning.py` запускается вручную (`python3 test/test_ble_learning.py`), в CMake не включён.

## Радио: SDK и NimBLE (2026-11-03)

В freeink-sdk единственная реализация BLE — `BleKeyboardHost` (HID-хост на NimBLE-Arduino). Отдельной листалки в SDK нет, это `lib/BlePageTurner`. Включение радио: окружение `x4pro-ble` (секция `[ble_page_turner]` в `platformio.ini`): `FREEINK_CAP_BLE_HID_HOST=1`, NimBLE-Arduino 2.5.1 и `custom_sdkconfig` (центральная роль, одно соединение). `custom_sdkconfig` обязателен: на готовом ядре NimBLE дублируется и линковка падает. Не собиралось и не проверялось на устройстве; для X3/X4 (C3) дополнительно нужны `scripts/patch_arduino_ble_controller.py` и контроль кучи.
