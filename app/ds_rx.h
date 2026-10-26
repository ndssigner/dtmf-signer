/* DTMF-Signer device logic: the tone receiver (a seed or a PSBT).
 *
 * Audio goes in with ds_rx_push, which only feeds the listener. The heavy
 * work (Reed-Solomon, fountain codes) happens in ds_rx_poll, which takes at
 * most one group of tones per call; callers loop until it returns DS_RX_NONE.
 * No malloc: a ds_rx is about 40 KB, so declare it static, never on the
 * stack. */
#ifndef DS_RX_H
#define DS_RX_H

#include <stddef.h>
#include <stdint.h>

#include "ds_core.h"
#include "ur_tones.h"

#ifndef DS_APP_PSBT_MAX
#define DS_APP_PSBT_MAX 16384 /* bytes of the largest PSBT received */
#endif

/* What the receiver waits for. */
enum { DS_RX_SEED = 1, DS_RX_PSBT };

/* Results of ds_rx_poll. */
#define DS_RX_NONE 0     /* no group to take */
#define DS_RX_GOT_SEED 1 /* a seed group is held */
#define DS_RX_PART 2     /* a part of the PSBT was taken (progress) */
#define DS_RX_DONE 3     /* the PSBT is complete */
#define DS_RX_OTHER 4    /* not what was expected: discarded, listening goes on */

typedef struct {
    ut_listener l;
    ut_fountain_decoder d;
    uint8_t work[UT_FOUNTAIN_WORK(DS_APP_PSBT_MAX)];
    char group[UT_GROUP + 1]; /* the held seed group (a secret) */
    int mode, has_group;
    uint32_t tones, parts;
} ds_rx;

/* mode: DS_RX_SEED or DS_RX_PSBT. rate: the sample rate of ds_rx_push. */
void ds_rx_init(ds_rx *rx, uint32_t rate, int mode);

/* Feeds the listener; no heavy work. */
void ds_rx_push(ds_rx *rx, const int16_t *x, size_t n);

/* Takes at most one group and processes it. In seed mode a keypad group or a
 * frame holding a crypto-seed/seed UR is kept (a group already held is wiped
 * and replaced) and gives DS_RX_GOT_SEED; anything else is wiped and gives
 * DS_RX_OTHER. In PSBT mode every valid part goes to the fountain decoder:
 * DS_RX_PART while more are needed, DS_RX_DONE when the crypto-psbt/psbt is
 * complete. A frame of another type, a part of another message or a message
 * that is not a PSBT gives DS_RX_OTHER and restarts the decoder. After
 * DS_RX_DONE it returns DS_RX_NONE and takes nothing more. */
int32_t ds_rx_poll(ds_rx *rx);

/* Decrypts the held seed group with pin (NULL or "" for none) into entropy
 * (32 bytes): returns its length (16 or 32) or the ur-tones error (DS_ERR_STATE
 * when no group is held). The group is always wiped. */
int32_t ds_rx_seed(ds_rx *rx, const char *pin, uint8_t entropy[32]);

/* The complete PSBT, pointing inside rx's fixed work buffer (the ds_source
 * contract): valid until rx is reused or wiped, so after DS_RX_DONE no more
 * audio goes to this ds_rx. DS_ERR_STATE before DS_RX_DONE. */
int32_t ds_rx_psbt(const ds_rx *rx, const uint8_t **psbt, size_t *len);

/* PSBT progress: fragments known and fragments needed (0 when unknown). */
void ds_rx_progress(const ds_rx *rx, uint32_t *have, uint32_t *need);

/* Wipes everything, secrets included. Call ds_rx_init before reuse. */
void ds_rx_wipe(ds_rx *rx);

#endif
