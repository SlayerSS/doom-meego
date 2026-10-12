// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 SlayerSS
// Part of doom-meego (doomgeneric port for Hyundai Genesis DH / LG DHAVN MeeGo head units).
// On-device vehicle input for the flat-card launch: the binary is named AppUpgrade and
// started directly by the head unit when it finds /.lge.upgrade.xml on the SD card.
//
// Input sources:
//  1) Direct EVIOCGRAB of /dev/input/event* (MICOM jog + console keys) — primary.
//  2) Optional stdin protocol (legacy): "B/E/R/S/Q" lines if a parent still forwards keys.
//     "S 0"/"S 1" = hide+pause / show+resume; "Q" = quit.
//
// Diagnostics ("doom-meego: [diag] ..." on stderr -> doom_log.txt next to the executable):
//  - every /dev/input/event*: name, fd/grab state, EVIOCGRAB probe, holders from /proc/*/fd.
#include "meego_input.h"
#include "doomkeys.h"
extern "C" void I_AlsaPause(int p);
extern "C" void I_AlsaFocusPause(int lost, const char *why);
extern "C" void I_AlsaShutdown(void);
static int s_xLost = 0;   // X says our window is unmapped/obscured/unfocused -> audio off
/* Auto-return to foreground: "hard" loss = our window unmapped / fully obscured by
 * another app (call, rear camera, any system overlay) while we did not hide ourselves. */
static int s_hardLost = 0;
static unsigned s_hardLostAt = 0;
static int s_focusPaused = 0;     // game paused by us because of the hard loss
static unsigned s_nextRaise = 0;
static unsigned s_raiseBackoff = 3000;
static int s_onHU = 0;           // LG DHAVN head unit detected: auto re-raise allowed (never on a desktop)
static int s_intr = 0;            // Updater reports a known interrupt (S 0 / I 1): never fight it
static void requestRaiseNow(const char *why);
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/stat.h>
// linux/input.h clashes with doomkeys.h (KEY_ENTER etc.), so define what we need.
struct lx_input_event { struct timeval time; uint16_t type; uint16_t code; int32_t value; };
#define LX_EVIOCGRAB _IOW('E', 0x90, int)
#define LX_EVIOCGNAME(len) _IOC(_IOC_READ, 'E', 0x06, len)
enum { LX_EV_SYN = 0, LX_EV_KEY = 1, LX_EV_REL = 2, LX_EV_ABS = 3, LX_REL_Y = 1 };
struct lx_absinfo { int32_t value, minimum, maximum, fuzz, flat, resolution; };
#define LX_EVIOCGABS(abs) _IOR('E', 0x40 + (abs), struct lx_absinfo)
extern "C" int menuactive;  // m_menu.c (boolean)
extern "C" void meego_touch(int x, int y, int down);

extern "C" uint32_t DG_GetTicksMs(void);
extern "C" int paused;      // g_game.c (boolean == int in doomgeneric)
extern "C" int sendpause;

enum {
    EV_HU_BACK = 20, EV_HU_HOME = 21, EV_HU_MENU = 22,
    EV_HU_SEEK_NEXT = 28, EV_HU_SEEK_PREV = 29,
    EV_RRC_BACK = 38, EV_RRC_SEEK_NEXT = 45, EV_RRC_SEEK_PREV = 46, EV_RRC_MUTE = 49,
    EV_SWRC_MODE = 52, EV_SWRC_CALL_SEND = 53, EV_SWRC_CALL_END = 54, EV_SWRC_MUTE = 55,
    EV_SWRC_SEEK_NEXT = 58, EV_SWRC_SEEK_PREV = 59, EV_SOFT_BACK = 60,
    EV_HU_TUNE_LEFT = 78, EV_HU_TUNE_RIGHT = 79, EV_HU_TUNE_PRESSED = 80,
    EV_CCP_BACK = 81, EV_CCP_HOME = 82, EV_CCP_MENU = 83,
    EV_CCP_JOG_FIRST = 84, EV_RRC_JOG_FIRST = 93,
    EV_HU_ENC = 103, EV_CCP_ENC = 104, EV_RRC_ENC = 105
};

static int s_stdinOk = 0;
static char s_buf[512];
static int s_len = 0;
static int s_hidden = 0;
static int s_pausedByUs = 0;
static uint32_t s_hiddenAt = 0;
static uint32_t s_startMs = 0;

// own evdev
#define MAXDEV 16
static int s_fd[MAXDEV];
static int s_grabWanted[MAXDEV];
static int s_grabbed[MAXDEV];
static int s_isTouch[MAXDEV];
static int s_lastErr[MAXDEV];       // last read() errno other than EAGAIN (0 = fine)
static int s_grabErr[MAXDEV];       // last EVIOCGRAB errno logged (avoid repeats)
static char s_path[MAXDEV][32];
static char s_name[MAXDEV][96];
static int s_ndev = 0;
static int s_needReopen = 0;
static uint32_t s_nextGrabTry = 0;
static uint32_t s_backDownAt = 0;    // evdev BACK/ESC held since
static int s_tMinX = 0, s_tMaxX = 1279, s_tMinY = 0, s_tMaxY = 1439;
static int s_tX = -1, s_tY = -1, s_tDown = 0, s_tDownSent = 0;

static uint32_t s_jogPressAt = 0;
static int s_jogPressKey = -1;
static int s_jogUsed = 0;
static uint32_t rel(void) { return DG_GetTicksMs() - s_startMs; }

