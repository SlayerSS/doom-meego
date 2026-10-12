// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 SlayerSS
// Part of doom-meego (doomgeneric port for Hyundai Genesis DH / LG DHAVN MeeGo head units).
// i_alsa.c - ALSA SFX backend for doom-meego (head unit). libasound is dlopen'ed,
// so the binary still runs (silently) if it is missing. Mixer runs in its own thread.
// Log lines: "doom-meego: [audio] ..." on stderr (-> doom_log.txt next to the executable).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include "doomtype.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"

#define MIXRATE   48000     // = dmix rate of main_mixer: no extra rate conversion in ALSA
#define NCHAN     16
#define FRAMES    480       // 10 ms, one dmix period
#define KNEE      20000     // soft limiter knee (16-bit)
#define SFXGAIN   180       // SFX master gain /256 (headroom for 16 channels)

typedef struct { int rate; int len; uint8_t *data; } dsfx_t;
typedef struct { dsfx_t *s; uint32_t pos; uint32_t step; int left, right; int active; } chan_t;

static void *s_lib;
static void *s_pcm;
static int (*p_open)(void **, const char *, int, int);
typedef unsigned long uframes_t;   /* snd_pcm_uframes_t */
static int (*p_hw_malloc)(void **);
static void (*p_hw_free)(void *);
static int (*p_hw_any)(void *, void *);
static int (*p_hw_access)(void *, void *, int);
static int (*p_hw_format)(void *, void *, int);
static int (*p_hw_channels)(void *, void *, unsigned);
static int (*p_hw_resample)(void *, void *, unsigned);
static int (*p_hw_rate)(void *, void *, unsigned, int);
static int (*p_hw_rate_near)(void *, void *, unsigned *, int *);
static unsigned s_rate = MIXRATE;
static int (*p_hw_period)(void *, void *, uframes_t *, int *);
static int (*p_hw_buffer)(void *, void *, uframes_t *);
static int (*p_hw_apply)(void *, void *);
static int (*p_sw_malloc)(void **);
static void (*p_sw_free)(void *);
static int (*p_sw_current)(void *, void *);
static int (*p_sw_start)(void *, void *, uframes_t);
static int (*p_sw_availmin)(void *, void *, uframes_t);
static int (*p_sw_apply)(void *, void *);
static long (*p_writei)(void *, const void *, unsigned long);
static int (*p_recover)(void *, int, int);
static int (*p_close)(void *);
static int (*p_nonblock)(void *, int);
static int (*p_drop)(void *);
static int (*p_prepare)(void *);
static const char *(*p_strerror)(int);

static pthread_t s_thr;
static pthread_mutex_t s_mx = PTHREAD_MUTEX_INITIALIZER;
static volatile int s_run, s_paused, s_wantPause, s_inited;
static volatile int s_reasons;      // bit0: hide/S0, bit1: X window lost (unmap/obscured/focus out)
#define PCM_DEV   "main_mix2"   // plug -> main_aa -> main_mixer (dmix hw:0,1); audible on the car (sound test 12.10)
static const char *const s_pcmOrder[] = { "main_mix2", "media_mixer", "default" };
#define PERIOD    (FRAMES * 2)   // 960: whole mixer blocks per period
#define BUFFER    (PERIOD * 4)   // 3840 frames, 80 ms
static const char *s_devName = PCM_DEV;
static uframes_t s_period = PERIOD, s_buffer = BUFFER;
static chan_t s_ch[NCHAN];
static boolean s_prefix;
extern void MUS_Mix(int32_t *acc, int frames);

#define LOGA(...) do { fprintf(stderr, "doom-meego: [audio] " __VA_ARGS__); fflush(stderr); } while (0)

