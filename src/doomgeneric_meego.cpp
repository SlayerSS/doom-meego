#include <sys/stat.h>
#include <unistd.h>
// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 SlayerSS
// Part of doom-meego (doomgeneric port for Hyundai Genesis DH / LG DHAVN MeeGo head units).
// doomgeneric platform layer for Hyundai/Genesis MeeGo head units (Qt 4.8 / Qt 5).
// DG_Init, DG_DrawFrame, DG_SleepMs, DG_GetTicksMs, DG_GetKey, DG_SetWindowTitle.
//
// Screen: the DHAVN X screen / touch space is reported as 1280x1440 (micom.ts_height),
// but the visible panel is 1280x720. We never use showFullScreen(): the window is
// placed at 0,0 with the visible panel size and the 640x400 frame is letterboxed.
// Touch: tap zones (see touchZone()) + hold top-left corner 2 s = quit.

#include <QApplication>
#include <QLabel>
#include <QElapsedTimer>
#include <QWidget>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QPolygon>
#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QDesktopWidget>
#include <QString>
#include <QEvent>
#ifdef Q_WS_X11
#include <QX11Info>
#include <dlfcn.h>
#endif

#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
#include "doomkeys.h"
#include "doomgeneric.h"
}
#include "meego_input.h"

// ---------------- key queue ----------------
#define KEYQUEUE_SIZE 64
static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex = 0;
static unsigned char s_keyDown[256];   // what Doom currently believes is held

extern "C" void meego_addKeyToQueue(int pressed, unsigned char doomKey)
{
    s_keyDown[doomKey] = pressed ? 1 : 0;
    unsigned int next = (s_KeyQueueWriteIndex + 1) % KEYQUEUE_SIZE;
    if (next == s_KeyQueueReadIndex)
        return;                                  // full: drop rather than corrupt
    s_KeyQueue[s_KeyQueueWriteIndex] = (unsigned short)((pressed << 8) | doomKey);
    s_KeyQueueWriteIndex = next;
}

extern "C" int meego_releaseAllKeys(void)
{
    int n = 0;
    for (int k = 0; k < 256; ++k)
        if (s_keyDown[k]) { meego_addKeyToQueue(0, (unsigned char)k); ++n; }
    return n;
}

static int controlToDoomKey(HuControl c)
{
    switch (c) {
    case HU_WHEEL_LEFT:       return KEY_LEFTARROW;
    case HU_WHEEL_RIGHT:      return KEY_RIGHTARROW;
    case HU_SEEK_DOWN:        return KEY_UPARROW;
    case HU_SEEK_UP:          return KEY_DOWNARROW;
    case HU_MUTE:             return KEY_USE;
    case HU_END_CALL:         return KEY_FIRE;
    case HU_VOLUME_KNOB_PUSH: return KEY_ENTER;
    case HU_TUNE_KNOB_PUSH:   return KEY_ESCAPE;
    default:                  return -1;
    }
}

extern "C" void meego_controlEvent(HuControl c, int pressed)
{
    int k = controlToDoomKey(c);
    if (k >= 0) meego_addKeyToQueue(pressed, (unsigned char)k);
}

static int qtKeyToDoomKey(int key)
{
    switch (key) {
    case Qt::Key_Left:   return KEY_LEFTARROW;
    case Qt::Key_Right:  return KEY_RIGHTARROW;
    case Qt::Key_Up:     return KEY_UPARROW;
    case Qt::Key_Down:   return KEY_DOWNARROW;
    case Qt::Key_Control:return KEY_FIRE;
    case Qt::Key_Space:  return KEY_USE;
    case Qt::Key_Return:
    case Qt::Key_Enter:  return KEY_ENTER;
    case Qt::Key_Escape:
    case Qt::Key_Back:   return KEY_ESCAPE;
    case Qt::Key_Shift:  return KEY_RSHIFT;
    case Qt::Key_Alt:    return KEY_LALT;
    case Qt::Key_Tab:    return KEY_TAB;
    case Qt::Key_Backspace: return KEY_BACKSPACE;
    default:
        if (key >= Qt::Key_A && key <= Qt::Key_Z) return 'a' + (key - Qt::Key_A);
        if (key >= Qt::Key_0 && key <= Qt::Key_9) return '0' + (key - Qt::Key_0);
        return -1;
    }
}

extern "C" uint32_t DG_GetTicksMs();

