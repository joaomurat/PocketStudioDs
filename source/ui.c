#include "app.h"
#include "font.h"

/* ============================================================ colori */
#define HX(h) ARGB16(1, (((h) >> 16) & 255) >> 3, (((h) >> 8) & 255) >> 3, ((h) & 255) >> 3)
#define C_INK  HX(0x2a1a4a)
#define C_SHD  HX(0x1a0f33)
#define C_CR   HX(0xffeeb8)
#define C_BG   HX(0x3a2a6b)
#define C_DOT  HX(0x4b3a85)
#define C_PK   HX(0xff6ba8)
#define C_YL   HX(0xffd23f)
#define C_MT   HX(0x3ddc97)
#define C_SK   HX(0x4cc9f0)
#define C_OR   HX(0xff9f45)
#define C_VI   HX(0x9b6bff)
#define C_RD   HX(0xff4d6d)
#define C_LM   HX(0xb6f24a)
#define C_DM   HX(0xcbbd8c)
#define C_WH   HX(0xfffaf0)
#define C_TAB  HX(0x5a4896)
#define C_DK   HX(0x46347f)
#define C_HL   HX(0xfff8d8)
#define C_LG   HX(0xe0d3a6)

static const u16 TC[8] = { C_OR, C_YL, C_MT, C_SK, C_RD, C_VI, C_PK, C_LM };

typedef enum { PG_CLIP, PG_ROLL, PG_REC, PG_SD, PG_FX, PG_SET, PG_COUNT } Page;

/* ============================================================ stato */
static u16 *fbMain, *fbSub, *fb;
static bool dirty = true;
static bool inverted = false;           /* false: mixer in alto (tasti), pagine in basso (touch) */
static Page page = PG_CLIP;
static u32  frame = 0;
static char status[28] = "";

static bool fileMenu = false;
static int  openDd = -1;
static bool rollErase = false;
static int  rollLenIdx = 1;
static int  rollCx = 0, rollCy = 6;
static int  fxDrag = -1, fxDragY = 0, fxDragLvl = 0;
static char sdNames[24][16];
static int  sdCount = 0, sdOff = 0;

/* ============================================================ primitive */
static inline void px(int x, int y, u16 c) { if ((unsigned)x < 256 && (unsigned)y < 192) fb[y * 256 + x] = c; }

static void rect(int x, int y, int w, int h, u16 c)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > 256 ? 256 : x + w, y1 = y + h > 192 ? 192 : y + h;
    for (int j = y0; j < y1; j++) {
        u16 *p = fb + j * 256;
        for (int i = x0; i < x1; i++) p[i] = c;
    }
}

static void step(int x, int y, int w, int h, u16 c) { rect(x + 1, y, w - 2, h, c); rect(x, y + 1, w, h - 2, c); }

static void card(int x, int y, int w, int h, u16 f)
{
    step(x + 2, y + 2, w, h, C_SHD);
    step(x, y, w, h, C_INK);
    step(x + 2, y + 2, w - 4, h - 4, f);
    rect(x + 3, y + 3, w - 6, 1, C_HL);
}

static const unsigned char *glyph(char ch)
{
    static const unsigned char under[5] = { 0x40, 0x40, 0x40, 0x40, 0x40 };
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch == '_') return under;
    if (ch < 0x20 || ch > 0x5A) ch = 0x20;
    return font5x7[ch - 0x20];
}

static int tw(const char *s) { int n = (int)strlen(s); return n ? n * 6 - 1 : 0; }

static void text(int x, int y, const char *s, u16 c)
{
    for (; *s; s++, x += 6) {
        const unsigned char *g = glyph(*s);
        for (int cx = 0; cx < 5; cx++)
            for (int cy = 0; cy < 7; cy++)
                if (g[cx] & (1 << cy)) px(x + cx, y + cy, c);
    }
}

static void textc(int cx, int y, const char *s, u16 c) { text(cx - tw(s) / 2, y, s, c); }
static void textr(int rx, int y, const char *s, u16 c) { text(rx - tw(s), y, s, c); }

static void btn(int x, int y, int w, int h, u16 f, const char *t, u16 tc)
{
    step(x, y, w, h, C_INK);
    step(x + 1, y + 1, w - 2, h - 2, f);
    rect(x + 2, y + 2, w - 4, 1, C_HL);
    if (t) textc(x + w / 2, y + (h - 7) / 2 + 1, t, tc);
}

static void tri(int x, int y, u16 c) { rect(x, y, 7, 1, c); rect(x + 1, y + 1, 5, 1, c); rect(x + 2, y + 2, 3, 1, c); rect(x + 3, y + 3, 1, 1, c); }

static void dd(int x, int y, int w, int h, const char *t, bool open)
{
    step(x, y, w, h, C_INK);
    step(x + 1, y + 1, w - 2, h - 2, open ? C_YL : C_WH);
    text(x + 5, y + (h - 7) / 2 + 1, t, C_INK);
    tri(x + w - 11, y + h / 2 - 2, C_INK);
}

