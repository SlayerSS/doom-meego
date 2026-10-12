// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 SlayerSS
// Part of doom-meego (doomgeneric port for Hyundai Genesis DH / LG DHAVN MeeGo head units).
// i_oplmus.c - OPL music for doom-meego (head unit, i686 Atom).
// MUS (via mus2mid) and MIDI songs, GENMIDI instruments from the WAD, played on the
// Nuked OPL3 emulator (src/opl3.c, LGPL-2.1+) in OPL3 mode, 18 two-operator voices,
// chip at 49716 Hz resampled by Nuked to 48000. Rendered inside the ALSA mixer thread
// (i_alsa.c calls MUS_Mix). Logs "[music] ...".
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include "doomtype.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"
#include "memio.h"
#include "mus2mid.h"
#include "opl3.h"

#define RATE 48000          // output rate (= ALSA mixer); OPL3 chip runs at 49716 Hz inside Nuked
#define NVOICE 18          // OPL3 mode, 18 voices (matched a DMX reference render better than 9)
#define MUSGAIN 1.0f        // OPL3 output gain before the music soft clamp (Doom tracks peak ~8-12k)
#define LOGM(...) do { fprintf(stderr, "doom-meego: [music] " __VA_ARGS__); fflush(stderr); } while (0)

typedef struct { uint8_t trem, att, sus, wave, scale, level; } __attribute__((packed)) gop_t;
typedef struct { gop_t mod; uint8_t fb; gop_t car; uint8_t unused; int16_t offset; } __attribute__((packed)) gvoice_t;
typedef struct { uint16_t flags; uint8_t finetune, fixnote; gvoice_t v[2]; } __attribute__((packed)) ginstr_t;

typedef struct { uint32_t tick; uint8_t st, d1, d2; uint32_t tempo; } ev_t;
typedef struct { ev_t *ev; int n; int div; } song_t;

typedef struct {
    int used, rel, ch, key, vel; uint32_t age;
    const gvoice_t *g;
    float note;               // MIDI note (incl. offset/finetune) for bend recompute
    uint8_t b0;               // last 0xB0 value (key-on bit 0x20)
} voice_t;

static ginstr_t *s_gm;
static int s_ok;
static pthread_mutex_t m_mx = PTHREAD_MUTEX_INITIALIZER;
static song_t *s_song;
static int s_idx, s_loop, s_playing, s_paused;
static double s_tickpos, s_tps;
static float s_musvol = 100.0f / 127.0f;
static int s_chprog[16], s_chvol[16]; static float s_chbend[16];
static voice_t s_v[NVOICE];
static uint32_t s_age;
static opl3_chip s_chip;
static double s_cpu, s_wall; static int s_logn;
static double s_sq; static long s_nsq; static int32_t s_pk;
double MUS_TestRms(void) { return s_nsq ? sqrt(s_sq / s_nsq) : 0; }

// OPL3 register helpers: voice i -> bank (i / 9), channel (i % 9)
static const uint8_t kOpOff[9] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };
static void wr(int i, int reg, int v) { OPL3_WriteReg(&s_chip, (uint16_t)((i >= 9 ? 0x100 : 0) + reg), (uint8_t)v); }
static int modOp(int i) { return kOpOff[i % 9]; }
static int carOp(int i) { return kOpOff[i % 9] + 3; }

