#include <string.h>

#include "ds_core.h"
#include "ds_multitap.h"

/* The cycle of each key: the digit first, then its letters. */
static const char *cycle_of(char key)
{
    switch (key) {
    case '0': return "0";
    case '1': return "1";
    case '2': return "2ABC";
    case '3': return "3DEF";
    case '4': return "4GHI";
    case '5': return "5JKL";
    case '6': return "6MNO";
    case '7': return "7PQRS";
    case '8': return "8TUV";
    case '9': return "9WXYZ";
    default:  return NULL;
    }
}

/* The key whose cycle holds c (the cycles do not overlap). */
static char key_of(char c)
{
    char k;
    for (k = '0'; k <= '9'; k++) {
        if (strchr(cycle_of(k), c) != NULL) {
            return k;
        }
    }
    return 0;
}

static void append(ds_multitap *m, char c)
{
    m->text[m->len++] = c;
    m->text[m->len] = '\0';
}

/* Commits the pending character, if any. */
static int commit(ds_multitap *m)
{
    if (!m->has_pending) {
        return 0;
    }
    append(m, m->pending);
    m->has_pending = 0;
    m->presses = 0;
    return 1;
}

void ds_multitap_init(ds_multitap *m)
{
    ds_wipe(m, sizeof *m);
}

int ds_multitap_key(ds_multitap *m, char key, uint32_t now_ms)
{
    const char *cycle;
    char c;
    int committed = 0;
    int same;

    /* A pending character that has timed out goes first. */
    if (m->has_pending && (uint32_t)(now_ms - m->last_ms) >= DS_MT_TIMEOUT_MS) {
        commit(m);
        committed = 1;
    }

    if (key == '#') {
        if (commit(m) || committed) {
            return DS_MT_COMMIT;
        }
        return DS_MT_DONE;
    }
    if (key == '*') {
        m->presses = 0;
        if (m->has_pending) {
            m->has_pending = 0;
            return DS_MT_BACK;
        }
        if (m->len > 0) {
            m->len--;
            m->text[m->len] = '\0';
            return DS_MT_BACK;
        }
        return DS_MT_CANCEL;
    }

    cycle = cycle_of(key);
    if (cycle == NULL) {
        return committed ? DS_MT_COMMIT : DS_MT_NONE;
    }

    same = m->has_pending && key_of(m->pending) == key;
    if (!same) {
        /* A different key commits the pending character first. */
        if (commit(m)) {
            committed = 1;
        }
        if (m->len >= DS_MT_MAX) {
            return committed ? DS_MT_COMMIT : DS_MT_NONE;
        }
        m->presses = 0;
    }

    m->presses++;
    c = cycle[(m->presses - 1) % strlen(cycle)];
    m->pending = c;
    m->has_pending = 1;
    m->last_ms = now_ms;
    return committed ? DS_MT_COMMIT : DS_MT_SHOW;
}

int ds_multitap_tick(ds_multitap *m, uint32_t now_ms)
{
    if (m->has_pending && (uint32_t)(now_ms - m->last_ms) >= DS_MT_TIMEOUT_MS) {
        commit(m);
        return DS_MT_COMMIT;
    }
    return DS_MT_NONE;
}

void ds_multitap_wipe(ds_multitap *m)
{
    ds_wipe(m, sizeof *m);
}