void I_AlsaPauseReason(int bit, int on, const char *why)
{
    int old;
    if (!s_inited) return;
    old = s_reasons;
    if (on) s_reasons |= bit; else s_reasons &= ~bit;
    s_wantPause = s_reasons ? 1 : 0;
    if (old != s_reasons)
        LOGA("%s by %s (reasons 0x%x -> 0x%x): sfx+music %s\n", on ? "pause" : "unpause", why, old, s_reasons,
             s_reasons ? "STOPPED, pcm closed" : "running");
}
void I_AlsaPause(int p) { I_AlsaPauseReason(1, p, p ? "hide/pause" : "show/resume"); }
void I_AlsaFocusPause(int lost, const char *why) { I_AlsaPauseReason(2, lost, why); }

/* Open hw:0,0 and configure it by hand: S16_LE, stereo, exactly 48000 Hz (no resampling),
 * period/buffer near PERIOD/BUFFER, then sw_params. Any failure -> NULL (no sound). */
static void *openPcm(int nonblock)
{
    void *pcm = NULL, *hw = NULL, *sw = NULL;
    uframes_t period = PERIOD, buffer = BUFFER;
    unsigned rate = MIXRATE;
    int dir = 0, rdir = 0, err;
#define STEP(call, what) do { err = (call); if (err < 0) { LOGA("pcm '%s': %s FAILED: %s\n", s_devName, what, p_strerror(err)); goto fail; } } while (0)
    STEP(p_open(&pcm, s_devName, 0 /*PLAYBACK*/, nonblock ? 1 /*SND_PCM_NONBLOCK*/ : 0), "open");
    LOGA("pcm '%s': open OK\n", s_devName);
    STEP(p_hw_malloc(&hw), "hw_params_malloc");
    STEP(p_hw_any(pcm, hw), "hw_params_any");
    STEP(p_hw_access(pcm, hw, 3 /*RW_INTERLEAVED*/), "set_access RW_INTERLEAVED");
    STEP(p_hw_format(pcm, hw, 2 /*S16_LE*/), "set_format S16_LE");
    STEP(p_hw_channels(pcm, hw, 2), "set_channels 2");
    STEP(p_hw_resample(pcm, hw, 1), "set_rate_resample 1");
    STEP(p_hw_rate_near(pcm, hw, &rate, &rdir), "set_rate_near 48000");
    if (rate != MIXRATE) LOGA("pcm '%s': rate %u instead of %d (pitch will differ)\n", s_devName, rate, MIXRATE);
    STEP(p_hw_period(pcm, hw, &period, &dir), "set_period_size_near");
    STEP(p_hw_buffer(pcm, hw, &buffer), "set_buffer_size_near");
    STEP(p_hw_apply(pcm, hw), "hw_params");
    LOGA("pcm '%s': hw_params OK: RW_INTERLEAVED S16_LE 2ch %u Hz, period %lu (asked %d), buffer %lu (asked %d)\n",
         s_devName, rate, period, PERIOD, buffer, BUFFER);
    p_hw_free(hw); hw = NULL;
    STEP(p_sw_malloc(&sw), "sw_params_malloc");
    STEP(p_sw_current(pcm, sw), "sw_params_current");
    STEP(p_sw_start(pcm, sw, buffer - period), "set_start_threshold");
    STEP(p_sw_availmin(pcm, sw, period), "set_avail_min");
    STEP(p_sw_apply(pcm, sw), "sw_params");
    LOGA("pcm '%s': sw_params OK: start_threshold %lu, avail_min %lu\n", s_devName, buffer - period, period);
    p_sw_free(sw);
    s_period = period; s_buffer = buffer; s_rate = rate;
    return pcm;
fail:
#undef STEP
    if (hw) p_hw_free(hw);
    if (sw) p_sw_free(sw);
    if (pcm) p_close(pcm);
    return NULL;
}

/* Paused = PCM really closed: the dmix on hw:0,1 is released, so the head unit's
 * own media players do not see a foreign stream while Doom is in the background. */
static void *reopenPcm(void)
{
    void *pcm = openPcm(0);
    if (pcm) LOGA("pcm '%s' reopened\n", s_devName);
    return pcm;
}

static unsigned long s_xruns;
static long s_limited;
/* One block: SFX (linear interpolation, 8-bit unsigned DMX -> 16-bit), music,
 * SFX headroom + soft limiter (no hard clipping, no dither). Also used by the PC harness. */