// Volume as in chocolate-doom i_oplmusic.c (DMX): mapping table, carrier level ignores the instrument level.
static const uint8_t kVolMap[128] = {
    0, 1, 3, 5, 6, 8, 10, 11, 13, 14, 16, 17, 19, 20, 22, 23,
    25, 26, 27, 29, 30, 32, 33, 34, 36, 37, 39, 41, 43, 45, 47, 49,
    50, 52, 54, 55, 57, 59, 60, 61, 63, 64, 66, 67, 68, 69, 71, 72,
    73, 74, 75, 76, 77, 79, 80, 81, 82, 83, 84, 84, 85, 86, 87, 88,
    89, 90, 91, 92, 92, 93, 94, 95, 96, 96, 97, 98, 99, 99, 100, 101,
    101, 102, 103, 103, 104, 105, 105, 106, 107, 107, 108, 109, 109, 110, 110, 111,
    112, 112, 113, 113, 114, 114, 115, 115, 116, 117, 117, 118, 118, 119, 119, 120,
    120, 121, 121, 122, 122, 123, 123, 123, 124, 124, 125, 125, 126, 126, 127, 127
};
static void setLevels(int i)
{
    const voice_t *v = &s_v[i];
    unsigned midi = 2 * (kVolMap[s_chvol[v->ch] & 127] + 1);
    unsigned full = (kVolMap[v->vel & 127] * midi) >> 9;
    unsigned car = 0x3f - (full > 0x3f ? 0x3f : full);
    wr(i, 0x40 + carOp(i), (v->g->car.scale & 0xC0) | car);
    if ((v->g->fb & 1) && (v->g->mod.level & 0x3f) != 0x3f) {   // additive: modulator follows volume
        unsigned mod = v->g->mod.level & 0x3f;
        if (mod < car) mod = car;
        wr(i, 0x40 + modOp(i), (v->g->mod.scale & 0xC0) | mod);
    } else
        wr(i, 0x40 + modOp(i), (v->g->mod.scale & 0xC0) | (v->g->mod.level & 0x3f));
}
static void setfreq(int i, int keyon)
{
    voice_t *v = &s_v[i];
    // GENMIDI note numbers are one octave below MIDI pitch here (DMX/chocolate-doom: note 60 -> 261.6 Hz
    // after the instrument offset), so 57 instead of 69. Verified against a libADLMIDI DMX render.
    // One octave up vs plain MIDI pitch (69): matches DMX / chocolate-doom (note 60 -> 261.6 Hz with
    // the GENMIDI offsets); 69 played everything an octave low. Verified against a libADLMIDI DMX render.
    double f = 440.0 * pow(2.0, (v->note + s_chbend[v->ch] - 57.0) / 12.0);
    int block = 0, fnum;
    fnum = (int)(f * 1048576.0 / 49716.0 + 0.5);          // block 0: fnum = f * 2^20 / 49716
    while (fnum > 1023 && block < 7) { block++; fnum = (int)(f * (1048576.0 / (1 << block)) / 49716.0 + 0.5); }
    if (fnum > 1023) fnum = 1023;
    wr(i, 0xA0 + i % 9, fnum & 0xFF);
    v->b0 = (uint8_t)((keyon ? 0x20 : 0) | (block << 2) | (fnum >> 8));
    wr(i, 0xB0 + i % 9, v->b0);
}
static void keyOff(int i) { s_v[i].b0 &= ~0x20; wr(i, 0xB0 + i % 9, s_v[i].b0); }

static void allOff(void)
{
    int i;
    for (i = 0; i < NVOICE; i++) if (s_v[i].used) keyOff(i);
    memset(s_v, 0, sizeof(s_v));
}

static void resetCh(void)
{
    int i; for (i = 0; i < 16; i++) { s_chprog[i] = 0; s_chvol[i] = 100; s_chbend[i] = 0; }
}

static int allocVoice(void)
{
    int i, bo = -1; uint32_t oldest = 0xffffffff;
    for (i = 0; i < NVOICE; i++) if (!s_v[i].used) return i;
    for (i = 0; i < NVOICE; i++) if (s_v[i].rel && s_v[i].age < oldest) { oldest = s_v[i].age; bo = i; }
    if (bo >= 0) return bo;
    for (i = 0; i < NVOICE; i++) if (s_v[i].age < oldest) { oldest = s_v[i].age; bo = i; }
    return bo;
}

static void noteOn(int ch, int key, int vel)
{
    ginstr_t *in; int nv, k; float note;
    if (!s_gm) return;
    if (ch == 9) { if (key < 35 || key > 81) return; in = &s_gm[128 + key - 35]; }
    else in = &s_gm[s_chprog[ch] & 127];
    nv = (in->flags & 4) ? 2 : 1;
    for (k = 0; k < nv; k++) {
        const gvoice_t *g = &in->v[k];
        int i = allocVoice(), mo, co;
        voice_t *v = &s_v[i];
        int base = (in->flags & 1) ? in->fixnote : key;
        if (v->used) keyOff(i);
        memset(v, 0, sizeof(*v));
        v->used = 1; v->ch = ch; v->key = key; v->vel = vel; v->age = ++s_age; v->g = g;
        mo = modOp(i); co = carOp(i);
        wr(i, 0x20 + mo, g->mod.trem); wr(i, 0x60 + mo, g->mod.att); wr(i, 0x80 + mo, g->mod.sus); wr(i, 0xE0 + mo, g->mod.wave & 3);
        wr(i, 0x20 + co, g->car.trem); wr(i, 0x60 + co, g->car.att); wr(i, 0x80 + co, g->car.sus); wr(i, 0xE0 + co, g->car.wave & 3);
        wr(i, 0xC0 + i % 9, 0x30 | (g->fb & 0x0F));       // L+R, feedback, connection
        setLevels(i);
        note = (float)(base + g->offset);
        while (note < 12) note += 12; while (note > 107) note -= 12;
        if (k == 1) note += ((int)in->finetune - 128) / 64.0f;   // chocolate-doom: (fine_tuning/2 - 64) / 32 semitones
        v->note = note;
        setfreq(i, 1);
    }
}

