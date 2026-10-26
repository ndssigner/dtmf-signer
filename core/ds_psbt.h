/* DTMF-Signer portable core: PSBT (BIP 174, version 0) analysis, the first
 * of the two passes over a PSBT, and its review summary.
 *
 * The PSBT is never held in memory: it is read from a random-access source
 * through a 64-byte buffer. The analysis checks the signing rules, decides
 * which inputs are ours and which outputs are change, computes the fee and
 * keeps in a ds_psbt_review what the user is shown and what the signing pass
 * needs. That review is all the core keeps between the two passes; with the
 * default limits (32 inputs, 32 outputs) it takes about 3.8 KB.
 *
 * Rules applied by ds_psbt_analyze (the first one found is returned):
 *  - DS_ERR_PSBT: not a well-formed version 0 PSBT: bad magic, truncated or
 *    trailing data, a key repeated in a map, a missing or malformed unsigned
 *    transaction (it must have empty scriptSigs and no witnesses), a known key
 *    with an unexpected key or value length; or a source whose content
 *    changed during the analysis.
 *  - DS_ERR_UNSUPPORTED: a global version (0xFB) other than 0.
 *  - DS_ERR_LIMIT: more than DS_MAX_INPUTS inputs or DS_MAX_OUTPUTS outputs
 *    in the unsigned transaction, more than DS_PSBT_PREV_MAX inputs or
 *    outputs in a previous transaction (non_witness_utxo), or more than
 *    DS_PSBT_MAX_KEYS pairs in one map.
 *  - DS_ERR_SIGHASH: an input whose sighash type is present and not 0x01.
 *  - DS_ERR_AMOUNTS: an input with neither witness_utxo nor non_witness_utxo;
 *    with two or more inputs, an input without non_witness_utxo; a
 *    non_witness_utxo whose txid is not the outpoint's, or whose output at the
 *    outpoint's index differs from witness_utxo in amount or script (checked
 *    whenever both are present); an amount above 21 000 000 BTC; outputs
 *    adding up to more than the inputs.
 *  - DS_ERR_NETWORK: a BIP 32 derivation with our wallet fingerprint whose
 *    second path step (the coin type) is not the wallet's (0' mainnet,
 *    1' testnet).
 *  - DS_ERR_CHANGE: an output whose derivation has our fingerprint and the
 *    account path + /0/i or /1/i, but whose script is not exactly P2WPKH of the
 *    key we derive for that path (the declared public key is never trusted).
 *
 * An input is ours when its witness_utxo script is P2WPKH of the key derived
 * from one of its derivations with our fingerprint and the account path +
 * /0/i or /1/i (i < 2^31). Any other input is left unsigned, and the summary
 * says so. The fee is the sum of all inputs (ours and foreign) minus the sum of
 * all outputs. fee_warning is set when the fee exceeds 10 % of the amount sent
 * to external outputs, or 1 000 000 sat (only the latter when there are no
 * external outputs). */
#ifndef DS_PSBT_H
#define DS_PSBT_H

#include "ds_tx.h"
#include "ds_wallet.h"

/* Most key-value pairs accepted in one map (duplicate keys are searched
 * pairwise through the source, so the work grows with the square of this). */
#ifndef DS_PSBT_MAX_KEYS
#define DS_PSBT_MAX_KEYS 64
#endif

/* Most inputs and most outputs accepted in a previous transaction. They are
 * only streamed (nothing is stored per entry); this is a sanity cap. */
#ifndef DS_PSBT_PREV_MAX
#define DS_PSBT_PREV_MAX 100000
#endif

/* A random-access byte source. read copies n bytes at offset into buf and
 * returns the number of bytes copied, or a negative DS_ERR_* that the
 * analysis returns as is. It is only asked for bytes inside [0, len).
 *
 * Contract: the content must not change from the start of the analysis to
 * the end of the signing pass (a fixed buffer filled before the review). The
 * analysis reads the source several times; it hashes the whole source at its
 * start and again at its end and fails with DS_ERR_PSBT if they differ, and
 * psbt_sha256 lets the signing pass check the same. */
typedef struct {
    int32_t (*read)(void *ctx, uint32_t offset, uint8_t *buf, size_t n);
    void *ctx;
    uint32_t len;
} ds_source;

#define DS_OUT_EXTERNAL 0
#define DS_OUT_CHANGE 1 /* our derivation, account path + /1/i */
#define DS_OUT_OWN 2    /* our derivation, account path + /0/i (receive) */

typedef struct {
    uint64_t amount;
    uint8_t script[DS_TX_SCRIPT_KEEP]; /* first script_len bytes */
    uint8_t script_len;                /* min(full_len, DS_TX_SCRIPT_KEEP) */
    uint16_t full_len;
    uint8_t kind; /* DS_OUT_* */
} ds_out;

/* Per input, for the signing pass. */
typedef struct {
    uint8_t outpoint[36]; /* txid (internal order) and vout, little-endian */
    uint32_t sequence;
    uint64_t amount;
    uint32_t index; /* when ours: the key is account/chain/index */
    uint8_t chain;  /* 0 receive, 1 change */
    uint8_t ours;
} ds_psbt_input;

typedef struct {
    uint32_t n_in, n_out, n_ours;
    uint64_t total_in, fee;
    int fee_warning;
    int testnet;
    ds_out out[DS_MAX_OUTPUTS];

    /* For the signing pass. */
    ds_psbt_input in[DS_MAX_INPUTS];
    uint32_t version, locktime;
    uint8_t hash_prevouts[32], hash_sequence[32], hash_outputs[32];
    uint8_t psbt_sha256[32]; /* of the whole source, to detect a change */
} ds_psbt_review;

/* Analyzes the PSBT in src for wallet w. Returns DS_OK and fills *out, or one
 * of the errors above (DS_ERR_ARG for NULL arguments); on error *out is
 * zeroed. */
int32_t ds_psbt_analyze(const ds_source *src, const ds_wallet *w,
                        ds_psbt_review *out);

/* The review as plain English text, one line per item, each ending in '\n':
 *   OUTPUT n: <amount> BTC to <address>   (or "to script, N bytes")
 *   with ", CHANGE" or ", OWN" appended for our outputs; then
 *   FEE: <amount> BTC
 *   WARNING: high fee                      (when fee_warning)
 *   INPUT n NOT SIGNED: not ours           (for each input that is not ours)
 * n counts from 1 (for people; the PSBT's index is n - 1); amounts have 8
 * decimals. Returns the
 * length without the NUL (the text is NUL-terminated), DS_ERR_SPACE if cap is
 * too small, or DS_ERR_ARG. */
int32_t ds_psbt_summary_text(const ds_psbt_review *r, char *out, size_t cap);

#endif
