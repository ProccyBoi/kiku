#ifndef KIKU_APP_H
#define KIKU_APP_H

#include "app_config.h"
#include "audio_router.h"
#include "bm83.h"
#include "fm_state.h"
#include "input_manager.h"
#include "local_playback.h"
#include "power_manager.h"
#include "self_test.h"
#include "settings.h"
#include "st7789.h"
#include "ui_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bq25895_t *charger;
    max17048_t *gauge;
    tlv320aic3104_t *codec;
    si4705_t *radio;
    bm83_t *bluetooth;
    st7789_t *display;
    audio_io_t *audio_io;
    settings_storage_t settings_storage;
    local_fs_t local_fs;
    local_pcm_sink_t pcm_sink;
    local_mp3_decoder_t mp3_decoder;
    audio_route_ops_t audio_route_ops;
    dev_gpio_t button1;
    dev_gpio_t button2;
    dev_gpio_t encoder_push;
    dev_clock_t clock;
    self_test_storage_probe_fn storage_probe;
    void *storage_probe_ctx;
    bm83_control_ids_t bm83_controls;
    bool si4705_power_on_init;
    uint8_t si4705_power_func;
    uint8_t si4705_power_opmode;
    const si4705_property_t *si4705_properties;
    size_t si4705_property_count;
    dev_status_t (*local_track_path)(void *ctx, int32_t step, char *path, size_t path_capacity);
    dev_status_t (*local_random_track_path)(void *ctx, char *path, size_t path_capacity);
    void *local_track_ctx;
    dev_status_t (*soft_power_set)(void *ctx, bool on);
    void *soft_power_ctx;
    dev_status_t (*bluetooth_recover)(void *ctx);
    void *bluetooth_recover_ctx;
} app_deps_t;

typedef struct {
    uint32_t notification_count;
    uint32_t item_attr_response_count;
    uint32_t control_queued_count;
    uint32_t control_sent_count;
    uint32_t control_queue_overflow_count;
    uint32_t metadata_request_count;
    uint32_t metadata_response_count;
    uint32_t metadata_core_complete_count;
    uint32_t metadata_core_change_count;
    uint32_t metadata_timeout_count;
    uint32_t metadata_backpressure_count;
    uint32_t metadata_forced_refresh_count;
    uint32_t metadata_track_change_count;
    uint16_t player_id;
    uint16_t uid_counter;
    uint8_t track_uid[8];
    uint8_t browse_stage;
    uint8_t database_index;
    uint8_t uid_counter_valid;
    uint8_t track_uid_valid;
    uint8_t player_valid;
    uint8_t avrcp_connected;
    uint8_t last_notification_response;
    uint8_t last_notification_event_id;
    uint8_t last_notification_param_len;
    uint8_t last_item_status;
    uint8_t metadata_inflight_kind;
    uint8_t metadata_inflight_attribute;
    uint8_t metadata_stage_seen_mask;
    uint8_t raw_notification[20];
    char title[64];
    char artist[64];
    char album[64];
    char genre[32];
    uint16_t track_number;
    uint16_t track_total;
    uint32_t duration_ms;
} bt_metadata_diag_t;

extern volatile bt_metadata_diag_t g_bt_metadata_diag;

typedef struct {
    app_deps_t deps;
    settings_manager_t settings;
    input_manager_t input;
    audio_router_t audio;
    local_playback_t local;
    fm_state_t fm;
    power_manager_t power;
    ui_state_t ui;
    self_test_result_t self_test;
    bm83_control_ids_t bm83_controls;
    uint32_t last_fm_status_ms;
    uint32_t last_rds_poll_ms;
    uint32_t fm_seek_started_ms;
    uint32_t last_bt_metadata_ms;
    uint32_t bt_metadata_request_started_ms;
    uint32_t bt_metadata_next_due_ms;
    uint32_t last_bt_browse_request_ms;
    uint16_t bt_browse_expected;
    uint16_t bt_browse_received;
    uint8_t bt_browse_buffer[APP_BT_BROWSE_BUFFER_BYTES];
    uint16_t bt_player_id;
    uint16_t bt_uid_counter;
    uint8_t bt_track_uid[8];
    /* AVRCP/metadata traffic shares the BM83's single command/ACK transport
     * with physical play/pause/skip controls. Keep a tiny bounded FIFO so a
     * button press that lands during a metadata transaction is delayed rather
     * than silently discarded. */
    uint8_t bt_control_queue[APP_BT_CONTROL_QUEUE_DEPTH];
    uint8_t bt_control_queue_head;
    uint8_t bt_control_queue_tail;
    uint8_t bt_control_queue_count;
    uint8_t bt_metadata_detail_phase;
    uint8_t bt_metadata_request_kind;
    uint8_t bt_metadata_requested_attribute;
    uint8_t bt_metadata_core_next_attribute;
    uint8_t bt_metadata_stage_seen_mask;
    uint8_t bt_browse_stage;
    uint8_t bt_database_index;
    char bt_metadata_stage_title[64];
    char bt_metadata_stage_artist[64];
    char bt_metadata_stage_album[64];
    char bt_metadata_guard_title[64];
    char bt_metadata_guard_artist[64];
    char bt_metadata_guard_album[64];
    uint32_t bt_metadata_identity_guard_started_ms;
    bool bt_uid_counter_valid;
    bool bt_track_uid_valid;
    bool bt_player_valid;
    bool bt_avrcp_capability_pending;
    bool bt_track_notification_pending;
    bool bt_avrcp_connected;
    bool bt_metadata_request_inflight;
    bool bt_metadata_command_accepted;
    bool bt_metadata_response_stalled;
    bool bt_metadata_force_core;
    bool bt_metadata_discard_response;
    bool bt_metadata_identity_guard_active;
    bool bt_power_recovery_attempted;
    bool fm_seek_active;
    bool soft_powered_off;
    bool power_resume_local_playing;
    uint8_t power_resume_volume_percent;
    uint16_t power_fade_gain_permille;
    uint8_t home_b2_press_count;
    uint32_t home_b2_first_press_ms;
    audio_source_t power_resume_source;
    ui_screen_t transition_return_screen;
    uint32_t transition_started_ms;
    bool initialised;
} kiku_app_t;

dev_status_t kiku_app_init(kiku_app_t *app, const app_deps_t *deps);
dev_status_t kiku_app_tick(kiku_app_t *app);
void kiku_app_encoder_delta(kiku_app_t *app, int16_t delta);
void kiku_app_bm83_rx(kiku_app_t *app, const uint8_t *data, size_t len);
dev_status_t kiku_app_select_source(kiku_app_t *app, audio_source_t source);
dev_status_t kiku_app_open_local(kiku_app_t *app, const char *path);
dev_status_t kiku_app_tune_fm(kiku_app_t *app, uint16_t frequency_10khz);
dev_status_t kiku_app_seek_fm(kiku_app_t *app, bool seek_up);
void kiku_app_toggle_soft_power(kiku_app_t *app);
dev_status_t kiku_app_bluetooth_music_action(kiku_app_t *app,
                                             bm83_music_action_t action);
self_test_result_t kiku_app_run_self_test(kiku_app_t *app);

#ifdef __cplusplus
}
#endif

#endif
