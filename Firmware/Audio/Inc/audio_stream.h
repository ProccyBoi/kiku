#ifndef KIKU_AUDIO_STREAM_H
#define KIKU_AUDIO_STREAM_H

#include "audio_router.h"
#include "device_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Continuous 48 kHz stereo transport for the board I2S domain. */

typedef struct {
    uint32_t bt_rx_half_count;
    uint32_t bt_rx_full_count;
    uint32_t tx_half_count;
    uint32_t tx_full_count;
    uint32_t dma_error_count;
    uint32_t fifo_overrun_count;
    uint32_t bt_underrun_count;
    uint32_t bt_rebuffer_count;
    uint32_t bt_rearm_count;
    uint32_t bt_stall_rearm_count;
    uint32_t bt_stall_rearm_fail_count;
    uint32_t bt_last_progress_ms;
    uint32_t bt_last_stall_rearm_ms;
    uint32_t bt_last_rx_sr;
    uint32_t transport_recovery_count;
    uint32_t fifo_frames;
    uint32_t transport_last_fault;
    uint32_t transport_last_cr1;
    uint32_t transport_last_cfg1;
    uint32_t transport_last_sr;
    uint32_t transport_last_tx_ccr;
    uint32_t transport_last_rx_ccr;
    uint16_t bt_peak_abs;
    uint8_t source;
    uint8_t tx_started;
    uint8_t bt_stall_rearm_attempts;
} audio_stream_diag_t;

extern volatile audio_stream_diag_t g_audio_stream_diag;

dev_status_t audio_stream_init(void);
dev_status_t audio_stream_set_source(audio_source_t source);
dev_status_t audio_stream_suspend(void);
dev_status_t audio_stream_rearm_bluetooth_rx(void);
void audio_stream_set_volume(uint8_t percent);
void audio_stream_set_transition_gain(uint16_t permille);

/* Queue already-resampled 48 kHz stereo PCM for local playback. This is
 * intentionally non-blocking; the platform adapter decides how to wait when
 * the FIFO is temporarily full. */
dev_status_t audio_stream_write_stereo(const int16_t *interleaved, size_t frames);

/* SWD/bench bring-up helper. This deliberately bypasses the normal FIFO,
 * codec receive path and full-duplex DMA plumbing and drives a short 16-bit
 * Philips-I2S stereo tone using the simplest possible MASTER_TX path. It is
 * intended to answer one hardware question: can the MAX98357A reproduce a
 * valid stream when the STM32 is configured like a minimal I2S source? */
dev_status_t audio_stream_bench_direct_speaker_tone(uint32_t duration_ms);

/* Cheap task-context service: starts Bluetooth playback after prebuffering and
 * recovers a DMA stream if an IRQ reports an audio transport error. */
void audio_stream_service(void);
size_t audio_stream_free_frames(void);
size_t audio_stream_queued_frames(void);

#ifdef __cplusplus
}
#endif

#endif