// ---------------------------------------------------------------- diagnostics
// Every process that has /dev/input/eventN open: "pid:comm[cmdline] ..."
static void holdersOf(const char *dev, char *out, size_t outSize)
{
    out[0] = 0;
    size_t used = 0;
    DIR *proc = opendir("/proc");
    if (!proc) { snprintf(out, outSize, "(no /proc)"); return; }
    struct dirent *pe;
    pid_t self = getpid();
    while ((pe = readdir(proc)) != 0) {
        if (!isdigit((unsigned char)pe->d_name[0])) continue;
        char fdDir[64];
        snprintf(fdDir, sizeof(fdDir), "/proc/%s/fd", pe->d_name);
        DIR *fds = opendir(fdDir);
        if (!fds) continue;
        struct dirent *fe;
        int nOpen = 0;
        while ((fe = readdir(fds)) != 0) {
            if (fe->d_name[0] == '.') continue;
            char lp[128], tgt[128];
            snprintf(lp, sizeof(lp), "%s/%s", fdDir, fe->d_name);
            ssize_t n = readlink(lp, tgt, sizeof(tgt) - 1);
            if (n <= 0) continue;
            tgt[n] = 0;
            if (strcmp(tgt, dev) == 0) ++nOpen;
        }
        closedir(fds);
        if (!nOpen) continue;
        char comm[64] = "?", cmd[160] = "", p[64];
        snprintf(p, sizeof(p), "/proc/%s/comm", pe->d_name);
        FILE *f = fopen(p, "r");
        if (f) { if (fgets(comm, sizeof(comm), f)) { char *nl = strchr(comm, '\n'); if (nl) *nl = 0; } fclose(f); }
        snprintf(p, sizeof(p), "/proc/%s/cmdline", pe->d_name);
        int cf = open(p, O_RDONLY);
        if (cf >= 0) {
            ssize_t n = read(cf, cmd, sizeof(cmd) - 1);
            close(cf);
            if (n < 0) n = 0;
            cmd[n] = 0;
            for (ssize_t i = 0; i + 1 < n; ++i) if (!cmd[i]) cmd[i] = ' ';
        }
        int w = snprintf(out + used, outSize - used, "%s%s:%s%s[%s]x%d", used ? " " : "", pe->d_name, comm,
                         atoi(pe->d_name) == (int)self ? "(us)" : "", cmd, nOpen);
        if (w < 0 || (size_t)w >= outSize - used) { used = outSize - 1; break; }
        used += (size_t)w;
    }
    closedir(proc);
    if (!out[0]) snprintf(out, outSize, "(nobody)");
}

static const char *errName(int e)
{
    switch (e) {
    case 0: return "ok";
    case EBUSY: return "EBUSY";
    case ENODEV: return "ENODEV";
    case EBADF: return "EBADF";
    case EINVAL: return "EINVAL";
    case EACCES: return "EACCES";
    case EPERM: return "EPERM";
    case EIO: return "EIO";
    case ENOENT: return "ENOENT";
    default: return "err";
    }
}

// Probe EVIOCGRAB on fd without stealing it: grab, and release again unless we want it.
static const char *probeGrab(int fd, int keep, int *gotIt)
{
    static char r[48];
    *gotIt = 0;
    if (ioctl(fd, LX_EVIOCGRAB, 1) == 0) {
        if (keep) { *gotIt = 1; snprintf(r, sizeof(r), "free->grabbed by us"); }
        else { ioctl(fd, LX_EVIOCGRAB, 0); snprintf(r, sizeof(r), "free (nobody grabs)"); }
    } else {
        int e = errno;
        snprintf(r, sizeof(r), "%s%s", errName(e), e == EBUSY ? " (grabbed by another process)" : "");
    }
    return r;
}

extern "C" void meego_x11_report(const char *why);   // doomgeneric_meego.cpp

static uint32_t s_lastDiagAt = 0;
extern "C" void meego_diag(const char *why)
{
    s_lastDiagAt = DG_GetTicksMs();
    fprintf(stderr, "doom-meego: [diag] === %s t=%u hidden=%d paused=%d menu=%d ndev=%d\n",
            why, rel(), s_hidden, paused ? 1 : 0, menuactive ? 1 : 0, s_ndev);
    char holders[1024];
    for (int n = 0; n < 32; ++n) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/input/event%d", n);
        struct stat st;
        int ours = -1;
        for (int i = 0; i < s_ndev; ++i) if (strcmp(s_path[i], path) == 0) ours = i;
        if (stat(path, &st) != 0) {
            if (ours >= 0) fprintf(stderr, "doom-meego: [diag]  %s GONE (our fd %d stale)\n", path, s_fd[ours]);
            continue;
        }
        holdersOf(path, holders, sizeof(holders));
        if (ours >= 0) {
            int i = ours;
            int alive = fcntl(s_fd[i], F_GETFD) >= 0;
            const char *g;
            char gb[64];
            if (!alive) g = "fd closed";
            else if (s_grabbed[i]) {
                // re-assert: EVIOCGRAB(1) on an fd that already holds the grab returns EBUSY on
                // some kernels; a dead device returns ENODEV.
                int rc = ioctl(s_fd[i], LX_EVIOCGRAB, 1);
                int e = rc ? errno : 0;
                snprintf(gb, sizeof(gb), "held by us (%s)", rc == 0 ? "re-grab ok" : e == EBUSY ? "still grabbed" : errName(e));
                if (rc && e == ENODEV) { s_grabbed[i] = 0; s_needReopen = 1; }
                g = gb;
            } else if (s_isTouch[i]) {
                int got; g = probeGrab(s_fd[i], 0, &got);
            } else {
                int got; g = probeGrab(s_fd[i], s_grabWanted[i] && !s_hidden, &got);
                if (got) s_grabbed[i] = 1;
            }
            fprintf(stderr, "doom-meego: [diag]  %s '%s' ours fd=%d %s%s want=%d grab=[%s] lastReadErr=%s holders: %s\n",
                    path, s_name[i], s_fd[i], alive ? "open" : "CLOSED", s_isTouch[i] ? " touch" : "",
                    s_grabWanted[i], g, errName(s_lastErr[i]), holders);
        } else {
            char nm[96] = "";
            char sys[96];
            snprintf(sys, sizeof(sys), "/sys/class/input/event%d/device/name", n);
            FILE *f = fopen(sys, "r");
            if (f) { if (fgets(nm, sizeof(nm), f)) { char *nl = strchr(nm, '\n'); if (nl) *nl = 0; } fclose(f); }
            const char *g = "not opened";
            int fd = open(path, O_RDONLY | O_NONBLOCK);
            if (fd >= 0) { int got; g = probeGrab(fd, 0, &got); close(fd); }
            fprintf(stderr, "doom-meego: [diag]  %s '%s' not ours (skipped) grab=[%s] holders: %s\n", path, nm, g, holders);
        }
    }
    meego_x11_report(why);
}

static unsigned char s_evDown[1024];   // evdev key state: one event per press, ignore repeats
static int s_evdevSeen = 0;             // evdev keys arrive -> menus ignore framework nav
// ---------------------------------------------------------------- keys
static void key(int doomKey, int pressed)
{
    if (pressed && s_xLost && !s_hidden) {   // user is playing -> window is really ours, audio back
        s_xLost = 0; I_AlsaFocusPause(0, "key press while visible");
    }
    if (pressed && s_hardLost && !s_hidden && !s_intr)
        requestRaiseNow("key press while window lost");
    if (doomKey >= 0 && !s_hidden)
        meego_addKeyToQueue(pressed, (unsigned char)doomKey);
}