// ---------------- touch zones: only in the black side bars (game area 960x720 is inert) ----------------
// Left bar  (x 0..159), top to bottom, 5 equal rows:  ESC (hold 2 s = QUIT) | FORWARD | turn LEFT | turn RIGHT | BACK
// Right bar (x 1120..1279), top to bottom, 4 equal rows: MAP | ENTER | USE | FIRE
enum { Z_NONE = -1, Z_ESC, Z_ENTER, Z_MAP, Z_LEFT, Z_RIGHT, Z_FWD, Z_BACK, Z_USE, Z_FIRE, Z_COUNT };
static const int kZoneKey[Z_COUNT] = {
    KEY_ESCAPE, KEY_ENTER, KEY_TAB, KEY_LEFTARROW, KEY_RIGHTARROW,
    KEY_UPARROW, KEY_DOWNARROW, KEY_USE, KEY_FIRE
};
static const char *kZoneName[Z_COUNT] = {   // UTF-8, shown under the icon in the side bars
    "Меню", "Ввод", "Карта", "Влево", "Вправо", "Вперёд", "Назад", "Действие", "Огонь"
};

#define GAME_W 960   // 320 x3
#define GAME_H 720   // 200 x3.6 (rows repeated 4,4,3,4,3 -> exactly 18 rows per 5 source rows)

class DoomWidget : public QWidget
{
public:
    QImage image;
    int zone;
    uint32_t pressedAt;
    uint32_t shownAt;
    QPixmap bars;        // cached side bars (icons + labels), rebuilt only on resize
    DoomWidget() : image(GAME_W, GAME_H, QImage::Format_ARGB32),
                   zone(Z_NONE), pressedAt(0), shownAt(0)
    {
        image.fill(0xFF000000u);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_OpaquePaintEvent);
        /* No white X background before the first paint: black palette, no system fill. */
        setAttribute(Qt::WA_NoSystemBackground);
        setAutoFillBackground(false);
        QPalette pal = palette();
        pal.setColor(QPalette::Window, Qt::black);
        pal.setColor(QPalette::Base, Qt::black);
        setPalette(pal);
        setAttribute(Qt::WA_AcceptTouchEvents, false);
    }
    QRect target() const
    {
        int w = width(), h = height();
        if (w >= GAME_W && h >= GAME_H) return QRect((w - GAME_W) / 2, (h - GAME_H) / 2, GAME_W, GAME_H);
        int tw = w, th = w * GAME_H / GAME_W;                 // smaller window: scaled fallback
        if (th > h) { th = h; tw = h * GAME_W / GAME_H; }
        return QRect((w - tw) / 2, (h - th) / 2, tw, th);
    }
    QRect zoneRect(int z) const
    {
        QRect t = target();
        int h = height(), lw = t.left(), rx = t.right() + 1, rw = width() - rx;
        if (lw <= 0 || rw <= 0) return QRect();               // no side bars -> no touch zones
        int l = -1, r = -1;
        switch (z) {
        case Z_ESC:   l = 0; break;
        case Z_FWD:   l = 1; break;
        case Z_LEFT:  l = 2; break;
        case Z_RIGHT: l = 3; break;
        case Z_BACK:  l = 4; break;
        case Z_MAP:   r = 0; break;
        case Z_ENTER: r = 1; break;
        case Z_USE:   r = 2; break;
        case Z_FIRE:  r = 3; break;
        default: return QRect();
        }
        if (l >= 0) return QRect(0, h * l / 5, lw, h * (l + 1) / 5 - h * l / 5);
        return QRect(rx, h * r / 4, rw, h * (r + 1) / 4 - h * r / 4);
    }
    int zoneAt(const QPoint &p) const
    {
        for (int z = 0; z < Z_COUNT; ++z)
            if (zoneRect(z).contains(p)) return z;
        return Z_NONE;
    }
    void setZone(int z)
    {
        if (z == zone) return;
        if (zone != Z_NONE) meego_addKeyToQueue(0, (unsigned char)kZoneKey[zone]);
        zone = z;
        pressedAt = DG_GetTicksMs();
        if (zone != Z_NONE) meego_addKeyToQueue(1, (unsigned char)kZoneKey[zone]);
    }
    void checkHold()
    {
        if (zone == Z_ESC && DG_GetTicksMs() - pressedAt >= 2000) {
            fprintf(stderr, "doom-meego: quit by touch hold (top-left)\n");
            meego_quit();
        }
    }
