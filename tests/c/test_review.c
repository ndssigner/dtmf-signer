#include <stdint.h>
#include <string.h>

#include "ds_review.h"
#include "ds_wallet.h"
#include "psbt_vectors.h"
#include "test.h"

static ds_psbt_review rv;
static ds_screen scr;
static uint8_t fb[8][128];

typedef struct {
    const uint8_t *p;
    uint32_t len;
} mem_src;

static int32_t mem_read(void *ctx, uint32_t off, uint8_t *buf, size_t n)
{
    const mem_src *m = ctx;
    if (off > m->len || n > m->len - off) {
        return DS_ERR_ARG;
    }
    memcpy(buf, m->p + off, n);
    return (int32_t)n;
}

/* Analyzes the vector called name into rv. */
static void analyze(const char *name)
{
    size_t k;
    ds_wallet w;
    mem_src m;
    ds_source src;
    for (k = 0; k < sizeof psbt_vectors / sizeof psbt_vectors[0]; k++) {
        if (strcmp(psbt_vectors[k].name, name) == 0) {
            break;
        }
    }
    CHECK(k < sizeof psbt_vectors / sizeof psbt_vectors[0]);
    m.p = psbt_vectors[k].psbt;
    m.len = (uint32_t)psbt_vectors[k].psbt_len;
    src.read = mem_read;
    src.ctx = &m;
    src.len = m.len;
    CHECK_EQ_INT(ds_wallet_load(&w, psbt_vectors[k].entropy, psbt_vectors[k].entropy_len,
                                psbt_vectors[k].passphrase, psbt_vectors[k].testnet),
                 DS_OK);
    CHECK_EQ_INT(ds_psbt_analyze(&src, &w, &rv), DS_OK);
    ds_wallet_wipe(&w);
}

static int lit(int x, int y)
{
    return (fb[y / 8][x] >> (y % 8)) & 1;
}

/* The address rows joined without their spaces. */
static void joined(char *out)
{
    int r;
    out[0] = '\0';
    for (r = scr.addr_from; r < scr.addr_from + scr.addr_rows; r++) {
        const char *c;
        for (c = scr.line[r]; *c; c++) {
            if (*c != ' ') {
                strncat(out, c, 1);
            }
        }
    }
}

/* One card per output, then the fee, then the confirmation. Each output
 * says first what it is, then its amount, then its whole address in groups
 * of 4, never split. */
static void test_review_cards(void)
{
    char a[128];
    int i;

    analyze("one input with change");
    CHECK_EQ_INT(ds_review_cards(&rv), 4);

    ds_review_card(&rv, 0, 1, &scr);
    CHECK(scr.review);
    CHECK_EQ_INT(scr.chip, 0);
    CHECK(scr.tall);
    CHECK(strncmp(scr.line[0], "Send to", 7) == 0);
    CHECK(strcmp(scr.line[0] + strlen(scr.line[0]) - 3, "1/4") == 0);
    CHECK_EQ_INT(strlen(scr.line[0]), DS_UI_COLS);
    CHECK_EQ_STR(scr.line[1], "0.00060000 BTC");
    CHECK_EQ_INT(scr.addr_from, 2);
    CHECK_EQ_INT(scr.addr_rows, 3);
    CHECK_EQ_STR(scr.line[2], "bc1q kd33 yck7 4lg0");
    CHECK_EQ_STR(scr.line[3], "kaq4 tdcm u3hk 4yru");
    CHECK_EQ_STR(scr.line[4], "hjay xpe9 ug");
    CHECK_EQ_INT(scr.highlight, -1);

    ds_review_card(&rv, 1, 1, &scr);
    CHECK(strncmp(scr.line[0], "Change, yours", 13) == 0);
    CHECK_EQ_INT(scr.chip, 13);
    CHECK_EQ_STR(scr.line[1], "0.00039000 BTC");
    joined(a);
    CHECK_EQ_STR(a, "bc1q8c6fshw2dlwun7ekn9qwf37cu2rn755upcp6el");

    ds_review_card(&rv, 2, 1, &scr);
    CHECK(strncmp(scr.line[0], "Fee", 3) == 0);
    CHECK_EQ_STR(scr.line[1], "0.00001000 BTC");
    CHECK_EQ_STR(scr.line[2], "Sending    0.00060000");
    CHECK_EQ_STR(scr.line[3], "Change     0.00039000");
    CHECK_EQ_STR(scr.line[4], "Total      0.00061000");
    CHECK_EQ_INT(scr.addr_rows, 0);

    /* The confirmation: what goes out, then how to sign; until a press
     * has armed the review, how to arm it. */
    ds_review_card(&rv, 3, 1, &scr);
    CHECK(strncmp(scr.line[0], "Sign?", 5) == 0);
    CHECK_EQ_STR(scr.line[1], "0.00061000 BTC");
    CHECK_EQ_STR(scr.line[2], "out, fee included");
    CHECK_EQ_INT(scr.highlight, 4);
    CHECK(strstr(scr.line[4], "Hold OK: sign") != NULL);
    CHECK_EQ_STR(scr.line[5], DS_ICON_LEFT " cancel");
    ds_review_card(&rv, 3, 0, &scr);
    CHECK(strstr(scr.line[4], "Tap OK, then hold OK") != NULL);

    for (i = 0; i < 4; i++) {
        int r;
        ds_review_card(&rv, (uint32_t)i, 1, &scr);
        for (r = 0; r < DS_UI_ROWS; r++) {
            CHECK(strlen(scr.line[r]) <= DS_UI_COLS);
        }
    }
}