void A_MixBlock(int16_t *buf, int32_t *acc)
{
    int i, c;
    memset(acc, 0, sizeof(int32_t) * FRAMES * 2);
    pthread_mutex_lock(&s_mx);
    for (c = 0; c < NCHAN; c++) {
        chan_t *ch = &s_ch[c];
        int gl, gr;
        if (!ch->active) continue;
        gl = ch->left * SFXGAIN; gr = ch->right * SFXGAIN;      // x/65536
        for (i = 0; i < FRAMES; i++) {
            uint32_t idx = ch->pos >> 16, fr = (ch->pos >> 4) & 0xFFF;
            int v0, v1, v;
            if (idx >= (uint32_t)ch->s->len) { ch->active = 0; break; }
            v0 = (int)ch->s->data[idx] - 128;
            v1 = idx + 1 < (uint32_t)ch->s->len ? (int)ch->s->data[idx + 1] - 128 : v0;
            v = (v0 << 8) + (((v1 - v0) * (int)fr) >> 4);        // 16-bit, linear interp
            acc[i * 2]     += (int32_t)(((int64_t)v * gl) >> 16);
            acc[i * 2 + 1] += (int32_t)(((int64_t)v * gr) >> 16);
            ch->pos += ch->step;
        }
    }
    pthread_mutex_unlock(&s_mx);
    MUS_Mix(acc, FRAMES);
    for (i = 0; i < FRAMES * 2; i++) {
        int32_t v = acc[i], a = v < 0 ? -v : v;
        if (a > KNEE) {       // rational soft knee: approaches 32767, never reaches it
            int32_t over = a - KNEE, room = 32767 - KNEE;
            a = KNEE + (int32_t)((int64_t)over * room / (over + room));
            v = v < 0 ? -a : a; ++s_limited;
        }
        buf[i] = (int16_t)v;
    }
}

static void *mixThread(void *arg)
{
    int16_t buf[FRAMES * 2];
    int32_t acc[FRAMES * 2];
    int errs = 0;
    (void)arg;
    while (s_run) {
        if (s_wantPause != s_paused) {
            s_paused = s_wantPause;
            if (s_paused) {
                if (s_pcm) { p_drop(s_pcm); p_close(s_pcm); s_pcm = NULL; LOGA("pcm closed (paused)\n"); }
            }
        }
        if (s_paused) { usleep(20000); continue; }
        if (!s_pcm) {
            s_pcm = reopenPcm();
            if (!s_pcm) { usleep(500000); continue; }
        }
        A_MixBlock(buf, acc);
        {
            long r = p_writei(s_pcm, buf, FRAMES);
            if (r == -32 /*EPIPE: underrun*/) {
                ++s_xruns;
                if (s_xruns <= 10 || (s_xruns % 100) == 0) LOGA("xrun (underrun) #%lu\n", s_xruns);
            }
            if (r < 0) {
                r = p_recover(s_pcm, (int)r, 1);
                if (r < 0) {
                    if (errs++ < 5) LOGA("write error: %s\n", p_strerror((int)r));
                    usleep(20000);
                }
            }
        }
    }
    return NULL;
}

extern int snd_sfxdevice, snd_musicdevice;
static void A_DumpProc(const char *path)
{
    char line[256];
    FILE *f = fopen(path, "r");
    if (!f) { LOGA("%s: cannot open\n", path); return; }
    LOGA("---- %s\n", path);
    while (fgets(line, sizeof(line), f)) LOGA("  %s%s", line, strchr(line, '\n') ? "" : "\n");
    fclose(f);
}

