#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app.h"
#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bq25895_t charger;
    max17048_t gauge;
    tlv320aic3104_t codec;
    si4705_t radio;
    bm83_t bluetooth;
    st7789_t display;
    audio_io_t audio_io;
    storage_t storage;
    storage_track_t tracks[64];
    size_t track_count;
    size_t track_index;
    app_deps_t deps;
} platform_devices_t;


typedef struct {
    uint32_t tick_ms;
    uint16_t max17048_version;
    uint16_t max17048_vcell_raw;
    uint16_t max17048_soc_raw;
    uint16_t max17048_crate_raw;
    uint16_t max17048_status_raw;
    uint8_t bq_reg02;
    uint8_t bq_reg06;
    uint8_t bq_reg0e;
    uint8_t max17048_read_mask;
    uint8_t hp_detect_raw;
    uint8_t headphones_present;
    uint8_t speaker_gpio_high;
    uint8_t bm83_tx_ind_raw;
    uint8_t speaker_test_request;
    uint8_t speaker_test_active;
    uint8_t tlv_reg03;
    uint8_t tlv_reg07;
    uint8_t tlv_reg08;
    uint8_t tlv_reg09;
    uint8_t tlv_reg37;
    uint8_t tlv_reg43;
    uint8_t tlv_reg44;
    uint8_t tlv_reg47;
    uint8_t tlv_reg51;
    uint8_t tlv_reg64;
    uint8_t tlv_reg65;
    uint8_t tlv_reg94;
    uint8_t tlv_reg95;
    uint8_t tlv_reg101;
    uint8_t tlv_reg102;
    uint16_t tlv_read_mask;
    uint8_t storage_mounted;
    uint8_t storage_mount_error;
    uint8_t storage_scan_error;
    uint8_t reserved0;
    uint32_t storage_entries_seen;
    uint32_t storage_dirs_seen;
    uint32_t storage_audio_seen;
    uint32_t storage_track_count;
    uint32_t bm83_uart_rx_overrun_count;
    uint32_t i2c1_recovery_count;
    uint32_t i2c3_recovery_count;
    /* Counts successful bytes/calls sent through SPI4 to the ST7789. These are
     * SWD diagnostics only; they let wake/shutdown tests prove that animation
     * frames reached the physical display bus rather than merely advancing UI
     * state in RAM. */
    uint32_t display_spi_write_calls;
    uint32_t display_spi_write_bytes;
    uint32_t bm83_uart_error_count;
    uint32_t bm83_uart_last_error;
} platform_diag_t;

extern volatile platform_diag_t g_platform_diag;

/* SWD-only non-destructive BM83 diagnostic mailboxes. Writing a non-zero
 * request from a debugger schedules the command in normal task context.
 * g_bench_bm83_play_request: 1=play, 2=next, 3=pause, 4=toggle, 5=previous. */
extern volatile uint32_t g_bench_bm83_toggle_audio_request;
extern volatile int32_t g_bench_bm83_toggle_audio_result;
extern volatile uint32_t g_bench_bm83_play_request;
extern volatile int32_t g_bench_bm83_play_result;
extern volatile uint32_t g_bench_bm83_eeprom_read_request;
extern volatile uint32_t g_bench_bm83_eeprom_read_offset;
extern volatile uint32_t g_bench_bm83_eeprom_read_length;
extern volatile int32_t g_bench_bm83_eeprom_read_result;
extern volatile uint32_t g_bench_bm83_link_status_request;
extern volatile int32_t g_bench_bm83_link_status_result;
extern volatile uint32_t g_bench_bm83_link_back_request;
extern volatile int32_t g_bench_bm83_link_back_result;
extern volatile uint32_t g_bench_bm83_tone_request;
extern volatile int32_t g_bench_bm83_tone_result;
extern volatile uint32_t g_bench_app_source_request;
extern volatile int32_t g_bench_app_source_result;
extern volatile uint32_t g_bench_app_output_request;
extern volatile int32_t g_bench_app_output_result;
extern volatile uint32_t g_bench_local_play_request;
extern volatile int32_t g_bench_local_play_result;
extern volatile uint32_t g_bench_speaker_direct_request;
extern volatile int32_t g_bench_speaker_direct_result;

dev_status_t platform_devices_init(platform_devices_t *platform);
int16_t platform_encoder_delta(void);
void platform_set_backlight(uint8_t percent);
void platform_set_backlight_level(uint16_t level_10000);
void platform_poll_bm83(kiku_app_t *app);
void platform_service_stream_audio(const kiku_app_t *app);
void platform_watchdog_refresh(void);

#ifdef __cplusplus
}
#endif
