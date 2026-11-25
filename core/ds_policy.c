#include "ds_session.h"

int32_t ds_policy_accepts(int data, int channel, size_t pin_len)
{
    (void)pin_len; /* kept: the signature is public; no rule depends on it */
    if (channel != DS_AIR && channel != DS_CABLE &&
        channel != DS_CABLE_TERMINAL && channel != DS_LOCAL) {
        return DS_ERR_POLICY;
    }
    switch (data) {
    case DS_DATA_PSBT:
        return channel == DS_LOCAL ? DS_ERR_POLICY : DS_OK;
    case DS_DATA_SEED_ENCRYPTED:
    case DS_DATA_SEED_PLAIN:
        /* The signer cannot tell whether a seed is encrypted, nor with which
         * PIN, and the exposure happens when the tones are played: air warns
         * (once, before listening) and never refuses. */
        if (channel == DS_AIR) {
            return DS_WARN_AIR;
        }
        break;
    case DS_DATA_PIN:
    case DS_DATA_PASSPHRASE:
        if (channel == DS_AIR) {
            return DS_ERR_POLICY;
        }
        if (channel == DS_LOCAL) {
            return DS_OK;
        }
        break;
    default:
        return DS_ERR_POLICY;
    }
    /* Seeds: cable or terminal only (never local). PIN and passphrase reach
     * here only on a cable. */
    if (channel == DS_LOCAL) {
        return DS_ERR_POLICY;
    }
    return channel == DS_CABLE_TERMINAL ? DS_NEED_CONFIRM : DS_OK;
}