protected:
    // Vector icon for zone z, centred in box c (no font needed for icons).
    static void drawIcon(QPainter &p, int z, const QRect &c)
    {
        int cx = c.center().x(), cy = c.center().y(), r = qMin(c.width(), c.height()) / 2;
        QPolygon tri;
        switch (z) {
        case Z_FWD:   tri << QPoint(cx, cy - r) << QPoint(cx + r, cy + r / 2) << QPoint(cx - r, cy + r / 2); break;
        case Z_BACK:  tri << QPoint(cx, cy + r) << QPoint(cx + r, cy - r / 2) << QPoint(cx - r, cy - r / 2); break;
        case Z_LEFT:  tri << QPoint(cx - r, cy) << QPoint(cx + r / 2, cy - r) << QPoint(cx + r / 2, cy + r); break;
        case Z_RIGHT: tri << QPoint(cx + r, cy) << QPoint(cx - r / 2, cy - r) << QPoint(cx - r / 2, cy + r); break;
        case Z_ESC:   // X
            p.drawLine(cx - r, cy - r, cx + r, cy + r); p.drawLine(cx - r, cy + r, cx + r, cy - r); return;
        case Z_MAP: { // folded map
            int w = r * 2, h = r * 3 / 2, x = cx - r, y = cy - h / 2;
            QPolygon m; m << QPoint(x, y) << QPoint(x + w / 3, y + h / 6) << QPoint(x + 2 * w / 3, y)
                          << QPoint(x + w, y + h / 6) << QPoint(x + w, y + h) << QPoint(x + 2 * w / 3, y + h - h / 6)
                          << QPoint(x + w / 3, y + h) << QPoint(x, y + h - h / 6);
            p.drawPolygon(m);
            p.drawLine(x + w / 3, y + h / 6, x + w / 3, y + h); p.drawLine(x + 2 * w / 3, y, x + 2 * w / 3, y + h - h / 6);
            return; }
        case Z_ENTER: { // return arrow
            p.drawLine(cx + r, cy - r, cx + r, cy); p.drawLine(cx + r, cy, cx - r / 2, cy);
            QPolygon h; h << QPoint(cx - r, cy) << QPoint(cx - r / 2, cy - r / 2) << QPoint(cx - r / 2, cy + r / 2);
            p.drawPolygon(h); return; }
        case Z_USE:   // hand-ish: ring with dot
            p.drawEllipse(QPoint(cx, cy), r, r); p.drawEllipse(QPoint(cx, cy), r / 4, r / 4); return;
        case Z_FIRE:  // crosshair
            p.drawEllipse(QPoint(cx, cy), r * 2 / 3, r * 2 / 3);
            p.drawLine(cx - r, cy, cx + r, cy); p.drawLine(cx, cy - r, cx, cy + r); return;
        default: return;
        }
        p.drawPolygon(tri);
    }
    void buildBars()
    {
        bars = QPixmap(size());
        bars.fill(Qt::black);
        QPainter p(&bars);
        p.setRenderHint(QPainter::Antialiasing, true);
        QFont f = font(); f.setPixelSize(20); p.setFont(f);
        const QColor sep(60, 60, 60), fg(120, 120, 120);
        for (int z = 0; z < Z_COUNT; ++z) {
            QRect r = zoneRect(z);
            if (r.isEmpty()) continue;
            p.setPen(sep); p.setBrush(Qt::NoBrush);
            p.drawLine(r.left() + 8, r.bottom(), r.right() - 8, r.bottom());          // thin separator
            QRect icon(r.center().x() - 26, r.top() + r.height() / 2 - 44, 52, 52);
            p.setPen(QPen(fg, 3)); p.setBrush(z == Z_FWD || z == Z_BACK || z == Z_LEFT || z == Z_RIGHT || z == Z_ENTER ? QBrush(fg) : Qt::NoBrush);
            drawIcon(p, z, icon);
            p.setPen(fg);
            // Zone 160x144: icon y+28..y+79, text area y+85..y+135 (50 px).
            // Name: 20 px font (~24 px line); Esc adds "держать: выход" at 14 px (~17 px) -> ~41 px, fits.
            QRect tr(r.left() + 2, icon.bottom() + 6, r.width() - 4, r.bottom() - icon.bottom() - 8);
            p.drawText(tr, Qt::AlignHCenter | Qt::AlignTop | Qt::TextSingleLine, QString::fromUtf8(kZoneName[z]));
            if (z == Z_ESC) {
                QFont sf = f; sf.setPixelSize(14); p.setFont(sf);
                QRect tr2 = tr.adjusted(0, QFontMetrics(f).height() + 1, 0, 0);
                p.drawText(tr2, Qt::AlignHCenter | Qt::AlignTop | Qt::TextSingleLine, QString::fromUtf8("держать: выход"));
                p.setFont(f);
            }
        }
        // vertical dark-gray edge between bars and game area
        QRect t = target();
        p.setPen(sep);
        if (t.left() > 0) p.drawLine(t.left() - 1, 0, t.left() - 1, height() - 1);
        if (t.right() + 1 < width()) p.drawLine(t.right() + 1, 0, t.right() + 1, height() - 1);
    }
    void paintEvent(QPaintEvent *)
    {
        QPainter p(this);
        QRect t = target();
        if (bars.size() != size()) buildBars();
        // blit only the bars; the game image covers the middle
        if (t.left() > 0) p.drawPixmap(QRect(0, 0, t.left(), height()), bars, QRect(0, 0, t.left(), height()));
        if (t.right() + 1 < width()) {
            QRect rr(t.right() + 1, 0, width() - t.right() - 1, height());
            p.drawPixmap(rr, bars, rr);
        }
        if (t.top() > 0 || t.bottom() + 1 < height()) {          // letterbox rows (only if window != 720 high)
            p.fillRect(QRect(t.left(), 0, t.width(), t.top()), Qt::black);
            p.fillRect(QRect(t.left(), t.bottom() + 1, t.width(), height() - t.bottom() - 1), Qt::black);
        }
        if (t.size() == image.size()) p.drawImage(t.topLeft(), image);   // 1:1, no Qt scaling
        else p.drawImage(t, image);
        if (zone != Z_NONE)                                       // press highlight (inside the bar only)
            p.fillRect(zoneRect(zone), QColor(255, 255, 255, 40));
    }
    bool event(QEvent *e)
    {
        const char *what = 0;
        int lost = 0;
        switch (e->type()) {
        case QEvent::FocusIn:          what = "Qt FocusIn"; break;
        case QEvent::FocusOut:         what = "Qt FocusOut"; lost = 1; break;
        case QEvent::WindowActivate:   what = "Qt WindowActivate"; break;
        case QEvent::WindowDeactivate: what = "Qt WindowDeactivate"; lost = 1; break;
        case QEvent::Show:             what = "Qt Show"; break;
        case QEvent::Hide:             what = "Qt Hide"; lost = 1; break;
        case QEvent::WindowStateChange: what = "Qt WindowStateChange"; break;
        default: break;
        }
        if (what) {
            fprintf(stderr, "doom-meego: [focus] %s visible=%d active=%d minimized=%d appActive=%p self=%p\n", what,
                    isVisible() ? 1 : 0, isActiveWindow() ? 1 : 0, isMinimized() ? 1 : 0,
                    (void *)QApplication::activeWindow(), (void *)this);
            if (zone != Z_NONE && lost) setZone(Z_NONE);
            meego_focusEvent(what, lost);
        }
        return QWidget::event(e);
    }
    void mousePressEvent(QMouseEvent *e);
    void mouseMoveEvent(QMouseEvent *e);
    void mouseReleaseEvent(QMouseEvent *);
    void keyPressEvent(QKeyEvent *e)
    {
        if (e->isAutoRepeat()) return;
        int k = qtKeyToDoomKey(e->key());
        if (k >= 0) meego_addKeyToQueue(1, (unsigned char)k);
    }
    void closeEvent(QCloseEvent *e)     // window manager close (desktop run): really quit
    {
        e->accept();
        fprintf(stderr, "doom-meego: window closed by the window manager\n");
        meego_quit();
    }
    void keyReleaseEvent(QKeyEvent *e)
    {
        if (e->isAutoRepeat()) return;
        int k = qtKeyToDoomKey(e->key());
        if (k >= 0) meego_addKeyToQueue(0, (unsigned char)k);
    }
};