// Timed holds: Doom builds movement from gamekeydown[] once per tic, so a press and
// release in the same frame never moves the player. Each pulse keeps the key down
// for a while and extends while more detents/events arrive.
static uint32_t s_holdUntil[256];
static unsigned char s_holdDown[256];
static void pulse(int doomKey, uint32_t ms, uint32_t cap)
{
    if (doomKey < 0 || doomKey > 255 || s_hidden) return;
    uint32_t now = DG_GetTicksMs();
    if (!s_holdDown[doomKey]) { key(doomKey, 1); s_holdDown[doomKey] = 1; s_holdUntil[doomKey] = now; }
    if (s_holdUntil[doomKey] < now) s_holdUntil[doomKey] = now;
    s_holdUntil[doomKey] += ms;
    if (s_holdUntil[doomKey] > now + cap) s_holdUntil[doomKey] = now + cap;
}
static void releaseHolds(int all)
{
    uint32_t now = DG_GetTicksMs();
    for (int k = 0; k < 256; ++k)
        if (s_holdDown[k] && (all || (int32_t)(now - s_holdUntil[k]) >= 0)) {
            meego_addKeyToQueue(0, (unsigned char)k);
            s_holdDown[k] = 0;
        }
}
static void tap(int doomKey) { pulse(doomKey, 90, 90); }

// Release every key Doom thinks is down (focus loss / hide / re-entry).
extern "C" void meego_resetKeys(const char *why)
{
    releaseHolds(1);
    s_backDownAt = 0;
    s_jogPressAt = 0; s_jogPressKey = -1;
    s_tDown = 0;
    if (s_tDownSent) { meego_touch(0, 0, 0); s_tDownSent = 0; }
    memset(s_evDown, 0, sizeof(s_evDown));
    int n = meego_releaseAllKeys();
    fprintf(stderr, "doom-meego: [focus] keys reset (%s): %d held key(s) released\n", why, n);
}

// diagnostics: log input events to stderr (-> doom_log.txt), rate-limited
static int s_logCount = 0;
static void logEv(const char *src, int type, int code, int value)
{
    ++s_logCount;
    if (s_logCount <= 400 || (s_logCount % 50) == 0)
        fprintf(stderr, "doom-meego: ev %s type=%d code=%d val=%d menu=%d hidden=%d\n", src, type, code, value,
                menuactive ? 1 : 0, s_hidden);
}

static void turn(int delta)
{
    if (delta == 0) return;
    int n = delta < 0 ? -delta : delta;
    if (n > 6) n = 6;
    if (menuactive) { tap(delta < 0 ? KEY_UPARROW : KEY_DOWNARROW); return; }
    pulse(delta < 0 ? KEY_LEFTARROW : KEY_RIGHTARROW, 160 * n, 600);
}
static void walk(int delta)
{
    if (delta == 0) return;
    if (menuactive) { tap(delta < 0 ? KEY_UPARROW : KEY_DOWNARROW); return; }
    pulse(delta < 0 ? KEY_UPARROW : KEY_DOWNARROW, 250, 700);
}

// Back: short = Esc (Doom menu), long = quit to system.
static void backKey(int status)
{
    if (status == 0) key(KEY_ESCAPE, 1);
    else if (status == 1 || status == 4) key(KEY_ESCAPE, 0);
    else if (status == 2 || status == 3) { key(KEY_ESCAPE, 0); meego_quit(); }
}

// Jog press: menu = Enter; game = Fire while held, held >= 600 ms = Use (doors/switches).
static void jogPress(int down)
{
    if (down) {
        if (s_jogPressAt) return;
        s_jogPressAt = DG_GetTicksMs() | 1; s_jogUsed = 0;
        s_jogPressKey = menuactive ? KEY_ENTER : KEY_FIRE;
        key(s_jogPressKey, 1);
    } else {
        if (s_jogPressKey >= 0) key(s_jogPressKey, 0);
        s_jogPressKey = -1; s_jogPressAt = 0;
    }
}
static void jogPressPoll(void)
{
    if (!s_jogPressAt || s_jogUsed || s_jogPressKey != KEY_FIRE) return;
    if (DG_GetTicksMs() - s_jogPressAt >= 600) {
        s_jogUsed = 1;
        key(KEY_FIRE, 0); s_jogPressKey = -1;
        fprintf(stderr, "doom-meego: jog long press -> use\n");
        tap(KEY_USE);
    }
}

static void held(int doomKey, int status)
{
    if (status == 0) key(doomKey, 1);
    else if (status == 1 || status == 4) key(doomKey, 0);
}

static void jogDirection(int which, int status)
{
    // 0 TL,1 L,2 DL,3 UP,4 CENTER,5 DOWN,6 TR,7 R,8 DR
    static const int fwd[9]  = {1,0,0, 1,0,0, 1,0,0};
    static const int back[9] = {0,0,1, 0,0,1, 0,0,1};
    static const int sl[9]   = {1,1,1, 0,0,0, 0,0,0};
    static const int sr[9]   = {0,0,0, 0,0,0, 1,1,1};
    if (which < 0 || which > 8) return;
    if (which == 4) { if (status == 0) jogPress(1); else if (status == 1 || status == 4) jogPress(0); return; }
    if (fwd[which])  held(KEY_UPARROW, status);
    if (back[which]) held(KEY_DOWNARROW, status);
    if (sl[which])   held(KEY_LEFTARROW, status);   // menu-friendly: tilt left/right turns
    if (sr[which])   held(KEY_RIGHTARROW, status);
}

static void releaseGrabs(void)
{
    for (int i = 0; i < s_ndev; ++i)
        if (s_grabbed[i]) {
            int rc = ioctl(s_fd[i], LX_EVIOCGRAB, 0);
            fprintf(stderr, "doom-meego: [grab] release %s -> %s\n", s_path[i], rc == 0 ? "ok" : errName(errno));
            s_grabbed[i] = 0;
        }
}

static void sigReleaseGrabs(int sig)
{
    for (int i = 0; i < s_ndev; ++i)
        if (s_grabbed[i] && s_fd[i] >= 0) { ioctl(s_fd[i], LX_EVIOCGRAB, 0); s_grabbed[i] = 0; }
    _exit(128 + sig);
}

