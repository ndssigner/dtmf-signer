#include "ds_say.h"

#include <stdio.h>
#include <string.h>

#include "ds_core.h"
#include "ds_prompts.h"

enum { K_CLIP = 1, K_EAR, K_PAUSE, K_TONES };
enum { M_IDLE = 0, M_CLIP, M_SILENCE, M_EAR, M_TONES };

#define SAMPLES_PER_MS (DS_SAY_RATE / 1000u)
#define RAMP (5u * SAMPLES_PER_MS) /* 5 ms linear ramps */
#define AMP_FULL 16422             /* -6 dBFS peak */
#define AMP_LISTEN 4125            /* 12 dB below that */
#define NUMBER_CLIPS 40u

/* The sines of the earcons by the recurrence y[n] = c y[n-1] - y[n-2], c = 2 cos w
 * (Q30), started from sin w (Q30); 16 kHz. Values from round(x * 2^30). */
typedef struct {
    int32_t coef, sin;
} ds_say_osc;

enum { F_NONE, F_392, F_494, F_523, F_587, F_2000, F_2400 };
static const ds_say_osc osc[] = {
    {0, 0},
    {2122089480, 164637675},  /* 392 Hz */
    {2107201640, 206994727},  /* 494 Hz */
    {2102350474, 218979739},  /* 523 Hz */
    {2090680565, 245326717},  /* 587 Hz */
    {1518500250, 759250125},  /* 2000 Hz */
    {1262259218, 868675383},  /* 2400 Hz */
};

typedef struct {
    uint8_t osc;
    uint16_t ms;
} ds_say_seg;

#define EAR_SEGS 3
static const struct {
    ds_say_seg seg[EAR_SEGS];
    int32_t amp;
    const char *name;
} ear_def[] = {
    {{{0, 0}}, 0, ""},
    {{{F_494, 120}, {F_587, 120}}, AMP_FULL, "ok"},        /* DS_EAR_OK */
    {{{F_587, 120}, {F_392, 120}}, AMP_FULL, "fail"},      /* DS_EAR_FAIL */
    {{{F_523, 120}, {F_NONE, 80}, {F_523, 120}}, AMP_FULL, "warn"},
    {{{F_392, 150}}, AMP_LISTEN, "listen"},
    {{{F_2400, 40}}, AMP_FULL, "next"},
    {{{F_2000, 40}}, AMP_FULL, "back"},
};

/* ---- the log ---- */

static void log_text(ds_say *s, const char *head, const char *text, size_t max)
{
    uint8_t slot;
    char *t;
    int n;

    if (s->lcount == DS_SAY_LOG_N) { /* the oldest goes */
        s->lhead = (uint8_t)((s->lhead + 1u) % DS_SAY_LOG_N);
        s->lcount--;
    }
    slot = (uint8_t)((s->lhead + s->lcount) % DS_SAY_LOG_N);
    t = s->log[slot];
    n = snprintf(t, DS_SAY_TOKEN_MAX, "%s", head);
    if (text != NULL && n > 0) {
        size_t len = strlen(text);
        if (len > max) {
            len = max;
        }
        if ((size_t)n + 1u + len < DS_SAY_TOKEN_MAX) {
            t[n] = ' ';
            memcpy(t + n + 1, text, len);
            t[n + 1 + len] = '\0';
        }
    }
    s->lcount++;
}

int32_t ds_say_take_log(ds_say *s, char *out, size_t cap)
{
    size_t n;

    if (cap < DS_SAY_TOKEN_MAX) {
        return DS_ERR_SPACE;
    }
    if (s->lcount == 0) {
        return 0;
    }
    n = strlen(s->log[s->lhead]);
    memcpy(out, s->log[s->lhead], n + 1);
    s->lhead = (uint8_t)((s->lhead + 1u) % DS_SAY_LOG_N);
    s->lcount--;
    return (int32_t)n;
}

/* ---- the queue ---- */

void ds_say_init(ds_say *s, const ds_pack *pack)
{
    memset(s, 0, sizeof *s);
    s->pack = pack;
}

void ds_say_set_pack(ds_say *s, const ds_pack *pack)
{
    s->pack = pack;
}

void ds_say_clear(ds_say *s)
{
    s->qhead = 0;
    s->qcount = 0;
    s->mode = M_IDLE;
    s->silence = 0;
}

static int room(const ds_say *s, unsigned n)
{
    return s->qcount + n <= DS_SAY_QUEUE;
}

static void push(ds_say *s, uint8_t kind, uint16_t v, uint16_t gap, const char *keys)
{
    ds_say_item *it = &s->q[(s->qhead + s->qcount) % DS_SAY_QUEUE];
    it->kind = kind;
    it->v = v;
    it->gap = gap;
    it->keys = keys;
    s->qcount++;
}

