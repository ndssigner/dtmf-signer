/* DTMF-Signer device logic: the button debouncer.
 *
 * The port reads the resistor ladder and turns the voltage into a class
 * (DS_BTN_NONE to DS_BTN_RIGHT); ds_btn_feed turns the stream of classes,
 * sampled about once per millisecond, into events:
 *  - a class counts only once it has been stable for 30 ms, so the voltages
 *    crossed while releasing a button never give a phantom press;
 *  - DS_BTN_PRESS when the button is released before 1500 ms;
 *  - DS_BTN_LONG on reaching 1500 ms held (once);
 *  - DS_BTN_WIPE when LEFT stays held 3000 ms (after its LONG).
 * After LONG or WIPE, releasing gives nothing. Going from one button to
 * another without 30 ms of NONE in between counts as releasing the first.
 * One event at most per call. No malloc, no globals. */
#ifndef DS_BTN_H
#define DS_BTN_H

#include <stdint.h>

enum { DS_BTN_NONE = 0, DS_BTN_LEFT, DS_BTN_UP, DS_BTN_OK, DS_BTN_DOWN, DS_BTN_RIGHT };
enum { DS_BTN_PRESS = 1, DS_BTN_LONG, DS_BTN_WIPE };

#define DS_BTN_DEBOUNCE_MS 30
#define DS_BTN_LONG_MS 1500
#define DS_BTN_WIPE_MS 3000

typedef struct {
    int stable, cand;
    uint32_t cand_since, down_since;
    int long_sent, wipe_sent;
} ds_btn;

void ds_btn_init(ds_btn *b);

/* cls is the class read at time now_ms (a free-running millisecond clock).
 * Returns the button of an event, with its kind (DS_BTN_PRESS, DS_BTN_LONG or
 * DS_BTN_WIPE) in *kind, or DS_BTN_NONE (and *kind 0) when there is none. */
int ds_btn_feed(ds_btn *b, int cls, uint32_t now_ms, int *kind);

#endif