static boolean A_Init(boolean use_sfx_prefix)
{
    const char *dev = PCM_DEV;
    s_prefix = use_sfx_prefix;
    s_lib = dlopen("libasound.so.2", RTLD_NOW | RTLD_GLOBAL);
    LOGA("init: snd_sfxdevice %d snd_musicdevice %d\n", snd_sfxdevice, snd_musicdevice);
    A_DumpProc("/proc/asound/cards");
    A_DumpProc("/proc/asound/pcm");
    if (!s_lib) { LOGA("init FAILED: dlopen libasound.so.2: %s -> no sound\n", dlerror()); return false; }
    LOGA("dlopen libasound.so.2 OK\n");
#define SYM(v, n) *(void **)(&v) = dlsym(s_lib, n); if (!v) { LOGA("init FAILED: no symbol %s -> no sound\n", n); return false; }
    SYM(p_open, "snd_pcm_open"); SYM(p_writei, "snd_pcm_writei");
    SYM(p_recover, "snd_pcm_recover"); SYM(p_close, "snd_pcm_close"); SYM(p_nonblock, "snd_pcm_nonblock");
    SYM(p_drop, "snd_pcm_drop"); SYM(p_prepare, "snd_pcm_prepare"); SYM(p_strerror, "snd_strerror");
    SYM(p_hw_malloc, "snd_pcm_hw_params_malloc"); SYM(p_hw_free, "snd_pcm_hw_params_free");
    SYM(p_hw_any, "snd_pcm_hw_params_any"); SYM(p_hw_access, "snd_pcm_hw_params_set_access");
    SYM(p_hw_format, "snd_pcm_hw_params_set_format"); SYM(p_hw_channels, "snd_pcm_hw_params_set_channels");
    SYM(p_hw_resample, "snd_pcm_hw_params_set_rate_resample"); SYM(p_hw_rate, "snd_pcm_hw_params_set_rate"); SYM(p_hw_rate_near, "snd_pcm_hw_params_set_rate_near");
    SYM(p_hw_period, "snd_pcm_hw_params_set_period_size_near"); SYM(p_hw_buffer, "snd_pcm_hw_params_set_buffer_size_near");
    SYM(p_hw_apply, "snd_pcm_hw_params");
    SYM(p_sw_malloc, "snd_pcm_sw_params_malloc"); SYM(p_sw_free, "snd_pcm_sw_params_free");
    SYM(p_sw_current, "snd_pcm_sw_params_current"); SYM(p_sw_start, "snd_pcm_sw_params_set_start_threshold");
    SYM(p_sw_availmin, "snd_pcm_sw_params_set_avail_min"); SYM(p_sw_apply, "snd_pcm_sw_params");
    {
        int k;
        s_pcm = NULL;
        for (k = 0; k < (int)(sizeof(s_pcmOrder) / sizeof(s_pcmOrder[0])) && !s_pcm; k++) {
            s_devName = s_pcmOrder[k];
            s_pcm = openPcm(1);
        }
        if (!s_pcm) { LOGA("init FAILED: main_mix2, media_mixer, default all failed -> no sound\n"); return false; }
        dev = s_devName;
        LOGA("pcm -> %s\n", s_devName);
    }
    p_nonblock(s_pcm, 0); // writes from our own thread may block
    memset(s_ch, 0, sizeof(s_ch));
    s_run = 1; s_paused = s_wantPause = 0; s_reasons = 0; s_inited = 1;
    if (pthread_create(&s_thr, NULL, mixThread, NULL) != 0) {
        LOGA("init FAILED: pthread_create -> no sound\n"); p_close(s_pcm); s_pcm = NULL; return false;
    }
    LOGA("init OK: pcm '%s' %d Hz stereo S16, period %lu buffer %lu, %d channels, linear interp, soft limiter, no dither\n", dev, MIXRATE, s_period, s_buffer, NCHAN);
    return true;
}

void I_AlsaShutdown(void);
static void A_Shutdown(void)
{
    if (!s_inited) return;
    s_run = 0;
    pthread_join(s_thr, NULL);
    if (s_pcm) { p_drop(s_pcm); p_close(s_pcm); s_pcm = NULL; }
    s_inited = 0;
    LOGA("shutdown\n");
}

void I_AlsaShutdown(void)
{
    A_Shutdown();
}


static int A_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char name[16];
    if (sfx->link) sfx = sfx->link;
    if (s_prefix) snprintf(name, sizeof(name), "ds%s", sfx->name);
    else snprintf(name, sizeof(name), "%s", sfx->name);
    return W_CheckNumForName(name);
}