static void background(void)
{
    rect(0, 0, 256, 192, C_BG);
    for (int gx = 4; gx < 256; gx += 16)
        for (int gy = 4; gy < 192; gy += 16)
            rect(gx, gy, 2, 2, C_DOT);
}

static void tbar(const char *l, const char *r)
{
    card(2, 1, 252, 17, C_VI);
    text(9, 6, l, C_WH);
    textr(248, 6, r, C_YL);
}

static void setStatus(const char *s) { strncpy(status, s, sizeof status - 1); status[sizeof status - 1] = 0; dirty = true; }

static Clip *selClip(void) { return &clips[selTrack][selScene]; }

/* ============================================================ mascotte */
static void mascot(int x, int y)
{
    rect(x, y, 12, 2, C_PK); rect(x - 1, y + 4, 2, 6, C_PK); rect(x + 11, y + 4, 2, 6, C_PK);
    step(x, y + 2, 12, 11, C_INK); step(x + 1, y + 3, 10, 9, C_MT);
    rect(x + 3, y + 6, 2, 3, C_INK); rect(x + 7, y + 6, 2, 3, C_INK); rect(x + 5, y + 10, 2, 1, C_INK);
    rect(x + 2, y + 9, 1, 1, C_PK); rect(x + 9, y + 9, 1, 1, C_PK);
}

/* ============================================================ tab ed icone */
static void icon(int t, int x, int y, u16 c, u16 f)
{
    switch (t) {
    case 0: rect(x, y, 3, 3, c); rect(x + 4, y, 3, 3, c); rect(x, y + 4, 3, 3, c); rect(x + 4, y + 4, 3, 3, c); break;
    case 1: rect(x, y + 1, 4, 2, c); rect(x + 3, y + 3, 4, 2, c); rect(x + 1, y + 5, 3, 2, c); break;
    case 2: rect(x + 2, y, 3, 7, C_RD); rect(x, y + 2, 7, 3, C_RD); rect(x + 1, y + 1, 5, 5, C_RD); break;
    case 3: rect(x, y, 7, 7, c); rect(x + 1, y, 4, 3, f); rect(x + 2, y + 4, 3, 2, f); break;
    case 4: rect(x + 3, y, 1, 7, c); rect(x, y + 3, 7, 1, c); rect(x + 2, y + 2, 3, 3, c); break;
    default: rect(x, y + 1, 7, 1, c); rect(x, y + 3, 7, 1, c); rect(x, y + 5, 7, 1, c);
             rect(x + 1, y, 2, 3, c); rect(x + 4, y + 2, 2, 3, c); rect(x + 2, y + 4, 2, 3, c); break;
    }
}

static void drawTabs(void)
{
    static const char *const n[6] = { "CLIP", "ROLL", "REC", "SD", "FX", "SET" };
    for (int t = 0; t < 6; t++) {
        bool a = (t == (int)page);
        int x = 4 + t * 42, y = a ? 152 : 156, h = a ? 36 : 32;
        u16 f = a ? C_YL : C_TAB;
        step(x, y, 38, h, C_INK);
        step(x + 1, y + 1, 36, h - 2, f);
        rect(x + 3, y + 2, 32, 1, C_HL);
        icon(t, x + 16, y + 7, a ? C_INK : C_CR, f);
        textc(x + 19, y + h - 12, n[t], a ? C_INK : C_CR);
    }
}

