#include "app.h"
#include <malloc.h>

Clip     clips[NTRACKS][NSCENES];
Track    tracks[NTRACKS];
Settings cfg = { 2, 1, 3 };
int      selTrack = 0, selScene = 0;
bool     seqRunning = false;
int      seqStep = 0;
bool     gVinylOn = false;
u8       gVinylLvl = 3;
int      rollOct = 0;

static const u16 micRates[3]   = { 8000, 11025, 16000 };
static const u8  clipSecsTab[3] = { 1, 2, 3 };
static const u16 bpmTab[6]     = { 90, 100, 110, 120, 130, 140 };

u16 cfg_micRate(void) { return micRates[cfg.micRate % 3]; }
u8  cfg_secs(void)    { return clipSecsTab[cfg.clipSecs % 3]; }
u16 cfg_bpm(void)     { return bpmTab[cfg.bpmIdx % 6]; }

/* ------------------------------------------------------------ pitch */
static u32 ratio1000(int semi)
{
    static const u16 t[12] = { 1000, 1059, 1122, 1189, 1260, 1335, 1414, 1498, 1587, 1682, 1782, 1888 };
    int o = 0;
    while (semi < 0)   { semi += 12; o--; }
    while (semi >= 12) { semi -= 12; o++; }
    u32 r = t[semi];
    if (o > 0) r <<= o; else if (o < 0) r >>= -o;
    return r;
}

u16 pitched_rate(u16 base, int semi)
{
    u32 f = ((u32)base * ratio1000(semi)) / 1000;
    if (f > 65000) f = 65000;
    if (f < 2000)  f = 2000;
    return (u16)f;
}

/* ------------------------------------------------------------ mixer */
static bool anySolo(void)
{
    for (int i = 0; i < NTRACKS; i++) if (tracks[i].solo) return true;
    return false;
}

static u8 eff_vol(int t)
{
    Track *k = &tracks[t];
    if (k->mute) return 0;
    if (anySolo() && !k->solo) return 0;
    return k->vol;
}

static int track_semi(int t)
{
    Track *k = &tracks[t];
    return k->fxOn[5] ? ((int)k->fxLevel[5] - 4) * 3 : 0;
}

/* ------------------------------------------------------------ vinile */
#define VINYL_LEN 16384
static s8  *vinyl = NULL;
static int  vinylVoice = -1;

/* ------------------------------------------------------------ sequencer */
typedef struct { int voice; int left; } ActiveNote;
static ActiveNote act[24];
static int seqAcc = 0;
static u32 frameCnt = 0;

/* ------------------------------------------------------------ registrazione */
static u8 *recBuf = NULL;
static u32 recBytes = 0;
static volatile bool micDone = false;
static bool recording = false;
static int  recFrames = 0, recT = 0, recS = 0;
static u16  recRate = 16000;
static char lastName[16] = "";

static void micCb(void *buf, int len) { (void)buf; (void)len; micDone = true; }

void audio_init(void)
{
    soundEnable();

    for (int i = 0; i < NTRACKS; i++) {
        Track *k = &tracks[i];
        memset(k, 0, sizeof(*k));
        k->vol = 100; k->pan = 64; k->scene = -1; k->voice = -1;
        static const u8 defLvl[6] = { 3, 3, 3, 2, 3, 4 };
        memcpy(k->fxLevel, defLvl, 6);
    }
    for (int i = 0; i < 24; i++) act[i].voice = -1;

    recBuf = (u8 *)memalign(32, 3 * 16000 + 64);

    vinyl = (s8 *)memalign(32, VINYL_LEN);
    if (vinyl) {
        for (int i = 0; i < VINYL_LEN; i++) {
            int r = rand();
            vinyl[i] = ((r & 63) == 0) ? (s8)(((r >> 8) & 127) - 64) : (s8)(((r >> 8) & 3) - 2);
        }
        DC_FlushRange(vinyl, VINYL_LEN);
        vinylVoice = soundPlaySample(vinyl, SoundFormat_8Bit, VINYL_LEN, 11025, 0, 64, true, 0);
    }
}

