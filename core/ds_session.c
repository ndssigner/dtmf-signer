#include "ds_session.h"

#include <string.h>

void ds_session_init(ds_session *s, int testnet)
{
    if (s == NULL) {
        return;
    }
    ds_wipe(s, sizeof *s);
    s->testnet = testnet != 0;
    s->state = DS_S_EMPTY;
}

void ds_session_wipe(ds_session *s)
{
    if (s == NULL) {
        return;
    }
    ds_wallet_wipe(&s->w);
    ds_wipe(s, sizeof *s);
}

/* Policy result for data entered through a channel: a value >= 0 to go on
 * with (DS_OK or DS_WARN_AIR), DS_NEED_CONFIRM to stop and ask, or an
 * error. */
static int32_t admit(int data, int channel, size_t pin_len, int confirmed,
                     int32_t *warn)
{
    int32_t p = ds_policy_accepts(data, channel, pin_len);
    *warn = DS_OK;
    if (p == DS_NEED_CONFIRM && confirmed) {
        return DS_OK;
    }
    if (p == DS_WARN_AIR) {
        *warn = DS_WARN_AIR;
        return DS_OK;
    }
    return p;
}

int32_t ds_session_seed(ds_session *s, const uint8_t *entropy, size_t len,
                        int encrypted, size_t pin_len, int channel,
                        int confirmed)
{
    int32_t warn, e;

    if (s == NULL || entropy == NULL) {
        return DS_ERR_ARG;
    }
    if (s->state != DS_S_EMPTY) {
        return DS_ERR_STATE;
    }
    e = admit(encrypted ? DS_DATA_SEED_ENCRYPTED : DS_DATA_SEED_PLAIN, channel,
              pin_len, confirmed, &warn);
    if (e != DS_OK) {
        return e;
    }
    if (len != 16 && len != 32) {
        return DS_ERR_ARG;
    }
    memcpy(s->entropy, entropy, len);
    s->entropy_len = len;
    s->state = DS_S_SEED;
    return warn;
}

int32_t ds_session_passphrase(ds_session *s, const char *text, int channel,
                              int confirmed)
{
    int32_t warn, e;

    if (s == NULL) {
        return DS_ERR_ARG;
    }
    if (s->state != DS_S_SEED) {
        return DS_ERR_STATE;
    }
    if (text != NULL) {
        e = admit(DS_DATA_PASSPHRASE, channel, 0, confirmed, &warn);
        if (e != DS_OK) {
            return e;
        }
    }
    e = ds_wallet_load(&s->w, s->entropy, s->entropy_len, text, s->testnet);
    if (e == DS_ERR_PASSPHRASE) {
        return e;
    }
    if (e != DS_OK) {
        ds_session_wipe(s);
        return e;
    }
    s->state = DS_S_READY;
    return DS_OK;
}

static void discard_review(ds_session *s)
{
    ds_wipe(&s->r, sizeof s->r);
    s->state = DS_S_READY;
}

int32_t ds_session_review(ds_session *s, const ds_source *src, int channel)
{
    int32_t e;

    if (s == NULL || src == NULL) {
        return DS_ERR_ARG;
    }
    if (s->state != DS_S_READY && s->state != DS_S_REVIEW &&
        s->state != DS_S_CONFIRMED) {
        return DS_ERR_STATE;
    }
    discard_review(s);
    e = ds_policy_accepts(DS_DATA_PSBT, channel, 0);
    if (e < 0) {
        return e;
    }
    e = ds_psbt_analyze(src, &s->w, &s->r);
    if (e != DS_OK) {
        discard_review(s);
        return e;
    }
    s->state = DS_S_REVIEW;
    return DS_OK;
}

int32_t ds_session_confirm(ds_session *s)
{
    if (s == NULL) {
        return DS_ERR_ARG;
    }
    if (s->state != DS_S_REVIEW) {
        return DS_ERR_STATE;
    }
    s->state = DS_S_CONFIRMED;
    return DS_OK;
}

int32_t ds_session_sign(ds_session *s, const ds_source *src,
                        const ds_sink *dst)
{
    int32_t e;

    if (s == NULL || src == NULL || dst == NULL) {
        return DS_ERR_ARG;
    }
    if (s->state != DS_S_CONFIRMED) {
        return DS_ERR_STATE;
    }
    e = ds_psbt_sign(src, &s->w, &s->r, dst);
    discard_review(s);
    return e;
}

int32_t ds_session_cancel(ds_session *s)
{
    if (s == NULL) {
        return DS_ERR_ARG;
    }
    if (s->state == DS_S_REVIEW || s->state == DS_S_CONFIRMED) {
        discard_review(s);
        return DS_OK;
    }
    return s->state == DS_S_READY ? DS_OK : DS_ERR_STATE;
}
