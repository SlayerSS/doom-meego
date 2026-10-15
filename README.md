# doom-meego v1.1

Doom (Chocolate Doom / doomgeneric) для магнитолы Hyundai Genesis (LG DHAVN, MeeGo IVI, i686).

> В репозитории **нет** прошивки, ключей Hyundai/LG и WAD-файлов.
> Нужен свой `freedoom1.wad` (или другой IWAD) — см. установку ниже.

> The repository contains **no** firmware, Hyundai/LG keys or WAD files.
> You need your own `freedoom1.wad` (or another IWAD) — see install below.

---

## На русском

### Возможности

- Doom на штатной магнитоле Genesis (DH 2015 / DHPE 2018), запуск как `AppUpgrade` с SD-карты
- Музыка на эмуляторе **Nuked OPL3** v1.8 (режим OPL3, 18 голосов, чип 49716 Гц → 48 кГц, инструменты GENMIDI из WAD; MUS через mus2mid)
- Сенсор (зоны движения/стрельбы), шайба CCP/RRC, кнопки руля
- **Прямое чтение кнопок с MICOM** через `EVIOCGRAB` (`/dev/input/event*`) — без посредника Updater
- **Новое в v1.1:** меню выбора игры, если рядом с программой или в папке `WAD\` лежит больше одного поддерживаемого WAD (Freedoom 1/2, Doom, Doom II, Final Doom, **Chex Quest** — в т.ч. `CHEX.WAD` с заголовком PWAD); последний выбор только подсвечивается, сохраняется в `doom_last_wad.txt`
- **Новое в v1.1:** при потере переднего плана (задняя камера, меню магнитолы, вызов) кнопки сразу отпускаются; игра ставится на паузу и возвращается, когда снова впереди
- **Новое в v1.1:** журнал по умолчанию выключен; включается файлом `doom_log_on.txt` рядом с программой (или `DOOM_LOG=1`)
- HOME ставит игру на паузу / возвращает в Updater по правилам раскладки; длинный BACK 3 с — выход

### Установка (готовый релиз)

1. **Готовый пакет** из [Releases](https://github.com/SlayerSS/doom-meego/releases) — выберите свой вариант:
   - `doom-meego-v1.1-DH_2015.zip` — автомобили **DH, 2015** (`DH.EUR.SOP.10.017`)
   - `doom-meego-v1.1-DHPE_2018.zip` — автомобили **DHPE, 2018** (`DHPE.EUR.SOP.10.017`)
2. **Только WAD** (данные игры): `freedoom1.wad` (Freedoom Phase 1) с  
   https://github.com/freedoom/freedoom/releases (файл `freedoom-*.zip` → внутри `freedoom1.wad`).
3. Распакуйте zip релиза в **корень** SD-карты / флешки. Должны появиться:
   - `AppUpgrade`
   - `.lge.upgrade.xml`
   - папку `WAD\` (для WAD-файлов)  
   Имя `AppUpgrade` **не менять**. На Windows файл `.lge.upgrade.xml` может быть скрытым — включите показ скрытых файлов.
4. Положите `freedoom1.wad` (и при желании другие WAD) в папку **`WAD\`** или **рядом с `AppUpgrade`**. Если файлов больше одного — при запуске будет меню выбора.

Структура на карте после установки:

```
<корень карты>
├── AppUpgrade              ← Doom (из Releases)
├── .lge.upgrade.xml
├── README.txt
└── WAD\                    ← папка для WAD (из zip)
    ├── положите_сюда_WAD.txt
    └── freedoom1.wad       ← скачать отдельно