/* ============================================================ mixer */
static void drawMixer(bool hint)
{
    background();
    card(2, 1, 252, 18, C_VI);
    mascot(6, 3 + ((frame / 20) & 1));
    text(26, 7, "POCKET STUDIO", C_WH);
    char b[24];
    snprintf(b, sizeof b, "BPM %d", cfg_bpm());
    text(118, 7, b, C_YL);
    btn(176, 4, 14, 12, seqRunning ? C_PK : C_MT, NULL, C_INK);
    if (seqRunning) { rect(179, 7, 3, 6, C_INK); rect(184, 7, 3, 6, C_INK); }
    else            { rect(180, 6, 2, 8, C_INK); rect(182, 7, 2, 6, C_INK); rect(184, 8, 1, 4, C_INK); }
    snprintf(b, sizeof b, "STEP %02d", seqRunning ? seqStep + 1 : 0);
    textr(248, 7, b, C_WH);

    for (int i = 0; i < NTRACKS; i++) {
        Track *k = &tracks[i];
        int x = 3 + i * 31, y = 24, cx = x + 11;
        bool sel = (i == selTrack);
        if (sel) step(x - 1, y - 1, 31, 148, C_PK);
        card(x, y, 29, 146, C_CR);
        rect(x + 3, y + 3, 23, 9, TC[i]);
        char num[4]; snprintf(num, sizeof num, "%d", i + 1);
        textc(x + 14, y + 4, num, C_INK);
        rect(cx - 2, y + 17, 4, 78, C_INK);

        int n = 0;
        bool audible = (k->voice >= 0) && !k->mute;
        if (audible) { n = k->vol * 12 / 127 - (int)((frame / 4 + i * 3) % 4); if (n < 0) n = 0; }
        for (int j = 0; j < 12; j++)
            rect(x + 21, y + 90 - j * 6, 4, 5, j < n ? (j < 7 ? C_MT : (j < 10 ? C_YL : C_RD)) : C_LG);

        int cap = y + 85 - k->vol * 62 / 127;
        step(cx - 8, cap, 16, 10, C_INK);
        step(cx - 7, cap + 1, 14, 8, TC[i]);
        rect(cx - 5, cap + 2, 10, 1, C_HL);

        rect(x + 4, y + 104, 21, 3, C_LG);
        rect(x + 4 + k->pan * 17 / 128, y + 101, 4, 9, C_INK);

        btn(x + 4, y + 114, 21, 13, k->mute ? C_RD : C_LG, "M", k->mute ? C_WH : C_INK);
        btn(x + 4, y + 130, 21, 13, k->solo ? C_YL : C_LG, "S", C_INK);
    }

    card(3, 173, 250, 16, C_CR);
    char info[40];
    snprintf(info, sizeof info, "T%d  VOL %d  %s", selTrack + 1, tracks[selTrack].vol,
             tracks[selTrack].mute ? "MUTE" : (tracks[selTrack].solo ? "SOLO" : "OK"));
    text(10, 178, info, C_INK);
    textr(246, 178, hint ? "SEL: SWAP" : "TOUCH", C_PK);
}

/* ============================================================ clip */
static void drawClips(void)
{
    background();
    card(2, 1, 252, 17, C_VI);
    btn(5, 3, 46, 13, C_PK, NULL, C_INK);
    text(11, 6, "FILE", C_INK);
    tri(38, 8, C_INK);
    text(64, 6, "CLIPS", C_WH);
    char b[16]; snprintf(b, sizeof b, "BPM %d", cfg_bpm());
    textr(248, 6, b, C_YL);

    for (int c = 0; c < 8; c++) {
        char t[4]; snprintf(t, sizeof t, "T%d", c + 1);
        textc(23 + c * 30, 24, t, c == selTrack ? C_WH : TC[c]);
    }
    for (int r = 0; r < NSCENES; r++) {
        for (int c = 0; c < NTRACKS; c++) {
            int x = 10 + c * 30, y = 33 + r * 24;
            bool has = clips[c][r].data != NULL;
            bool playing = has && tracks[c].scene == r;
            if (has) {
                step(x, y, 26, 21, C_INK);
                step(x + 1, y + 1, 24, 19, TC[c]);
                rect(x + 3, y + 2, 20, 1, C_HL);
            } else {
                step(x, y, 26, 21, C_TAB);
                step(x + 1, y + 1, 24, 19, C_DK);
                rect(x + 10, y + 10, 6, 1, C_DOT); rect(x + 13, y + 7, 1, 7, C_DOT);
            }
            if (playing && ((frame / 15) & 1) == 0) {
                rect(x + 2, y + 2, 22, 1, C_WH); rect(x + 2, y + 18, 22, 1, C_WH);
                rect(x + 2, y + 2, 1, 17, C_WH); rect(x + 23, y + 2, 1, 17, C_WH);
            }
            if (playing) { rect(x + 10, y + 6, 2, 9, C_INK); rect(x + 12, y + 7, 2, 7, C_INK); rect(x + 14, y + 8, 2, 5, C_INK); rect(x + 16, y + 9, 1, 3, C_INK); }
            if (c == selTrack && r == selScene) {
                rect(x - 3, y - 3, 32, 2, C_WH); rect(x - 3, y + 22, 32, 2, C_WH);
                rect(x - 3, y - 3, 2, 27, C_WH); rect(x + 27, y - 3, 2, 27, C_WH);
            }
        }
    }

    card(3, 131, 250, 18, C_CR);
    Clip *cl = selClip();
    char info[64];
    snprintf(info, sizeof info, "T%d S%d %s", selTrack + 1, selScene + 1, cl->data ? cl->name : "VUOTA");
    text(10, 137, info, C_INK);
    if (status[0]) textr(246, 137, status, C_PK);
    else if (cl->data) { snprintf(info, sizeof info, "%lu.%luS", (unsigned long)(cl->len / cl->rate), (unsigned long)((cl->len % cl->rate) * 10 / cl->rate)); textr(246, 137, info, C_PK); }

    drawTabs();

    if (fileMenu) {
        card(4, 19, 92, 58, C_CR);
        static const char *const it[4] = { "NEW", "SAVE CFG", "EXPORT WAV", "SETTINGS" };
        for (int i = 0; i < 4; i++) {
            int yy = 22 + i * 13;
            if (i == 1) rect(7, yy, 86, 12, C_YL);
            text(12, yy + 3, it[i], C_INK);
        }
    }
}

