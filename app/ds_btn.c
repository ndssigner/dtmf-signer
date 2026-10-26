#include "ds_btn.h"

void ds_btn_init(ds_btn *b)
{
    b->stable = DS_BTN_NONE;
    b->cand = DS_BTN_NONE;
    b->cand_since = 0;
    b->down_since = 0;
    b->long_sent = 0;
    b->wipe_sent = 0;
}

int ds_btn_feed(ds_btn *b, int cls, uint32_t now_ms, int *kind)
{
    int ev = DS_BTN_NONE;
    int k = 0;

    if (cls < DS_BTN_NONE || cls > DS_BTN_RIGHT) {
        cls = DS_BTN_NONE;
    }
    if (cls != b->cand) {
        b->cand = cls;
        b->cand_since = now_ms;
    }
    if (b->cand != b->stable && (uint32_t)(now_ms - b->cand_since) >= DS_BTN_DEBOUNCE_MS) {
        /* The candidate held long enough: the old button (if any) was released. */
        if (b->stable != DS_BTN_NONE && !b->long_sent && !b->wipe_sent &&
            (uint32_t)(b->cand_since - b->down_since) < DS_BTN_LONG_MS) {
            ev = b->stable;
            k = DS_BTN_PRESS;
        }
        b->stable = b->cand;
        b->down_since = b->cand_since;
        b->long_sent = 0;
        b->wipe_sent = 0;
    } else if (b->stable != DS_BTN_NONE) {
        uint32_t held = now_ms - b->down_since;
        if (!b->long_sent && held >= DS_BTN_LONG_MS) {
            b->long_sent = 1;
            ev = b->stable;
            k = DS_BTN_LONG;
        } else if (b->stable == DS_BTN_LEFT && !b->wipe_sent && held >= DS_BTN_WIPE_MS) {
            b->wipe_sent = 1;
            ev = b->stable;
            k = DS_BTN_WIPE;
        }
    }
    if (kind) {
        *kind = k;
    }
    return ev;
}