static void noteOff(int ch, int key)
{
    int i;
    for (i = 0; i < NVOICE; i++) if (s_v[i].used && !s_v[i].rel && s_v[i].ch == ch && s_v[i].key == key) {
        s_v[i].rel = 1; keyOff(i);
    }
}

static void doEvent(const ev_t *e)
{
    int ch = e->st & 15, i;
    switch (e->st & 0xF0) {
    case 0x80: noteOff(ch, e->d1); break;
    case 0x90: if (e->d2) noteOn(ch, e->d1, e->d2); else noteOff(ch, e->d1); break;
    case 0xB0:
        if (e->d1 == 7) { s_chvol[ch] = e->d2; for (i = 0; i < NVOICE; i++) if (s_v[i].used && s_v[i].ch == ch) setLevels(i); }
        else if (e->d1 == 120 || e->d1 == 123) { for (i = 0; i < NVOICE; i++) if (s_v[i].used && s_v[i].ch == ch && !s_v[i].rel) { s_v[i].rel = 1; keyOff(i); } }
        else if (e->d1 == 121) { s_chvol[ch] = 100; s_chbend[ch] = 0; }
        break;
    case 0xC0: s_chprog[ch] = e->d1; break;
    case 0xE0:
        s_chbend[ch] = (((e->d2 << 7) | e->d1) - 8192) / 4096.0f;
        for (i = 0; i < NVOICE; i++) if (s_v[i].used && s_v[i].ch == ch) setfreq(i, !s_v[i].rel);
        break;
    case 0xF0:
        if (e->st == 0xFF && e->tempo) s_tps = (double)s_song->div * 1e6 / ((double)e->tempo * RATE);
        break;
    }
}

// Nuked OPL3 -> mono int32 (L+R)/2, 'n' frames at 48000 Hz
static void renderVoices(int32_t *acc, int n)
{
    int s;
    int16_t o[4];
    for (s = 0; s < n; s++) {
        OPL3_GenerateResampled(&s_chip, o);
        acc[s * 2] += ((int32_t)o[0] + o[1]) / 2;
    }
}

// Called from the ALSA mixer thread for each block (stereo interleaved accumulator).
static void MUS_MixRate(int32_t *acc, int frames);
void MUS_Mix(int32_t *acc, int frames)
{
    static int32_t tmp[2048];
    const float lim = 16000.0f;
    float g;
    int n = frames, j;
    if (!s_ok) return;
    if (n > 1024) n = 1024;
    memset(tmp, 0, sizeof(int32_t) * n * 2);
    MUS_MixRate(tmp, n);
    g = MUSGAIN * s_musvol;
    for (j = 0; j < n; j++) {
        float x = tmp[j * 2] * g, ax = x < 0 ? -x : x;
        int32_t xi;
        if (ax > lim) x = (x < 0 ? -1.0f : 1.0f) * lim * (1.0f + (ax - lim) / (ax + lim) * 0.5f);   // soft clamp, max 1.5*lim
        xi = (int32_t)x;
        s_sq += (double)xi * xi; s_nsq++; if (xi > s_pk) s_pk = xi; else if (-xi > s_pk) s_pk = -xi;
        acc[j * 2] += xi; acc[j * 2 + 1] += xi;   // music is mono
    }
}
static void MUS_MixRate(int32_t *acc, int frames)
{
    struct timespec c0, c1;
    if (!s_ok) return;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &c0);
    pthread_mutex_lock(&m_mx);
    if (s_playing && !s_paused && s_song) {
        int done = 0;
        while (done < frames) {
            int chunk = frames - done;
            while (s_idx < s_song->n && s_song->ev[s_idx].tick <= s_tickpos) doEvent(&s_song->ev[s_idx++]);
            if (s_idx >= s_song->n) {
                if (s_loop && s_song->n) { s_idx = 0; s_tickpos = 0; s_tps = (double)s_song->div * 1e6 / (500000.0 * RATE); resetCh(); }
                else { s_playing = 0; break; }
                continue;
            }
            {
                double need = (s_song->ev[s_idx].tick - s_tickpos) / s_tps;
                int ns = (int)need + 1;
                if (ns < chunk) chunk = ns;
            }
            renderVoices(acc + done * 2, chunk);
            s_tickpos += s_tps * chunk;
            done += chunk;
        }
        if (!s_playing) {}
    }
    pthread_mutex_unlock(&m_mx);
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &c1);
    s_cpu += (c1.tv_sec - c0.tv_sec) + (c1.tv_nsec - c0.tv_nsec) * 1e-9;
    s_wall += (double)frames / RATE;
    if (s_wall >= 30.0) {
        int nv = 0, i; for (i = 0; i < NVOICE; i++) nv += s_v[i].used;
        if (s_logn++ < 200) LOGM("cpu %.1f%% of one core (synth, last %.0fs), voices %d, level rms %.0f peak %d vol %.2f playing %d paused %d\n", 100.0 * s_cpu / s_wall, s_wall, nv, s_nsq ? sqrt(s_sq / s_nsq) : 0.0, (int)s_pk, s_musvol, s_playing, s_paused);
        s_sq = 0; s_nsq = 0; s_pk = 0;
        s_cpu = s_wall = 0;
    }
}

