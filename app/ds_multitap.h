/* DTMF-Signer device logic: PIN entry by multi-tap, as on old phones.
 *
 * Keys 2 to 9 cycle through their letters (2 is "2ABC", 7 is "7PQRS"; the
 * digit comes first and the cycle wraps). 0 and 1 are just their digit.
 * Letters are uppercase. The pending character is not in text until it is
 * committed:
 *  - by a different key, or by 1500 ms without a key (ds_multitap_tick);
 *  - by '#' (which then only commits, and is not DONE).
 * '*' drops the pending character, or else deletes the last one; with
 * nothing to delete it is CANCEL. '#' with nothing pending is DONE. Other
 * keys (A to D) are ignored. A '*' after a timed-out pending character
 * commits it and then deletes it (BACK). The text holds at most DS_MT_MAX characters;
 * a new character past that is ignored.
 * The PIN is a secret: ds_multitap_wipe zeroes all of it.
 * Times are a free-running millisecond clock; differences wrap safely.
 * No malloc, no globals. */
#ifndef DS_MULTITAP_H
#define DS_MULTITAP_H

#include <stddef.h>
#include <stdint.h>

#define DS_MT_MAX 63
#define DS_MT_TIMEOUT_MS 1500

enum {
    DS_MT_NONE = 0, /* nothing to say */
    DS_MT_SHOW,     /* say m->pending (a new or changed pending character) */
    DS_MT_COMMIT,   /* a character was committed: play NEXT; m->pending is
                       the new pending character if has_pending is set */
    DS_MT_BACK,     /* a character was dropped or deleted */
    DS_MT_DONE,     /* '#' with nothing pending: the PIN is complete */
    DS_MT_CANCEL    /* '*' with nothing to delete: give up */
};

typedef struct {
    char text[64];   /* the committed PIN, NUL-terminated */
    size_t len;      /* characters in text, at most DS_MT_MAX */
    char pending;    /* the pending character, valid if has_pending */
    int presses;     /* presses of the pending key so far */
    uint32_t last_ms;
    int has_pending;
} ds_multitap;

void ds_multitap_init(ds_multitap *m);

/* A key at now_ms. Returns one of the DS_MT_ codes above. */
int ds_multitap_key(ds_multitap *m, char key, uint32_t now_ms);

/* Commits the pending character after DS_MT_TIMEOUT_MS without a key.
 * Returns DS_MT_COMMIT when it does, DS_MT_NONE otherwise. */
int ds_multitap_tick(ds_multitap *m, uint32_t now_ms);

/* Zeroes the whole struct (the PIN is a secret). */
void ds_multitap_wipe(ds_multitap *m);

#endif