/* ============================================================ piano roll */
static const u8 lenTab[4] = { 1, 2, 4, 8 };
static const char *const lenOpts[4] = { "1/16", "1/8", "1/4", "1/2" };
static const char *const octOpts[3] = { "OCT 3", "OCT 4", "OCT 5" };
static const char *const micOpts[3] = { "8 KHZ", "11 KHZ", "16 KHZ" };
static const char *const secOpts[3] = { "1 S", "2 S", "3 S" };
static const char *const bpmOpts[6] = { "90", "100", "110", "120", "130", "140" };

static int ddCount(int id) { return id == 0 ? 4 : (id == 4 ? 6 : 3); }
static const char *ddOpt(int id, int i)
{
    switch (id) { case 0: return lenOpts[i]; case 1: return octOpts[i]; case 2: return micOpts[i]; case 3: return secOpts[i]; default: return bpmOpts[i]; }
}
static int ddCurrent(int id)
{
    switch (id) { case 0: return rollLenIdx; case 1: return rollOct + 1; case 2: return cfg.micRate; case 3: return cfg.clipSecs; default: return cfg.bpmIdx; }
}
static void ddSelect(int id, int i)
{
    switch (id) {
    case 0: rollLenIdx = i; break;
    case 1: rollOct = i - 1; break;
    case 2: cfg.micRate = (u8)i; break;
    case 3: cfg.clipSecs = (u8)i; break;
    default: cfg.bpmIdx = (u8)i; break;
    }
}
static void ddRect(int id, int *x, int *y, int *w, int *h)
{
    int n = ddCount(id);
    *h = n * 13 + 4;
    switch (id) {
    case 0: *x = 4;   *w = 80;  *y = 134 - *h; break;
    case 1: *x = 88;  *w = 80;  *y = 134 - *h; break;
    default: *x = 130; *w = 118; *y = 22 + (id - 2) * 19 + 15; break;
    }
    if (*y + *h > 150) *y = 150 - *h;
}
static void drawDdList(void)
{
    if (openDd < 0) return;
    int x, y, w, h;
    ddRect(openDd, &x, &y, &w, &h);
    card(x, y, w, h, C_CR);
    for (int i = 0; i < ddCount(openDd); i++) {
        int yy = y + 3 + i * 13;
        if (i == ddCurrent(openDd)) { rect(x + 3, yy, w - 6, 12, C_YL); text(x + 6, yy + 3, ">", C_INK); }
        text(x + 15, yy + 3, ddOpt(openDd, i), C_INK);
    }
}

static void drawRoll(void)
{
    background();
    Track *k = &tracks[selTrack];
    char b[24]; snprintf(b, sizeof b, "ROLL T%d", selTrack + 1);
    card(2, 1, 252, 17, C_VI);
    text(9, 6, b, C_WH);
    btn(150, 4, 48, 11, rollErase ? C_CR : C_YL, "PEN", C_INK);
    btn(202, 4, 48, 11, rollErase ? C_YL : C_CR, "ERASE", C_INK);

    static const u8 blk[12] = { 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0 };
    for (int i = 0; i < NROWS; i++) {
        int y = 22 + i * 9;
        rect(4, y, 24, 9, C_CR); rect(4, y + 8, 24, 1, C_DM);
        if (blk[i]) rect(4, y, 15, 9, C_INK);
        rect(30, y, 224, 9, blk[i] ? HX(0x3d2d70) : C_DOT);
    }
    text(6, 22 + 11 * 9 + 1, "C4", C_INK);
    for (int s = 0; s <= NSTEPS; s++) rect(30 + s * 14, 22, 1, 108, (s % 4 == 0) ? C_SHD : C_TAB);

    for (int s = 0; s < NSTEPS; s++)
        for (int r = 0; r < NROWS; r++)
            if (k->roll[s][r]) {
                int x = 30 + s * 14 + 1, y = 22 + r * 9 + 1, w = k->roll[s][r] * 14 - 1;
                rect(x, y, w, 8, C_INK); rect(x + 1, y + 1, w - 2, 6, TC[selTrack]); rect(x + 2, y + 1, w - 4, 1, C_HL);
            }
    if (seqRunning) rect(30 + ((seqStep + NSTEPS - 1) % NSTEPS) * 14, 22, 2, 108, C_PK);
    if (inverted) {
        int x = 30 + rollCx * 14, y = 22 + rollCy * 9;
        rect(x, y, 14, 1, C_WH); rect(x, y + 8, 14, 1, C_WH); rect(x, y, 1, 9, C_WH); rect(x + 13, y, 1, 9, C_WH);
    }

    dd(4, 134, 80, 15, lenOpts[rollLenIdx], openDd == 0);
    dd(88, 134, 80, 15, octOpts[rollOct + 1], openDd == 1);
    btn(172, 134, 80, 15, seqRunning ? C_PK : C_MT, seqRunning ? "STOP" : "PLAY", C_INK);
    drawTabs();
    drawDdList();
}