static void atexitReleaseGrabs(void) { releaseGrabs(); }

static void closeDevices(void)
{
    releaseGrabs();
    for (int i = 0; i < s_ndev; ++i) if (s_fd[i] >= 0) close(s_fd[i]);
    s_ndev = 0;
}

static void openDevices(void);
static void tryGrabs(int force);

static void reopenDevices(const char *why)
{
    fprintf(stderr, "doom-meego: [grab] reopen all input devices (%s)\n", why);
    closeDevices();
    openDevices();
    s_needReopen = 0;
    tryGrabs(1);
}

static void setHidden(int hide, const char *why)
{
    fprintf(stderr, "doom-meego: [focus] %s from %s (hidden %d -> %d) t=%u\n", hide ? "HIDE" : "SHOW", why, s_hidden, hide, rel());
    if (hide) s_intr = 1; else s_intr = 0;
    if (hide == s_hidden) { if (!hide) { meego_setVisible(1); fprintf(stderr, "doom-meego: [ui] want front (S 1 while visible)\n"); meego_resetKeys("show again"); reopenDevices("show again"); meego_diag("show (already visible)"); } return; }
    if (hide) {
        meego_resetKeys("hide");
        s_hidden = 1;
        s_hiddenAt = DG_GetTicksMs();
        if (!paused) { sendpause = 1; s_pausedByUs = 1; }
        releaseGrabs();                 // give the keys back to the system during call/camera
        meego_setVisible(0);
        I_AlsaPause(1);              // audio off while hidden
        meego_diag("after hide");
    } else {
        meego_resetKeys("show");
        s_hidden = 0;
        if (s_pausedByUs && paused) sendpause = 1;
        s_pausedByUs = 0;
        meego_setVisible(1);
        I_AlsaPause(0);              // audio back on
        // fds may be stale (micom re-open -> ENODEV) or another process grabbed meanwhile:
        // close everything, rescan /dev/input and grab again.
        reopenDevices("show/resume");
        meego_diag("after show");
    }
}

static void busEvent(int id, int st, int cmd)
{
    logEv("bus", id, st, cmd);
    if (s_hidden) {
        // Safety net: if FG was not restored (so no "S 1"), but the UI manager is
        // routing jog/knob keys to us again, we are clearly in front -> show again.
        int nav = (id >= EV_CCP_JOG_FIRST && id < EV_RRC_JOG_FIRST + 9) || id == EV_HU_ENC || id == EV_CCP_ENC
            || id == EV_RRC_ENC || id == EV_HU_TUNE_LEFT || id == EV_HU_TUNE_RIGHT || id == EV_HU_TUNE_PRESSED;
        if (nav && st == 0 && DG_GetTicksMs() - s_hiddenAt > 1500) setHidden(0, "jog key while hidden (no S 1)");
        else if ((id == EV_HU_HOME || id == EV_CCP_HOME) && (st == 0 || st == 2)) { meego_quit(); return; }
        else if ((id == EV_HU_BACK || id == EV_CCP_BACK || id == EV_RRC_BACK || id == EV_SOFT_BACK) && (st == 2 || st == 3)) { meego_quit(); return; }
        if (s_hidden) return;
    }
    if (menuactive && s_evdevSeen) {
        // Menus read the keys directly from evdev; framework nav copies would double-step.
        int nav = (id >= EV_CCP_JOG_FIRST && id < EV_RRC_JOG_FIRST + 9) || id == EV_HU_ENC || id == EV_CCP_ENC
            || id == EV_RRC_ENC || id == EV_HU_TUNE_LEFT || id == EV_HU_TUNE_RIGHT || id == EV_HU_TUNE_PRESSED
            || id == EV_HU_MENU || id == EV_CCP_MENU || id == EV_SWRC_CALL_SEND;
        if (nav) return;
    }
    if (id >= EV_CCP_JOG_FIRST && id < EV_CCP_JOG_FIRST + 9) { jogDirection(id - EV_CCP_JOG_FIRST, st); return; }
    if (id >= EV_RRC_JOG_FIRST && id < EV_RRC_JOG_FIRST + 9) { jogDirection(id - EV_RRC_JOG_FIRST, st); return; }
    switch (id) {
    case EV_HU_ENC: case EV_CCP_ENC: case EV_RRC_ENC: turn(cmd); break;
    case EV_HU_TUNE_LEFT:  turn(-1); break;
    case EV_HU_TUNE_RIGHT: turn(1); break;
    case EV_HU_TUNE_PRESSED: held(KEY_USE, st); break;
    case EV_SWRC_SEEK_NEXT: case EV_HU_SEEK_NEXT: case EV_RRC_SEEK_NEXT: held(KEY_UPARROW, st); break;
    case EV_SWRC_SEEK_PREV: case EV_HU_SEEK_PREV: case EV_RRC_SEEK_PREV: held(KEY_DOWNARROW, st); break;
    case EV_SWRC_MUTE: case EV_RRC_MUTE: held(KEY_USE, st); break;
    case EV_SWRC_CALL_END: held(KEY_FIRE, st); break;
    case EV_SWRC_CALL_SEND: held(KEY_ENTER, st); break;
    case EV_SWRC_MODE: if (st == 0) tap(KEY_PAUSE); else if (st == 2) meego_quit(); break;
    case EV_HU_HOME: case EV_CCP_HOME: if (st == 0 || st == 2) meego_quit(); break;
    case EV_HU_MENU: case EV_CCP_MENU: held(KEY_ENTER, st); break;
    case EV_HU_BACK: case EV_RRC_BACK: case EV_SOFT_BACK: case EV_CCP_BACK: backKey(st); break;
    default: break;
    }
}

