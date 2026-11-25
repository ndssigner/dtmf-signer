/* Nintendo DS port: the microphone ring (see mic.h). */
#include "mic.h"

#include <string.h>

#include "ds_core.h"

#define BUF_SAMPLES 512 /* one DMA half: 1/32 s */
#define SLOTS 32        /* the ring: 1 s of slack */

static int16_t s_dma[2][BUF_SAMPLES] __attribute__((aligned(32)));
static int16_t s_ring[SLOTS][BUF_SAMPLES];
/* Slots written by the interrupt (head) and read by the loop (tail); pos:
 * samples already read from the tail slot. */
static volatile uint32_t s_head, s_tail, s_dropped;
static size_t s_pos;
static bool s_running, s_init;

static void on_buffer(void *user, void *buf, size_t size)
{
    (void)user;
    DC_InvalidateRange(buf, size);
    if (s_head - s_tail >= SLOTS) {
        s_dropped += BUF_SAMPLES;
        return;
    }
    memcpy(s_ring[s_head % SLOTS], buf, size < sizeof s_ring[0] ? size : sizeof s_ring[0]);
    s_head++;
}

static void wipe(void)
{
    ds_wipe(s_dma, sizeof s_dma);
    DC_FlushRange(s_dma, sizeof s_dma);
    ds_wipe(s_ring, sizeof s_ring);
    s_head = s_tail = s_dropped = 0;
    s_pos = 0;
}

bool nds_mic_start(void)
{
    if (s_running) {
        return true;
    }
    if (!s_init) {
        micInit();
        s_init = true;
    }
    wipe();
    if (!micSetDmaRate(MicRate_Div2)) { /* NDS_MIC_RATE */
        return false;
    }
    micSetCallback(on_buffer, NULL);
    pmMicSetAmp(true, PmMicGain_160);
    if (!micStart(s_dma, sizeof s_dma[0], MicFmt_Pcm16, MicMode_DoubleBuffer)) {
        pmMicSetAmp(false, 0);
        return false;
    }
    s_running = true;
    return true;
}

size_t nds_mic_read(int16_t *out, size_t cap, uint32_t *lost)
{
    size_t got = 0;

    *lost = 0;
    while (got < cap && s_tail != s_head) {
        size_t n = BUF_SAMPLES - s_pos;

        if (n > cap - got) {
            n = cap - got;
        }
        memcpy(out + got, s_ring[s_tail % SLOTS] + s_pos, n * sizeof out[0]);
        got += n;
        s_pos += n;
        if (s_pos == BUF_SAMPLES) {
            s_pos = 0;
            s_tail++;
        }
    }
    if (s_tail == s_head) {
        int old = enterCriticalSection();

        *lost = s_dropped;
        s_dropped = 0;
        leaveCriticalSection(old);
    }
    return got;
}

void nds_mic_stop_wipe(void)
{
    if (s_running) {
        micStop();
        pmMicSetAmp(false, 0);
        s_running = false;
    }
    wipe();
}
