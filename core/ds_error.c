/* DTMF-Signer portable core: the names of the results. See ds_core.h. */
#include "ds_core.h"

const char *ds_error_name(int32_t code)
{
    switch (code) {
    case DS_OK: return "DS_OK";
    case DS_ERR_ARG: return "DS_ERR_ARG";
    case DS_ERR_SPACE: return "DS_ERR_SPACE";
    case DS_ERR_PASSPHRASE: return "DS_ERR_PASSPHRASE";
    case DS_ERR_DERIVE: return "DS_ERR_DERIVE";
    case DS_ERR_SEED: return "DS_ERR_SEED";
    case DS_ERR_AUDIO: return "DS_ERR_AUDIO";
    case DS_ERR_PSBT: return "DS_ERR_PSBT";
    case DS_ERR_UNSUPPORTED: return "DS_ERR_UNSUPPORTED";
    case DS_ERR_AMOUNTS: return "DS_ERR_AMOUNTS";
    case DS_ERR_SIGHASH: return "DS_ERR_SIGHASH";
    case DS_ERR_NETWORK: return "DS_ERR_NETWORK";
    case DS_ERR_CHANGE: return "DS_ERR_CHANGE";
    case DS_ERR_LIMIT: return "DS_ERR_LIMIT";
    case DS_ERR_STATE: return "DS_ERR_STATE";
    case DS_ERR_POLICY: return "DS_ERR_POLICY";
    case DS_WARN_AIR: return "DS_WARN_AIR";
    case DS_NEED_CONFIRM: return "DS_NEED_CONFIRM";
    default: return "unknown error";
    }
}
