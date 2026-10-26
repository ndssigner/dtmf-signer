/* DTMF-Signer portable core: channel policy and the session state machine.
 *
 * The policy says which kind of data a channel may carry (SPEC 4.2). The
 * session enforces the signing invariants on top of the wallet, the PSBT
 * review and the signing pass:
 *
 *   - nothing is signed without a review of that very PSBT and a physical
 *     confirmation after it (the port must show the review before it calls
 *     ds_session_confirm);
 *   - a new PSBT discards the previous review and confirmation;
 *   - signing, successful or not, consumes the confirmation;
 *   - wipe returns the whole session to zero.
 *
 *   EMPTY --seed--> SEED --passphrase--> READY --review--> REVIEW
 *   REVIEW --confirm--> CONFIRMED --sign--> READY      (cancel: -> READY)
 *
 * A call made in a state that does not allow it returns DS_ERR_STATE and
 * changes nothing, unless said otherwise. A wiped session is in no state. The session holds secrets (the
 * seed entropy, kept to derive again with a passphrase, and the account
 * key); ds_session_wipe clears them, and so does any failure that cannot be
 * retried. It uses about 4 KB, most of it the review. */
#ifndef DS_SESSION_H
#define DS_SESSION_H

#include "ds_sign.h"
#include "ds_wallet.h"

/* Channel classes. DS_CABLE_TERMINAL is a cable with a terminal on the
 * other end: sensitive data needs physical confirmation. */
#define DS_AIR 1
#define DS_CABLE 2
#define DS_CABLE_TERMINAL 3
#define DS_LOCAL 4

/* Kinds of data. */
#define DS_DATA_PSBT 1
#define DS_DATA_SEED_ENCRYPTED 2
#define DS_DATA_SEED_PLAIN 3
#define DS_DATA_PIN 4
#define DS_DATA_PASSPHRASE 5

/* Whether a channel may carry the data:
 *   PSBT             air, cable, cable with terminal; not local (DS_ERR_POLICY)
 *   SEED_ENCRYPTED   cable; air only with pin_len >= 12 (DS_WARN_AIR, the port
 *                    must show the warning); terminal: DS_NEED_CONFIRM
 *   SEED_PLAIN       cable; terminal: DS_NEED_CONFIRM; nothing else
 *   PIN, PASSPHRASE  cable and local; terminal: DS_NEED_CONFIRM; never air
 * Returns DS_OK, DS_WARN_AIR, DS_NEED_CONFIRM or DS_ERR_POLICY (also for an
 * unknown data or channel). pin_len only matters for SEED_ENCRYPTED. */
int32_t ds_policy_accepts(int data, int channel, size_t pin_len);

/* The states start at 1: an all-zero struct (a wiped session) is in no state,
 * so every call on it returns DS_ERR_STATE until ds_session_init. */
enum { DS_S_EMPTY = 1, DS_S_SEED, DS_S_READY, DS_S_REVIEW, DS_S_CONFIRMED };

typedef struct {
    ds_wallet w;
    ds_psbt_review r;
    uint8_t entropy[32]; /* secret */
    size_t entropy_len;
    int testnet;
    int state;
} ds_session;

/* An empty session for mainnet, or testnet if testnet is not 0. */
void ds_session_init(ds_session *s, int testnet);

/* EMPTY -> SEED. Applies the policy for SEED_ENCRYPTED (encrypted != 0) or
 * SEED_PLAIN on the channel; pin_len is the PIN length used for the encrypted
 * seed. entropy is the decrypted seed (16 or 32 bytes). Returns DS_OK,
 * DS_WARN_AIR (accepted, show the air warning), DS_NEED_CONFIRM (nothing
 * done: call again with confirmed != 0 after the physical confirmation),
 * DS_ERR_POLICY, DS_ERR_ARG (bad length or NULL), DS_ERR_STATE. */
int32_t ds_session_seed(ds_session *s, const uint8_t *entropy, size_t len,
                        int encrypted, size_t pin_len, int channel,
                        int confirmed);

/* SEED -> READY. Derives the wallet with the passphrase (NULL: none; no data,
 * so no policy check). Policy as for DS_DATA_PASSPHRASE; DS_NEED_CONFIRM as
 * for the seed. A passphrase the wallet rejects (DS_ERR_PASSPHRASE) leaves
 * the session in SEED to try again; any other derivation failure wipes the
 * session. */
int32_t ds_session_passphrase(ds_session *s, const char *text, int channel,
                              int confirmed);

/* READY, REVIEW or CONFIRMED -> REVIEW. Any previous review and confirmation
 * are discarded first, even if this call fails (then the state is READY).
 * Policy for DS_DATA_PSBT, then ds_psbt_analyze; its errors are returned. */
int32_t ds_session_review(ds_session *s, const ds_source *src, int channel);

/* REVIEW -> CONFIRMED. Call only after the user has seen the review and
 * confirmed physically. */
int32_t ds_session_confirm(ds_session *s);

/* CONFIRMED -> READY, whatever the result. src must be the reviewed PSBT.
 * Returns what ds_psbt_sign returns; on error the caller discards the sink's
 * output. */
int32_t ds_session_sign(ds_session *s, const ds_source *src,
                        const ds_sink *dst);

/* REVIEW or CONFIRMED -> READY without signing; a no-op in READY. */
int32_t ds_session_cancel(ds_session *s);

/* Everything to zero. The zeroed session is in no state (and has forgotten
 * its network): every call on it returns DS_ERR_STATE until ds_session_init. */
void ds_session_wipe(ds_session *s);

#endif