/* Taproot (62 characters, 4 rows), P2SH, a script without an address, and
 * an output to our own receive address. */
static void test_review_kinds(void)
{
    char a[128];
    uint32_t i, n;
    int seen_tr = 0, seen_script = 0;

    analyze("outputs to P2TR, P2SH and OP_RETURN");
    n = ds_review_cards(&rv);
    CHECK_EQ_INT(n, rv.n_out + 2);
    for (i = 0; i < rv.n_out; i++) {
        ds_review_card(&rv, i, 1, &scr);
        CHECK(scr.tall);
        if (strncmp(scr.line[2], "bc1p", 4) == 0) {
            seen_tr = 1;
            CHECK_EQ_INT(scr.addr_rows, 4);
            joined(a);
            CHECK_EQ_INT(strlen(a), 62);
        }
        if (strncmp(scr.line[2], "Script, ", 8) == 0) {
            seen_script = 1;
            CHECK_EQ_INT(scr.addr_rows, 0);
        }
    }
    CHECK(seen_tr);
    CHECK(seen_script);

    analyze("receive-address output counts as own");
    for (i = 0, n = 0; i < rv.n_out; i++) {
        ds_review_card(&rv, i, 1, &scr);
        if (strncmp(scr.line[0], "To this wallet", 14) == 0) {
            CHECK_EQ_INT(scr.chip, 14);
            n++;
        }
    }
    CHECK(n >= 1);
}

/* The longest address there is, P2TR (62 characters, testnet too), fits
 * 4 rows under a tall amount; a witness program the firmware has no address
 * for is shown as a script. Amounts are never cut, not even 21 million BTC. */
static void test_review_longest_address(void)
{
    char a[128];
    memset(&rv, 0, sizeof rv);
    rv.testnet = 1;
    rv.n_out = 2;
    rv.n_ours = 1;
    rv.out[0].amount = 2100000000000000ull;
    rv.out[0].script[0] = 0x51;
    rv.out[0].script[1] = 32;
    memset(rv.out[0].script + 2, 0x11, 32);
    rv.out[0].script_len = 34;
    rv.out[0].full_len = 34;
    rv.out[1].script[0] = 0x52;
    rv.out[1].script[1] = 40;
    rv.out[1].script_len = 42;
    rv.out[1].full_len = 42;
    ds_review_card(&rv, 0, 1, &scr);
    CHECK(scr.tall);
    CHECK_EQ_STR(scr.line[1], "21000000.00000000 BTC");
    CHECK_EQ_INT(scr.addr_rows, 4);
    joined(a);
    CHECK_EQ_INT(strlen(a), 62);
    CHECK(strncmp(a, "tb1p", 4) == 0);
    ds_review_card(&rv, 1, 1, &scr);
    CHECK_EQ_STR(scr.line[2], "Script, 42 bytes");
    ds_review_card(&rv, 2, 1, &scr); /* the fee card */
    CHECK(strstr(scr.line[2], "21000000.00000000") != NULL);
    CHECK(strlen(scr.line[2]) <= DS_UI_COLS);
}

/* Warnings get a card of their own before the confirmation. */
static void test_review_warnings(void)
{
    uint32_t n;

    analyze("fee above 1000000 sat");
    CHECK(rv.fee_warning);
    n = ds_review_cards(&rv);
    CHECK_EQ_INT(n, rv.n_out + 3);
    ds_review_card(&rv, n - 2, 1, &scr);
    CHECK(strncmp(scr.line[0], "Check", 5) == 0);
    CHECK_EQ_STR(scr.line[1], "High fee");

    analyze("foreign input is not signed");
    n = ds_review_cards(&rv);
    ds_review_card(&rv, n - 2, 1, &scr);
    CHECK(strncmp(scr.line[0], "Check", 5) == 0);
    CHECK(strstr(scr.line[1], "not ours") != NULL || strstr(scr.line[2], "not ours") != NULL);
}

/* The drawing: the amount twice as tall, the title's chip inverted, the
 * bar inverted, and in addresses g, j, p, q and y going below the line
 * while 9 stays as it is. */
static void test_review_draw(void)
{
    memset(&scr, 0, sizeof scr);
    scr.highlight = -1;
    scr.review = 1;
    scr.tall = 1;
    scr.chip = 3;
    strcpy(scr.line[0], "Abc");
    strcpy(scr.line[1], "8");
    strcpy(scr.line[2], "g9");
    scr.addr_from = 2;
    scr.addr_rows = 1;
    strcpy(scr.line[4], "bar");
    scr.highlight = 4;
    memset(fb, 0xA5, sizeof fb);
    ds_review_draw(&scr, fb);
    CHECK(lit(0, 0));                /* the chip's box */
    CHECK(lit(2, 11) && lit(2, 12)); /* '8' top row, doubled */
    CHECK(!lit(2, 10));
    CHECK(lit(2, 32 + 7));           /* g: its tail on the row below the line */
    CHECK(!lit(2, 32 + 1));          /* g starts lower than ... */
    CHECK(lit(8, 32 + 0));           /* ... 9, which keeps its top row */
    CHECK(!lit(8, 32 + 7));
    CHECK(lit(127, 48));             /* the bar, across the whole width */
    CHECK(!lit(127, 63));
}

TEST_MAIN(test_review_cards, test_review_kinds, test_review_longest_address,
          test_review_warnings, test_review_draw)
