/* What the signer says on the telephone line: a queue of phrases, numbers,
 * spelled characters, earcons and data tones, read out as 16 kHz samples. */
#ifndef DS_SAY_H
#define DS_SAY_H

#include <stddef.h>
#include <stdint.h>

#include "ds_adpcm.h"
#include "ds_pack.h"
#include "ur_tones.h"

enum { DS_EAR_OK = 1, DS_EAR_FAIL, DS_EAR_WARN, DS_EAR_LISTEN, DS_EAR_NEXT, DS_EAR_BACK };

#define DS_SAY_RATE 16000u
#define DS_SAY_QUEUE 64u      /* items waiting (a character is one item) */
#define DS_SAY_LOG_N 16u      /* tokens kept for the echo */
#define DS_SAY_TOKEN_MAX 80u  /* longest token with its NUL; cap for ds_say_take_log */
#define DS_SAY_CHAR_GAP_MS 120u
#define DS_SAY_GROUP_GAP_MS 400u

typedef struct {
    uint8_t kind;
    uint16_t v;   /* clip id, earcon, or pause in ms */
    uint16_t gap; /* silence after a clip, in ms */
    const char *keys;
} ds_say_item;

typedef struct {
    const ds_pack *pack;
    ds_say_item q[DS_SAY_QUEUE];
    uint8_t qhead, qcount;
    char log[DS_SAY_LOG_N][DS_SAY_TOKEN_MAX];
    uint8_t lhead, lcount;
    /* what plays now */
    uint8_t mode;
    uint32_t silence;     /* samples of silence left (a pause or a gap after a clip) */
    uint16_t gap_after;   /* clip: silence to follow, in ms */
    /* a clip: ADPCM in blocks, 8 kHz up to 16 kHz by linear interpolation */
    const uint8_t *adpcm;
    uint32_t nib_total, nib_pos;
    ds_adpcm st;
    int16_t blk[32];
    uint8_t blen, bpos, phase;
    int16_t s0;
    /* an earcon */
    uint8_t ear, seg;
    uint32_t seg_pos, seg_len;
    int32_t y1, y2, coef, amp;
    /* data tones */
    ut_synth synth;
} ds_say;

void ds_say_init(ds_say *s, const ds_pack *pack);
/* The language: affects what is queued from now on. */
void ds_say_set_pack(ds_say *s, const ds_pack *pack);
/* Drops what is queued and what plays, wiped (it may be a PIN's
 * characters); the log is kept. */
void ds_say_clear(ds_say *s);

/* Each returns DS_OK, DS_ERR_SPACE when the queue lacks room (nothing is
 * queued) or DS_ERR_ARG. */
int32_t ds_say_clip(ds_say *s, uint16_t id);
int32_t ds_say_earcon(ds_say *s, int e);
int32_t ds_say_pause(ds_say *s, uint16_t ms);
/* n as the current pack's grammar says it, clips back to back. */
int32_t ds_say_number(ds_say *s, uint64_t n);
/* Each letter or digit of text as its clip, 120 ms after each (anything else
 * is skipped). The grouped form waits 400 ms instead after every 4th. */
int32_t ds_say_chars(ds_say *s, const char *text);
int32_t ds_say_chars_grouped(ds_say *s, const char *text);
/* One character of a secret (a PIN): the log says "say pin-char", never c. */
int32_t ds_say_secret_char(ds_say *s, char c);
/* Data tones at cable pace (40 ms tone, 20 ms gap, 400 ms pauses). keys is
 * kept, not copied: it must stay valid until the queue is clear or empty. The
 * log holds at most its first 60 keys. */
int32_t ds_say_tones(ds_say *s, const char *keys);

/* Writes n samples at 16 kHz (silence when nothing is queued); returns n. */
size_t ds_say_read(ds_say *s, int16_t *out, size_t n);
/* 1 while anything is queued or playing. */
int ds_say_busy(const ds_say *s);

/* The next token (NUL-terminated) of what was queued: returns its length, 0
 * when there is none, or DS_ERR_SPACE (nothing taken) when cap is under
 * DS_SAY_TOKEN_MAX. */
int32_t ds_say_take_log(ds_say *s, char *out, size_t cap);

#endif
