/* Reads PCM WAV recordings for the computer build. */
#ifndef DS_WAV_H
#define DS_WAV_H

#include "ds_core.h"

/* Reads a RIFF/WAVE file of 16-bit PCM (mono; from a stereo file, the left
 * channel). Stores up to cap samples in `samples` and the sample rate in
 * *rate. With samples NULL it only counts. Returns the number of samples or
 * a negative DS_ERR_*: DS_ERR_ARG (cannot open or read the file),
 * DS_ERR_AUDIO (not a WAV this reader understands), DS_ERR_SPACE (more than
 * cap samples). */
int32_t ds_wav_read(const char *path, int16_t *samples, size_t cap, uint32_t *rate);

#endif
