/* DTMF-Signer device logic: the About pages. See ds_about.h. */
#include "ds_about.h"

#include <stdio.h>
#include <string.h>

#include "qrcodegen.h"

/* The catalog's capabilities, in its order, with a short name. */
static const struct {
    const char *code, *name;
} CAPS[] = {
    {"TONES-SW", "Tones in software"},
    {"TONES-CHIP", "Tones from a chip"},
    {"IN-MIC", "Microphone"},
    {"IN-LINE", "Line input"},
    {"IN-UART", "Serial input"},
    {"IN-PS2", "PS/2 keyboard"},
    {"IN-PHONEPAD", "Phone keypad"},
    {"IN-BUTTONS", "Buttons"},
    {"OUT-AIR", "Tones through air"},
    {"OUT-LINE", "Line output"},
    {"OUT-UART", "Serial output"},
    {"OUT-QR", "QR codes"},
    {"VERIFY-SCREEN", "Its own screen"},
    {"VERIFY-TERMINAL", "A terminal"},
    {"VERIFY-TV", "A television"},
    {"VERIFY-LISTENERS", "Several listeners"},
    {"STORE-SEED", "Keeps the seed"},
    {"BTC-TAPROOT", "Taproot"},
    {"BTC-MULTISIG", "Multisig"},
    {"BTC-PATHS", "Other accounts"},
    {"BTC-INPUT-AMOUNTS", "Checked amounts"},
    {"SPLIT-MCU", "Several chips"},
    {"SEEDCRAFT-CONSOLE", "SeedCraft console"},
};
#define N_CAPS (sizeof CAPS / sizeof CAPS[0])
#define CAPS_PER_PAGE 6
#define CAP_PAGES ((N_CAPS + CAPS_PER_PAGE - 1) / CAPS_PER_PAGE)

uint32_t ds_about_pages(void)
{
    return 1 + CAP_PAGES + 1;
}

int ds_about_has(const char *code)
{
    const char *caps = DS_CAPS, *p = caps;
    size_t n = strlen(code);
    while ((p = strstr(p, code)) != NULL) {
        int start = p == caps || p[-1] == ' ';
        int end = p[n] == '\0' || p[n] == ' ';
        if (start && end) {
            return 1;
        }
        p += n;
    }
    return 0;
}

/* Row 0: the title, then "k/N" at the right. */
static void title(ds_screen *s, const char *t, uint32_t i)
{
    char pos[12];
    size_t lp;
    snprintf(pos, sizeof pos, "%u/%u", (unsigned)(i + 1), (unsigned)ds_about_pages());
    lp = strlen(pos);
    memset(s->line[0], ' ', DS_UI_COLS);
    memcpy(s->line[0], t, strlen(t));
    memcpy(s->line[0] + DS_UI_COLS - lp, pos, lp);
    s->line[0][DS_UI_COLS] = '\0';
}

void ds_about_page(uint32_t i, ds_screen *s)
{
    const char *version = DS_VERSION;
    memset(s, 0, sizeof *s);
    s->highlight = -1;
    s->about = (int)i + 1;
    if (i == 0) {
        title(s, "About", i);
        strcpy(s->line[1], "Firmware");
        {
            /* A long version breaks after a '-', over two rows. */
            size_t n = strlen(version), cut = n;
            if (n > DS_UI_COLS) {
                cut = DS_UI_COLS;
                while (cut > 1 && version[cut - 1] != '-') {
                    cut--;
                }
                if (cut <= 1) {
                    cut = DS_UI_COLS;
                }
            }
            snprintf(s->line[2], sizeof s->line[2], "%.*s", (int)cut, version);
            snprintf(s->line[3], sizeof s->line[3], "%.21s", version + cut);
        }
        snprintf(s->line[4], sizeof s->line[4], "%.21s", DS_PROFILE);
        snprintf(s->line[5], sizeof s->line[5], "%.21s", DS_HARDWARE);
        strcpy(s->line[7], DS_ICON_UP DS_ICON_DOWN " pages  " DS_ICON_LEFT " back");
        s->footer = 1;
    } else if (i <= CAP_PAGES) {
        size_t k, first = (i - 1) * CAPS_PER_PAGE;
        title(s, "Capabilities", i);
        for (k = 0; k < CAPS_PER_PAGE && first + k < N_CAPS; k++) {
            snprintf(s->line[1 + k], sizeof s->line[1 + k], "%s %s",
                     ds_about_has(CAPS[first + k].code) ? DS_ICON_ON : DS_ICON_OFF,
                     CAPS[first + k].name);
        }
    } else {
        /* The QR code takes x 0-53; the text starts at column 10. */
        char pos[12];
        snprintf(pos, sizeof pos, "%u/%u", (unsigned)(i + 1), (unsigned)ds_about_pages());
        strcpy(s->line[0], "          Code and");
        strcpy(s->line[1], "          build guide");
        strcpy(s->line[3], "          github.com/");
        strcpy(s->line[4], "          ndssigner/");
        strcpy(s->line[5], "          dtmf-signer");
        snprintf(s->line[7], sizeof s->line[7], "%21s", pos);
    }
}

void ds_about_draw(const ds_screen *s, uint8_t fb[8][128])
{
    uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(3)], tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(3)];
    int n, box, off, x, y;

    if (s->about != (int)ds_about_pages()) {
        return;
    }
    if (!qrcodegen_encodeText(DS_REPO_QR, tmp, qr, qrcodegen_Ecc_LOW, 1, 3,
                              qrcodegen_Mask_AUTO, 1)) {
        return;
    }
    n = qrcodegen_getSize(qr);
    box = 2 * (n + 2);
    off = (64 - box) / 2;
    for (y = 0; y < box; y++) {
        for (x = 0; x < box; x++) {
            int mx = x / 2 - 1, my = y / 2 - 1;
            int dark = mx >= 0 && my >= 0 && mx < n && my < n && qrcodegen_getModule(qr, mx, my);
            int px = off + x, py = off + y;
            if (dark) {
                fb[py / 8][px] &= (uint8_t)~(1u << (py % 8));
            } else {
                fb[py / 8][px] |= (uint8_t)(1u << (py % 8));
            }
        }
    }
}
