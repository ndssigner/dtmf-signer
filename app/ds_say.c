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

/* The sines of the motifs by the recurrence y[n] = c y[n-1] - y[n-2], c = 2 cos w
 * (Q30), started from sin w (Q30); 16 kHz. Values from round(x * 2^30). */
typedef struct {
    uint16_t hz;
    int32_t coef, sin;
} ds_say_osc;

static const ds_say_osc osc[] = {
    {330, 2129476719, 138757812},
    {392, 2122089480, 164637675},
    {440, 2115506169, 184607466},
    {494, 2107201640, 206994727},
    {523, 2102350474, 218979739},
    {587, 2090680565, 245326717},
    {659, 2075973886, 274781024},
    {740, 2057445885, 307653396},
    {880, 2020526066, 363717072},
    {988, 1987866817, 406223934},
    {1760, 1654664589, 684428797},
};

/* The motifs: single notes, one frequency at a time, hz 0 a rest. Every note
 * must be in osc[]. */
#define EAR_SEGS 3
static const struct {
    uint8_t n;
    uint16_t hz[EAR_SEGS], ms[EAR_SEGS];
    int32_t amp;
    const char *name;
} motif_def[] = {
    {0, {0}, {0}, 0, ""},
    {2, {440, 659}, {150, 200}, AMP_FULL, "notice"},               /* DS_EAR_NOTICE: 2 rising */
    {1, {1760}, {300}, AMP_FULL, "turn"},                          /* DS_EAR_TURN: 1 high */
    {1, {988}, {100}, AMP_FULL, "option"},                         /* DS_EAR_OPTION: 1 mid, short */
    {3, {659, 523, 440}, {110, 110, 110}, AMP_LISTEN, "receive"},  /* DS_EAR_RECEIVE: 3 falling, soft */
    {3, {392, 494, 587}, {110, 110, 110}, AMP_FULL, "send"},       /* DS_EAR_SEND: 3 rising */
    {3, {587, 740, 880}, {120, 120, 220}, AMP_FULL, "ok"},         /* DS_EAR_OK: major arpeggio up */
    {2, {392, 330}, {150, 300}, AMP_FULL, "fail"},                 /* DS_EAR_FAIL: 2 falling, minor third */
    {2, {587, 392}, {130, 220}, AMP_FULL, "close"},                /* DS_EAR_CLOSE: 2 falling, fifth */
};

int32_t ds_say_motif(int e, const uint16_t **hz, const uint16_t **ms, uint8_t *n)
{
    if (e < DS_EAR_NOTICE || e > DS_EAR_CLOSE || hz == NULL || ms == NULL || n == NULL) {
        return DS_ERR_ARG;
    }
    *hz = motif_def[e].hz;
    *ms = motif_def[e].ms;
    *n = motif_def[e].n;
    return DS_OK;
}

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
    /* the queue and the clip playing may be a PIN's characters: wiped */
    ds_wipe(s->q, sizeof s->q);
    s->qhead = 0;
    s->qcount = 0;
    s->mode = M_IDLE;
    s->silence = 0;
    s->gap_after = 0;
    s->frames = NULL;
    s->clip_bytes = 0;
    s->clip_pos = 0;
    ds_wipe(&s->dec, sizeof s->dec);
    ds_wipe(s->blk, sizeof s->blk);
    s->blen = 0;
    s->bpos = 0;
    s->phase = 0;
    s->s0 = 0;
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
    if (e < DS_EAR_NOTICE || e > DS_EAR_CLOSE) {
        return DS_ERR_ARG;
    }
    if (!room(s, 1)) {
        return DS_ERR_SPACE;
    }
    push(s, K_EAR, (uint16_t)e, 0, NULL);
    log_text(s, "ear", motif_def[e].name, DS_SAY_TOKEN_MAX);
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
        uint8_t len;
        if (s->clip_pos >= s->clip_bytes) {
            return 0;
        }
        /* ds_pack_open checked that the frames tile the clip; a frame the
         * decoder refuses plays as silence */
        len = s->frames[s->clip_pos];
        (void)ds_speex_frame(&s->dec, s->frames + s->clip_pos + 1u, len, s->blk);
        s->clip_pos += 1u + len;
        s->blen = DS_SPEEX_FRAME;
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
    uint16_t hz = motif_def[s->ear].hz[s->seg];
    const ds_say_osc *o = NULL;

    for (size_t i = 0; hz != 0 && i < sizeof osc / sizeof osc[0]; i++) {
        if (osc[i].hz == hz) {
            o = &osc[i];
        }
    }
    s->seg_pos = 0;
    s->seg_len = (uint32_t)motif_def[s->ear].ms[s->seg] * SAMPLES_PER_MS;
    s->coef = o != NULL ? o->coef : 0;
    s->y1 = 0;
    s->y2 = o != NULL ? -(int32_t)(((int64_t)s->amp * 65536 * o->sin) >> 30) : 0;
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
            const uint8_t *frames;
            uint32_t bytes;
            if (s->pack == NULL || ds_pack_clip(s->pack, it.v, &frames, &bytes) != DS_OK ||
                bytes == 0 || ds_speex_init(&s->dec) != DS_OK) {
                continue;
            }
            s->frames = frames;
            s->clip_bytes = bytes;
            s->clip_pos = 0;
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
            s->amp = motif_def[s->ear].amp;
            start_segment(s);
            s->mode = M_EAR;
            break;
        case K_TONES:
            ut_synth_init(&s->synth, it.keys, DS_SAY_RATE, UT_CABLE_TONE_MS, UT_CABLE_GAP_MS,
                          DS_SAY_TONES_PAUSE_MS);
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

/* One sample of the motif, with the 5 ms ramps at both ends of each tone. */
static int16_t ear_sample(ds_say *s)
{
    int32_t v = 0;

    if (motif_def[s->ear].hz[s->seg] != 0) {
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
        if (s->seg == motif_def[s->ear].n) {
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