// ---------------- raw X11 focus/visibility log (Qt4 X11 only; libX11 via dlopen) -------------
static unsigned long s_activeWin = (unsigned long)-1;
static unsigned long s_Widget_winId();
#ifdef Q_WS_X11
struct lx_XAny { int type; unsigned long serial; int send_event; void *display; unsigned long window; };
struct lx_XFocus { int type; unsigned long serial; int send_event; void *display; unsigned long window; int mode; int detail; };
struct lx_XVis { int type; unsigned long serial; int send_event; void *display; unsigned long window; int state; };
struct lx_XProp { int type; unsigned long serial; int send_event; void *display; unsigned long window; unsigned long atom; unsigned long time; int state; };
typedef unsigned long (*fnInternAtom)(Display *, const char *, int);
typedef int (*fnGetProp)(Display *, unsigned long, unsigned long, long, long, int, unsigned long,
                         unsigned long *, int *, unsigned long *, unsigned long *, unsigned char **);
typedef int (*fnFree)(void *);
static fnInternAtom pInternAtom = 0;
static fnGetProp pGetProp = 0;
static fnFree pFree = 0;
static unsigned long aActive = 0, aPid = 0, aName = 0, aUtf8 = 0;
static int s_xInit = 0;
static void xInit()
{
    if (s_xInit) return;
    s_xInit = 1;
    void *h = dlopen("libX11.so.6", RTLD_LAZY);
    if (!h) { fprintf(stderr, "doom-meego: [x11] libX11 not loadable: %s\n", dlerror()); return; }
    pInternAtom = (fnInternAtom)dlsym(h, "XInternAtom");
    pGetProp = (fnGetProp)dlsym(h, "XGetWindowProperty");
    pFree = (fnFree)dlsym(h, "XFree");
    Display *d = QX11Info::display();
    if (!pInternAtom || !pGetProp || !pFree || !d) { pGetProp = 0; return; }
    aActive = pInternAtom(d, "_NET_ACTIVE_WINDOW", 0);
    aPid = pInternAtom(d, "_NET_WM_PID", 0);
    aName = pInternAtom(d, "_NET_WM_NAME", 0);
    aUtf8 = pInternAtom(d, "UTF8_STRING", 0);
}
static int xProp(unsigned long w, unsigned long atom, unsigned long type, unsigned long *val, char *str, int strSize)
{
    if (!pGetProp) return 0;
    unsigned long at = 0, n = 0, after = 0; int fmt = 0; unsigned char *data = 0;
    if (pGetProp(QX11Info::display(), w, atom, 0, 64, 0, type, &at, &fmt, &n, &after, &data) != 0 || !data) return 0;
    int ok = 0;
    if (val && n >= 1 && fmt == 32) { *val = *(unsigned long *)data; ok = 1; }
    if (str && n > 0 && fmt == 8) { int k = (int)n < strSize - 1 ? (int)n : strSize - 1; memcpy(str, data, k); str[k] = 0; ok = 1; }
    pFree(data);
    return ok;
}
// "_NET_ACTIVE_WINDOW = 0x... pid N (comm) 'name' [ours]"
static void xActiveOwner(char *out, int size, unsigned long *winOut)
{
    xInit();
    unsigned long w = 0;
    *winOut = (unsigned long)-2;
    if (!pGetProp) { snprintf(out, size, "n/a (no libX11)"); return; }
    if (!xProp(QX11Info::appRootWindow(), aActive, 33 /*XA_WINDOW*/, &w, 0, 0)) { snprintf(out, size, "n/a (WM does not set _NET_ACTIVE_WINDOW)"); return; }
    *winOut = w;
    unsigned long pid = 0; char name[96] = "", comm[64] = "?";
    xProp(w, aPid, 6 /*XA_CARDINAL*/, &pid, 0, 0);
    if (!xProp(w, aName, aUtf8, 0, name, sizeof(name))) xProp(w, 39 /*WM_NAME*/, 31 /*XA_STRING*/, 0, name, sizeof(name));
    if (pid) {
        char p[48]; snprintf(p, sizeof(p), "/proc/%lu/comm", pid);
        FILE *f = fopen(p, "r");
        if (f) { if (fgets(comm, sizeof(comm), f)) { char *nl = strchr(comm, '\n'); if (nl) *nl = 0; } fclose(f); }
    }
    snprintf(out, size, "0x%lx pid %lu (%s) '%s'%s", w, pid, comm, name, (s_Widget_winId() == w) ? " [Doom = ours]" : "");
}
#endif

