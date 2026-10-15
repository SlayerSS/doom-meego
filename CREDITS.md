# Credits / Third-party notices

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

## Packaging note

Release layout for the SD card is **flat**: `AppUpgrade` (the Doom binary), `.lge.upgrade.xml`, and user-supplied `freedoom1.wad`.
Two `.lge.upgrade.xml` variants exist for head-unit families **DH (2015)** and **DHPE (2018)**; see `card/DH_2015` and `card/DHPE_2018`.

