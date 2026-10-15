# Changelog

## v1.1

- Меню выбора игры (если рядом с программой или в `WAD\` больше одного поддерживаемого WAD); сенсор, шайба, стрелки, NAV; последний выбор только подсвечивается (`doom_last_wad.txt`); пропуск — `-nowadmenu` / `DOOM_NOWADMENU`
- **Chex Quest:** `chex.wad` / `CHEX.WAD` в меню и в запуске (файл с заголовком PWAD больше не отбрасывается)
- При потере переднего плана (камера ЗХ, меню магнитолы, вызов) EVIOCGRAB отпускается сразу (watchdog >1 с); повторный захват только в настоящем foreground; автовозврат после камеры; backoff авто-raise ограничен ~8 с
- Кнопки руля — прямое чтение MICOM (`EVIOCGRAB`)
- Журнал по умолчанию выключен; `doom_log_on.txt` рядом с программой или `DOOM_LOG=1`
- В zip релиза: `AppUpgrade` + `.lge.upgrade.xml` + `README.txt` + папка `WAD\` (WAD также можно класть рядом с `AppUpgrade`)

- IWAD selection menu (when more than one supported WAD is next to the binary or in `WAD\`); touch, jog, arrows, NAV; last choice only preselects (`doom_last_wad.txt`); skip with `-nowadmenu` / `DOOM_NOWADMENU`
- **Chex Quest:** `chex.wad` / `CHEX.WAD` listed and playable (PWAD header no longer rejected)
- On foreground loss (reverse camera, HU menu, call) EVIOCGRAB is released at once (>1 s watchdog); re-grab only when truly foreground; auto-return after camera; auto-raise backoff capped ~8 s
- Steering-wheel keys via direct MICOM `EVIOCGRAB`
- Logging off by default; `doom_log_on.txt` next to the binary or `DOOM_LOG=1`
- Release zip layout: `AppUpgrade` + `.lge.upgrade.xml` + `README.txt` + `WAD\` folder (WADs may also sit next to `AppUpgrade`)

## v1.0

First public release of doom-meego for the Hyundai Genesis head unit (LG DHAVN, MeeGo IVI, i686).

- Flat-card launch as `AppUpgrade` + `.lge.upgrade.xml` (DH 2015 / DHPE 2018)
- Touch, jog, steering-wheel input
- Music: Nuked OPL3 v1.8 emulator (LGPL-2.1+), OPL3 mode, 18 voices, GENMIDI from the WAD, DMX/Chocolate Doom pitch and volume tables