static QApplication *s_App = 0;
static int s_evTouch = 0;   // evdev touch seen -> ignore Qt mouse (avoid double input)
static DoomWidget *s_Widget = 0;
static unsigned long s_Widget_winId() { return s_Widget ? (unsigned long)s_Widget->winId() : 0; }

extern "C" void meego_x11_report(const char *why)
{
    char buf[256];
    unsigned long w = 0;
#ifdef Q_WS_X11
    xActiveOwner(buf, sizeof(buf), &w);
#else
    snprintf(buf, sizeof(buf), "n/a (not X11)");
#endif
    s_activeWin = w;
    fprintf(stderr, "doom-meego: [x11] %s: doom win=0x%lx visible=%d active=%d qtActive=%p; _NET_ACTIVE_WINDOW=%s\n", why,
            s_Widget_winId(), s_Widget && s_Widget->isVisible() ? 1 : 0, s_Widget && s_Widget->isActiveWindow() ? 1 : 0,
            (void *)QApplication::activeWindow(), buf);
}

// poll _NET_ACTIVE_WINDOW (cheap) and log when its owner changes
static void x11Poll()
{
#ifdef Q_WS_X11
    static uint32_t next = 0;
    uint32_t now = DG_GetTicksMs();
    if (now < next) return;
    next = now + 1000;
    char buf[256]; unsigned long w = 0;
    xActiveOwner(buf, sizeof(buf), &w);
    if (w != s_activeWin) {
        fprintf(stderr, "doom-meego: [x11] active window changed -> %s\n", buf);
        s_activeWin = w;
        if (w && w != (unsigned long)-1 && s_Widget) {   // only if the WM sets _NET_ACTIVE_WINDOW
            if (w == s_Widget_winId()) meego_focusEvent("_NET_ACTIVE_WINDOW = doom", 0);
            else meego_focusEvent("_NET_ACTIVE_WINDOW = other app", 1);
        }
    }
#endif
}

