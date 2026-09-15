#include "audio_io.h"

dev_status_t audio_io_init(audio_io_t *io,
                           const dev_gpio_t *hp_detect,
                           const dev_gpio_t *speaker_sd_mode,
                           bool hp_detect_active_high,
                           bool speaker_enable_active_high)
{
    if ((io == NULL) || (hp_detect == NULL) || (speaker_sd_mode == NULL) ||
        (hp_detect->read == NULL) || (speaker_sd_mode->write == NULL)) return DEV_EINVAL;
    io->hp_detect = *hp_detect;
    io->speaker_sd_mode = *speaker_sd_mode;
    io->hp_detect_active_high = hp_detect_active_high;
    io->speaker_enable_active_high = speaker_enable_active_high;
    return DEV_OK;
}

bool audio_io_headphones_present(const audio_io_t *io)
{
    if ((io == NULL) || (io->hp_detect.read == NULL)) return false;
    bool raw = io->hp_detect.read(io->hp_detect.ctx);
    return io->hp_detect_active_high ? raw : !raw;
}

dev_status_t audio_io_set_speaker_enabled(audio_io_t *io, bool enabled)
{
    if ((io == NULL) || (io->speaker_sd_mode.write == NULL)) return DEV_EINVAL;
    bool level = io->speaker_enable_active_high ? enabled : !enabled;
    io->speaker_sd_mode.write(io->speaker_sd_mode.ctx, level);
    return DEV_OK;
}
