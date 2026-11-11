#ifndef APP_H
#define APP_H

#include <nds.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NTRACKS 8
#define NSCENES 4
#define NSTEPS  16
#define NROWS   12
#define MAX_CLIP_SAMPLES 64000

/* ---- dati ---- */
typedef struct {
    s8  *data;          /* PCM 8 bit con segno, mono */
    u32  len;           /* campioni (multiplo di 4) */
    u16  rate;          /* Hz */
    char name[16];
} Clip;

typedef struct {
    u8   vol;           /* 0..127 */
    u8   pan;           /* 0..127, 64 = centro */
    bool mute, solo;
    int  scene;         /* scena in riproduzione, -1 = ferma */
    int  voice;         /* id suono libnds, -1 = nessuno */
    int  rollScene;     /* clip usata dal piano roll */
    u8   roll[NSTEPS][NROWS];   /* durata nota (0 = vuoto), nello step di inizio */
    u8   fxLevel[6];    /* 0..8 : bitcrush, rate, filtro, wobble, (vinyl globale), pitch */
    bool fxOn[6];
} Track;

typedef struct {
    u8 micRate;         /* 0:8k 1:11k 2:16k */
    u8 clipSecs;        /* 0:1s 1:2s 2:3s */
    u8 bpmIdx;          /* indice tabella bpm */
} Settings;

extern Clip     clips[NTRACKS][NSCENES];
extern Track    tracks[NTRACKS];
extern Settings cfg;
extern int      selTrack, selScene;
extern bool     seqRunning;
extern int      seqStep;
extern bool     gVinylOn;
extern u8       gVinylLvl;
extern int      rollOct;

/* ---- audio.c ---- */
void audio_init(void);
void audio_update(void);
u16  cfg_micRate(void);
u8   cfg_secs(void);
u16  cfg_bpm(void);
u16  pitched_rate(u16 base, int semi);
void audio_launch(int t, int s);
void audio_stop(int t);
void audio_levels_changed(void);
void audio_clear_all(void);
void seq_toggle(void);
bool rec_start(int t, int s);
void rec_stop(void);
bool rec_active(void);
int  rec_level(void);       /* 0..127 */
int  rec_frames(void);
const char *rec_last(void);
bool fx_bounce(int t, int s);
bool clip_reverse(int t, int s);

/* ---- sdio.c ---- */
bool sd_init(void);
bool sd_ready(void);
bool sd_next_name(const char *prefix, char *out);
bool sd_save_wav(const char *name, const Clip *c);
bool sd_load_wav(const char *fname, Clip *c);
int  sd_list(char names[][16], int max);
void sd_save_cfg(void);
void sd_load_cfg(void);

/* ---- ui.c ---- */
void ui_init(void);
void ui_input(u32 down, u32 held, const touchPosition *tp);
void ui_draw(void);

#endif