/* ============================================================ rec */
static void drawRec(void)
{
    background();
    char b[24]; snprintf(b, sizeof b, "REC T%d S%d", selTrack + 1, selScene + 1);
    tbar(b, sd_ready() ? "SD OK" : "NO SD");
    bool r = rec_active();
    card(60, 30, 136, 56, C_CR);
    btn(76, 40, 104, 36, r ? C_YL : C_RD, r ? "STOP" : "REC", r ? C_INK : C_WH);

    card(20, 98, 216, 18, C_CR);
    rect(26, 104, 204, 6, C_LG);
    int lv = rec_level() * 204 / 100; if (lv > 204) lv = 204;
    for (int x = 0; x < lv; x += 6) rect(26 + x, 104, 5, 6, x < 120 ? C_MT : (x < 168 ? C_YL : C_RD));

    int f = rec_frames();
    snprintf(b, sizeof b, "%d.%d / %d S", f / 60, (f % 60) * 10 / 60, cfg_secs());
    textc(128, 124, b, C_WH);
    if (rec_last()[0]) { snprintf(b, sizeof b, "SALVATO %s", rec_last()); textc(128, 136, b, C_YL); }
    drawTabs();
}

/* ============================================================ sd */
static void sdRefresh(void)
{
    sdCount = sd_list(sdNames, 24);
    if (sdOff >= sdCount) sdOff = 0;
}

static void drawSd(void)
{
    background();
    tbar("SD  /POCKET", sd_ready() ? "WAV" : "NO SD");
    if (sdCount == 0) textc(128, 70, sd_ready() ? "NESSUN FILE WAV" : "NESSUNA SD", C_WH);
    for (int i = 0; i < 7 && sdOff + i < sdCount; i++) {
        int y = 22 + i * 15;
        step(4, y, 248, 13, C_INK); step(5, y + 1, 246, 11, i % 2 ? C_CR : C_HL);
        text(10, y + 3, sdNames[sdOff + i], C_INK);
        textr(246, y + 3, "LOAD", C_PK);
    }
    btn(4, 130, 60, 14, C_CR, "PREV", C_INK);
    btn(192, 130, 60, 14, C_CR, "NEXT", C_INK);
    char b[24]; snprintf(b, sizeof b, "-> T%d S%d", selTrack + 1, selScene + 1);
    textc(128, 134, b, C_YL);
    drawTabs();
}

/* ============================================================ fx */
static const char *const fxName[6] = { "BITCRUSH", "RATE", "FILTRO", "WOBBLE", "VINYL", "PITCH" };
static const u16 fxCol[6] = { C_PK, C_SK, C_MT, C_OR, C_VI, C_YL };

static bool fxIsOn(int k)  { return k == 4 ? gVinylOn : tracks[selTrack].fxOn[k]; }
static int  fxLvl(int k)   { return k == 4 ? gVinylLvl : tracks[selTrack].fxLevel[k]; }
static void fxSetOn(int k, bool v)  { if (k == 4) gVinylOn = v; else tracks[selTrack].fxOn[k] = v; }
static void fxSetLvl(int k, int v)  { if (k == 4) gVinylLvl = (u8)v; else tracks[selTrack].fxLevel[k] = (u8)v; }

static void fxValue(int k, char *o, int n)
{
    int l = fxLvl(k);
    if (!fxIsOn(k)) { snprintf(o, n, "OFF"); return; }
    switch (k) {
    case 0: snprintf(o, n, "%d BIT", 8 - (l > 7 ? 7 : l)); break;
    case 1: snprintf(o, n, "/%d", 1 + (l > 7 ? 7 : l)); break;
    case 2: snprintf(o, n, "%d%%", l * 12); break;
    case 3: snprintf(o, n, "%d%%", l * 12); break;
    case 4: snprintf(o, n, "%d%%", l * 12); break;
    default: snprintf(o, n, "%+d ST", (l - 4) * 3); break;
    }
}

