#include "ui_state.h"

#include <string.h>

void ui_state_init(ui_state_t *ui, const app_settings_t *settings)
{
    if (ui == NULL) return;
    memset(ui, 0, sizeof(*ui));
    ui->screen = UI_SCREEN_HOME;
    ui->menu_index = 0U;
    ui->output = AUDIO_OUTPUT_AUTO;
    ui->theme = UI_THEME_KIKU;
    ui->fm_control_mode = UI_FM_CONTROL_TUNE;
    if (settings != NULL) {
        ui->volume_percent = settings->volume_percent;
        ui->brightness_percent = settings->brightness_percent;
        ui->source = (audio_source_t)(settings->preferred_source + 1U);
        if (settings->output_mode <= (uint8_t)AUDIO_OUTPUT_BOTH) {
            ui->output = (audio_output_t)settings->output_mode;
        }
        ui->power_save_enabled = settings->power_save_enabled != 0U;
        ui->shuffle_enabled = settings->shuffle_enabled != 0U;
        fm_state_reset(&ui->fm, settings->fm_frequency_10khz);
    } else {
        ui->volume_percent = 45U;
        ui->brightness_percent = 70U;
        ui->source = AUDIO_SOURCE_LOCAL;
        fm_state_reset(&ui->fm, 10170U);
    }
}

void ui_state_sync_power(ui_state_t *ui, const power_manager_t *power)
{
    if ((ui == NULL) || (power == NULL)) return;
    ui->battery_centi_percent = power->effective_soc_centi_percent;
    ui->battery_mv = power->effective_battery_mv;
    ui->battery_estimated = power->using_charger_fallback;
    ui->fuel_gauge_fault = power->gauge_valid && !power->gauge_consistent;
    if (power->charger_valid) {
        const uint8_t charge_state = (uint8_t)((power->charger_status.status_raw >> 3U) & 0x03U);
        ui->usb_vbus_mv = power->charger_status.vbus_mv;
        ui->charge_current_ma = power->charger_status.charge_current_ma;
        ui->charger_status_raw = power->charger_status.status_raw;
        ui->charger_fault_raw = power->charger_status.fault_raw;
        ui->usb_power_good = power->charger_status.power_good;
        ui->charging = charge_state == 0x01U || charge_state == 0x02U;
    } else {
        ui->usb_vbus_mv = 0U;
        ui->charge_current_ma = 0U;
        ui->charger_status_raw = 0U;
        ui->charger_fault_raw = 0U;
        ui->usb_power_good = false;
        ui->charging = false;
    }
}

void ui_state_set_error(ui_state_t *ui, uint32_t flags)
{
    if (ui == NULL) return;
    ui->error_flags |= flags;
}

static void ui_copy_text(char *dst, size_t capacity, const char *src)
{
    if (dst == NULL || capacity == 0U) return;
    if (src == NULL) { dst[0] = '\0'; return; }
    size_t n = 0U;
    while (n + 1U < capacity && src[n] != '\0') {
        dst[n] = src[n];
        ++n;
    }
    dst[n] = '\0';
}

void ui_state_set_track(ui_state_t *ui, const char *title, const char *artist,
                        const char *album)
{
    if (ui == NULL) return;
    ui_copy_text(ui->track_title, sizeof(ui->track_title), title);
    ui_copy_text(ui->track_artist, sizeof(ui->track_artist), artist);
    ui_copy_text(ui->track_album, sizeof(ui->track_album), album);
}

void ui_state_set_track_details(ui_state_t *ui, const char *genre,
                                uint16_t track_number, uint16_t track_total,
                                uint32_t duration_ms)
{
    if (ui == NULL) return;
    ui_copy_text(ui->track_genre, sizeof(ui->track_genre), genre);
    ui->track_number = track_number;
    ui->track_total = track_total;
    ui->track_duration_ms = duration_ms;
}

void ui_state_clear_track_details(ui_state_t *ui)
{
    if (ui == NULL) return;
    ui->track_genre[0] = '\0';
    ui->track_number = 0U;
    ui->track_total = 0U;
    ui->track_duration_ms = 0U;
}
