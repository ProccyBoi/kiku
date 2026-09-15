#ifndef AUDIO_IO_H
#define AUDIO_IO_H

#include "device_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    dev_gpio_t hp_detect;
    dev_gpio_t speaker_sd_mode;
    bool hp_detect_active_high;
    bool speaker_enable_active_high;
} audio_io_t;

dev_status_t audio_io_init(audio_io_t *io,
                           const dev_gpio_t *hp_detect,
                           const dev_gpio_t *speaker_sd_mode,
                           bool hp_detect_active_high,
                           bool speaker_enable_active_high);
bool audio_io_headphones_present(const audio_io_t *io);
dev_status_t audio_io_set_speaker_enabled(audio_io_t *io, bool enabled);

#ifdef __cplusplus
}
#endif

#endif
