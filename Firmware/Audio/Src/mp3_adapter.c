#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include "mp3_adapter.h"

#include <limits.h>
#include <string.h>

static mp3dec_t s_decoder;

static dev_status_t decoder_reset(void *ctx)
{
    (void)ctx;
    mp3dec_init(&s_decoder);
    return DEV_OK;
}

static dev_status_t decoder_decode(void *ctx, const uint8_t *input, size_t input_len,
                                   size_t *consumed, int16_t *pcm,
                                   size_t pcm_capacity_frames, size_t *frames,
                                   local_audio_format_t *format)
{
    (void)ctx;
    if (input == NULL || consumed == NULL || pcm == NULL || frames == NULL || format == NULL) {
        return DEV_EINVAL;
    }
    if (input_len > INT_MAX || pcm_capacity_frames < MINIMP3_MAX_SAMPLES_PER_FRAME / 2U) {
        return DEV_EOVERFLOW;
    }
    mp3dec_frame_info_t info;
    memset(&info, 0, sizeof(info));
    int samples_per_channel = mp3dec_decode_frame(&s_decoder, input, (int)input_len, pcm, &info);
    if (info.frame_bytes < 0 || (size_t)info.frame_bytes > input_len) return DEV_EIO;
    *consumed = (size_t)info.frame_bytes;
    *frames = samples_per_channel > 0 ? (size_t)samples_per_channel : 0U;
    if (*frames != 0U) {
        if (info.channels < 1 || info.channels > 2 || info.hz <= 0) return DEV_EIO;
        format->channels = (uint16_t)info.channels;
        format->sample_rate_hz = (uint32_t)info.hz;
        format->bits_per_sample = 16U;
    }
    return DEV_OK;
}

local_mp3_decoder_t mp3_adapter_create(void)
{
    (void)decoder_reset(NULL);
    return (local_mp3_decoder_t){ .ctx = NULL, .reset = decoder_reset, .decode = decoder_decode };
}
