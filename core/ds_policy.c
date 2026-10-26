#include "ds_session.h"

#define DS_AIR_MIN_PIN 12

int32_t ds_policy_accepts(int data, int channel, size_t pin_len)
{
    if (channel != DS_AIR && channel != DS_CABLE &&
        channel != DS_CABLE_TERMINAL && channel != DS_LOCAL) {
        return DS_ERR_POLICY;
    }
    switch (data) {
    case DS_DATA_PSBT:
        return channel == DS_LOCAL ? DS_ERR_POLICY : DS_OK;
    case DS_DATA_SEED_ENCRYPTED:
        if (channel == DS_AIR) {
            return pin_len >= DS_AIR_MIN_PIN ? DS_WARN_AIR : DS_ERR_POLICY;
        }
        break;
    case DS_DATA_SEED_PLAIN:
        if (channel == DS_AIR) {
            return DS_ERR_POLICY;
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
