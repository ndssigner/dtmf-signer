/* DTMF-Signer portable core: incremental transaction reader and the BIP 143
 * signature hash for P2WPKH inputs.
 *
 * The reader is a byte-driven state machine with bounded state: the
 * transaction is never stored. While it streams past, the reader computes the
 * txid and the three BIP 143 hashes and reports each input and output to the
 * caller through callbacks. Both the legacy serialization and the segwit one
 * (marker 00, flag 01, witnesses) are accepted; the witness bytes are parsed
 * and skipped, so the txid is always that of the non-witness serialization.
 *
 * Hashes are raw double SHA-256 digests in internal byte order (the order
 * used inside serializations); a txid shown to a user is this reversed. */
#ifndef DS_TX_H
#define DS_TX_H

#include "ds_bitcoin.h"

#ifndef DS_MAX_INPUTS
#define DS_MAX_INPUTS 32
#endif
#ifndef DS_MAX_OUTPUTS
#define DS_MAX_OUTPUTS 32
#endif
/* Output scripts up to this many bytes are handed to the callback. */
#define DS_TX_SCRIPT_KEEP 42
/* Longest input or output script accepted. */
#define DS_TX_MAX_SCRIPT 10000

typedef struct {
    uint32_t version;
    uint32_t locktime;
    uint32_t n_inputs;
    uint32_t n_outputs;
    int has_witness;
    uint8_t txid[32];
    uint8_t hash_prevouts[32];
    uint8_t hash_sequence[32];
    uint8_t hash_outputs[32];
} ds_tx_result;

typedef struct ds_tx_reader ds_tx_reader;

/* Callbacks return DS_OK (0) to go on, or a negative DS_ERR_* that makes the
 * pending ds_tx_reader_feed fail with that code. Any of them may be NULL. */
typedef int32_t (*ds_tx_input_cb)(void *user, uint32_t index,
                                  const uint8_t outpoint[36],
                                  uint32_t sequence);
/* script holds the first min(len, DS_TX_SCRIPT_KEEP) bytes; len is the full
 * script length, so len > DS_TX_SCRIPT_KEEP means the script was cut. */
typedef int32_t (*ds_tx_output_cb)(void *user, uint32_t index, uint64_t amount,
                                   const uint8_t *script, size_t len);
typedef int32_t (*ds_tx_end_cb)(void *user, const ds_tx_result *result);

struct ds_tx_reader {
    /* Set after ds_tx_reader_init; init leaves the callbacks NULL. */
    ds_tx_input_cb on_input;
    ds_tx_output_cb on_output;
    ds_tx_end_cb on_end;
    void *user;
    /* Most inputs and outputs accepted; init sets DS_MAX_INPUTS and
     * DS_MAX_OUTPUTS. A caller that stores nothing per entry (a previous
     * transaction, only streamed) may raise them. */
    uint32_t max_inputs, max_outputs;

    /* Private state. */
    ds_sha256_ctx txid_h, prevouts_h, sequence_h, outputs_h;
    ds_tx_result res;
    uint8_t fix[40]; /* outpoint + sequence, or amount, or a 4-byte field */
    uint8_t script[DS_TX_SCRIPT_KEEP];
    uint8_t cs[9];
    uint8_t cs_need, cs_got;
    uint8_t state;
    int32_t err;
    uint32_t fix_got, index, wit_index;
    uint32_t script_len, script_got;
    uint64_t items_left, skip; /* witness items, and bytes of the current one */
};

void ds_tx_reader_init(ds_tx_reader *r);
/* Consumes n bytes. Returns DS_OK, or a negative error that sticks:
 * DS_ERR_PSBT for malformed data (bad flag, oversized script, bytes after the
 * end), DS_ERR_LIMIT for more than max_inputs / max_outputs, or a
 * callback's error. Any split of the stream gives the same result. */
int32_t ds_tx_reader_feed(ds_tx_reader *r, const uint8_t *p, size_t n);
/* 1 once the whole transaction has been read, 0 while it is incomplete, or
 * the sticky error code. The result is valid only when this returns 1. */
int32_t ds_tx_reader_done(const ds_tx_reader *r);
const ds_tx_result *ds_tx_reader_result(const ds_tx_reader *r);

/* BIP 143 signature hash of a P2WPKH input with SIGHASH_ALL: the script code
 * is the P2PKH script of pubkey_hash. */
void ds_bip143_sighash(const uint8_t hash_prevouts[32],
                       const uint8_t hash_sequence[32],
                       const uint8_t hash_outputs[32], uint32_t version,
                       const uint8_t outpoint[36], const uint8_t pubkey_hash[20],
                       uint64_t amount, uint32_t sequence, uint32_t locktime,
                       uint8_t out[32]);

#endif
