# Changelog

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
