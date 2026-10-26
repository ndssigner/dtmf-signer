/* DTMF-Signer device logic: animated UR QR codes in the framebuffer.
 *
 * A message (the xpub, the signed PSBT) leaves as a fountain-coded UR: each
 * ds_qr_next makes the next part as a QR code (uppercase UR, alphanumeric
 * mode, error correction L) and ds_qr_draw puts it in the left 64x64 square
 * of the framebuffer. The pace (4 parts per second) is the caller's.
 *
 * Scale 2 px/module reaches version 3 (29 modules); scale 1 reaches version 7
 * (45 modules). The fragment is the largest (200 down) whose part number W,
 * the highest sent (W = 2 * parts + 10), really encodes in the version: the
 * sequence number grows its digits and its CBOR bytes, so the part is
 * measured with W, not estimated, and with the message CRC-32 in its CBOR
 * at its longest (5 bytes), so the choice depends on the message's length
 * only, never on its bytes. After part W the sequence starts over, so
 * the sequence number never passes W and every part fits. When scale 2 does
 * not fit (or would need more than UT_MAX_PARTS parts), scale 1 is used. At
 * 2 px a PSBT's parts practically never fit (fragments are at least 10 bytes
 * and need version 4 or more), so they go at 1 px; callers must handle both
 * scales.
 *
 * A ds_qr holds a fountain encoder (about 3 KB) and the QR buffers: declare
 * it static, never on the stack. No malloc. */
#ifndef DS_QR_H
#define DS_QR_H

#include <stddef.h>
#include <stdint.h>

#include "ds_core.h"
#include "qrcodegen.h"
#include "ur_tones.h"

/* The longest UR text a part can be: 224 characters, the capacity of a
 * version 7 alphanumeric QR code at error correction L, plus the NUL. */
#define DS_QR_UR_MAX 225

typedef struct {
    ut_fountain_encoder e;
    const char *type; /* the UR type; NULL until started */
    int scale, max_version;
    char ur[DS_QR_UR_MAX];
    uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(7)];
    uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(7)];
    uint32_t shown;
} ds_qr;

/* Starts sending msg (kept by pointer, not copied, like type: both must
 * outlive the sending) as UR type. scale is 2 (version up to 3) or 1 (up to
 * 7). Returns the effective scale (2 may become 1 when scale 2 would need
 * more than UT_MAX_PARTS parts), DS_ERR_ARG for bad arguments or DS_ERR_LIMIT
 * when it does not fit even at 1 px. */
int32_t ds_qr_start(ds_qr *q, const char *type, const uint8_t *msg, size_t len, int scale);

/* Makes the next part, in q->ur (uppercase) and q->qr. Returns its size in
 * modules, DS_ERR_STATE before ds_qr_start. After a successful ds_qr_start it
 * cannot fail (the part with the highest sequence number was encoded there);
 * DS_ERR_LIMIT would mean a broken invariant. */
int32_t ds_qr_next(ds_qr *q);

/* Draws the current code in the square x 0-63, y 0-63 of fb (8 pages of 8
 * rows, one byte per column, bit 0 the top row; a set bit is a light pixel):
 * a light box of the code plus 1 quiet module on each side, centred, the dark
 * modules off; the rest of the square off. The other columns are untouched. */
void ds_qr_draw(const ds_qr *q, uint8_t fb[8][128]);

#endif
