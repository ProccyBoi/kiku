#include "audio_router.h"

#include <string.h>

static dev_status_t apply_route(audio_router_t *router)
{
    if ((router == NULL) || (router->io == NULL)) return DEV_EINVAL;
    bool speaker;
    bool headphones;
    if (router->source == AUDIO_SOURCE_NONE) {
        speaker = false;
        headphones = false;
    } else {
    switch (router->output) {
    case AUDIO_OUTPUT_HEADPHONES:
        speaker = false;
        headphones = true;
        break;
    case AUDIO_OUTPUT_SPEAKER:
        speaker = true;
        headphones = false;
        break;
    case AUDIO_OUTPUT_BOTH:
        speaker = true;
        headphones = true;
        break;
    case AUDIO_OUTPUT_AUTO:
    default:
        /* The SJ1-3525N detect contact is authoritative in AUTO mode: use
         * headphones when a plug is present, otherwise the internal speaker. */
        headphones = router->headphones_present;
        speaker = !router->headphones_present;
        break;
    }
    }
    dev_status_t st = audio_io_set_speaker_enabled(router->io, speaker);
    if (st != DEV_OK) return st;
    if (router->ops.apply_codec_profile != NULL) {
        st = router->ops.apply_codec_profile(router->ops.ctx, router->source,
                                             headphones,
                                             router->volume_percent);
    }
    return st;
}

dev_status_t audio_router_init(audio_router_t *router, audio_io_t *io,
                               const audio_route_ops_t *ops)
{
    if ((router == NULL) || (io == NULL)) return DEV_EINVAL;
    memset(router, 0, sizeof(*router));
    router->io = io;
    if (ops != NULL) router->ops = *ops;
    router->source = AUDIO_SOURCE_NONE;
    router->output = AUDIO_OUTPUT_AUTO;
    router->speaker_enabled_by_user = true;
    router->volume_percent = 45U;
    router->headphones_present = audio_io_headphones_present(io);
    return apply_route(router);
}

dev_status_t audio_router_set_source(audio_router_t *router, audio_source_t source)
{
    if (router == NULL || source > AUDIO_SOURCE_BLUETOOTH) return DEV_EINVAL;
    if (source == router->source) return DEV_OK;
    if (router->ops.switch_source != NULL) {
        dev_status_t st = router->ops.switch_source(router->ops.ctx, source);
        if (st != DEV_OK) return st;
    }
    router->source = source;
    return apply_route(router);
}

dev_status_t audio_router_rearm_current_source(audio_router_t *router)
{
    if (router == NULL) return DEV_EINVAL;
    if (router->source == AUDIO_SOURCE_NONE) return DEV_ESTATE;
    if (router->ops.rearm_source == NULL) return DEV_ENOTSUP;
    return router->ops.rearm_source(router->ops.ctx, router->source);
}

dev_status_t audio_router_set_output(audio_router_t *router, audio_output_t output)
{
    if (router == NULL || output > AUDIO_OUTPUT_BOTH) return DEV_EINVAL;
    router->output = output;
    return apply_route(router);
}

dev_status_t audio_router_set_volume(audio_router_t *router, uint8_t volume_percent)
{
    if (router == NULL) return DEV_EINVAL;
    router->volume_percent = (volume_percent > 100U) ? 100U : volume_percent;
    return apply_route(router);
}

void audio_router_set_transition_gain(audio_router_t *router, uint16_t gain_permille)
{
    if (router == NULL || router->ops.set_transition_gain == NULL) return;
    if (gain_permille > 1000U) gain_permille = 1000U;
    router->ops.set_transition_gain(router->ops.ctx, gain_permille);
}

dev_status_t audio_router_poll_headphone(audio_router_t *router)
{
    if ((router == NULL) || (router->io == NULL)) return DEV_EINVAL;
    bool now = audio_io_headphones_present(router->io);
    if (now == router->headphones_present) return DEV_OK;
    router->headphones_present = now;
    return apply_route(router);
}