#ifdef Q_WS_X11
class DoomApp : public QApplication
{
public:
    DoomApp(int &argc, char **argv) : QApplication(argc, argv), logged(0) {}
    int logged;
    bool x11EventFilter(XEvent *xe)
    {
        const lx_XAny *a = (const lx_XAny *)xe;
        const char *nm = 0; char extra[64] = "";
        switch (a->type) {
        case 9: case 10: {
            const lx_XFocus *f = (const lx_XFocus *)xe;
            nm = a->type == 9 ? "X FocusIn" : "X FocusOut";
            snprintf(extra, sizeof(extra), " mode=%d detail=%d", f->mode, f->detail);
            break; }
        case 18: nm = "X UnmapNotify"; break;
        case 19: nm = "X MapNotify"; break;
        case 15: {
            const lx_XVis *v = (const lx_XVis *)xe;
            nm = "X VisibilityNotify";
            snprintf(extra, sizeof(extra), " state=%d(%s)", v->state, v->state == 0 ? "unobscured" : v->state == 1 ? "partially obscured" : "fully obscured");
            break; }
        case 28: {
            const lx_XProp *pr = (const lx_XProp *)xe;
            xInit();
            if (aActive && pr->atom == aActive) { meego_x11_report("X PropertyNotify _NET_ACTIVE_WINDOW"); }
            break; }
        default: break;
        }
        if (nm && logged < 600) {
            ++logged;
            fprintf(stderr, "doom-meego: [x11] %s win=0x%lx%s%s\n", nm, a->window, extra,
                    a->window == s_Widget_winId() ? " (doom)" : "");
        }
        if (nm) {   // focus/visibility handling is NOT limited by the log counter
            if (a->type == 10 && (((const lx_XFocus *)xe)->detail <= 4)) meego_focusEvent("X FocusOut", 1);
            else if (a->type == 9 && (((const lx_XFocus *)xe)->detail <= 4)) meego_focusEvent("X FocusIn", 0);
            else if (a->type == 18 && a->window == s_Widget_winId()) meego_focusEvent("X UnmapNotify (doom)", 1);
            else if (a->type == 19 && a->window == s_Widget_winId()) meego_focusEvent("X MapNotify (doom)", 0);
            else if (a->type == 15 && ((const lx_XVis *)xe)->state == 2) meego_focusEvent("X fully obscured", 1);
            else if (a->type == 15) meego_focusEvent("X visible again", 0);
        }
        return QApplication::x11EventFilter(xe);
    }
};
#endif