static void drawFx(void)
{
    background();
    char b[16]; snprintf(b, sizeof b, "FX T%d", selTrack + 1);
    tbar(b, "TOUCH");
    for (int k = 0; k < 6; k++) {
        int col = k % 3, row = k / 3, x = 4 + col * 84, y = 21 + row * 58;
        bool on = fxIsOn(k);
        card(x, y, 80, 54, C_CR);
        text(x + 6, y + 6, fxName[k], C_INK);
        rect(x + 65, y + 4, 9, 9, C_INK); rect(x + 66, y + 5, 7, 7, on ? C_MT : C_DM);
        int dx = x + 22, dy = y + 31;
        for (int j = -3; j <= 3; j++)
            for (int i = -3; i <= 3; i++) {
                int d2 = i * i + j * j;
                if (d2 <= 13) rect(dx + i * 3 - 1, dy + j * 3 - 1, 3, 3, d2 >= 7 ? C_INK : (on ? fxCol[k] : C_DM));
            }
        /* puntatore: 8 posizioni su 270 gradi */
        static const signed char ox[9] = { -1, -2, -2, -1, 0, 1, 2, 2, 1 };
        static const signed char oy[9] = {  1,  0, -1, -2, -2, -2, -1, 0, 1 };
        int l = fxLvl(k); if (l > 8) l = 8;
        rect(dx + ox[l] * 3 - 1, dy + oy[l] * 3 - 1, 3, 3, C_WH);
        char v[12]; fxValue(k, v, sizeof v);
        text(x + 42, y + 29, v, C_INK);
        for (int u = 0; u < 8; u++) rect(x + 8 + u * 8, y + 43, 7, 5, u < l ? (on ? fxCol[k] : C_DM) : HX(0xe8dcb0));
    }
    btn(4, 137, 122, 14, C_CR, "REVERSE", C_INK);
    btn(130, 137, 122, 14, C_MT, "BOUNCE > SD", C_INK);
    drawTabs();
}

/* ============================================================ impostazioni */
static void drawSet(void)
{
    background();
    mascot(6, 2);
    card(22, 1, 232, 17, C_VI);
    text(30, 6, "SETTINGS", C_WH);
    textr(248, 6, sd_ready() ? "SD OK" : "NO SD", C_YL);
    static const char *const lb[4] = { "MIC RATE", "CLIP LEN", "BPM", "SD FOLDER" };
    for (int i = 0; i < 4; i++) {
        int y = 22 + i * 19;
        step(4, y, 248, 16, C_INK); step(5, y + 1, 246, 14, C_CR);
        text(10, y + 5, lb[i], C_INK);
        if (i < 3) dd(130, y + 2, 118, 12, ddOpt(i + 2, ddCurrent(i + 2)), openDd == i + 2);
        else       text(134, y + 5, "/POCKET", C_INK);
    }
    btn(4, 137, 122, 14, C_MT, "SAVE", C_INK);
    btn(130, 137, 122, 14, C_RD, "RESET", C_WH);
    drawTabs();
    drawDdList();
}

/* ============================================================ azioni */
static void setPage(Page p)
{
    page = p; openDd = -1; fileMenu = false; fxDrag = -1; status[0] = 0;
    if (p == PG_SD) sdRefresh();
    dirty = true;
}

static void fileAction(int i)
{
    switch (i) {
    case 0: audio_clear_all(); setStatus("NUOVO"); break;
    case 1: sd_save_cfg(); setStatus("CFG SALVATA"); break;
    case 2: {
        Clip *c = selClip();
        if (!c->data) { setStatus("CLIP VUOTA"); break; }
        char nm[16];
        if (sd_ready() && sd_next_name("EXP", nm) && sd_save_wav(nm, c)) { char m[28]; snprintf(m, sizeof m, "%s.WAV", nm); setStatus(m); }
        else setStatus("ERRORE SD");
        break; }
    default: setPage(PG_SET); break;
    }
}

static void launchSelected(void)
{
    Clip *c = selClip();
    tracks[selTrack].rollScene = selScene;
    if (!c->data) return;
    if (tracks[selTrack].scene == selScene) audio_stop(selTrack);
    else audio_launch(selTrack, selScene);
}

static void rollToggle(int s, int r)
{
    Track *k = &tracks[selTrack];
    for (int s0 = s; s0 >= 0; s0--) {
        u8 len = k->roll[s0][r];
        if (len && s0 + len > s) { k->roll[s0][r] = 0; return; }
        if (s - s0 >= 8) break;
    }
    if (rollErase) return;
    int len = lenTab[rollLenIdx];
    if (s + len > NSTEPS) len = NSTEPS - s;
    k->roll[s][r] = (u8)len;
}

/* ============================================================ touch: pagine */
static void touchClip(int x, int y, bool tap)
{
    if (!tap) return;
    if (fileMenu) {
        if (x >= 4 && x < 96 && y >= 19 && y < 77) fileAction((y - 22) / 13 > 3 ? 3 : (y - 22) / 13);
        fileMenu = false; dirty = true;
        return;
    }
    if (x >= 5 && x < 51 && y >= 3 && y < 16) { fileMenu = true; dirty = true; return; }
    if (x >= 10 && y >= 33) {
        int c = (x - 10) / 30, r = (y - 33) / 24;
        if (c < NTRACKS && r < NSCENES && (x - 10) % 30 < 26 && (y - 33) % 24 < 21) {
            selTrack = c; selScene = r;
            status[0] = 0;
            launchSelected();
            dirty = true;
        }
    }
}

