/* Nintendo DS port: the microphone, through calico.
 *
 * It records 16-bit PCM at SOUND_MIXER_FREQ_HZ / 2 (about 16364 Hz) into two
 * alternating DMA buffers; the interrupt copies each full buffer into a
 * ring, which the main loop drains. On the DS (NTR mode) calico samples the
 * microphone from an ARM7 timer instead of by DMA: the API is the same.
 * The microphone and its amplifier are on only between nds_mic_start and
 * nds_mic_stop_wipe. */
#ifndef NDS_MIC_H
#define NDS_MIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <nds.h>

/* The rate of the samples nds_mic_read gives (for ds_app_init). */
#define NDS_MIC_RATE (SOUND_MIXER_FREQ_HZ / 2)

/* Starts recording (amplifier gain 160); true on success. */
bool nds_mic_start(void);

/* Up to cap samples, oldest first; returns how many. *lost: samples dropped
 * because the ring was full, reported once the ring is drained (they came
 * after the samples still queued), else 0. */
size_t nds_mic_read(int16_t *out, size_t cap, uint32_t *lost);

/* Stops recording, turns the amplifier off and zeroes the DMA buffers and
 * the ring (what was heard may have been a seed). Safe when not running. */
void nds_mic_stop_wipe(void);

#endif
