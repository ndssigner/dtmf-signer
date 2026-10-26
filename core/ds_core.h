/* DTMF-Signer portable core: common definitions. */
#ifndef DS_CORE_H
#define DS_CORE_H

#include <stddef.h>
#include <stdint.h>

#define DS_OK 0
#define DS_ERR_ARG (-1)
#define DS_ERR_SPACE (-2)
#define DS_ERR_PASSPHRASE (-3)
#define DS_ERR_DERIVE (-4)
#define DS_ERR_SEED (-5)
#define DS_ERR_AUDIO (-6)
#define DS_ERR_PSBT (-7)
#define DS_ERR_UNSUPPORTED (-8)
#define DS_ERR_AMOUNTS (-9)
#define DS_ERR_SIGHASH (-10)
#define DS_ERR_NETWORK (-11)
#define DS_ERR_CHANGE (-12)
#define DS_ERR_LIMIT (-13)
#define DS_ERR_STATE (-14)
#define DS_ERR_POLICY (-15)

/* Positive results of the channel policy. */
#define DS_WARN_AIR 1     /* allowed, with a warning about the air channel */
#define DS_NEED_CONFIRM 2 /* allowed only with physical confirmation */

/* The name of a result above ("DS_ERR_CHANGE"...), or "unknown error". */
const char *ds_error_name(int32_t code);

/* Zeroes n bytes at p; the compiler cannot elide the writes. */
void ds_wipe(void *p, size_t n);

#endif