static bool ddTouch(int x, int y)
{
    int lx, ly, lw, lh;
    ddRect(openDd, &lx, &ly, &lw, &lh);
    if (x >= lx && x < lx + lw && y >= ly && y < ly + lh) {
        int i = (y - ly - 3) / 13;
        if (i < 0) i = 0;
        if (i >= ddCount(openDd)) i = ddCount(openDd) - 1;
        ddSelect(openDd, i);
        audio_levels_changed();
    }
    openDd = -1; dirty = true;
    return true;
}

static void touchRoll(int x, int y, bool tap)
{
    if (!tap) return;
    if (openDd >= 0) { ddTouch(x, y); return; }
    if (y >= 4 && y < 15) {
        if (x >= 150 && x < 198) rollErase = false; else if (x >= 202 && x < 250) rollErase = true;
        dirty = true; return;
    }
    if (y >= 134 && y < 149) {
        if (x >= 4 && x < 84) openDd = 0; else if (x >= 88 && x < 168) openDd = 1; else if (x >= 172 && x < 252) seq_toggle();
        dirty = true; return;
    }
    if (x >= 30 && x < 254 && y >= 22 && y < 130) { rollToggle((x - 30) / 14, (y - 22) / 9); dirty = true; }
}

static void touchRec(int x, int y, bool tap)
{
    if (!tap) return;
    if (x >= 76 && x < 180 && y >= 40 && y < 76) {
        if (rec_active()) rec_stop(); else rec_start(selTrack, selScene);
        dirty = true;
    }
}

static void touchSd(int x, int y, bool tap)
{
    if (!tap) return;
    if (y >= 130 && y < 144) {
        if (x < 64 && sdOff >= 7) sdOff -= 7;
        else if (x >= 192 && sdOff + 7 < sdCount) sdOff += 7;
        dirty = true; return;
    }
    if (y >= 22 && y < 22 + 7 * 15) {
        int i = (y - 22) / 15;
        if (sdOff + i < sdCount) {
            audio_stop(selTrack);
            setStatus(sd_load_wav(sdNames[sdOff + i], selClip()) ? "CARICATA" : "ERRORE WAV");
        }
    }
}

static void touchFx(int x, int y, bool tap)
{
    if (tap) {
        if (y >= 137 && y < 151) {
            if (x < 126) setStatus(clip_reverse(selTrack, selScene) ? "REVERSE" : "CLIP VUOTA");
            else setStatus(fx_bounce(selTrack, selScene) ? "BOUNCE SALVATO" : "CLIP VUOTA");
            return;
        }
        for (int k = 0; k < 6; k++) {
            int cx = 4 + (k % 3) * 84, cy = 21 + (k / 3) * 58;
            if (x >= cx && x < cx + 80 && y >= cy && y < cy + 54) {
                if (x >= cx + 60 && y < cy + 16) { fxSetOn(k, !fxIsOn(k)); audio_levels_changed(); }
                else { fxDrag = k; fxDragY = y; fxDragLvl = fxLvl(k); fxSetOn(k, true); }
                dirty = true; return;
            }
        }
    } else if (fxDrag >= 0) {
        int l = fxDragLvl + (fxDragY - y) / 6;
        if (l < 0) l = 0;
        if (l > 8) l = 8;
        if (l != fxLvl(fxDrag)) { fxSetLvl(fxDrag, l); audio_levels_changed(); dirty = true; }
    }
}

static void touchSet(int x, int y, bool tap)
{
    if (!tap) return;
    if (openDd >= 0) { ddTouch(x, y); return; }
    for (int i = 0; i < 3; i++) {
        int ry = 22 + i * 19;
        if (x >= 130 && x < 248 && y >= ry && y < ry + 16) { openDd = i + 2; dirty = true; return; }
    }
    if (y >= 137 && y < 151) {
        if (x < 126) { sd_save_cfg(); setStatus("SALVATE"); }
        else { cfg.micRate = 2; cfg.clipSecs = 1; cfg.bpmIdx = 3; setStatus("RESET"); }
        dirty = true;
    }
}

static void pageTouch(int x, int y, bool tap)
{
    if (tap && y >= 152) {
        int t = (x - 4) / 42;
        if (t < 0) t = 0;
        if (t > 5) t = 5;
        setPage((Page)t);
        return;
    }
    switch (page) {
    case PG_CLIP: touchClip(x, y, tap); break;
    case PG_ROLL: touchRoll(x, y, tap); break;
    case PG_REC:  touchRec(x, y, tap);  break;
    case PG_SD:   touchSd(x, y, tap);   break;
    case PG_FX:   touchFx(x, y, tap);   break;
    default:      touchSet(x, y, tap);  break;
    }
}