static int s_argc = 1;
static char s_arg0[] = "doom-meego";
static char *s_argv[] = { s_arg0, 0 };

// Visible panel size. The DHAVN X screen reports 1280x1440 (touch space) but only
// the top 1280x720 is on the glass. Override: DOOM_SCREEN=WxH.
static QRect panelRect()
{
    const char *env = getenv("DOOM_SCREEN");
    int w = 0, h = 0;
    if (env && sscanf(env, "%dx%d", &w, &h) == 2 && w > 0 && h > 0)
        return QRect(0, 0, w, h);
    QRect g = QApplication::desktop()->screenGeometry();
    w = g.width() > 0 ? g.width() : 1280;
    h = g.height() > 0 ? g.height() : 720;
    if (h * 16 > w * 9 + 16)        // taller than 16:9 (e.g. 1280x1440) -> visible part is 16:9
        h = w * 9 / 16;
    return QRect(0, 0, w, h);
}

static int s_mouseLog = 0;
void DoomWidget::mousePressEvent(QMouseEvent *e)
{
    if (s_mouseLog++ < 20) fprintf(stderr, "doom-meego: qt mouse %d,%d evdev=%d\n", e->pos().x(), e->pos().y(), s_evTouch);
    if (!s_evTouch) setZone(zoneAt(e->pos()));
}
void DoomWidget::mouseMoveEvent(QMouseEvent *e) { if (!s_evTouch && e->buttons()) setZone(zoneAt(e->pos())); }
void DoomWidget::mouseReleaseEvent(QMouseEvent *) { if (!s_evTouch) setZone(Z_NONE); }

extern "C" void meego_touch(int x, int y, int down)
{
    if (!s_Widget) return;
    s_evTouch = 1;
    if (!down) { s_Widget->setZone(Z_NONE); return; }
    int z = s_Widget->zoneAt(QPoint(x, y));
    if (z != s_Widget->zone) s_Widget->setZone(z);
}

extern "C" void meego_setVisible(int on)
{
    if (!s_Widget) return;
    fprintf(stderr, "doom-meego: [focus] setVisible(%d) widget visible=%d\n", on, s_Widget->isVisible() ? 1 : 0);
    if (on) {
        s_Widget->setZone(Z_NONE);
        s_Widget->show();
        s_Widget->raise();
        s_Widget->activateWindow();
    } else {
        s_Widget->setZone(Z_NONE);
        s_Widget->lower();
        s_Widget->hide();
    }
}


extern "C" void meego_show_fatal(const char *msg_utf8)
{
    /* Head unit launches us as AppUpgrade (Qt app). Show a readable fullscreen hint, then exit. */
    int argc = 1;
    static char arg0[] = "AppUpgrade";
    static char *argv[] = { arg0, 0 };
    DoomApp app(argc, argv);
    QWidget w;
    w.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    w.setStyleSheet(QString::fromLatin1("background-color:#000000;"));
    QLabel lab(&w);
    lab.setText(QString::fromUtf8(msg_utf8));
    lab.setAlignment(Qt::AlignCenter);
    lab.setWordWrap(true);
    lab.setStyleSheet(QString::fromLatin1("color:#ffffff; font-size:28px; padding:40px;"));
    QRect g = QApplication::desktop()->screenGeometry();
    w.setGeometry(g);
    lab.setGeometry(0, 0, g.width(), g.height());
    w.showFullScreen();
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < 6000) {
        app.processEvents();
        usleep(20000);
    }
    /* release anything we might have grabbed if init raced; then exit */
    exit(1);
}

extern "C" void DG_Init()
{
    memset(s_KeyQueue, 0, sizeof(s_KeyQueue));
    if (!QApplication::instance())
#ifdef Q_WS_X11
        s_App = new DoomApp(s_argc, s_argv);
#else
        s_App = new QApplication(s_argc, s_argv);
#endif
    s_Widget = new DoomWidget();
#ifdef MEEGO_TARGET
    QRect r = panelRect();
    fprintf(stderr, "doom-meego: screen %dx%d -> window %dx%d\n",
            QApplication::desktop()->screenGeometry().width(),
            QApplication::desktop()->screenGeometry().height(), r.width(), r.height());
    s_Widget->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    s_Widget->setGeometry(r);
    s_Widget->setFixedSize(r.size());
    s_Widget->show();
    s_Widget->move(0, 0);
    s_Widget->raise();
    s_Widget->activateWindow();
#else
    s_Widget->resize(1280, 720);
    s_Widget->show();
#endif
    /* Paint black + side bars right away; init (WAD, sound) can take seconds. */
    s_Widget->repaint();
    QApplication::processEvents();
    s_Widget->shownAt = DG_GetTicksMs();
    meego_input_init();
}