void audio_levels_changed(void)
{
    for (int t = 0; t < NTRACKS; t++) {
        Track *k = &tracks[t];
        if (k->voice < 0 || k->scene < 0) continue;
        soundSetVolume(k->voice, eff_vol(t));
        soundSetPan(k->voice, k->pan);
        soundSetFreq(k->voice, pitched_rate(clips[t][k->scene].rate, track_semi(t)));
    }
    if (vinylVoice >= 0) soundSetVolume(vinylVoice, gVinylOn ? gVinylLvl * 9 : 0);
}

void audio_stop(int t)
{
    Track *k = &tracks[t];
    if (k->voice >= 0) { soundKill(k->voice); k->voice = -1; }
    k->scene = -1;
}

void audio_launch(int t, int s)
{
    Clip *c = &clips[t][s];
    Track *k = &tracks[t];
    if (!c->data || c->len < 4) return;
    audio_stop(t);
    k->voice = soundPlaySample(c->data, SoundFormat_8Bit, c->len,
                               pitched_rate(c->rate, track_semi(t)),
                               eff_vol(t), k->pan, true, 0);
    k->scene = (k->voice >= 0) ? s : -1;
}

void audio_clear_all(void)
{
    for (int t = 0; t < NTRACKS; t++) {
        audio_stop(t);
        for (int s = 0; s < NSCENES; s++) {
            free(clips[t][s].data);
            memset(&clips[t][s], 0, sizeof(Clip));
        }
        memset(tracks[t].roll, 0, sizeof(tracks[t].roll));
    }
}

/* ------------------------------------------------------------ sequencer */
static void seq_kill_notes(void)
{
    for (int i = 0; i < 24; i++)
        if (act[i].voice >= 0) { soundKill(act[i].voice); act[i].voice = -1; }
}

void seq_toggle(void)
{
    seqRunning = !seqRunning;
    seqStep = 0;
    seqAcc = 3600;
    if (!seqRunning) seq_kill_notes();
}

static void seq_trigger(int step)
{
    for (int i = 0; i < 24; i++) {
        if (act[i].voice >= 0 && --act[i].left <= 0) { soundKill(act[i].voice); act[i].voice = -1; }
    }
    for (int t = 0; t < NTRACKS; t++) {
        Track *k = &tracks[t];
        Clip *c = &clips[t][k->rollScene];
        if (!c->data) continue;
        for (int r = 0; r < NROWS; r++) {
            u8 len = k->roll[step][r];
            if (!len) continue;
            int slot = -1;
            for (int i = 0; i < 24; i++) if (act[i].voice < 0) { slot = i; break; }
            if (slot < 0) return;
            int semi = (11 - r) + rollOct * 12 + track_semi(t);
            int v = soundPlaySample(c->data, SoundFormat_8Bit, c->len,
                                    pitched_rate(c->rate, semi), eff_vol(t), k->pan, false, 0);
            if (v >= 0) { act[slot].voice = v; act[slot].left = len; }
        }
    }
}

static void seq_update(void)
{
    if (!seqRunning) return;
    seqAcc += cfg_bpm() * 4;
    while (seqAcc >= 3600) {
        seqAcc -= 3600;
        seq_trigger(seqStep);
        seqStep = (seqStep + 1) % NSTEPS;
    }
}

/* ------------------------------------------------------------ wobble */
static void wobble_update(void)
{
    if (frameCnt & 1) return;
    int ph = (frameCnt >> 1) & 31;
    int tri = (ph < 16) ? ph - 8 : 24 - ph;
    for (int t = 0; t < NTRACKS; t++) {
        Track *k = &tracks[t];
        if (k->voice < 0 || k->scene < 0 || !k->fxOn[3]) continue;
        u32 base = pitched_rate(clips[t][k->scene].rate, track_semi(t));
        int dev = (int)(base * tri * k->fxLevel[3]) / (64 * 20);
        soundSetFreq(k->voice, (u16)(base + dev));
    }
}

