/* DTMF-Signer device logic: the About pages.
 *
 * The firmware's version, its profile and hardware, the capabilities of the
 * catalog (docs/catalog.md) with a filled box for those this build has and
 * an empty one for the rest (another build or other hardware has them),
 * and the repository as a QR code. What a build has comes from the build
 * itself: DS_VERSION (git describe), DS_PROFILE, DS_HARDWARE and DS_CAPS
 * (the catalog's codes, separated by spaces). No malloc, no globals. */
#ifndef DS_ABOUT_H
#define DS_ABOUT_H

#include <stdint.h>

#include "ds_ui.h"

#ifndef DS_VERSION
#define DS_VERSION "dev"
#endif
#ifndef DS_PROFILE
#define DS_PROFILE "Cable and QR"
#endif
#ifndef DS_HARDWARE
#define DS_HARDWARE "computer build"
#endif
#ifndef DS_CAPS
#define DS_CAPS "TONES-SW IN-LINE IN-BUTTONS VERIFY-SCREEN OUT-QR BTC-INPUT-AMOUNTS"
#endif

/* The repository, as shown and as put in the QR code (upper case keeps the
 * code small; the host and the path do not care). */
#define DS_REPO_TEXT "github.com/ndssigner/dtmf-signer"
#define DS_REPO_QR "HTTPS://GITHUB.COM/NDSSIGNER/DTMF-SIGNER"

/* The number of pages: the version, the capabilities, the repository. */
uint32_t ds_about_pages(void);

/* 1 if code (a catalog code such as "IN-LINE") is in DS_CAPS. */
int ds_about_has(const char *code);

/* Fills s with page i (from 0) and sets s->about to i + 1. */
void ds_about_page(uint32_t i, ds_screen *s);

/* After ds_ui_render: draws the QR code of the repository page, if s is
 * that page. */
void ds_about_draw(const ds_screen *s, uint8_t fb[8][128]);

#endif