/* ============================================================ touch: mixer (layout invertito) */
static void mixerTouch(int x, int y, bool tap)
{
    static int dragTrack = -1;
    if (tap) {
        int i = (x - 3) / 31;
        dragTrack = -1;
        if (i < 0 || i > 7 || y < 24 || y > 170) return;
        selTrack = i;
        Track *k = &tracks[i];
        if (y >= 138 && y < 151)      { k->mute = !k->mute; audio_levels_changed(); }
        else if (y >= 154 && y < 167) { k->solo = !k->solo; audio_levels_changed(); }
        else if (y >= 40 && y < 120)  dragTrack = i;
        dirty = true;
    }
    if (dragTrack >= 0) {
        int v = (114 - y) * 127 / 62;
        if (v < 0) v = 0;
        if (v > 127) v = 127;
        if (tracks[dragTrack].vol != v) { tracks[dragTrack].vol = (u8)v; audio_levels_changed(); dirty = true; }
    }
}

/* ============================================================ tasti */
static void mixerKeys(u32 down, u32 held)
{
    Track *k = &tracks[selTrack];
    if (down & KEY_LEFT)  { selTrack = (selTrack + NTRACKS - 1) % NTRACKS; dirty = true; }
    if (down & KEY_RIGHT) { selTrack = (selTrack + 1) % NTRACKS; dirty = true; }
    if (held & (KEY_UP | KEY_DOWN)) {
        int v = k->vol + ((held & KEY_UP) ? 2 : -2);
        if (v < 0) v = 0;
        if (v > 127) v = 127;
        k->vol = (u8)v; audio_levels_changed(); dirty = true;
    }
    if (down & KEY_A) { k->mute = !k->mute; audio_levels_changed(); dirty = true; }
    if (down & KEY_B) { k->solo = !k->solo; audio_levels_changed(); dirty = true; }
}

static void pageKeys(u32 down)
{
    if (page == PG_CLIP) {
        if (down & KEY_LEFT)  selTrack = (selTrack + NTRACKS - 1) % NTRACKS;
        if (down & KEY_RIGHT) selTrack = (selTrack + 1) % NTRACKS;
        if (down & KEY_UP)    selScene = (selScene + NSCENES - 1) % NSCENES;
        if (down & KEY_DOWN)  selScene = (selScene + 1) % NSCENES;
        if (down & KEY_A)     launchSelected();
        if (down & (KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN | KEY_A)) dirty = true;
    } else if (page == PG_ROLL) {
        if (down & KEY_LEFT)  rollCx = (rollCx + NSTEPS - 1) % NSTEPS;
        if (down & KEY_RIGHT) rollCx = (rollCx + 1) % NSTEPS;
        if (down & KEY_UP)    rollCy = (rollCy + NROWS - 1) % NROWS;
        if (down & KEY_DOWN)  rollCy = (rollCy + 1) % NROWS;
        if (down & KEY_A)     rollToggle(rollCx, rollCy);
        if (down & KEY_B)     rollErase = !rollErase;
        if (down & (KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN | KEY_A | KEY_B)) dirty = true;
    }
}

/* ============================================================ API */
void ui_init(void)
{
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    int bm = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    int bs = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    fbMain = bgGetGfxPtr(bm);
    fbSub  = bgGetGfxPtr(bs);
    lcdMainOnTop();
    dirty = true;
}

void ui_input(u32 down, u32 held, const touchPosition *tp)
{
    frame++;

    if (down & KEY_SELECT) { inverted = !inverted; openDd = -1; fileMenu = false; dirty = true; }
    if (down & KEY_START)  { seq_toggle(); dirty = true; }
    if (down & KEY_R) setPage((Page)((page + 1) % PG_COUNT));
    if (down & KEY_L) setPage((Page)((page + PG_COUNT - 1) % PG_COUNT));

    if (!inverted) mixerKeys(down, held); else pageKeys(down);

    bool tapped = (down & KEY_TOUCH) != 0, hold = (held & KEY_TOUCH) != 0;
    if (tapped || hold) {
        if (!inverted) pageTouch(tp->px, tp->py, tapped);
        else           mixerTouch(tp->px, tp->py, tapped);
    }
    if (!hold) fxDrag = -1;

    if (frame % 15 == 0) dirty = true;
    if (rec_active() && frame % 3 == 0) dirty = true;
    if (seqRunning && frame % 4 == 0) dirty = true;
}

void ui_draw(void)
{
    if (!dirty) return;
    dirty = false;

    u16 *top = fbMain, *bot = fbSub;     /* il motore principale sta sullo schermo alto */

    fb = inverted ? bot : top;
    drawMixer(!inverted);

    fb = inverted ? top : bot;
    switch (page) {
    case PG_CLIP: drawClips(); break;
    case PG_ROLL: drawRoll();  break;
    case PG_REC:  drawRec();   break;
    case PG_SD:   drawSd();    break;
    case PG_FX:   drawFx();    break;
    default:      drawSet();   break;
    }
}
