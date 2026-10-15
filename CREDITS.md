# Авторы и сторонние компоненты / Credits / Third-party notices

## Русский

Порт для MeeGo / Genesis: **SlayerSS** (Copyright (C) 2026 SlayerSS).

doom-meego распространяется целиком под лицензией **GNU General Public License v2** (см. `LICENSE`).
Исходные заголовки с авторскими правами во всех файлах сохранены без изменений.

| Компонент | Файлы | Авторы | Лицензия |
|---|---|---|---|
| Движок DOOM | `doomgeneric/*` (код игры; в `doomgeneric/i_video.c` гамма по умолчанию изменена на 2) | Copyright (C) 1993-1996 Id Software, Inc. | GPL-2.0 (публикация исходников 1997/1999) |
| Chocolate Doom | `doomgeneric/*` (i_*, m_*, w_*, d_iwad, deh_*.h и др.) | Copyright (C) 2005-2014 Simon Howard и соавторы | GPL-2.0-or-later |
| doomgeneric | `doomgeneric/doomgeneric.[ch]` (порты upstream под SDL/X11/Win и др. в это дерево не входят) | ozkl (https://github.com/ozkl/doomgeneric) | GPL-2.0 |
| mus2mid | `doomgeneric/mus2mid.c` | Ben Ryves 2006, Simon Howard | GPL-2.0 |
| sha1 | `doomgeneric/sha1.c` | Werner Koch / Simon Howard (на основе GnuPG) | GPL-2.0-or-later |
| Платформенный слой doom-meego (видео, сенсор, захват/отпускание кнопок, фокус, меню выбора IWAD) | `src/doomgeneric_meego.cpp`, `src/meego_input.*` | Copyright (C) 2026 SlayerSS | GPL-2.0 |
| Микшер звуков ALSA | `src/i_alsa.c` | Copyright (C) 2026 SlayerSS | GPL-2.0 |
| Драйвер музыки OPL | `src/i_oplmus.c` (проигрыватель MUS/MIDI, инструменты GENMIDI из WAD) | Copyright (C) 2026 SlayerSS | GPL-2.0 |
| Эмулятор Nuked OPL3 | `src/opl3.c`, `src/opl3.h` (v1.8, без изменений; текст лицензии `src/opl3-LICENSE.txt`) | Copyright (C) 2013-2020 Nuke.YKT (https://github.com/nukeykt/Nuked-OPL3) | LGPL-2.1-or-later (совместима с GPL) |

Библиотеки времени выполнения (не входят в проект, подключаются динамически / через dlopen из ШГУ):
Qt 4.8 (LGPL-2.1), ALSA libasound (LGPL-2.1), glibc (LGPL-2.1), библиотеки X11 (MIT).

Игровые данные (не входят): **Freedoom** (https://freedoom.github.io/) — BSD-3-Clause, Copyright © 2001-2024 Contributors to the Freedoom project.
Оригинальные IWAD DOOM / DOOM II коммерческие, распространять их нельзя.

Вдохновение: DAudio2Doom от greenluigi1 (https://github.com/greenluigi1/DAudio2Doom).

DOOM — зарегистрированная торговая марка id Software LLC (ZeniMax / Microsoft). Hyundai, Genesis, LG —
торговые марки соответствующих владельцев. Проект не связан с ними и не одобрен ими.

### Об упаковке

Раскладка релиза на SD-карте: `AppUpgrade` (бинарник Doom), `.lge.upgrade.xml` и папка `doom-meego\` для WAD-файлов пользователя (например `freedoom1.wad`).
Есть два варианта `.lge.upgrade.xml` для семейств ШГУ **DH (2015)** и **DHPE (2018)**; см. `card/DH_2015` и `card/DHPE_2018`.

## English

MeeGo / Genesis port: **SlayerSS** (Copyright (C) 2026 SlayerSS).

doom-meego is distributed as a whole under the **GNU General Public License v2** (see `LICENSE`).
Original copyright headers in every file are kept unchanged.

| Component | Files | Authors | License |
|---|---|---|---|
| DOOM engine | `doomgeneric/*` (game code; `doomgeneric/i_video.c` default gamma changed to 2) | Copyright (C) 1993-1996 Id Software, Inc. | GPL-2.0 (source release 1997/1999) |
| Chocolate Doom | `doomgeneric/*` (i_*, m_*, w_*, d_iwad, deh_*.h etc.) | Copyright (C) 2005-2014 Simon Howard and contributors | GPL-2.0-or-later |
| doomgeneric | `doomgeneric/doomgeneric.[ch]` (upstream platform ports SDL/X11/Win/etc. are not included in this tree) | ozkl (https://github.com/ozkl/doomgeneric) | GPL-2.0 |
| mus2mid | `doomgeneric/mus2mid.c` | Ben Ryves 2006, Simon Howard | GPL-2.0 |
| sha1 | `doomgeneric/sha1.c` | Werner Koch / Simon Howard (GnuPG-derived) | GPL-2.0-or-later |
| doom-meego platform layer (video, touch, input grab/release, focus handling, IWAD menu) | `src/doomgeneric_meego.cpp`, `src/meego_input.*` | Copyright (C) 2026 SlayerSS | GPL-2.0 |
| ALSA SFX mixer | `src/i_alsa.c` | Copyright (C) 2026 SlayerSS | GPL-2.0 |
| OPL music driver | `src/i_oplmus.c` (MUS/MIDI player, GENMIDI instruments from the WAD) | Copyright (C) 2026 SlayerSS | GPL-2.0 |
| Nuked OPL3 emulator | `src/opl3.c`, `src/opl3.h` (v1.8, unmodified; license text `src/opl3-LICENSE.txt`) | Copyright (C) 2013-2020 Nuke.YKT (https://github.com/nukeykt/Nuked-OPL3) | LGPL-2.1-or-later (GPL-compatible) |

Runtime libraries (not included, dynamically linked / dlopen'ed from the head unit):
Qt 4.8 (LGPL-2.1), ALSA libasound (LGPL-2.1), glibc (LGPL-2.1), X11 libraries (MIT).

Game data (not included): **Freedoom** (https://freedoom.github.io/) — BSD-3-Clause, Copyright © 2001-2024 Contributors to the Freedoom project.
The original DOOM / DOOM II IWADs are commercial and must not be redistributed.

Inspiration: DAudio2Doom by greenluigi1 (https://github.com/greenluigi1/DAudio2Doom).

DOOM is a registered trademark of id Software LLC (ZeniMax / Microsoft). Hyundai, Genesis, LG are
trademarks of their respective owners. This project is not affiliated with or endorsed by any of them.

### Packaging note

Release layout for the SD card: `AppUpgrade` (the Doom binary), `.lge.upgrade.xml` and a `doom-meego\` folder for the user-supplied WAD files (e.g. `freedoom1.wad`).
Two `.lge.upgrade.xml` variants exist for head-unit families **DH (2015)** and **DHPE (2018)**; see `card/DH_2015` and `card/DHPE_2018`.