static int s_userMap[1024];
static int s_userMapLoaded = 0;
static void loadUserMap(void)
{
    s_userMapLoaded = 1;
    char nextToExe[600];
    snprintf(nextToExe, sizeof(nextToExe), "%s/keys.txt", meego_exe_dir());
    const char *paths[] = { nextToExe, "/navi/keys.txt", 0 };
    for (int i = 0; paths[i]; ++i) {
        FILE *f = fopen(paths[i], "r");
        if (!f) continue;
        char ln[128]; int n = 0;
        while (fgets(ln, sizeof(ln), f)) {
            int c = -1, k = -1;
            if (ln[0] == '#') continue;
            if (sscanf(ln, "%d %i", &c, &k) == 2 && c > 0 && c < 1024) { s_userMap[c] = k + 1; ++n; }
        }
        fclose(f);
        fprintf(stderr, "doom-meego: keymap %s: %d entries\n", paths[i], n);
        return;
    }
}
static int s_runOn = 0;
static void hkLog(int code, int value, const char *what)
{
    fprintf(stderr, "doom-meego: [hk] code %d val %d -> %s (menu=%d)\n", code, value, what, menuactive ? 1 : 0);
}
static void weaponKey(int dir, int p)
{
    static int w = 1, down = -1;
    if (p) {
        if (menuactive) return;
        w = ((w - 1 + dir + 7) % 7) + 1;   // weapons 1..7
        down = '0' + w; key(down, 1);
    } else if (down >= 0) { key(down, 0); down = -1; }
}
static void evdevKey(int code, int value)
{
    if (value == 2) return;
    if (code > 0 && code < 1024) {
        if (value && s_evDown[code]) return;   // already down: wait for release
        s_evDown[code] = value ? 1 : 0;
    }
    if (code != 330 && code != 325) s_evdevSeen = 1;
    if (!s_userMapLoaded) loadUserMap();
    if (code > 0 && code < 1024 && s_userMap[code]) { key(s_userMap[code] - 1, value ? 1 : 0); return; }
    if (code == 330 || code == 325) return;               // BTN_TOUCH / BTN_TOOL_FINGER
    int p = value ? 1 : 0;
    switch (code) {
    case 103: key(KEY_UPARROW, p); break;                 // KEY_UP
    case 108: key(KEY_DOWNARROW, p); break;               // KEY_DOWN
    case 105: key(KEY_LEFTARROW, p); break;               // KEY_LEFT
    case 106: key(KEY_RIGHTARROW, p); break;              // KEY_RIGHT
    case 28: case 96: case 352: case 207: case 353:       // ENTER/KPENTER/OK/PLAY/SELECT
        key(KEY_FIRE, p); key(KEY_ENTER, p); break;
    case 57: case 164: case 119: case 113: key(KEY_USE, p); break;   // SPACE/PLAYPAUSE/PAUSE/MUTE
    case 1: case 14: case 158: case 174:                  // ESC/BACKSPACE/BACK/EXIT
        key(KEY_ESCAPE, p);
        s_backDownAt = p ? (DG_GetTicksMs() | 1) : 0;
        break;
    case 102: case 172: if (p) meego_quit(); break;       // HOME/HOMEPAGE
    case 139: case 127: key(KEY_ENTER, p); break;         // MENU/COMPOSE
    case 163: case 115: key(KEY_UPARROW, p); break;       // NEXTSONG/VOLUMEUP
    case 165: case 114: key(KEY_DOWNARROW, p); break;     // PREVIOUSSONG/VOLUMEDOWN
    // LGE IVI MICOM hard keys: CCP jog tilt/press arrive as keypad codes (seen in doom_log)
    case 72: key(KEY_UPARROW, p); break;                  // KP8  jog up
    case 80: key(KEY_DOWNARROW, p); break;                // KP2  jog down
    case 75: key(KEY_LEFTARROW, p); break;                // KP4  jog left
    case 77: key(KEY_RIGHTARROW, p); break;               // KP6  jog right
    case 71: key(KEY_UPARROW, p); key(KEY_LEFTARROW, p); break;    // KP7
    case 73: key(KEY_UPARROW, p); key(KEY_RIGHTARROW, p); break;   // KP9
    case 79: key(KEY_DOWNARROW, p); key(KEY_LEFTARROW, p); break;  // KP1
    case 81: key(KEY_DOWNARROW, p); key(KEY_RIGHTARROW, p); break; // KP3
    case 76: case 82:                                     // KP5/KP0 jog press
        jogPress(p); break;
    // ---- LGE IVI MICOM Hard Keys (/dev/input/event1), codes confirmed by doom_log 12.10 17:03 ----
    case 36:                                              // BACK = Esc (menu/back), long = exit
        key(KEY_ESCAPE, p);
        s_backDownAt = p ? (DG_GetTicksMs() | 1) : 0;
        hkLog(code, value, "BACK -> Esc (long: exit)"); break;
    case 37:                                              // HOME = pause
        if (p && !menuactive) { key(KEY_PAUSE, 1); key(KEY_PAUSE, 0); }
        hkLog(code, value, menuactive ? "HOME -> pause (ignored in menu)" : "HOME -> pause"); break;
    case 33:                                              // MAP/VOICE = automap
        key(KEY_TAB, p); hkLog(code, value, "MAP -> automap (Tab)"); break;
    case 69:                                              // long MAP/VOICE: ignored (no double Tab)
        hkLog(code, value, "MAP long -> ignored"); break;
    case 38:                                              // MENU = next weapon
        weaponKey(+1, p); hkLog(code, value, "MENU -> next weapon"); break;
    case 35:                                              // PHONE = previous weapon
        weaponKey(-1, p); hkLog(code, value, "PHONE -> prev weapon"); break;
    case 34: case 104: {                                  // NAV = fire, SETUP = use; Enter in menu
        static int nk[2] = { -1, -1 };
        int slot = code == 34 ? 0 : 1;
        if (p) { nk[slot] = menuactive ? KEY_ENTER : (slot ? KEY_USE : KEY_FIRE); key(nk[slot], 1); }
        else if (nk[slot] >= 0) { key(nk[slot], 0); nk[slot] = -1; }
        hkLog(code, value, menuactive ? (slot ? "SETUP -> Enter" : "NAV -> Enter")
                                      : (slot ? "SETUP -> use" : "NAV -> fire (hold)"));
        break; }
    default:
        hkLog(code, value, "unknown -> ignored"); break;   // no catch-all any more
    }
}

