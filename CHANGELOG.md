# Changelog

## v1.1 — 2026-10-15

### Русский

- Меню выбора игры (появляется, только если рядом с выбранным лежит больше одного поддерживаемого IWAD); сенсор, шайба, стрелки, NAV; последний выбор сохраняется в `doom_last_wad.txt`; пропустить — `-nowadmenu` / `DOOM_NOWADMENU`
- Устройства ввода открываются с `O_CLOEXEC`, дочерние процессы их не наследуют
- Захват кнопок (`EVIOCGRAB`) отпускается при любом выходе (обычный выход, BACK/HOME, SIGTERM/SIGINT/SIGHUP, сигналы сбоя, `atexit`), при скрытии (BG от UI-менеджера / камера заднего вида) и при потере фокуса X11; при возврате — повторный захват; все нажатые клавиши сбрасываются
- Фокус: пауза и тишина, когда другое окно перекрывает игру, даже без события от UI-менеджера
- Журнал по умолчанию выключен; включается файлом `doom_log_on.txt` рядом с WAD/программой или `DOOM_LOG=1`; расширенная диагностика ввода в журнале
- Кнопки руля читаются напрямую с устройства кнопок MICOM: принять вызов = огонь (держать — очередь), завершить вызов = «использовать», джойстик вверх/вниз = следующее/предыдущее оружие, MODE = карта, голос = пауза/продолжить; HOME переключает паузу, продолжение поднимает Doom на экран
- В zip релиза есть папка `doom-meego\` для WAD-файлов (рекомендуемое место; рядом с `AppUpgrade` тоже работает)
- Работает из папки `game\doom-meego\` сборки Genesis-Updater (WAD рядом с программой)

### English

- IWAD selection menu (shown only when more than one supported IWAD lies next to the chosen one); touch, jog, arrows, NAV; last choice saved in `doom_last_wad.txt`; `-nowadmenu` / `DOOM_NOWADMENU` to skip
- Input devices opened with `O_CLOEXEC`, so child processes never inherit them
- Key grab (`EVIOCGRAB`) is released on every exit path (normal quit, BACK/HOME, SIGTERM/SIGINT/SIGHUP, crash signals, `atexit`), on hide (UI-manager BG / rear camera) and on X11 focus loss; re-grabbed on return; all held keys are reset
- Focus handling: pause + mute when another window takes over even without a UI-manager event
- Logging off by default; enable with `doom_log_on.txt` next to the WAD/binary or `DOOM_LOG=1`; extended input diagnostics in the log
- Steering-wheel keys read directly from the MICOM hard-key device: call send = fire (hold = auto), call end = use, stick up/down = next/previous weapon, MODE = automap, voice = pause/resume; HOME toggles pause and resume raises Doom
- Release zips include a `doom-meego\` folder for WAD files (recommended place; next to `AppUpgrade` still works)
- Works from the Genesis-Updater card layout `game\doom-meego\` (WAD next to the binary)

## v1.0 — 2026-10-12

First public release of doom-meego for the Hyundai Genesis head unit (LG DHAVN, MeeGo IVI, i686).
Replaces all earlier repository content.

- Flat SD-card packages for **DH 2015** (`DH.EUR.SOP.10.017`) and **DHPE 2018** (`DHPE.EUR.SOP.10.017`)
- ALSA sound at 48 kHz, device order `main_mix2` → `media_mixer` → `default`
- Music: Nuked OPL3 v1.8 emulator (LGPL-2.1+), OPL3 mode, 18 voices, GENMIDI from the WAD, DMX/Chocolate Doom pitch and volume tables
- Video: 960×720 (4:3) from 320×200, centred on 1280×720; default gamma 2
- Touch zones on the side bars only (left: Меню=Esc / Вперёд / Влево / Вправо / Назад; right: Карта / Ввод / Действие / Огонь), drawn with labels and icons; hold Меню 2 s = quit
- Jog dial and panel buttons; steering-wheel keys via Genesis-Updater; optional `keys.txt` remap; long BACK (~1.5 s) quits; HOME = pause standalone, quit when launched from the Updater
- Interrupt handling: pause + mute when a call, rear camera or other system screen takes over; automatic return to the foreground afterwards (UI-manager BG/FG/rear-gear events via the Updater, X11 watchdog with 3→96 s backoff)
- Black window from the first moment (no white flash at startup)
- Diagnostics: `doom_log.txt`
