/* DTMF-Signer device logic: the tone receiver. See ds_rx.h. */
#include "ds_rx.h"

#include <string.h>

static int starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int is_psbt_type(const char *type)
{
    return strcmp(type, "crypto-psbt") == 0 || strcmp(type, "psbt") == 0;
}

static void restart_decoder(ds_rx *rx)
{
    ds_wipe(rx->work, sizeof rx->work);
    ut_fountain_decoder_init(&rx->d, rx->work, sizeof rx->work);
}

void ds_rx_init(ds_rx *rx, uint32_t rate, int mode)
{
    memset(rx, 0, sizeof *rx);
    ut_listener_init(&rx->l, rate);
    rx->group_level = -990;
    ut_fountain_decoder_init(&rx->d, rx->work, sizeof rx->work);
    rx->mode = mode;
}

/* Adds the keys heard to the shown ones, dropping the oldest. */
static void keep_live(ds_rx *rx, const char *k, size_t n)
{
    size_t have = strlen(rx->live), drop;
    if (n >= DS_RX_LIVE) {
        memcpy(rx->live, k + n - DS_RX_LIVE, DS_RX_LIVE);
        rx->live[DS_RX_LIVE] = '\0';
        return;
    }
    drop = have + n > DS_RX_LIVE ? have + n - DS_RX_LIVE : 0;
    memmove(rx->live, rx->live + drop, have - drop);
    memcpy(rx->live + have - drop, k, n);
    rx->live[have - drop + n] = '\0';
}

void ds_rx_push(ds_rx *rx, const int16_t *x, size_t n)
{
    char k[UT_LIVE + 1];
    int32_t got;

    ut_listener_push(&rx->l, x, n);
    if (rx->sound_n < 0x10000) {
        rx->sound_sum += rx->l.level;
        rx->sound_n++;
    }
    got = ut_listener_live(&rx->l, k, sizeof k);
    if (got > 0) {
        /* the level while a key sounds, not of the noise between groups */
        if (rx->l.level > -990 && rx->level_n < 0x10000) {
            rx->level_sum += rx->l.level;
            rx->level_n++;
        }
        rx->keys += (uint32_t)got;
        if (rx->mode == DS_RX_PSBT) {
            keep_live(rx, k, (size_t)got);
        }
    }
    ds_wipe(k, sizeof k); /* a seed's digits */
}

/* Seed mode: keep a keypad group or a frame holding a seed UR. */
static int32_t poll_seed(ds_rx *rx, const char *group)
{
    char ur[UT_MAX_UR + 1];
    int keep = 0;

    if (group[0] == '*') {
        keep = 1;
    } else if (ut_frame_to_ur(group, ur, sizeof ur, NULL) >= 0) {
        keep = starts_with(ur, "ur:crypto-seed/") || starts_with(ur, "ur:seed/");
    }
    ds_wipe(ur, sizeof ur);
    if (!keep) {
        return DS_RX_OTHER;
    }
    ds_wipe(rx->group, sizeof rx->group);
    strcpy(rx->group, group);
    rx->has_group = 1;
    return DS_RX_GOT_SEED;
}

/* PSBT mode: every valid part goes to the fountain decoder. */
static int32_t poll_psbt(ds_rx *rx, const char *group)
{
    ut_frame f;
    const char *type;
    const uint8_t *msg, *bytes;
    size_t msg_len, bytes_len;
    int32_t r, res;

    if (ut_frame_decode(group, &f, NULL) < 0 || !is_psbt_type(f.type)) {
        res = DS_RX_OTHER;
        goto done;
    }
    rx->parts++;
    r = ut_fountain_receive_frame(&rx->d, &f);
    if (r < 0) {
        restart_decoder(rx);
        res = DS_RX_OTHER;
    } else if (r == 0) {
        res = DS_RX_PART;
    } else if (ut_fountain_result(&rx->d, &type, &msg, &msg_len) < 0 ||
               !is_psbt_type(type) ||
               ut_cbor_to_psbt(msg, msg_len, &bytes, &bytes_len) < 0 ||
               bytes_len == 0) {
        restart_decoder(rx);
        res = DS_RX_OTHER;
    } else {
        res = DS_RX_DONE;
    }
done:
    ds_wipe(&f, sizeof f);
    return res;
}

int32_t ds_rx_poll(ds_rx *rx)
{
    char group[UT_GROUP + 1];
    int32_t n, res;

    if (rx->mode == DS_RX_PSBT && rx->d.done) {
        return DS_RX_NONE;
    }
    n = ut_listener_group(&rx->l, group, sizeof group);
    if (n <= 0) {
        ds_wipe(group, sizeof group);
        return DS_RX_NONE;
    }
    if (rx->level_n > 0) {
        rx->group_level = (int)(rx->level_sum / (int32_t)rx->level_n);
        rx->level_sum = 0;
        rx->level_n = 0;
    }
    if (rx->mode == DS_RX_SEED) {
        res = poll_seed(rx, group);
    } else {
        res = poll_psbt(rx, group);
    }
    ds_wipe(group, sizeof group); /* it may hold secret digits */
    return res;
}

int32_t ds_rx_seed(ds_rx *rx, const char *pin, uint8_t entropy[32])
{
    char ur[UT_MAX_UR + 1];
    int32_t n;

    if (!rx->has_group) {
        return DS_ERR_STATE;
    }
    if (rx->group[0] == '*') {
        n = ut_keypad_to_seed(rx->group, pin, entropy);
    } else if ((n = ut_frame_to_ur(rx->group, ur, sizeof ur, NULL)) >= 0) {
        n = ut_ur_to_seed(ur, pin, entropy);
    }
    if (n < 0) {
        ds_wipe(entropy, 32);
    }
    ds_wipe(ur, sizeof ur);
    ds_wipe(rx->group, sizeof rx->group);
    rx->has_group = 0;
    return n;
}

int32_t ds_rx_psbt(const ds_rx *rx, const uint8_t **psbt, size_t *len)
{
    const char *type;
    const uint8_t *msg;
    size_t msg_len;

    if (rx->mode != DS_RX_PSBT || !rx->d.done ||
        ut_fountain_result(&rx->d, &type, &msg, &msg_len) < 0 ||
        !is_psbt_type(type) || ut_cbor_to_psbt(msg, msg_len, psbt, len) < 0) {
        return DS_ERR_STATE;
    }
    return DS_OK;
}

int ds_rx_level(const ds_rx *rx)
{
    return rx->level_n > 0 ? (int)(rx->level_sum / (int32_t)rx->level_n) : rx->group_level;
}

int ds_rx_take_sound(ds_rx *rx)
{
    int mean = rx->sound_n > 0 ? (int)(rx->sound_sum / (int32_t)rx->sound_n) : -990;
    rx->sound_sum = 0;
    rx->sound_n = 0;
    return mean;
}

void ds_rx_progress(const ds_rx *rx, uint32_t *have, uint32_t *need)
{
    *have = rx->d.nknown;
    *need = rx->d.seq_len;
}

void ds_rx_wipe(ds_rx *rx)
{
    ds_wipe(rx, sizeof *rx);
}