static dsfx_t *loadSfx(sfxinfo_t *sfx)
{
    dsfx_t *d;
    uint8_t *raw;
    int lumplen, rate, len;
    if (sfx->driver_data) return (dsfx_t *)sfx->driver_data;
    if (sfx->lumpnum < 0) return NULL;
    lumplen = W_LumpLength(sfx->lumpnum);
    raw = (uint8_t *)W_CacheLumpNum(sfx->lumpnum, PU_STATIC);
    if (lumplen < 8 || raw[0] != 3 || raw[1] != 0) { W_ReleaseLumpNum(sfx->lumpnum); return NULL; }
    rate = raw[2] | (raw[3] << 8);
    len = raw[4] | (raw[5] << 8) | (raw[6] << 16) | (raw[7] << 24);
    if (len > lumplen - 8 || len <= 48) { W_ReleaseLumpNum(sfx->lumpnum); return NULL; }
    d = (dsfx_t *)malloc(sizeof(dsfx_t));
    d->rate = rate ? rate : 11025;
    d->len = len - 32;              // DMX: 16 pad bytes at each end
    d->data = (uint8_t *)malloc(d->len);
    memcpy(d->data, raw + 8 + 16, d->len);
    W_ReleaseLumpNum(sfx->lumpnum);
    sfx->driver_data = d;
    return d;
}

static void setVol(chan_t *ch, int vol, int sep)
{
    if (vol < 0) vol = 0; if (vol > 127) vol = 127;
    if (sep < 0) sep = 0; if (sep > 254) sep = 254;
    ch->left  = ((254 - sep) * vol) / 127;   // 0..254 (x/256 gain)
    ch->right = (sep * vol) / 127;
}

static void A_UpdateSoundParams(int channel, int vol, int sep)
{
    if (channel < 0 || channel >= NCHAN) return;
    pthread_mutex_lock(&s_mx); setVol(&s_ch[channel], vol, sep); pthread_mutex_unlock(&s_mx);
}

static int A_StartSound(sfxinfo_t *sfx, int channel, int vol, int sep)
{
    dsfx_t *d;
    if (channel < 0 || channel >= NCHAN) return -1;
    d = loadSfx(sfx);
    if (!d) return -1;
    pthread_mutex_lock(&s_mx);
    s_ch[channel].s = d;
    s_ch[channel].pos = 0;
    s_ch[channel].step = (uint32_t)(((uint64_t)d->rate << 16) / MIXRATE);
    setVol(&s_ch[channel], vol, sep);
    s_ch[channel].active = 1;
    pthread_mutex_unlock(&s_mx);
    return channel;
}

static void A_StopSound(int channel)
{
    if (channel < 0 || channel >= NCHAN) return;
    pthread_mutex_lock(&s_mx); s_ch[channel].active = 0; pthread_mutex_unlock(&s_mx);
}

static boolean A_SoundIsPlaying(int channel)
{
    if (channel < 0 || channel >= NCHAN) return false;
    return s_ch[channel].active ? true : false;
}

static void A_Update(void) {}

static void A_CacheSounds(sfxinfo_t *sounds, int num)
{
    int i;
    for (i = 0; i < num; i++) {
        sounds[i].lumpnum = A_GetSfxLumpNum(&sounds[i]);
        loadSfx(&sounds[i]);
    }
}

static snddevice_t s_devs[] = { SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS, SNDDEVICE_WAVEBLASTER,
                                SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32, SNDDEVICE_ADLIB, SNDDEVICE_GENMIDI };

sound_module_t DG_sound_module = {
    s_devs, sizeof(s_devs) / sizeof(s_devs[0]),
    A_Init, A_Shutdown, A_GetSfxLumpNum, A_Update, A_UpdateSoundParams,
    A_StartSound, A_StopSound, A_SoundIsPlaying, A_CacheSounds
};

// Music: see i_oplmus.c (DG_music_module, mixed via MUS_Mix).