static void handleLine(char *line)
{
    while (*line == ' ') ++line;
    if (line[0] == 'Q') { fprintf(stderr, "doom-meego: [stdin] Q on stdin\n"); meego_quit(); return; }
    char kind = 0;
    int a = 0, b = 0, c = 0;
    int n = sscanf(line, "%c %d %d %d", &kind, &a, &b, &c);
    if (kind == 'S' && n >= 2) {
        fprintf(stderr, "doom-meego: [stdin] S %d on stdin (%s)\n", a, a ? "show/resume" : "hide/pause");
        setHidden(a ? 0 : 1, a ? "show" : "hide");
        return;
    }
    if (kind == 'I' && n >= 2) {     // Updater: interrupt active (1) / over (0)
        fprintf(stderr, "doom-meego: [stdin] I %d (interrupt %s)\n", a, a ? "active" : "over");
        s_intr = a ? 1 : 0;
        if (!a && s_hardLost && !s_hidden) requestRaiseNow("interrupt over");
        return;
    }
    if (n < 3) return;
    if (kind == 'B') busEvent(a, b, n >= 4 ? c : 0);
    else if (kind == 'E') { if (!s_hidden) evdevKey(a, b); }
    else if (kind == 'R') { if (s_hidden) return; if (a == LX_REL_Y) turn(b); else walk(b); }
}

static void openDevices(void)
{
    for (int n = 0; n < 32 && s_ndev < MAXDEV; ++n) {
        char path[64], sys[96], name[128];
        snprintf(path, sizeof(path), "/dev/input/event%d", n);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        name[0] = 0;
        if (ioctl(fd, LX_EVIOCGNAME(sizeof(name)), name) < 0) {
            snprintf(sys, sizeof(sys), "/sys/class/input/event%d/device/name", n);
            FILE *f = fopen(sys, "r");
            if (f) { if (!fgets(name, sizeof(name), f)) name[0] = 0; fclose(f); }
        }
        name[sizeof(name) - 1] = 0;
        { char *nl = strchr(name, '\n'); if (nl) *nl = 0; }
        for (char *q = name; *q; ++q) *q = (char)tolower((unsigned char)*q);
        if (strstr(name, "mouse")) { close(fd); continue; }
        int i = s_ndev;
        snprintf(s_path[i], sizeof(s_path[i]), "%s", path);
        snprintf(s_name[i], sizeof(s_name[i]), "%s", name);
        s_lastErr[i] = 0;
        s_grabErr[i] = -1;
        if (strstr(name, "touch")) {
            struct lx_absinfo ai;
            int ax = 0x35, ay = 0x36;
            if (ioctl(fd, LX_EVIOCGABS(ax), &ai) < 0 || ai.maximum <= ai.minimum) { ax = 0; ay = 1; }
            if (ioctl(fd, LX_EVIOCGABS(ax), &ai) == 0 && ai.maximum > ai.minimum) { s_tMinX = ai.minimum; s_tMaxX = ai.maximum; }
            if (ioctl(fd, LX_EVIOCGABS(ay), &ai) == 0 && ai.maximum > ai.minimum) { s_tMinY = ai.minimum; s_tMaxY = ai.maximum; }
            s_fd[i] = fd; s_grabWanted[i] = 0; s_grabbed[i] = 0; s_isTouch[i] = 1;
            fprintf(stderr, "doom-meego: touch %s '%s' x %d..%d y %d..%d (no grab)\n", path, name, s_tMinX, s_tMaxX, s_tMinY, s_tMaxY);
            s_ndev++;
            continue;
        }
        int wantGrab = strstr(name, "jog") || strstr(name, "ccp") || strstr(name, "knob")
            || strstr(name, "dial") || strstr(name, "micom") || strstr(name, "key");
        s_fd[i] = fd;
        s_grabWanted[i] = wantGrab;
        s_grabbed[i] = 0;
        s_isTouch[i] = 0;
        fprintf(stderr, "doom-meego: input %s '%s'%s\n", path, name, wantGrab ? " (grab)" : "");
        s_ndev++;
    }
}

static void tryGrabs(int force)
{
    uint32_t now = DG_GetTicksMs();
    if (s_hidden || (!force && now < s_nextGrabTry)) return;
    s_nextGrabTry = now + 1000;
    int newly = 0;
    for (int i = 0; i < s_ndev; ++i) {
        if (!s_grabWanted[i] || s_grabbed[i]) continue;
        if (ioctl(s_fd[i], LX_EVIOCGRAB, 1) == 0) {
            s_grabbed[i] = 1;
            s_grabErr[i] = 0;
            ++newly;
            fprintf(stderr, "doom-meego: [grab] grabbed %s '%s' t=%u\n", s_path[i], s_name[i], rel());
        } else {
            int e = errno;
            if (e != s_grabErr[i])
                fprintf(stderr, "doom-meego: [grab] EVIOCGRAB %s '%s' failed: %s%s (retry every 1 s)\n", s_path[i], s_name[i],
                        errName(e), e == EBUSY ? " - another process holds the grab" : "");
            if (e != s_grabErr[i] && e == EBUSY) { s_grabErr[i] = e; meego_diag("grab EBUSY"); }
            s_grabErr[i] = e;
            if (e == ENODEV || e == EBADF) s_needReopen = 1;
        }
    }
    if (newly) meego_diag("grab");
}

// Touch space -> Doom window (visible panel). X range maps to 1280; Y uses the same
// scale as X range/1280 vs 1440-tall X screen, so raw y == X-screen y (top 720 visible).
static void touchFrame(void)
{
    if (s_tX < 0 || s_tY < 0) return;
    int sx = (s_tX - s_tMinX) * 1280 / (s_tMaxX - s_tMinX + 1);
    int sy = (s_tY - s_tMinY) * 1440 / (s_tMaxY - s_tMinY + 1);
    const char *ys = getenv("DOOM_TOUCH_YDIV");      // e.g. 2 if the panel is touch-scaled
    if (ys && atoi(ys) > 0) sy /= atoi(ys);
    if (s_tDown) {
        if (!s_tDownSent && s_logCount < 400) fprintf(stderr, "doom-meego: touch down raw %d,%d -> %d,%d\n", s_tX, s_tY, sx, sy);
        meego_touch(sx, sy, 1); s_tDownSent = 1;
    } else if (s_tDownSent) { meego_touch(sx, sy, 0); s_tDownSent = 0; }
}

