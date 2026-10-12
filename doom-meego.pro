# Flat-card release: copy the built binary to builds/AppUpgrade (head unit name).
# qmake build (works with Qt4.8 qmake for the head unit and Qt5 qmake on desktop)
TEMPLATE = app
TARGET = doom-meego
QT += core gui
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets
CONFIG -= app_bundle
INCLUDEPATH += doomgeneric src
DEFINES += NORMALUNIX LINUX _DEFAULT_SOURCE DOOMGENERIC_RESX=640 DOOMGENERIC_RESY=400
meego { DEFINES += MEEGO_TARGET }
QMAKE_CFLAGS += -Wno-unused -w
SRC_DOOM = dummy am_map doomdef doomstat dstrings d_event d_items d_iwad d_loop d_main d_mode d_net f_finale f_wipe g_game hu_lib hu_stuff info i_cdmus i_endoom i_joystick i_scale i_sound i_system i_timer memio m_argv m_bbox m_cheat m_config m_controls m_fixed m_menu m_misc m_random p_ceilng p_doors p_enemy p_floor p_inter p_lights p_map p_maputl p_mobj p_plats p_pspr p_saveg p_setup p_sight p_spec p_switch p_telept p_tick p_user r_bsp r_data r_draw r_main r_plane r_segs r_sky r_things sha1 sounds statdump st_lib st_stuff s_sound tables v_video wi_stuff w_checksum w_file w_main w_wad z_zone w_file_stdc i_input i_video doomgeneric mus2mid
for(f, SRC_DOOM): SOURCES += doomgeneric/$${f}.c
SOURCES += src/doomgeneric_meego.cpp src/meego_input.cpp
HEADERS += src/meego_input.h

meego { LIBS += -ldl }

# ALSA SFX (libasound dlopen at runtime)
DEFINES += DG_ALSA_SOUND
SOURCES += src/i_alsa.c src/i_oplmus.c src/opl3.c
HEADERS += src/opl3.h
LIBS += -lm -lrt
LIBS += -ldl -lpthread