/* ------------------------------------------------------------ registrazione */
static void rec_finish(u32 n)
{
    n &= ~3u;
    if (n < 64) return;
    Clip *c = &clips[recT][recS];
    DC_InvalidateRange(recBuf, 3 * 16000 + 64);

    s8 *d = (s8 *)malloc(n);
    if (!d) return;
    for (u32 i = 0; i < n; i++) d[i] = (s8)(recBuf[i] ^ 0x80);

    audio_stop(recT);
    free(c->data);
    c->data = d;
    c->len  = n;
    c->rate = recRate;
    DC_FlushRange(d, n);

    char nm[16];
    if (sd_ready() && sd_next_name("REC", nm)) {
        strcpy(c->name, nm);
        sd_save_wav(nm, c);
        strcpy(lastName, nm);
    } else {
        strcpy(c->name, "REC");
        strcpy(lastName, "NO SD");
    }
}

bool rec_start(int t, int s)
{
    if (recording || !recBuf) return false;
    recT = t; recS = s;
    recRate = cfg_micRate();
    recBytes = ((u32)recRate * cfg_secs() + 63) & ~63u;
    if (recBytes > 3 * 16000) recBytes = 3 * 16000;
    micDone = false;
    recFrames = 0;
    soundMicRecord(recBuf, recBytes, MicFormat_8Bit, recRate, micCb);
    recording = true;
    return true;
}

void rec_stop(void)
{
    if (!recording) return;
    soundMicOff();
    recording = false;
    u32 n = (u32)recFrames * recRate / 60;
    if (n > recBytes) n = recBytes;
    rec_finish(n);
}

bool rec_active(void) { return recording; }
int  rec_frames(void) { return recFrames; }
const char *rec_last(void) { return lastName; }

int rec_level(void)
{
    if (!recording) return 0;
    u32 idx = (u32)recFrames * recRate / 60;
    if (idx + 64 > recBytes) idx = recBytes > 64 ? recBytes - 64 : 0;
    idx &= ~31u;
    DC_InvalidateRange(recBuf + idx, 64);
    int peak = 0;
    for (int i = 0; i < 64; i++) {
        int v = (int)recBuf[idx + i] - 128;
        if (v < 0) v = -v;
        if (v > peak) peak = v;
    }
    return peak;
}

/* ------------------------------------------------------------ effetti */
bool fx_bounce(int t, int s)
{
    Clip *c = &clips[t][s];
    Track *k = &tracks[t];
    if (!c->data) return false;

    u32 n = c->len;
    s8 *o = (s8 *)malloc(n);
    if (!o) return false;

    int bits = k->fxOn[0] ? k->fxLevel[0] : 0;
    if (bits > 7) bits = 7;
    int div = k->fxOn[1] ? 1 + k->fxLevel[1] : 1;
    if (div > 8) div = 8;
    int a = k->fxOn[2] ? 256 - k->fxLevel[2] * 28 : 256;
    int y = 0, hold = 0;

    for (u32 i = 0; i < n; i++) {
        if ((int)(i % div) == 0) hold = c->data[i];
        y += ((hold - y) * a) >> 8;
        int v = y;
        if (bits) v = (v >> bits) << bits;
        if (v > 127) v = 127;
        if (v < -128) v = -128;
        o[i] = (s8)v;
    }

    audio_stop(t);
    free(c->data);
    c->data = o;
    DC_FlushRange(o, n);

    char nm[16];
    if (sd_ready() && sd_next_name("BNC", nm)) {
        strcpy(c->name, nm);
        sd_save_wav(nm, c);
        strcpy(lastName, nm);
    }
    return true;
}

bool clip_reverse(int t, int s)
{
    Clip *c = &clips[t][s];
    if (!c->data || c->len < 2) return false;
    audio_stop(t);
    for (u32 i = 0, j = c->len - 1; i < j; i++, j--) {
        s8 tmp = c->data[i]; c->data[i] = c->data[j]; c->data[j] = tmp;
    }
    DC_FlushRange(c->data, c->len);
    return true;
}

/* ------------------------------------------------------------ per frame */
void audio_update(void)
{
    frameCnt++;

    if (recording) {
        recFrames++;
        if (micDone) {
            soundMicOff();
            recording = false;
            rec_finish(recBytes);
        }
    }

    seq_update();
    wobble_update();
}