```

| Сообщение | Что сделать |
|---|---|
| «Не найден WAD-файл…» | `freedoom1.wad` в папке `WAD\` или рядом с `AppUpgrade` |

Вставьте карту в магнитолу и подтвердите обновление — запустится Doom.

### Управление (прямое чтение MICOM)

| Действие | Управление |
|---|---|
| Ходьба / поворот | Сенсорные зоны / шайба |
| Огонь | Кнопка на руле (END CALL) / зона экрана |
| Use / открыть | MUTE / нажатие шайбы |
| Пауза | MODE кратко |
| Выход | MODE долго / длинный BACK 3 с |

Точная раскладка — в игре и в опциональном `keys.txt` рядом с `AppUpgrade`.

### Журнал и поиск WAD

Рядом с `AppUpgrade`: `doom_log_on.txt` (пустой файл включает `doom_log.txt`). Опционально `keys.txt` (рядом с `AppUpgrade`, затем `/navi/keys.txt`).

Игра ищет WAD **рядом с собой** и в папке **`WAD`**. Имена по приоритету: `freedoom1.wad`, `doom.wad`, `doom1.wad`, `freedoom2.wad`, `doom2.wad`, `chex.wad`, затем любой `*.wad`. Chex Quest (`chex.wad` / `CHEX.WAD`) поддерживается, даже если файл с заголовком PWAD.

### Сборка из исходников

Нужен Ubuntu 12.04 i386 chroot (EGLIBC 2.15, Qt 4.8), см. `scripts/build-i686.sh`.

```bash
sudo ./scripts/build-i686.sh --bootstrap   # один раз
sudo ./scripts/build-i686.sh               # -> bin/doom-meego-i686 и builds/
```

Тест на ПК (Qt 5): `qmake doom-meego.pro && make && ./doom-meego -iwad freedoom1.wad`.

### Лицензии

См. [CREDITS.md](CREDITS.md). Код — GPL-2.0. Nuked OPL3 — LGPL-2.1+. WAD не входят в релиз.

---

## English

### Features

- Doom on the stock Genesis head unit (DH 2015 / DHPE 2018), launched as `AppUpgrade` from an SD card
- Music via the **Nuked OPL3** v1.8 emulator (OPL3 mode, 18 voices, 49716 Hz chip → 48 kHz, GENMIDI from the WAD; MUS via mus2mid)
- Touch (move/fire zones), CCP/RRC jog, steering-wheel keys
- **Direct MICOM key reading** via `EVIOCGRAB` (`/dev/input/event*`) — no Updater middleman
- **New in v1.1:** game selection menu when more than one supported WAD is next to the binary or in the `WAD\` folder (Freedoom 1/2, Doom, Doom II, Final Doom, **Chex Quest** including `CHEX.WAD` with a PWAD header); the last choice is only preselected and stored in `doom_last_wad.txt`
- **New in v1.1:** on foreground loss (rear camera, head-unit menu, call) keys are released at once; the game pauses and returns when it is in front again
- **New in v1.1:** logging is off by default; enable with `doom_log_on.txt` next to the binary (or `DOOM_LOG=1`)
- HOME pauses / returns per the key map; long BACK 3 s exits

### Install (release package)

1. **Ready package** from [Releases](https://github.com/SlayerSS/doom-meego/releases) — pick your variant:
   - `doom-meego-v1.1-DH_2015.zip` — **DH, 2015** (`DH.EUR.SOP.10.017`)
   - `doom-meego-v1.1-DHPE_2018.zip` — **DHPE, 2018** (`DHPE.EUR.SOP.10.017`)
2. **WAD only:** `freedoom1.wad` (Freedoom Phase 1) from  
   https://github.com/freedoom/freedoom/releases.
3. Unpack the release zip into the **root** of the SD card. You should get:
   - `AppUpgrade`
   - `.lge.upgrade.xml`
   - the `WAD\` folder (for WAD files)  
   **Do not rename** `AppUpgrade`. On Windows `.lge.upgrade.xml` may be hidden.
4. Put `freedoom1.wad` (and any other WADs) into **`WAD\`** or **next to `AppUpgrade`**. With more than one file, a selection menu appears at start.

Card layout:

```
<card root>
├── AppUpgrade
├── .lge.upgrade.xml
├── README.txt
└── WAD\
    ├── положите_сюда_WAD.txt
    └── freedoom1.wad
```

The game looks for WAD files **next to itself** and in the **`WAD`** folder. Chex Quest (`chex.wad` / `CHEX.WAD`) is supported even when the file has a PWAD header.

### Build

See `scripts/build-i686.sh` (Ubuntu 12.04 i386 chroot, Qt 4.8).

### Licenses

See [CREDITS.md](CREDITS.md). Code is GPL-2.0. Nuked OPL3 is LGPL-2.1+. WADs are not shipped.