extern "C" const unsigned char *I_GetVideoBuffer320(void);
extern "C" void I_GetPalette32(uint32_t *out);

// 320x200 indexed -> 960x720 ARGB: each pixel x3 horizontally, rows repeated 4,4,3,4,3 (18 per 5).
extern "C" void DG_DrawFrame()
{
    static uint32_t pal[256];
    const unsigned char *src = I_GetVideoBuffer320();
    I_GetPalette32(pal);
    int oy = 0;
    for (int y = 0; y < 200 && src; ++y) {
        int reps = (y + 1) * 18 / 5 - y * 18 / 5;
        uint32_t *dst = (uint32_t *)s_Widget->image.scanLine(oy);
        const unsigned char *row = src + y * 320;
        for (int x = 0; x < 320; ++x) {
            uint32_t c = pal[row[x]];
            dst[x * 3] = c; dst[x * 3 + 1] = c; dst[x * 3 + 2] = c;
        }
        for (int k = 1; k < reps; ++k)
            memcpy(s_Widget->image.scanLine(oy + k), dst, GAME_W * 4);
        oy += reps;
    }
    if (s_Widget->isVisible())
        s_Widget->repaint();
    static bool firstFrame = true;
    if (firstFrame) {
        firstFrame = false;
        QApplication::processEvents();
        fprintf(stderr, "doom-meego: [ui] first frame\n");
        fflush(stderr);
    }
    meego_input_poll();
    x11Poll();
    s_Widget->checkHold();
    QApplication::processEvents();
}

extern "C" void DG_SleepMs(uint32_t ms)
{
    usleep(ms * 1000);
}

extern "C" uint32_t DG_GetTicksMs()
{
    struct timeval tp;
    gettimeofday(&tp, 0);
    return (tp.tv_sec * 1000) + (tp.tv_usec / 1000);
}

extern "C" int DG_GetKey(int* pressed, unsigned char* doomKey)
{
    if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex)
        return 0;
    unsigned short keyData = s_KeyQueue[s_KeyQueueReadIndex];
    s_KeyQueueReadIndex = (s_KeyQueueReadIndex + 1) % KEYQUEUE_SIZE;
    *pressed = keyData >> 8;
    *doomKey = keyData & 0xFF;
    return 1;
}

extern "C" void DG_SetWindowTitle(const char * title)
{
    if (s_Widget) s_Widget->setWindowTitle(QString::fromLatin1(title));
}

int main(int argc, char **argv)
{
    setvbuf(stderr, 0, _IONBF, 0);
    setvbuf(stdout, 0, _IOLBF, 0);
    {
        /* Under Updater stdout is a pipe (MergedChannels -> doom_log.txt on card).
           Send stderr there too; exe dir is /tmp/doom-meego-run-<pid>, a file log there is lost. */
        struct stat st;
        if (fstat(1, &st) == 0 && S_ISFIFO(st.st_mode)) {
            setvbuf(stdout, 0, _IONBF, 0);
            dup2(1, 2);
            fprintf(stderr, "doom-meego: stderr -> stdout (Updater pipe), pid %d\n", (int)getpid());
        } else {
            meego_setup_log();
        }
    }

    /* Flat card launch: head unit runs AppUpgrade with no -iwad. Find WAD next to the exe. */
    int has_iwad = 0;
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], "-iwad") == 0) { has_iwad = 1; break; }

    char *found = 0;
    char **new_argv = argv;
    int new_argc = argc;
    if (!has_iwad) {
        found = meego_find_iwad(); /* shows fatal UI and exits if missing */
        if (found) {
            new_argc = argc + 2;
            new_argv = (char **)malloc((size_t)(new_argc + 1) * sizeof(char *));
            for (int i = 0; i < argc; ++i) new_argv[i] = argv[i];
            new_argv[argc] = (char *)"-iwad";
            new_argv[argc + 1] = found;
            new_argv[new_argc] = 0;
        }
    }

    doomgeneric_Create(new_argc, new_argv);
    for (;;)
        doomgeneric_Tick();
    return 0;
}