static boolean M_Init(void)
{
    int l = W_CheckNumForName("GENMIDI");
    OPL3_Reset(&s_chip, RATE);
    OPL3_WriteReg(&s_chip, 0x105, 0x01);      // OPL3 mode: 18 channels, stereo bits in 0xC0
    OPL3_WriteReg(&s_chip, 0x01, 0x20);       // waveform select enable
    OPL3_WriteReg(&s_chip, 0x08, 0x40);       // note select
    OPL3_WriteReg(&s_chip, 0xBD, 0x00);       // melodic mode, no rhythm
    if (l < 0 || W_LumpLength(l) < 8 + 175 * 36) { LOGM("init FAILED: no GENMIDI lump -> no music\n"); return false; }
    {
        uint8_t *d = (uint8_t *)W_CacheLumpNum(l, PU_STATIC);
        if (memcmp(d, "#OPL_II#", 8)) { LOGM("init FAILED: bad GENMIDI header\n"); return false; }
        s_gm = (ginstr_t *)malloc(175 * 36);
        memcpy(s_gm, d + 8, 175 * 36);
    }
    resetCh(); allOff();
    s_ok = 1;
    LOGM("init OK: GENMIDI 175 instr, Nuked OPL3 (49716 Hz -> %d Hz), %d voices, gain %.2f, mixed into ALSA thread\n", RATE, NVOICE, MUSGAIN);
    return true;
}

static void M_Shutdown(void) { pthread_mutex_lock(&m_mx); s_playing = 0; allOff(); pthread_mutex_unlock(&m_mx); }
static void M_SetVol(int v) { LOGM("volume %d\n", v); pthread_mutex_lock(&m_mx); s_musvol = (v < 0 ? 0 : v > 127 ? 127 : v) / 127.0f; pthread_mutex_unlock(&m_mx); }
static void M_Pause(void) { pthread_mutex_lock(&m_mx); s_paused = 1; pthread_mutex_unlock(&m_mx); }
static void M_Resume(void) { pthread_mutex_lock(&m_mx); s_paused = 0; pthread_mutex_unlock(&m_mx); }

static uint32_t rdvar(const uint8_t **p, const uint8_t *e)
{
    uint32_t v = 0; int i;
    for (i = 0; i < 4 && *p < e; i++) { uint8_t b = *(*p)++; v = (v << 7) | (b & 0x7F); if (!(b & 0x80)) break; }
    return v;
}

typedef struct { ev_t e; int seq; } sev_t;
static int evcmp(const void *a, const void *b)
{
    const sev_t *x = a, *y = b;
    if (x->e.tick != y->e.tick) return x->e.tick < y->e.tick ? -1 : 1;
    return x->seq - y->seq;
}