int32_t ds_say_clip(ds_say *s, uint16_t id)
{
    if (id >= DS_P_COUNT) {
        return DS_ERR_ARG;
    }
    if (!room(s, 1)) {
        return DS_ERR_SPACE;
    }
    push(s, K_CLIP, id, 0, NULL);
    log_text(s, "say", ds_prompt_name[id], DS_SAY_TOKEN_MAX);
    return DS_OK;
}

int32_t ds_say_earcon(ds_say *s, int e)
{
    if (e < DS_EAR_OK || e > DS_EAR_BACK) {
        return DS_ERR_ARG;
    }
    if (!room(s, 1)) {
        return DS_ERR_SPACE;
    }
    push(s, K_EAR, (uint16_t)e, 0, NULL);
    log_text(s, "ear", ear_def[e].name, DS_SAY_TOKEN_MAX);
    return DS_OK;
}

int32_t ds_say_pause(ds_say *s, uint16_t ms)
{
    if (!room(s, 1)) {
        return DS_ERR_SPACE;
    }
    push(s, K_PAUSE, ms, 0, NULL);
    return DS_OK;
}

int32_t ds_say_number(ds_say *s, uint64_t n)
{
    uint16_t ids[NUMBER_CLIPS];
    char num[24];
    int32_t k;

    if (s->pack == NULL) {
        return DS_ERR_ARG;
    }
    k = ds_pack_number(s->pack, n, ids, NUMBER_CLIPS);
    if (k < 0) {
        return k;
    }
    if (!room(s, (unsigned)k)) {
        return DS_ERR_SPACE;
    }
    for (int32_t i = 0; i < k; i++) {
        push(s, K_CLIP, ids[i], 0, NULL);
    }
    snprintf(num, sizeof num, "%llu", (unsigned long long)n);
    log_text(s, "num", num, sizeof num);
    return DS_OK;
}

static int char_clip(char c)
{
    if (c >= '0' && c <= '9') {
        return DS_P_C_0 + (c - '0');
    }
    if (c >= 'a' && c <= 'z') {
        return DS_P_C_A + (c - 'a');
    }
    if (c >= 'A' && c <= 'Z') {
        return DS_P_C_A + (c - 'A');
    }
    return -1;
}

static int32_t chars(ds_say *s, const char *text, int grouped)
{
    char spoken[DS_SAY_QUEUE + 1];
    unsigned n = 0;

    if (text == NULL) {
        return DS_ERR_ARG;
    }
    for (const char *p = text; *p != '\0'; p++) {
        if (char_clip(*p) >= 0) {
            n++;
        }
    }
    if (n > DS_SAY_QUEUE || !room(s, n)) {
        return DS_ERR_SPACE;
    }
    n = 0;
    for (const char *p = text; *p != '\0'; p++) {
        int id = char_clip(*p);
        if (id < 0) {
            continue;
        }
        spoken[n++] = *p;
        push(s, K_CLIP, (uint16_t)id,
             (grouped && n % 4u == 0u) ? DS_SAY_GROUP_GAP_MS : DS_SAY_CHAR_GAP_MS, NULL);
    }
    spoken[n] = '\0';
    log_text(s, "chars", spoken, n);
    return DS_OK;
}

int32_t ds_say_chars(ds_say *s, const char *text)
{
    return chars(s, text, 0);
}

int32_t ds_say_chars_grouped(ds_say *s, const char *text)
{
    return chars(s, text, 1);
}

int32_t ds_say_secret_char(ds_say *s, char c)
{
    int id = char_clip(c);

    if (id < 0) {
        return DS_ERR_ARG;
    }
    if (!room(s, 1)) {
        return DS_ERR_SPACE;
    }
    push(s, K_CLIP, (uint16_t)id, DS_SAY_CHAR_GAP_MS, NULL);
    log_text(s, "say pin-char", NULL, 0);
    return DS_OK;
}

int32_t ds_say_tones(ds_say *s, const char *keys)
{
    if (keys == NULL) {
        return DS_ERR_ARG;
    }
    if (!room(s, 1)) {
        return DS_ERR_SPACE;
    }
    push(s, K_TONES, 0, 0, keys);
    log_text(s, "tones", keys, 60);
    return DS_OK;
}

int ds_say_busy(const ds_say *s)
{
    return s->mode != M_IDLE || s->qcount > 0;
}

/* ---- playing ---- */

/* The next decoded sample of the clip, 0 when it has no more. */
static int clip_next(ds_say *s, int16_t *v)
{
    if (s->bpos == s->blen) {
        uint32_t left = s->nib_total - s->nib_pos;
        uint32_t k = left < 32u ? left : 32u;
        if (k == 0) {
            return 0;
        }
        ds_adpcm_decode(&s->st, s->adpcm + s->nib_pos / 2u, k, s->blk);
        s->nib_pos += k;
        s->blen = (uint8_t)k;
        s->bpos = 0;
    }
    *v = s->blk[s->bpos++];
    return 1;
}

