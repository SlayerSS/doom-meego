// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 SlayerSS
// Part of doom-meego (doomgeneric port for Hyundai Genesis DH / LG DHAVN MeeGo head units).
#ifndef MEEGO_INPUT_H
#define MEEGO_INPUT_H
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    HU_WHEEL_LEFT, HU_WHEEL_RIGHT, HU_SEEK_DOWN, HU_SEEK_UP,
    HU_MUTE, HU_END_CALL, HU_VOLUME_KNOB_PUSH, HU_TUNE_KNOB_PUSH
} HuControl;
void meego_controlEvent(HuControl c, int pressed); /* feed a control into the key queue */
void meego_addKeyToQueue(int pressed, unsigned char doomKey);
int  meego_releaseAllKeys(void);    /* queue a release for every key still down; returns count */
void meego_input_init(void);
void meego_input_poll(void);
void meego_quit(void);              /* release grabs and exit(0) */
void meego_setVisible(int on);      /* hide/show (call, camera, or stdin S) */
void meego_diag(const char *why);   /* [diag] dump of all /dev/input/event* + X focus */
void meego_focusEvent(const char *what, int lost);  /* Qt/X focus change */
void meego_resetKeys(const char *why);
void meego_x11_report(const char *why);
const char *meego_exe_dir(void);   /* directory of /proc/self/exe */
void meego_setup_log(void);        /* freopen stderr to <exe>/doom_log.txt */
char *meego_find_iwad(void);       /* malloc path or NULL; miss -> show msg */
void meego_show_fatal(const char *msg_utf8); /* fullscreen msg then exit */
void I_AlsaShutdown(void);         /* from i_alsa.c */
#ifdef __cplusplus
}
#endif
#endif
