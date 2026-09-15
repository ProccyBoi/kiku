#ifndef UI_STATE_H
#define UI_STATE_H

#include "audio_router.h"
#include "fm_state.h"
#include "power_manager.h"
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_SCREEN_HOME = 0,
    UI_SCREEN_BOOT,
    UI_SCREEN_NOW_PLAYING,
    UI_SCREEN_LIBRARY,
    UI_SCREEN_FM,
    UI_SCREEN_BLUETOOTH,
    UI_SCREEN_OUTPUT,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_DIAGNOSTICS,
    UI_SCREEN_POWERING_OFF,
    UI_SCREEN_POWERING_ON,
    UI_SCREEN_SLEEP,
    UI_SCREEN_ERROR
} ui_screen_t;

typedef enum {
    UI_THEME_KIKU = 0,
    UI_THEME_POLAROID,
    UI_THEME_WALKMAN,
    UI_THEME_INSTRUMENT,
    UI_THEME_MINIDISC,
    UI_THEME_TERMINAL,
    UI_THEME_AQUA,
    UI_THEME_COUNT
} ui_theme_t;

typedef enum {
    UI_FM_CONTROL_VOLUME = 0,
    UI_FM_CONTROL_TUNE
} ui_fm_control_mode_t;

typedef struct {
    ui_screen_t screen;
    audio_source_t source;
    audio_output_t output;
    uint8_t volume_percent;
    uint8_t brightness_percent;
    uint16_t battery_centi_percent;
    uint16_t battery_mv;
    uint16_t usb_vbus_mv;
    uint16_t charge_current_ma;
    uint8_t charger_status_raw;
    uint8_t charger_fault_raw;
    bool charging;
    bool usb_power_good;
    bool battery_estimated;
    bool fuel_gauge_fault;
    bool headphones_present;
    bool bluetooth_connected;
    bool storage_mounted;
    bool playing;
    bool bt_avrcp_connected;
    bool fm_seeking;
    bool power_save_enabled;
    bool shuffle_enabled;
    ui_theme_t theme;
    ui_fm_control_mode_t fm_control_mode;
    uint8_t fm_preset_index;
    uint8_t menu_index;
    /* 0..1000 continuous transition progress. Kept separate from menu state so
     * the renderer can animate at display cadence without six-step quantising. */
    uint16_t animation_progress;
    uint32_t error_flags;
    fm_state_t fm;
    char fm_preset_name[16];
    char track_title[64];
    char track_artist[64];
    char track_album[64];
    char track_genre[32];
    uint16_t track_number;
    uint16_t track_total;
    uint32_t track_duration_ms;
} ui_state_t;

void ui_state_init(ui_state_t *ui, const app_settings_t *settings);
void ui_state_sync_power(ui_state_t *ui, const power_manager_t *power);
void ui_state_set_error(ui_state_t *ui, uint32_t flags);
void ui_state_set_track(ui_state_t *ui, const char *title, const char *artist,
                        const char *album);
void ui_state_set_track_details(ui_state_t *ui, const char *genre,
                                uint16_t track_number, uint16_t track_total,
                                uint32_t duration_ms);
void ui_state_clear_track_details(ui_state_t *ui);

#ifdef __cplusplus
}
#endif

#endif