static song_t *parseMidi(const uint8_t *d, int len)
{
    int ntr, t, pos = 14, n = 0, cap = 4096, div;
    sev_t *a;
    song_t *s;
    if (len < 14 || memcmp(d, "MThd", 4)) return NULL;
    ntr = (d[10] << 8) | d[11];
    div = (d[12] << 8) | d[13];
    if (div & 0x8000) div = 96;
    if (div <= 0) div = 96;
    a = (sev_t *)malloc(cap * sizeof(sev_t));
    for (t = 0; t < ntr && pos + 8 <= len; t++) {
        uint32_t tl = ((uint32_t)d[pos + 4] << 24) | (d[pos + 5] << 16) | (d[pos + 6] << 8) | d[pos + 7];
        const uint8_t *p = d + pos + 8, *e = p + tl;
        uint32_t tick = 0; uint8_t rs = 0;
        if (memcmp(d + pos, "MTrk", 4)) break;
        if (e > d + len) e = d + len;
        pos += 8 + tl;
        while (p < e) {
            uint8_t st; ev_t ev;
            tick += rdvar(&p, e);
            if (p >= e) break;
            st = *p;
            if (st & 0x80) p++; else st = rs;
            memset(&ev, 0, sizeof(ev)); ev.tick = tick; ev.st = st;
            if (st == 0xFF) {
                uint8_t ty; uint32_t ml;
                if (p >= e) break;
                ty = *p++; ml = rdvar(&p, e);
                if (ty == 0x51 && ml >= 3 && p + 3 <= e) ev.tempo = (p[0] << 16) | (p[1] << 8) | p[2];
                p += ml;
                if (ty == 0x2F) break;
                if (!ev.tempo) continue;
            } else if (st == 0xF0 || st == 0xF7) { p += rdvar(&p, e); continue; }
            else if (st >= 0x80) {
                rs = st;
                if (p >= e) break;
                ev.d1 = *p++ & 0x7F;
                if ((st & 0xF0) != 0xC0 && (st & 0xF0) != 0xD0) { if (p >= e) break; ev.d2 = *p++ & 0x7F; }
            } else break;
            if (n >= cap) { cap *= 2; a = (sev_t *)realloc(a, cap * sizeof(sev_t)); }
            a[n].e = ev; a[n].seq = n; n++;
        }
    }
    qsort(a, n, sizeof(sev_t), evcmp);
    s = (song_t *)malloc(sizeof(song_t));
    s->ev = (ev_t *)malloc((n ? n : 1) * sizeof(ev_t));
    for (t = 0; t < n; t++) s->ev[t] = a[t].e;
    free(a);
    s->n = n; s->div = div;
    return s;
}

static void *M_Reg(void *data, int len)
{
    song_t *s = NULL; const char *fmt = "MIDI";
    if (!s_ok || !data || len < 4) return NULL;
    if (!memcmp(data, "MUS\x1a", 4)) {
        MEMFILE *in = mem_fopen_read(data, len), *out = mem_fopen_write();
        void *buf; size_t bl;
        fmt = "MUS";
        if (!mus2mid(in, out)) { mem_get_buf(out, &buf, &bl); s = parseMidi(buf, (int)bl); }
        mem_fclose(in); mem_fclose(out);
    } else s = parseMidi((const uint8_t *)data, len);
    if (!s) { LOGM("register: %s song (%d bytes) not parsed\n", fmt, len); return NULL; }
    LOGM("register: %s song %d bytes, %d events, div %d\n", fmt, len, s->n, s->div);
    return s;
}

static void M_Unreg(void *h)
{
    song_t *s = (song_t *)h;
    if (!s) return;
    pthread_mutex_lock(&m_mx);
    if (s_song == s) { s_song = NULL; s_playing = 0; allOff(); }
    pthread_mutex_unlock(&m_mx);
    free(s->ev); free(s);
}

static void M_Play(void *h, boolean looping)
{
    song_t *s = (song_t *)h;
    LOGM("play %p loop %d vol %.2f\n", h, looping ? 1 : 0, s_musvol);
    if (!s) return;
    pthread_mutex_lock(&m_mx);
    allOff(); resetCh();
    s_song = s; s_idx = 0; s_tickpos = 0; s_loop = looping ? 1 : 0;
    s_tps = (double)s->div * 1e6 / (500000.0 * RATE);
    s_playing = 1; s_paused = 0;
    pthread_mutex_unlock(&m_mx);
}

static void M_Stop(void) { pthread_mutex_lock(&m_mx); s_playing = 0; allOff(); pthread_mutex_unlock(&m_mx); }
static boolean M_Playing(void) { return s_playing ? true : false; }

static snddevice_t m_devs[] = { SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS, SNDDEVICE_WAVEBLASTER,
                                SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32, SNDDEVICE_ADLIB, SNDDEVICE_GENMIDI };

music_module_t DG_music_module = {
    m_devs, sizeof(m_devs) / sizeof(m_devs[0]),
    M_Init, M_Shutdown, M_SetVol, M_Pause, M_Resume, M_Reg, M_Unreg, M_Play, M_Stop, M_Playing, NULL
};
