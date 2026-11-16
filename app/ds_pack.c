#include "ds_pack.h"

#include "ds_bitcoin.h"
#include "ds_core.h"
#include "ds_crc32.h"
#include "voice/ds_prompts.h"

#define HEADER 22u /* "DSV2", lang, u16 format, u16 count, u32 total, u32 crc, u32 grammar */
#define ENTRY 12u  /* u32 offset, u32 bytes, i16 pred, u8 index, u8 pad */
#define CRC_AT 14u
#define SMALL_BYTES 200u /* u16 small[100] */
#define SCALE 18u        /* u64 value, u16 singular, u16 plural, u8 say_one, u8 pad[5] */
#define IRREGULAR 16u    /* u64 value, u16 clip, u8 kind, u8 pad[5] */
#define KIND_EXACT 1u
#define KIND_MORE 2u
#define KIND_BEFORE_SCALE 3u
/* Each nesting says a multiple of a scale; with every scale at least 10 a
 * 64-bit number nests a few levels at most, and the cap bounds the stack. */
#define MAX_DEPTH 16u

static uint16_t rd16(const uint8_t *b)
{
    return (uint16_t)(b[0] | b[1] << 8);
}

static uint32_t rd32(const uint8_t *b)
{
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static uint64_t rd64(const uint8_t *b)
{
    return (uint64_t)rd32(b) | (uint64_t)rd32(b + 4) << 32;
}

static const uint8_t *entry(const ds_pack *p, uint16_t id)
{
    return p->data + HEADER + (uint32_t)id * ENTRY;
}

static int clip_ok(uint16_t id, uint16_t count, int none_ok)
{
    return id < count || (none_ok && id == DS_PACK_NONE);
}

/* Bounds and clip numbers of the grammar at data[off..total). */
static int32_t open_grammar(ds_pack *q, uint32_t off, uint32_t total)
{
    const uint8_t *d = q->data;
    if (total - off < SMALL_BYTES + 1)
        return DS_ERR_ARG;
    q->small = d + off;
    for (uint32_t k = 0; k < 100; k++)
        if (!clip_ok(rd16(q->small + 2 * k), q->count, 1))
            return DS_ERR_ARG;
    off += SMALL_BYTES;
    q->nscales = d[off++];
    if (q->nscales > DS_PACK_MAX_SCALES || total - off < q->nscales * SCALE + 2u)
        return DS_ERR_ARG;
    q->scales = d + off;
    for (uint32_t k = 0; k < q->nscales; k++) {
        const uint8_t *s = q->scales + k * SCALE;
        uint64_t value = rd64(s);
        /* Largest first, strictly, and at least 10: the number algorithm
         * relies on both to end and to stay shallow. */
        if (value < 10 || (k > 0 && value >= rd64(s - SCALE)) ||
            !clip_ok(rd16(s + 8), q->count, 0) || !clip_ok(rd16(s + 10), q->count, 1) ||
            s[12] > 1)
            return DS_ERR_ARG;
    }
    off += q->nscales * SCALE;
    q->nirr = rd16(d + off);
    off += 2;
    if (q->nirr > DS_PACK_MAX_IRREGULARS || total - off < (uint32_t)q->nirr * IRREGULAR)
        return DS_ERR_ARG;
    q->irregulars = d + off;
    for (uint32_t k = 0; k < q->nirr; k++) {
        const uint8_t *r = q->irregulars + k * IRREGULAR;
        if (!clip_ok(rd16(r + 8), q->count, 0) || r[10] < KIND_EXACT || r[10] > KIND_BEFORE_SCALE)
            return DS_ERR_ARG;
    }
    return DS_OK;
}

int32_t ds_pack_open(ds_pack *p, const uint8_t *data, uint32_t len)
{
    if (!p || !data || len < HEADER || data[0] != 'D' || data[1] != 'S' || data[2] != 'V' ||
        data[3] != '2')
        return DS_ERR_ARG;
    for (int k = 4; k < 6; k++)
        if (data[k] < 'a' || data[k] > 'z')
            return DS_ERR_ARG;
    uint16_t format = rd16(data + 6), count = rd16(data + 8);
    uint32_t total = rd32(data + 10), crc = rd32(data + CRC_AT), goff = rd32(data + 18);
    /* A pack with another prompt list was made for another firmware: its
     * clip numbers would say the wrong things. */
    if (format != 2 || count != DS_P_COUNT || total < HEADER || total > len)
        return DS_ERR_ARG;
    /* count is at most 65535, so the table size cannot overflow 32 bits. */
    if ((uint32_t)count * ENTRY > total - HEADER || goff < HEADER + (uint32_t)count * ENTRY ||
        goff > total)
        return DS_ERR_ARG;

    static const uint8_t zero[4] = {0, 0, 0, 0};
    uint32_t c = ds_crc32(0, data, CRC_AT);
    c = ds_crc32(c, zero, 4);
    c = ds_crc32(c, data + CRC_AT + 4, total - CRC_AT - 4);
    if (c != crc)
        return DS_ERR_ARG;

    ds_pack q = {data, total, count, {(char)data[4], (char)data[5], 0}, 0, 0, 0, 0, 0};
    for (uint16_t id = 0; id < count; id++) {
        const uint8_t *e = entry(&q, id);
        uint32_t offset = rd32(e), bytes = rd32(e + 4);
        /* Compared as total - offset so that offset + bytes cannot wrap. */
        if (offset > total || bytes > total - offset || e[10] > 88)
            return DS_ERR_ARG;
    }
    if (open_grammar(&q, goff, total) != DS_OK)
        return DS_ERR_ARG;
    *p = q;
    return DS_OK;
}

int32_t ds_pack_clip(const ds_pack *p, uint16_t id, const uint8_t **adpcm,
                     uint32_t *bytes, ds_adpcm *start)
{
    if (id >= p->count)
        return DS_ERR_ARG;
    const uint8_t *e = entry(p, id);
    *adpcm = p->data + rd32(e);
    *bytes = rd32(e + 4);
    start->pred = (int16_t)rd16(e + 8);
    start->index = e[10];
    return DS_OK;
}

/* -- Numbers ------------------------------------------------------------ */

typedef struct {
    uint16_t *out;
    size_t cap;
    size_t len;
} sink;

static int32_t emit(sink *s, uint16_t clip)
{
    if (s->len == s->cap)
        return DS_ERR_SPACE;
    s->out[s->len++] = clip;
    return DS_OK;
}

static uint16_t small(const ds_pack *p, uint64_t n)
{
    return n < 100 ? rd16(p->small + 2 * n) : DS_PACK_NONE;
}

static uint16_t irregular(const ds_pack *p, uint64_t value, uint8_t kind)
{
    for (uint32_t k = 0; k < p->nirr; k++) {
        const uint8_t *r = p->irregulars + k * IRREGULAR;
        if (r[10] == kind && rd64(r) == value)
            return rd16(r + 8);
    }
    return DS_PACK_NONE;
}

/* Says n; before_scale when a scale word follows, so the last small number
 * takes its kind-3 form (es "un", "veintiún"). Mirrors say_number() in
 * tools/voice/make_pack.py. */
static int32_t say(const ds_pack *p, uint64_t n, int before_scale, unsigned depth, sink *s)
{
    if (depth > MAX_DEPTH)
        return DS_ERR_ARG;
    size_t start = s->len;
    int32_t rc;
    for (uint32_t i = 0; i < p->nscales; i++) {
        /* A whole clip, where the language has one, beats composing. */
        if (small(p, n) != DS_PACK_NONE)
            break;
        const uint8_t *sc = p->scales + i * SCALE;
        uint64_t value = rd64(sc), q = n / value, r = n % value;
        if (q == 0)
            continue;
        uint16_t c = irregular(p, q * value, r == 0 ? KIND_EXACT : KIND_MORE);
        if (c != DS_PACK_NONE) {
            rc = emit(s, c);
        } else {
            /* A multiple may need a larger scale again ("mil millones"), so
             * it is said from the largest scale down. */
            if (q > 1 || sc[12]) {
                rc = say(p, q, 1, depth + 1, s);
                if (rc != DS_OK)
                    return rc;
            }
            uint16_t plural = rd16(sc + 10);
            rc = emit(s, q > 1 && plural != DS_PACK_NONE ? plural : rd16(sc + 8));
        }
        if (rc != DS_OK)
            return rc;
        n = r;
    }
    if (n > 0 || s->len == start) {
        uint16_t c = small(p, n);
        if (c == DS_PACK_NONE)
            return DS_ERR_ARG;
        uint16_t before = before_scale ? irregular(p, n, KIND_BEFORE_SCALE) : DS_PACK_NONE;
        return emit(s, before != DS_PACK_NONE ? before : c);
    }
    return DS_OK;
}

int32_t ds_pack_number(const ds_pack *p, uint64_t n, uint16_t *out, size_t cap)
{
    if (!p || !out)
        return DS_ERR_ARG;
    sink s = {out, cap, 0};
    int32_t rc = say(p, n, 0, 0, &s);
    return rc != DS_OK ? rc : (int32_t)s.len;
}

/* -- The region --------------------------------------------------------- */

static void fingerprint(const ds_pack *p, uint8_t fp[4])
{
    ds_sha256_ctx ctx;
    uint8_t h[32];
    ds_sha256_init(&ctx);
    ds_sha256_update(&ctx, p->data, p->len);
    ds_sha256_final(&ctx, h);
    for (int k = 0; k < 4; k++)
        fp[k] = h[k];
}

int32_t ds_packs_scan(ds_packs *ps, const ds_pack *builtin, const uint8_t *region,
                      uint32_t len)
{
    if (!ps || !builtin || (!region && len))
        return DS_ERR_ARG;
    ps->pack[0] = *builtin;
    fingerprint(builtin, ps->fp[0]);
    ps->n = 1;
    uint32_t off = 0;
    while (ps->n < DS_PACKS_MAX && len - off >= HEADER) {
        if (ds_pack_open(&ps->pack[ps->n], region + off, len - off) == DS_OK) {
            fingerprint(&ps->pack[ps->n], ps->fp[ps->n]);
            /* Skip the pack's own bytes: its audio could hold "DSV2" too. */
            uint32_t used = ps->pack[ps->n].len;
            ps->n++;
            if (used > len - off - 1)
                break;
            off += used - 1;
        }
        /* Next boundary; stop before it would pass the end (or wrap). */
        uint32_t next = (off / DS_PACK_ALIGN + 1) * DS_PACK_ALIGN;
        if (next <= off || next >= len)
            break;
        off = next;
    }
    return DS_OK;
}
