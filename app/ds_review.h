/* DTMF-Signer device logic: the review of a PSBT as cards on the 128x64
 * screen, the last thing seen before signing.
 *
 * One card per output: what it is ("Send to", or "Change, yours" and
 * "To this wallet" inverted, only for outputs the analysis proved ours),
 * its amount with all 8 decimals, twice as tall, and its whole address in
 * groups of 4, four groups per row, never split across cards (the longest,
 * P2WSH or P2TR, 62 characters, takes 4 rows; a longer one
 * would take 5 with the amount at normal height). A script with no address
 * is shown as "Script, N bytes". Then the fee with what is sent, the change
 * and the total; then, only when there are any, the warnings (a high fee,
 * inputs that will not be signed); then the confirmation, with what goes
 * out and how to sign. Each card says "k/N" at the top right.
 *
 * In addresses g, j, p, q and y are drawn with a tail below the line, so g
 * and q do not look like 9, which keeps its glyph (taller, the mirror of
 * 6). No malloc, no globals. */
#ifndef DS_REVIEW_H
#define DS_REVIEW_H

#include <stdint.h>

#include "ds_psbt.h"
#include "ds_ui.h"

/* The number of cards: outputs + fee + (warnings) + confirmation. */
uint32_t ds_review_cards(const ds_psbt_review *r);

/* Fills s with card i (from 0) as text with its layout marks (s->review,
 * chip, tall, addr_from/addr_rows, highlight for the confirmation's bar).
 * armed: the confirmation says "Hold OK: sign"; otherwise
 * "Tap OK, then hold OK". Amounts are never cut. */
void ds_review_card(const ds_psbt_review *r, uint32_t i, int armed, ds_screen *s);

/* Draws a card filled by ds_review_card over the whole of fb. */
void ds_review_draw(const ds_screen *s, uint8_t fb[8][128]);

#endif
