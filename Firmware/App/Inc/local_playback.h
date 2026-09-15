#ifndef LOCAL_PLAYBACK_H
#define LOCAL_PLAYBACK_H

#include "device_bus.h"
#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOCAL_CODEC_NONE = 0,
    LOCAL_CODEC_WAV_PCM,
    LOCAL_CODEC_MP3
} local_codec_t;

typedef struct {
    uint32_t sample_rate_hz;
    uint16_t channels;
    uint16_t bits_per_sample;
} local_audio_format_t;

typedef dev_status_t (*local_file_open_fn)(void *ctx, const char *path, void **file);
typedef dev_status_t (*local_file_read_fn)(void *ctx, void *file, void *data, size_t requested, size_t *read);
typedef dev_status_t (*local_file_seek_fn)(void *ctx, void *file, uint32_t absolute_offset);
typedef dev_status_t (*local_file_tell_fn)(void *ctx, void *file, uint32_t *offset);
typedef void (*local_file_close_fn)(void *ctx, void *file);

typedef struct {
    void *ctx;
    local_file_open_fn open;
    local_file_read_fn read;
    local_file_seek_fn seek;
    local_file_tell_fn tell;
    local_file_close_fn close;
} local_fs_t;

typedef dev_status_t (*local_pcm_sink_fn)(void *ctx, const int16_t *interleaved,
                                         size_t frames, uint16_t channels,
                                         uint32_t sample_rate_hz);
typedef struct {
    void *ctx;
    local_pcm_sink_fn write;
} local_pcm_sink_t;

/* MP3 decode adapter; intended to be bound to minimp3 by the integration layer. */
typedef struct {
    void *ctx;
    dev_status_t (*reset)(void *ctx);
    dev_status_t (*decode)(void *ctx, const uint8_t *input, size_t input_len,
                           size_t *consumed, int16_t *pcm, size_t pcm_capacity_frames,
                           size_t *frames, local_audio_format_t *format);
} local_mp3_decoder_t;

/* minimp3's streaming API requires enough look-ahead to hold at least ten
 * worst-case frames while it establishes/resynchronises frame boundaries. */
#define LOCAL_MP3_INPUT_BUFFER_BYTES (16U * 1024U)

typedef struct {
    local_fs_t fs;
    local_pcm_sink_t sink;
    local_mp3_decoder_t mp3;
    void *file;
    local_codec_t codec;
    local_audio_format_t format;
    uint32_t data_offset;
    uint32_t data_bytes_remaining;
    bool playing;
    bool eof;
    bool source_eof;
    char path[APP_PATH_MAX];
    /* Decoded/read PCM is retained here until the non-blocking audio sink has
     * room. This prevents FIFO back-pressure from silently dropping a frame. */
    int16_t pending_pcm[APP_LOCAL_PCM_FRAMES_PER_CHUNK * 2U];
    size_t pending_frames;
    local_audio_format_t pending_format;
    uint8_t input_buffer[LOCAL_MP3_INPUT_BUFFER_BYTES];
    size_t input_valid;
} local_playback_t;

dev_status_t local_playback_init(local_playback_t *player, const local_fs_t *fs,
                                 const local_pcm_sink_t *sink,
                                 const local_mp3_decoder_t *mp3_decoder);
dev_status_t local_playback_open(local_playback_t *player, const char *path);
dev_status_t local_playback_process(local_playback_t *player);
dev_status_t local_playback_set_playing(local_playback_t *player, bool playing);
void local_playback_close(local_playback_t *player);

#ifdef __cplusplus
}
#endif

#endif
