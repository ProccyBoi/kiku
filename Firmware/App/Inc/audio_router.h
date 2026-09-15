#ifndef AUDIO_ROUTER_H
#define AUDIO_ROUTER_H

#include "audio_io.h"
#include "tlv320aic3104.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_SOURCE_NONE = 0,
    AUDIO_SOURCE_LOCAL,
    AUDIO_SOURCE_FM,
    AUDIO_SOURCE_BLUETOOTH
} audio_source_t;

typedef enum {
    AUDIO_OUTPUT_AUTO = 0,
    AUDIO_OUTPUT_HEADPHONES,
    AUDIO_OUTPUT_SPEAKER,
    AUDIO_OUTPUT_BOTH
} audio_output_t;

typedef dev_status_t (*audio_source_switch_fn)(void *ctx, audio_source_t source);
typedef dev_status_t (*audio_source_rearm_fn)(void *ctx, audio_source_t source);
typedef dev_status_t (*audio_codec_profile_fn)(void *ctx, audio_source_t source,
                                               bool headphones_enabled,
                                               uint8_t volume_percent);
typedef void (*audio_transition_gain_fn)(void *ctx, uint16_t gain_permille);

typedef struct {
    void *ctx;
    audio_source_switch_fn switch_source;
    audio_source_rearm_fn rearm_source;
    audio_codec_profile_fn apply_codec_profile;
    audio_transition_gain_fn set_transition_gain;
} audio_route_ops_t;

typedef struct {
    audio_io_t *io;
    audio_route_ops_t ops;
    audio_source_t source;
    audio_output_t output;
    bool headphones_present;
    bool speaker_enabled_by_user;
    uint8_t volume_percent;
} audio_router_t;

dev_status_t audio_router_init(audio_router_t *router, audio_io_t *io,
                               const audio_route_ops_t *ops);
dev_status_t audio_router_set_source(audio_router_t *router, audio_source_t source);
dev_status_t audio_router_rearm_current_source(audio_router_t *router);
dev_status_t audio_router_set_output(audio_router_t *router, audio_output_t output);
dev_status_t audio_router_set_volume(audio_router_t *router, uint8_t volume_percent);
void audio_router_set_transition_gain(audio_router_t *router, uint16_t gain_permille);
dev_status_t audio_router_poll_headphone(audio_router_t *router);

#ifdef __cplusplus
}
#endif

#endif