static void pollDevices(void)
{
    struct lx_input_event ev;
    for (int i = 0; i < s_ndev; ++i) {
        ssize_t r;
        while ((r = read(s_fd[i], &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
            if (s_hidden) {
                // hidden (call/camera/overlay): no game input, but exit must still work
                if (!s_isTouch[i] && ev.type == LX_EV_KEY) {
                    logEv("hidden", ev.type, ev.code, ev.value);
                    if (ev.code == 1 || ev.code == 14 || ev.code == 158 || ev.code == 174 || ev.code == 16)
                        s_backDownAt = ev.value ? (DG_GetTicksMs() | 1) : 0;
                    else if ((ev.code == 102 || ev.code == 172 || ev.code == 18) && ev.value == 1) meego_quit();
                }
                continue;
            }
            if (s_isTouch[i]) {
                if (ev.type == LX_EV_ABS) {
                    if (ev.code == 0 || ev.code == 0x35) s_tX = ev.value;
                    else if (ev.code == 1 || ev.code == 0x36) s_tY = ev.value;
                    else if (ev.code == 0x39) s_tDown = ev.value >= 0;   // ABS_MT_TRACKING_ID
                    else if (ev.code == 0x30 || ev.code == 0x18) { if (ev.value == 0) s_tDown = 0; }
                } else if (ev.type == LX_EV_KEY && (ev.code == 330 || ev.code == 325)) {
                    s_tDown = ev.value ? 1 : 0;
                } else if (ev.type == LX_EV_SYN) touchFrame();
                continue;
            }
            if (ev.type == LX_EV_SYN) continue;
            logEv(i == 0 ? "dev0" : "dev", ev.type, ev.code, ev.value);
            if (ev.type == LX_EV_KEY) evdevKey(ev.code, ev.value);
            else if (ev.type == LX_EV_REL && ev.value != 0) {
                if (ev.code != 0 && ev.code != LX_REL_Y && ev.code != 6 && ev.code != 7 && ev.code != 8)
                    fprintf(stderr, "doom-meego: unmapped jog REL code %d val %d -> walk\n", ev.code, ev.value);
                if (ev.code == LX_REL_Y) turn(ev.value);
                else walk(ev.value);
            }
        }
        if (r < 0 && errno != EAGAIN && errno != EINTR) {
            int e = errno;
            if (e != s_lastErr[i]) {
                fprintf(stderr, "doom-meego: [grab] read %s '%s' error %s (device re-opened by micom?)\n", s_path[i], s_name[i], errName(e));
                s_lastErr[i] = e;
                meego_diag("read error");
            }
            if (e == ENODEV || e == EBADF || e == EIO) s_needReopen = 1;
        } else if (r == 0) {
            if (s_lastErr[i] != ENODEV) {
                fprintf(stderr, "doom-meego: [grab] read %s EOF (device gone)\n", s_path[i]);
                s_lastErr[i] = ENODEV;
            }
            s_needReopen = 1;
        }
    }
}


/* ---- paths next to the executable (flat card root: AppUpgrade + freedoom1.wad) ---- */
static char s_exeDir[512];
static int s_exeDirReady = 0;
extern "C" const char *meego_exe_dir(void)
{
    if (s_exeDirReady) return s_exeDir;
    s_exeDir[0] = '.'; s_exeDir[1] = 0;
    char link[512];
    ssize_t n = readlink("/proc/self/exe", link, sizeof(link) - 1);
    if (n > 0) {
        link[n] = 0;
        char *slash = strrchr(link, '/');
        if (slash) { *slash = 0; snprintf(s_exeDir, sizeof(s_exeDir), "%s", link); }
    }
    s_exeDirReady = 1;
    return s_exeDir;
}

extern "C" void meego_setup_log(void)
{
    char path[600];
    snprintf(path, sizeof(path), "%s/doom_log.txt", meego_exe_dir());
    FILE *f = freopen(path, "a", stderr);
    if (!f) fprintf(stdout, "doom-meego: cannot open %s for log\n", path);
    else { setvbuf(stderr, 0, _IONBF, 0); fprintf(stderr, "\n=== doom-meego start pid %d exe_dir=%s ===\n", (int)getpid(), meego_exe_dir()); }
}

static int file_readable(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static char *dup_path(const char *p)
{
    size_t n = strlen(p) + 1;
    char *r = (char *)malloc(n);
    if (r) memcpy(r, p, n);
    return r;
}

static char *first_wad_in_dir(const char *dir)
{
    char path[600];
    static const char *prefer[] = {
        "freedoom1.wad", "FREEDOOM1.WAD", "doom.wad", "DOOM.WAD",
        "doom1.wad", "DOOM1.WAD", "freedoom2.wad", "doom2.wad", 0
    };
    for (int i = 0; prefer[i]; ++i) {
        snprintf(path, sizeof(path), "%s/%s", dir, prefer[i]);
        if (file_readable(path)) return dup_path(path);
    }
    /* any *.wad / *.WAD */
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    char *found = 0;
    while ((e = readdir(d)) != 0) {
        size_t n = strlen(e->d_name);
        if (n < 5) continue;
        const char *ext = e->d_name + n - 4;
        if (strcasecmp(ext, ".wad") != 0) continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (file_readable(path)) { found = dup_path(path); break; }
    }
    closedir(d);
    return found;
}

static void scan_mount_roots(char **out)
{
    static const char *roots[] = { "/media", "/mnt", 0 };
    for (int r = 0; roots[r] && !*out; ++r) {
        DIR *d = opendir(roots[r]);
        if (!d) continue;
        struct dirent *e;
        while ((e = readdir(d)) != 0 && !*out) {
            if (e->d_name[0] == '.') continue;
            char sub[600];
            snprintf(sub, sizeof(sub), "%s/%s", roots[r], e->d_name);
            *out = first_wad_in_dir(sub);
        }
        closedir(d);
    }
}

extern "C" void meego_show_fatal(const char *msg_utf8); /* in doomgeneric_meego.cpp */

extern "C" char *meego_find_iwad(void)
{
    char *wad = first_wad_in_dir(meego_exe_dir());
    if (!wad) {
        /* also try cwd (sometimes the unit starts with cwd = card root) */
        wad = first_wad_in_dir(".");
    }

    /* Legacy layout: <card>/doom-meego/freedoom1.wad (updater-era card) */
    if (!wad) {
        char path[600];
        snprintf(path, sizeof(path), "%s/doom-meego", meego_exe_dir());
        wad = first_wad_in_dir(path);
        if (!wad) wad = first_wad_in_dir("./doom-meego");
    }
    if (!wad) {
        static const char *roots[] = { "/media", "/mnt", 0 };
        for (int r = 0; roots[r] && !wad; ++r) {
            DIR *d = opendir(roots[r]);
            if (!d) continue;
            struct dirent *e;
            while ((e = readdir(d)) != 0 && !wad) {
                if (e->d_name[0] == '.') continue;
                char sub[600];
                snprintf(sub, sizeof(sub), "%s/%s/doom-meego", roots[r], e->d_name);
                wad = first_wad_in_dir(sub);
            }
            closedir(d);
        }
    }
    if (!wad) scan_mount_roots(&wad);
    if (wad) {
        fprintf(stderr, "doom-meego: IWAD %s\n", wad);
        return wad;
    }
    fprintf(stderr, "doom-meego: %s\n", "Не найден WAD-файл. Скопируйте freedoom1.wad в корень флешки (рядом с AppUpgrade).");
    meego_show_fatal("Не найден WAD-файл. Скопируйте freedoom1.wad в корень флешки (рядом с AppUpgrade).");
    return 0; /* not reached */
}

extern "C" void meego_quit(void)
{
    fprintf(stderr, "doom-meego: quit requested t=%u\n", rel());
    releaseGrabs();
    I_AlsaShutdown();
    exit(0);
}

extern "C" void meego_focusEvent(const char *what, int lost)
{
    fprintf(stderr, "doom-meego: [focus] %s t=%u hidden=%d\n", what, rel(), s_hidden);
    if (lost) meego_resetKeys(what);
    else s_nextGrabTry = 0;     // focus back: retry grabs right away
    if (lost != s_xLost) { s_xLost = lost; I_AlsaFocusPause(lost, what); }
    if (!s_hidden) {
        if (strstr(what, "obscured") || strstr(what, "UnmapNotify")) {
            if (!s_hardLost) { s_hardLost = 1; s_hardLostAt = DG_GetTicksMs(); s_nextRaise = 0;
                fprintf(stderr, "doom-meego: [ui] lost front (%s)\n", what); }
        } else if (strstr(what, "visible again") || strstr(what, "MapNotify")) {
            if (s_hardLost) fprintf(stderr, "doom-meego: [ui] front again (%s)\n", what);
            s_hardLost = 0;
        }
    }
    if (DG_GetTicksMs() - s_lastDiagAt > 700) meego_diag(what);
}

static uint32_t s_reopenAt = 0;
extern "C" void meego_input_init(void)
{
    s_startMs = DG_GetTicksMs();
    s_onHU = access("/app/bin/UIStateManagerDaemon", F_OK) == 0;
    fprintf(stderr, "doom-meego: head unit %s (auto re-raise %s)\n", s_onHU ? "detected" : "not detected", s_onHU ? "on" : "off");
    atexit(atexitReleaseGrabs);
    signal(SIGTERM, sigReleaseGrabs);
    signal(SIGINT, sigReleaseGrabs);
    signal(SIGHUP, sigReleaseGrabs);
    int fl = fcntl(0, F_GETFL);
    if (fl >= 0 && !isatty(0) && fcntl(0, F_SETFL, fl | O_NONBLOCK) == 0)
        s_stdinOk = 1;
    fprintf(stderr, "doom-meego: stdin %s, pid %d\n", s_stdinOk ? "pipe ok" : "not used", (int)getpid());
    openDevices();
    meego_diag("start (before grab)");
    tryGrabs(1);
}

static void requestRaiseNow(const char *why)
{
    if (!s_onHU) return;
    fprintf(stderr, "doom-meego: [ui] raise requested: %s\n", why);
    s_hardLostAt = DG_GetTicksMs() - s_raiseBackoff;
    s_nextRaise = 0;
}

/* Lost the screen without being told (no S 0): pause + mute, then try to come back.
 * Known interrupts (S 0 hidden / I 1) are never fought; "S 1" / "I 0" raise at once. */
static void focusWatch(uint32_t now)
{
    if (s_hidden) { s_hardLost = 0; return; }
    if (s_hardLost) {
        if (!s_focusPaused && now - s_hardLostAt >= 400) {
            meego_resetKeys("window lost");
            if (!paused && !menuactive) { sendpause = 1; s_focusPaused = 1; }
            else s_focusPaused = 2;      // nothing to undo later
            I_AlsaFocusPause(1, "window lost (auto)");
            fprintf(stderr, "doom-meego: [ui] paused+muted: window lost t=%u\n", rel());
        }
        if (s_onHU && !s_intr && now - s_hardLostAt >= s_raiseBackoff && now >= s_nextRaise) {
            fprintf(stderr, "doom-meego: [ui] want front (auto re-raise, backoff %u ms) t=%u\n", s_raiseBackoff, rel());
            meego_setVisible(1);
            s_nextRaise = now + s_raiseBackoff;
            if (s_raiseBackoff < 60000) s_raiseBackoff *= 2;
        }
    } else if (s_focusPaused) {
        if (s_focusPaused == 1 && paused) sendpause = 1;
        s_focusPaused = 0;
        s_raiseBackoff = 3000;
        s_xLost = 0;
        I_AlsaFocusPause(0, "window back (auto)");
        s_nextGrabTry = 0;
        fprintf(stderr, "doom-meego: [ui] resumed: window back t=%u\n", rel());
    } else s_raiseBackoff = 3000;
}

extern "C" void meego_input_poll(void)
{
    uint32_t now = DG_GetTicksMs();
    releaseHolds(0);
    jogPressPoll();
    if (s_backDownAt && now - s_backDownAt >= 1500) {
        fprintf(stderr, "doom-meego: long BACK (evdev)\n");
        meego_quit();
    }
    if (s_needReopen && now - s_reopenAt > 1000) { s_reopenAt = now; reopenDevices("stale fd (ENODEV/EOF)"); }
    tryGrabs(0);
    if (!s_hidden) pollDevices();   // no evdev polling while hidden/minimized (grab is released too)
    focusWatch(now);
    if (!s_stdinOk) return;
    for (;;) {
        ssize_t r = read(0, s_buf + s_len, sizeof(s_buf) - 1 - s_len);
        if (r == 0) { s_stdinOk = 0; fprintf(stderr, "doom-meego: [stdin] closed (stdin closed)\n"); return; }
        if (r < 0) return;
        s_len += (int)r;
        s_buf[s_len] = 0;
        char *start = s_buf, *nl;
        while ((nl = strchr(start, '\n')) != 0) {
            *nl = 0;
            handleLine(start);
            start = nl + 1;
        }
        s_len = (int)strlen(start);
        memmove(s_buf, start, s_len + 1);
        if (s_len >= (int)sizeof(s_buf) - 1) s_len = 0;
    }
}
