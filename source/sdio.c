#include "app.h"
#include <fat.h>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

static bool sdOk = false;

bool sd_init(void)
{
    sdOk = fatInitDefault();
    if (sdOk) mkdir("/POCKET", 0777);
    return sdOk;
}

bool sd_ready(void) { return sdOk; }

bool sd_next_name(const char *prefix, char *out)
{
    for (int i = 1; i < 1000; i++) {
        char p[48];
        snprintf(out, 16, "%s%03d", prefix, i);
        snprintf(p, sizeof p, "/POCKET/%s.WAV", out);
        FILE *f = fopen(p, "rb");
        if (!f) return true;
        fclose(f);
    }
    return false;
}

typedef struct __attribute__((packed)) {
    char riff[4]; u32 size; char wave[4];
    char fmt[4];  u32 fmtSize; u16 format; u16 ch; u32 rate; u32 byteRate; u16 align; u16 bits;
    char data[4]; u32 dataSize;
} WavHdr;

bool sd_save_wav(const char *name, const Clip *c)
{
    if (!sdOk || !c->data) return false;
    char p[48];
    snprintf(p, sizeof p, "/POCKET/%s.WAV", name);
    FILE *f = fopen(p, "wb");
    if (!f) return false;

    WavHdr h;
    memcpy(h.riff, "RIFF", 4); h.size = 36 + c->len; memcpy(h.wave, "WAVE", 4);
    memcpy(h.fmt, "fmt ", 4);  h.fmtSize = 16; h.format = 1; h.ch = 1;
    h.rate = c->rate; h.byteRate = c->rate; h.align = 1; h.bits = 8;
    memcpy(h.data, "data", 4); h.dataSize = c->len;
    fwrite(&h, sizeof h, 1, f);

    u8 buf[512];
    for (u32 i = 0; i < c->len; i += sizeof buf) {
        u32 n = c->len - i;
        if (n > sizeof buf) n = sizeof buf;
        for (u32 j = 0; j < n; j++) buf[j] = (u8)(c->data[i + j] ^ 0x80);   /* WAV 8 bit e' senza segno */
        fwrite(buf, 1, n, f);
    }
    fclose(f);
    return true;
}

static u32 rd32(const u8 *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((u32)b[3] << 24); }
static u16 rd16(const u8 *b) { return (u16)(b[0] | (b[1] << 8)); }

bool sd_load_wav(const char *fname, Clip *c)
{
    if (!sdOk) return false;
    char p[64];
    snprintf(p, sizeof p, "/POCKET/%s", fname);
    FILE *f = fopen(p, "rb");
    if (!f) return false;

    u8 h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fclose(f); return false; }

    u16 fmt = 0, ch = 0, bits = 0;
    u32 rate = 0, dsize = 0;
    bool gotFmt = false;
    for (;;) {
        u8 ck[8];
        if (fread(ck, 1, 8, f) != 8) break;
        u32 sz = rd32(ck + 4);
        if (!memcmp(ck, "fmt ", 4)) {
            u8 b[16];
            if (sz < 16 || fread(b, 1, 16, f) != 16) break;
            fmt = rd16(b); ch = rd16(b + 2); rate = rd32(b + 4); bits = rd16(b + 14);
            gotFmt = true;
            if (sz > 16) fseek(f, (long)(sz - 16 + (sz & 1)), SEEK_CUR);
        } else if (!memcmp(ck, "data", 4)) {
            dsize = sz;
            break;
        } else {
            fseek(f, (long)(sz + (sz & 1)), SEEK_CUR);
        }
    }
    if (!gotFmt || !dsize || fmt != 1 || (bits != 8 && bits != 16) || ch < 1 || ch > 2 || rate == 0 || rate > 48000) {
        fclose(f);
        return false;
    }

    u32 fb = (u32)(bits / 8) * ch;
    u32 frames = dsize / fb;
    if (frames > MAX_CLIP_SAMPLES) frames = MAX_CLIP_SAMPLES;
    frames &= ~3u;
    if (frames < 4) { fclose(f); return false; }

    s8 *d = (s8 *)malloc(frames);
    if (!d) { fclose(f); return false; }

    u8 buf[1024];
    u32 done = 0;
    u32 perChunk = sizeof buf / fb;
    while (done < frames) {
        u32 n = frames - done;
        if (n > perChunk) n = perChunk;
        if (fread(buf, fb, n, f) != n) break;
        for (u32 i = 0; i < n; i++) {
            const u8 *s = buf + i * fb;
            d[done + i] = (bits == 8) ? (s8)(s[0] ^ 0x80) : (s8)(((s16)rd16(s)) >> 8);
        }
        done += n;
    }
    fclose(f);
    if (done < 4) { free(d); return false; }
    done &= ~3u;

    free(c->data);
    c->data = d;
    c->len  = done;
    c->rate = (u16)rate;
    DC_FlushRange(d, done);

    int n = 0;
    while (fname[n] && fname[n] != '.' && n < 15) { c->name[n] = fname[n]; n++; }
    c->name[n] = 0;
    return true;
}

int sd_list(char names[][16], int max)
{
    if (!sdOk) return 0;
    DIR *d = opendir("/POCKET");
    if (!d) return 0;
    struct dirent *e;
    int n = 0;
    while (n < max && (e = readdir(d)) != NULL) {
        size_t l = strlen(e->d_name);
        if (l > 4 && l < 16 && strcasecmp(e->d_name + l - 4, ".WAV") == 0)
            strcpy(names[n++], e->d_name);
    }
    closedir(d);
    return n;
}

#define CFG_MAGIC 0x50534331u
typedef struct { u32 magic; Settings s; } CfgFile;

void sd_save_cfg(void)
{
    if (!sdOk) return;
    FILE *f = fopen("/POCKET/SETTINGS.CFG", "wb");
    if (!f) return;
    CfgFile c = { CFG_MAGIC, cfg };
    fwrite(&c, sizeof c, 1, f);
    fclose(f);
}

void sd_load_cfg(void)
{
    if (!sdOk) return;
    FILE *f = fopen("/POCKET/SETTINGS.CFG", "rb");
    if (!f) return;
    CfgFile c;
    if (fread(&c, sizeof c, 1, f) == 1 && c.magic == CFG_MAGIC &&
        c.s.micRate < 3 && c.s.clipSecs < 3 && c.s.bpmIdx < 6)
        cfg = c.s;
    fclose(f);
}