static void start_silence(ds_say *s, uint32_t ms)
{
    s->silence = ms * SAMPLES_PER_MS;
    s->mode = s->silence > 0 ? M_SILENCE : M_IDLE;
}

static void start_segment(ds_say *s)
{
    const ds_say_seg *g = &ear_def[s->ear].seg[s->seg];

    s->seg_pos = 0;
    s->seg_len = (uint32_t)g->ms * SAMPLES_PER_MS;
    s->coef = osc[g->osc].coef;
    s->y1 = 0;
    s->y2 = -(int32_t)(((int64_t)s->amp * 65536 * osc[g->osc].sin) >> 30);
}

/* Takes the next item off the queue and starts it; 0 when there is none. */
static int start_next(ds_say *s)
{
    while (s->qcount > 0) {
        ds_say_item it = s->q[s->qhead];
        s->qhead = (uint8_t)((s->qhead + 1u) % DS_SAY_QUEUE);
        s->qcount--;
        switch (it.kind) {
        case K_PAUSE:
            start_silence(s, it.v);
            break;
        case K_CLIP: {
            const uint8_t *adpcm;
            uint32_t bytes;
            ds_adpcm st;
            if (s->pack == NULL || ds_pack_clip(s->pack, it.v, &adpcm, &bytes, &st) != DS_OK ||
                bytes == 0) {
                continue;
            }
            s->adpcm = adpcm;
            s->nib_total = bytes * 2u;
            s->nib_pos = 0;
            s->st = st;
            s->blen = s->bpos = 0;
            s->phase = 0;
            s->gap_after = it.gap;
            if (!clip_next(s, &s->s0)) {
                continue;
            }
            s->mode = M_CLIP;
            break;
        }
        case K_EAR:
            s->ear = (uint8_t)it.v;
            s->seg = 0;
            s->amp = ear_def[s->ear].amp;
            start_segment(s);
            s->mode = M_EAR;
            break;
        case K_TONES:
            ut_synth_init(&s->synth, it.keys, DS_SAY_RATE, UT_CABLE_TONE_MS, UT_CABLE_GAP_MS, 400);
            s->mode = M_TONES;
            break;
        default:
            continue;
        }
        if (s->mode != M_IDLE) {
            return 1;
        }
    }
    return 0;
}

static int16_t clip_sample(ds_say *s)
{
    int16_t out = s->s0;

    if (s->phase == 0) {
        s->phase = 1;
    } else {
        int16_t b;
        if (clip_next(s, &b)) {
            out = (int16_t)(((int)s->s0 + (int)b) >> 1);
            s->s0 = b;
        } else {
            s->mode = M_IDLE; /* the last sample is held for its midpoint */
            start_silence(s, s->gap_after);
        }
        s->phase = 0;
    }
    return out;
}

/* One sample of the earcon, with the 5 ms ramps at both ends of each tone. */
static int16_t ear_sample(ds_say *s)
{
    const ds_say_seg *g = &ear_def[s->ear].seg[s->seg];
    int32_t v = 0;

    if (g->osc != F_NONE) {
        int64_t y = (((int64_t)s->coef * s->y1 + (1 << 29)) >> 30) - s->y2;
        uint32_t i = s->seg_pos, left = s->seg_len - 1u - s->seg_pos;
        uint32_t r = i < left ? i : left;
        s->y2 = s->y1;
        s->y1 = (int32_t)y;
        v = (int32_t)(y >> 16);
        if (r < RAMP) {
            v = (int32_t)((int64_t)v * (int64_t)(r + 1u) / (int64_t)(RAMP + 1u));
        }
    }
    s->seg_pos++;
    if (s->seg_pos == s->seg_len) {
        s->seg++;
        if (s->seg == EAR_SEGS || ear_def[s->ear].seg[s->seg].ms == 0) {
            s->mode = M_IDLE;
        } else {
            start_segment(s);
        }
    }
    return (int16_t)v;
}

size_t ds_say_read(ds_say *s, int16_t *out, size_t n)
{
    size_t i = 0;

    while (i < n) {
        if (s->mode == M_IDLE && !start_next(s)) {
            break;
        }
        switch (s->mode) {
        case M_CLIP:
            out[i++] = clip_sample(s);
            break;
        case M_SILENCE:
            out[i++] = 0;
            if (--s->silence == 0) {
                s->mode = M_IDLE;
            }
            break;
        case M_EAR:
            out[i++] = ear_sample(s);
            break;
        case M_TONES: {
            size_t k = ut_synth_read(&s->synth, out + i, n - i);
            i += k;
            if (k == 0 || s->synth.stage == 4) {
                s->mode = M_IDLE;
            }
            break;
        }
        default:
            s->mode = M_IDLE;
            break;
        }
    }
    if (i < n) {
        memset(out + i, 0, (n - i) * sizeof out[0]);
    }
    return n;
}
