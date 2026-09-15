#include "app.h"

#include <stdio.h>
#include <string.h>

volatile bt_metadata_diag_t g_bt_metadata_diag;

enum {
    APP_ERROR_POWER       = 1UL << 0,
    APP_ERROR_AUDIO_ROUTE = 1UL << 1,
    APP_ERROR_STORAGE     = 1UL << 2,
    APP_ERROR_RADIO       = 1UL << 3,
    APP_ERROR_DISPLAY     = 1UL << 4,
    APP_ERROR_BLUETOOTH   = 1UL << 5
};

enum {
    BT_BROWSE_IDLE = 0U,
    BT_BROWSE_NEED_PLAYER_LIST,
    BT_BROWSE_WAIT_PLAYER_LIST,
    BT_BROWSE_NEED_SET_ADDRESSED,
    BT_BROWSE_WAIT_SET_ADDRESSED,
    BT_BROWSE_NEED_SET_BROWSED,
    BT_BROWSE_WAIT_SET_BROWSED,
    BT_BROWSE_NEED_NOW_PLAYING,
    BT_BROWSE_WAIT_NOW_PLAYING,
    BT_BROWSE_NEED_TRACK_ATTRIBUTES,
    BT_BROWSE_WAIT_TRACK_ATTRIBUTES
};

enum {
    BT_METADATA_REQ_NONE = 0U,
    BT_METADATA_REQ_CORE,
    BT_METADATA_REQ_DETAIL
};

#define APP_BT_BROWSE_TIMEOUT_MS 1800U

typedef struct {
    uint16_t frequency_10khz;
    const char *name;
} app_fm_preset_t;

/* Default Sydney station bank. RDS remains the source of live station/program
 * text; this table only makes the physical controls useful immediately. */
static const app_fm_preset_t s_fm_presets[] = {
    {  9290U, "ABC CLASSIC" },
    {  9450U, "FBi RADIO" },
    {  9530U, "SMOOTH 95.3" },
    {  9690U, "NOVA 96.9" },
    { 10170U, "GOLD 101.7" },
    { 10250U, "FINE MUSIC" },
    { 10320U, "HOPE 103.2" },
    { 10410U, "2DAY FM" },
    { 10490U, "TRIPLE M" },
    { 10570U, "TRIPLE J" },
    { 10650U, "KIIS 106.5" },
    { 10730U, "2SER" },
};

#define APP_FM_PRESET_COUNT ((uint8_t)(sizeof(s_fm_presets) / sizeof(s_fm_presets[0])))

static uint32_t app_now(const kiku_app_t *app)
{
    if ((app == NULL) || (app->deps.clock.time_ms == NULL)) return 0U;
    return app->deps.clock.time_ms(app->deps.clock.ctx);
}

static void app_set_volume(kiku_app_t *app, int32_t volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    if ((uint8_t)volume == app->settings.value.volume_percent) return;
    app->settings.value.volume_percent = (uint8_t)volume;
    app->ui.volume_percent = (uint8_t)volume;
    if (audio_router_set_volume(&app->audio, (uint8_t)volume) != DEV_OK) {
        ui_state_set_error(&app->ui, APP_ERROR_AUDIO_ROUTE);
    }
    settings_mark_dirty(&app->settings, app_now(app));
}

dev_status_t kiku_app_seek_fm(kiku_app_t *app, bool seek_up)
{
    if (app == NULL || app->deps.radio == NULL || app->audio.source != AUDIO_SOURCE_FM) return DEV_ENOTREADY;
    {
        /* A second seek request should replace the first one cleanly rather than
         * being sent while the tuner is still searching. */
        si4705_tune_status_t ignored;
        dev_status_t ack = si4705_get_tune_status(app->deps.radio, app->fm_seek_active, &ignored);
        if (ack != DEV_OK) return ack;
        app->fm_seek_active = false;
        app->ui.fm_seeking = false;
    }
    dev_status_t st = si4705_seek(app->deps.radio, seek_up, true);
    if (st != DEV_OK) {
        ui_state_set_error(&app->ui, APP_ERROR_RADIO);
        return st;
    }
    /* Do not leave the previous station's RDS text visible while the tuner is
     * moving. Completion is picked up by the normal FM_TUNE_STATUS poll. */
    fm_state_reset(&app->fm, app->settings.value.fm_frequency_10khz);
    app->ui.fm = app->fm;
    app->fm_seek_active = true;
    app->ui.fm_seeking = true;
    app->fm_seek_started_ms = app_now(app);
    app->last_fm_status_ms = 0U;
    app->last_rds_poll_ms = app->fm_seek_started_ms;
    return DEV_OK;
}

static void app_open_relative_local_track(kiku_app_t *app, int32_t step, bool autoplay);
static void app_copy_metadata_text(char *dst, size_t dst_size,
                                   const uint8_t *src, size_t src_len);
static void app_cleanup_artist_text(char *artist);
static void app_bt_metadata_force_refresh(kiku_app_t *app, uint32_t now,
                                          bool clear_visible_track);

static void app_update_effective_brightness(kiku_app_t *app)
{
    if (app == NULL) return;
    uint8_t brightness = app->settings.value.brightness_percent;
    if (app->settings.value.power_save_enabled != 0U &&
        brightness > APP_POWER_SAVE_BRIGHTNESS_PERCENT) {
        brightness = APP_POWER_SAVE_BRIGHTNESS_PERCENT;
    }
    app->ui.power_save_enabled = app->settings.value.power_save_enabled != 0U;
    app->ui.brightness_percent = brightness;
}

static void app_sync_fm_preset(kiku_app_t *app)
{
    if (app == NULL) return;
    app->ui.fm_preset_index = 0xFFU;
    app->ui.fm_preset_name[0] = '\0';
    for (uint8_t i = 0U; i < APP_FM_PRESET_COUNT; ++i) {
        if (s_fm_presets[i].frequency_10khz == app->fm.frequency_10khz) {
            app->ui.fm_preset_index = i;
            (void)snprintf(app->ui.fm_preset_name, sizeof(app->ui.fm_preset_name),
                           "%s", s_fm_presets[i].name);
            return;
        }
    }
}

static void app_activate_settings_selection(kiku_app_t *app)
{
    if (app == NULL) return;
    switch (app->ui.menu_index) {
    case 0U:
        app->settings.value.power_save_enabled =
            app->settings.value.power_save_enabled == 0U ? 1U : 0U;
        app_update_effective_brightness(app);
        settings_mark_dirty(&app->settings, app_now(app));
        break;
    case 1U:
    default:
        app->ui.screen = UI_SCREEN_DIAGNOSTICS;
        break;
    }
}

static void app_activate_library_selection(kiku_app_t *app)
{
    if (app == NULL) return;
    switch (app->ui.menu_index) {
    case 0U:
        if (app->local.file == NULL) app_open_relative_local_track(app, 0, true);
        else if (!app->local.playing) (void)local_playback_set_playing(&app->local, true);
        app->ui.screen = UI_SCREEN_NOW_PLAYING;
        break;
    case 1U:
    default:
        app->settings.value.shuffle_enabled =
            app->settings.value.shuffle_enabled == 0U ? 1U : 0U;
        app->ui.shuffle_enabled = app->settings.value.shuffle_enabled != 0U;
        settings_mark_dirty(&app->settings, app_now(app));
        break;
    }
}

static uint32_t app_parse_metadata_u32(const uint8_t *src, size_t src_len)
{
    if (src == NULL) return 0U;
    uint32_t value = 0U;
    bool have_digit = false;
    for (size_t i = 0U; i < src_len; ++i) {
        uint8_t c = src[i];
        if (c < (uint8_t)'0' || c > (uint8_t)'9') {
            if (have_digit) break;
            continue;
        }
        have_digit = true;
        uint32_t digit = (uint32_t)(c - (uint8_t)'0');
        if (value > (UINT32_MAX - digit) / 10U) return UINT32_MAX;
        value = value * 10U + digit;
    }
    return have_digit ? value : 0U;
}

static void app_apply_output(kiku_app_t *app, audio_output_t output)
{
    if (app == NULL || output > AUDIO_OUTPUT_BOTH) return;
    if (audio_router_set_output(&app->audio, output) != DEV_OK) {
        ui_state_set_error(&app->ui, APP_ERROR_AUDIO_ROUTE);
        return;
    }
    app->ui.output = output;
    app->settings.value.output_mode = (uint8_t)output;
    settings_mark_dirty(&app->settings, app_now(app));
}

static void app_go_home(kiku_app_t *app)
{
    if (app == NULL) return;
    app->ui.screen = UI_SCREEN_HOME;
    switch (app->audio.source) {
    case AUDIO_SOURCE_FM: app->ui.menu_index = 1U; break;
    case AUDIO_SOURCE_BLUETOOTH: app->ui.menu_index = 2U; break;
    case AUDIO_SOURCE_LOCAL:
    default: app->ui.menu_index = 0U; break;
    }
}

static void app_go_back(kiku_app_t *app)
{
    if (app == NULL) return;
    switch (app->ui.screen) {
    case UI_SCREEN_NOW_PLAYING:
        app_go_home(app);
        break;
    case UI_SCREEN_DIAGNOSTICS:
        app->ui.menu_index = 1U;
        app->ui.screen = UI_SCREEN_SETTINGS;
        break;
    case UI_SCREEN_LIBRARY:
    case UI_SCREEN_FM:
    case UI_SCREEN_BLUETOOTH:
    case UI_SCREEN_OUTPUT:
    case UI_SCREEN_SETTINGS:
    case UI_SCREEN_ERROR:
        app_go_home(app);
        break;
    case UI_SCREEN_HOME:
    case UI_SCREEN_BOOT:
    case UI_SCREEN_POWERING_OFF:
    case UI_SCREEN_POWERING_ON:
    case UI_SCREEN_SLEEP:
    default:
        break;
    }
}

static void app_open_home_selection(kiku_app_t *app)
{
    if (app == NULL) return;
    switch (app->ui.menu_index) {
    case 0U:
        (void)kiku_app_select_source(app, AUDIO_SOURCE_LOCAL);
        if (app->local.file == NULL) app_open_relative_local_track(app, 0, true);
        else if (!app->local.playing) (void)local_playback_set_playing(&app->local, true);
        /* Music is a destination, not another menu. Enter the player immediately
         * so selecting Music is one action from Home. */
        app->ui.screen = UI_SCREEN_NOW_PLAYING;
        break;
    case 1U:
        (void)kiku_app_select_source(app, AUDIO_SOURCE_FM);
        break;
    case 2U:
        (void)kiku_app_select_source(app, AUDIO_SOURCE_BLUETOOTH);
        break;
    case 3U:
        app->ui.menu_index = (uint8_t)app->audio.output;
        app->ui.screen = UI_SCREEN_OUTPUT;
        break;
    case 4U:
    default:
        app->ui.menu_index = 0U;
        app->ui.screen = UI_SCREEN_SETTINGS;
        break;
    }
}

static void app_move_menu(uint8_t *index, int16_t delta, uint8_t count)
{
    if (index == NULL || count == 0U || delta == 0) return;
    int32_t next = (int32_t)*index + (int32_t)delta;
    while (next < 0) next += count;
    while (next >= count) next -= count;
    *index = (uint8_t)next;
}

static uint16_t app_animation_progress(uint32_t elapsed_ms, uint32_t duration_ms)
{
    if (duration_ms == 0U || elapsed_ms >= duration_ms) return 1000U;
    return (uint16_t)((elapsed_ms * 1000U) / duration_ms);
}

static uint16_t app_smootherstep_permille(uint16_t progress)
{
    uint64_t p = progress > 1000U ? 1000U : progress;
    uint64_t p2 = p * p;
    uint64_t p3 = p2 * p;
    uint64_t eased = (p3 * (10000000ULL + p * (6ULL * p - 15000ULL)) +
                       500000000000ULL) / 1000000000000ULL;
    return (uint16_t)(eased > 1000ULL ? 1000ULL : eased);
}

static void app_begin_power_off(kiku_app_t *app)
{
    if (app == NULL || app->soft_powered_off ||
        app->ui.screen == UI_SCREEN_BOOT ||
        app->ui.screen == UI_SCREEN_POWERING_OFF ||
        app->ui.screen == UI_SCREEN_POWERING_ON) return;

    app->power_resume_source = app->audio.source;
    app->power_resume_local_playing =
        app->audio.source == AUDIO_SOURCE_LOCAL && app->local.playing;
    app->power_resume_volume_percent = app->settings.value.volume_percent;
    app->power_fade_gain_permille = 1000U;
    audio_router_set_transition_gain(&app->audio, 1000U);
    app->transition_return_screen = app->ui.screen;
    app->transition_started_ms = app_now(app);
    app->ui.animation_progress = 0U;
    app->ui.screen = UI_SCREEN_POWERING_OFF;
}

static void app_finish_power_off(kiku_app_t *app)
{
    if (app == NULL) return;

    /* Stop removable-media activity before the final settings sync so shutdown
     * never cuts across a decoder read or a FatFs write. */
    if (app->audio.source == AUDIO_SOURCE_LOCAL) app->local.playing = false;
    if (app->settings.dirty) {
        dev_status_t save_st = settings_save(&app->settings);
        if (save_st != DEV_OK && save_st != DEV_ENOTSUP) {
            ui_state_set_error(&app->ui, APP_ERROR_STORAGE);
        }
    }

    if (app->deps.bluetooth != NULL) bm83_set_auto_reconnect(app->deps.bluetooth, false);
    if (audio_router_set_source(&app->audio, AUDIO_SOURCE_NONE) != DEV_OK) {
        ui_state_set_error(&app->ui, APP_ERROR_AUDIO_ROUTE);
    }
    /* Transition gain is independent of the user's saved volume. Reset it only
     * once the source is muted so wake restores exactly the listening level the
     * user left behind without a coarse 1%-step shutdown ramp. */
    audio_router_set_transition_gain(&app->audio, 1000U);
    app->power_fade_gain_permille = 1000U;
    if (app->deps.soft_power_set != NULL) {
        dev_status_t st = app->deps.soft_power_set(app->deps.soft_power_ctx, false);
        if (st != DEV_OK && st != DEV_ENOTSUP) ui_state_set_error(&app->ui, APP_ERROR_POWER);
    }

    app->soft_powered_off = true;
    app->ui.animation_progress = 0U;
    app->ui.playing = false;
    app->ui.screen = UI_SCREEN_SLEEP;
}

static void app_begin_power_on(kiku_app_t *app)
{
    if (app == NULL || !app->soft_powered_off) return;

    app->soft_powered_off = false;
    if (app->deps.soft_power_set != NULL) {
        dev_status_t st = app->deps.soft_power_set(app->deps.soft_power_ctx, true);
        if (st != DEV_OK && st != DEV_ENOTSUP) ui_state_set_error(&app->ui, APP_ERROR_POWER);
    }
    /* Do not restore a potentially slow FM/Bluetooth source here. Starting the
     * UI transition first guarantees the wake mark becomes visible immediately
     * instead of appearing to do nothing while a tuner crystal or BM83 wakes. */
    app->transition_started_ms = app_now(app);
    app->ui.animation_progress = 0U;
    app->ui.screen = UI_SCREEN_POWERING_ON;
}

static void app_finish_power_on(kiku_app_t *app)
{
    if (app == NULL) return;

    audio_source_t source = app->power_resume_source;
    if (source < AUDIO_SOURCE_LOCAL || source > AUDIO_SOURCE_BLUETOOTH) source = AUDIO_SOURCE_LOCAL;
    ui_screen_t return_screen = app->transition_return_screen;

    audio_router_set_transition_gain(&app->audio, 1000U);
    if (audio_router_set_volume(&app->audio, app->power_resume_volume_percent) != DEV_OK) {
        ui_state_set_error(&app->ui, APP_ERROR_AUDIO_ROUTE);
    }
    if (kiku_app_select_source(app, source) != DEV_OK) {
        ui_state_set_error(&app->ui, APP_ERROR_AUDIO_ROUTE);
        app->ui.animation_progress = 0U;
        app->ui.screen = UI_SCREEN_ERROR;
        return;
    }
    if (source == AUDIO_SOURCE_LOCAL && app->power_resume_local_playing && app->local.file != NULL) {
        (void)local_playback_set_playing(&app->local, true);
    }

    /* Source selection updates all source-specific state, then return to the
     * page the user actually left when it is still a sensible awake screen. */
    if (return_screen != UI_SCREEN_BOOT &&
        return_screen != UI_SCREEN_POWERING_OFF &&
        return_screen != UI_SCREEN_POWERING_ON &&
        return_screen != UI_SCREEN_SLEEP) {
        app->ui.screen = return_screen;
    }
    app->ui.animation_progress = 0U;
}

static void app_service_ui_transition(kiku_app_t *app, uint32_t now)
{
    if (app == NULL) return;
    /* HAL ticks wrap naturally, but a caller can also legitimately hold an old
     * timestamp across a blocking hardware callback. Treat only the latter as
     * zero elapsed; normal 32-bit tick wrap still produces a small positive
     * signed delta for every transition duration used here. */
    int32_t signed_elapsed = (int32_t)(now - app->transition_started_ms);
    uint32_t elapsed = signed_elapsed < 0 ? 0U : (uint32_t)signed_elapsed;

    if (app->ui.screen == UI_SCREEN_BOOT) {
        app->ui.animation_progress = app_animation_progress(elapsed, APP_BOOT_ANIMATION_MS);
        if (elapsed >= APP_BOOT_ANIMATION_MS) {
            app->ui.animation_progress = 0U;
            app->ui.screen = app->transition_return_screen;
        }
    } else if (app->ui.screen == UI_SCREEN_POWERING_OFF) {
        app->ui.animation_progress = app_animation_progress(elapsed, APP_POWER_ANIMATION_MS);
        /* Fade the PCM path at permille resolution. This is deliberately
         * separate from the user's integer volume setting: at a low listening
         * level (for example 6%) stepping 6,5,4... was visibly/audibly coarse. */
        uint32_t fade_elapsed = elapsed > APP_POWER_AUDIO_FADE_MS ? APP_POWER_AUDIO_FADE_MS : elapsed;
        uint16_t fade_progress = app_animation_progress(fade_elapsed, APP_POWER_AUDIO_FADE_MS);
        uint16_t target_gain = (uint16_t)(1000U - app_smootherstep_permille(fade_progress));
        if (target_gain != app->power_fade_gain_permille) {
            app->power_fade_gain_permille = target_gain;
            audio_router_set_transition_gain(&app->audio, target_gain);
        }
        if (elapsed >= APP_POWER_ANIMATION_MS) app_finish_power_off(app);
    } else if (app->ui.screen == UI_SCREEN_POWERING_ON) {
        app->ui.animation_progress = app_animation_progress(elapsed, APP_POWER_ANIMATION_MS);
        if (elapsed >= APP_POWER_ANIMATION_MS) {
            app_finish_power_on(app);
        }
    }
}

static void app_bt_control_queue_clear(kiku_app_t *app)
{
    if (app == NULL) return;
    app->bt_control_queue_head = 0U;
    app->bt_control_queue_tail = 0U;
    app->bt_control_queue_count = 0U;
}

static dev_status_t app_bt_control_queue_push(kiku_app_t *app,
                                              bm83_music_action_t action)
{
    if (app == NULL) return DEV_EINVAL;
    if (app->bt_control_queue_count >= APP_BT_CONTROL_QUEUE_DEPTH) {
        ++g_bt_metadata_diag.control_queue_overflow_count;
        return DEV_EOVERFLOW;
    }
    app->bt_control_queue[app->bt_control_queue_tail] = (uint8_t)action;
    app->bt_control_queue_tail =
        (uint8_t)((app->bt_control_queue_tail + 1U) % APP_BT_CONTROL_QUEUE_DEPTH);
    ++app->bt_control_queue_count;
    ++g_bt_metadata_diag.control_queued_count;
    return DEV_OK;
}

static void app_bt_control_queue_pop(kiku_app_t *app)
{
    if (app == NULL || app->bt_control_queue_count == 0U) return;
    app->bt_control_queue_head =
        (uint8_t)((app->bt_control_queue_head + 1U) % APP_BT_CONTROL_QUEUE_DEPTH);
    --app->bt_control_queue_count;
}

dev_status_t kiku_app_bluetooth_music_action(kiku_app_t *app,
                                             bm83_music_action_t action)
{
    if (app == NULL || app->deps.bluetooth == NULL ||
        (uint8_t)action > (uint8_t)BM83_MUSIC_PREVIOUS) return DEV_EINVAL;

    const bool track_step = action == BM83_MUSIC_NEXT || action == BM83_MUSIC_PREVIOUS;

    /* Preserve ordering once one user action has been deferred. A new button
     * press must not jump ahead merely because the metadata transaction that
     * blocked the first action completed between input polls. */
    if (app->bt_control_queue_count != 0U) {
        dev_status_t queued = app_bt_control_queue_push(app, action);
        if (queued == DEV_OK && track_step) {
            app_bt_metadata_force_refresh(app, app_now(app), true);
        }
        return queued;
    }

    dev_status_t st = bm83_music_control(app->deps.bluetooth, action);
    if (st == DEV_EBUSY) {
        /* EBUSY here normally means a metadata/browsing command is awaiting its
         * BM83 Command_ACK. Queue the human input and retry it from app_tick()
         * before issuing any more metadata traffic. */
        dev_status_t queued = app_bt_control_queue_push(app, action);
        if (queued == DEV_OK && track_step) {
            app_bt_metadata_force_refresh(app, app_now(app), true);
        }
        return queued;
    }
    if (st == DEV_OK) {
        ++g_bt_metadata_diag.control_sent_count;
        if (track_step) app_bt_metadata_force_refresh(app, app_now(app), true);
    }
    return st;
}

static void app_service_bt_control_queue(kiku_app_t *app)
{
    if (app == NULL || app->deps.bluetooth == NULL ||
        app->bt_control_queue_count == 0U) return;

    bm83_music_action_t action =
        (bm83_music_action_t)app->bt_control_queue[app->bt_control_queue_head];
    dev_status_t st = bm83_music_control(app->deps.bluetooth, action);
    if (st == DEV_EBUSY) return;

    /* Whether accepted or terminally rejected, this action has completed its
     * bounded retry life. A terminal failure is surfaced instead of wedging the
     * queue and preventing every later user control. */
    app_bt_control_queue_pop(app);
    if (st == DEV_OK) {
        ++g_bt_metadata_diag.control_sent_count;
        if (action == BM83_MUSIC_NEXT || action == BM83_MUSIC_PREVIOUS) {
            /* Re-anchor the metadata refresh to the instant the deferred skip
             * actually reaches BM83. The queue may have waited behind a prior
             * UART command, so the enqueue timestamp is not a reliable proxy for
             * when the phone starts changing tracks. */
            app_bt_metadata_force_refresh(app, app_now(app), true);
        }
    }
    else ui_state_set_error(&app->ui, APP_ERROR_BLUETOOTH);
}

static void app_bm83_control(kiku_app_t *app, bm83_control_t control)
{
    if ((app == NULL) || (app->deps.bluetooth == NULL)) return;
    dev_status_t st = DEV_ENOTSUP;
    switch (control) {
    case BM83_CONTROL_PLAY_PAUSE:
        st = kiku_app_bluetooth_music_action(app, BM83_MUSIC_TOGGLE);
        break;
    case BM83_CONTROL_NEXT_TRACK:
        st = kiku_app_bluetooth_music_action(app, BM83_MUSIC_NEXT);
        break;
    case BM83_CONTROL_PREVIOUS_TRACK:
        st = kiku_app_bluetooth_music_action(app, BM83_MUSIC_PREVIOUS);
        break;
    default:
        /* Volume is intentionally local DSP volume. Keep this fallback only
         * for future BM83-package-specific controls explicitly configured by
         * the integration layer. */
        st = bm83_send_configured_control(app->deps.bluetooth, &app->bm83_controls,
                                          control, NULL, 0U);
        break;
    }
    if (st != DEV_OK && st != DEV_EBUSY && st != DEV_ENOTSUP) {
        ui_state_set_error(&app->ui, APP_ERROR_BLUETOOTH);
    }
}

static void app_set_local_title_from_path(kiku_app_t *app, const char *path)
{
    if ((app == NULL) || (path == NULL)) return;
    const char *base = strrchr(path, '/');
    base = (base == NULL) ? path : (base + 1);
    char title[sizeof(app->ui.track_title)];
    (void)snprintf(title, sizeof(title), "%s", base);
    char *dot = strrchr(title, '.');
    if (dot != NULL) *dot = '\0';
    ui_state_set_track(&app->ui, title, "SD CARD", "");
    ui_state_clear_track_details(&app->ui);
}

static void app_open_relative_local_track(kiku_app_t *app, int32_t step, bool autoplay)
{
    if ((app == NULL) || (app->deps.local_track_path == NULL)) return;
    char path[APP_PATH_MAX];
    dev_status_t path_st;
    if (step >= 0 && app->settings.value.shuffle_enabled != 0U &&
        app->deps.local_random_track_path != NULL) {
        path_st = app->deps.local_random_track_path(app->deps.local_track_ctx,
                                                    path, sizeof(path));
    } else {
        path_st = app->deps.local_track_path(app->deps.local_track_ctx, step,
                                             path, sizeof(path));
    }
    if (path_st != DEV_OK) return;
    dev_status_t st = local_playback_open(&app->local, path);
    if (st != DEV_OK) {
        ui_state_set_error(&app->ui, APP_ERROR_STORAGE);
        return;
    }
    app_set_local_title_from_path(app, path);
    if (autoplay) (void)local_playback_set_playing(&app->local, true);
}

static void app_input_event(void *ctx, const input_event_t *event)
{
    kiku_app_t *app = (kiku_app_t *)ctx;
    if ((app == NULL) || (event == NULL)) return;

    /* The encoder has one global long-hold meaning: power. B1 is contextual:
     * screens which already use its short press keep long-hold for Back, while
     * menu/status screens use the much quicker short press for Back. */
    if (event->type == INPUT_EVENT_ENCODER_LONG) {
        kiku_app_toggle_soft_power(app);
        return;
    }

    /* Boot/power animations deliberately consume normal controls. This avoids
     * accidental play/skip actions from the same physical presses used for the
     * power gesture. */
    if (app->ui.screen == UI_SCREEN_BOOT ||
        app->ui.screen == UI_SCREEN_POWERING_OFF ||
        app->ui.screen == UI_SCREEN_POWERING_ON ||
        app->ui.screen == UI_SCREEN_SLEEP) return;

    switch (event->type) {
    case INPUT_EVENT_ENCODER_DELTA:
        if (app->ui.screen == UI_SCREEN_HOME) app->home_b2_press_count = 0U;
        if (app->ui.screen == UI_SCREEN_HOME) {
            app_move_menu(&app->ui.menu_index, event->value, 5U);
        } else if (app->ui.screen == UI_SCREEN_LIBRARY) {
            app_move_menu(&app->ui.menu_index, event->value, 2U);
        } else if (app->ui.screen == UI_SCREEN_SETTINGS) {
            app_move_menu(&app->ui.menu_index, event->value, 2U);
        } else if (app->ui.screen == UI_SCREEN_OUTPUT) {
            app_move_menu(&app->ui.menu_index, event->value, 4U);
            app_apply_output(app, (audio_output_t)app->ui.menu_index);
        } else if (app->ui.screen == UI_SCREEN_FM && app->audio.source == AUDIO_SOURCE_FM) {
            if (app->ui.fm_control_mode == UI_FM_CONTROL_VOLUME) {
                app_set_volume(app, (int32_t)app->settings.value.volume_percent +
                                    ((int32_t)event->value * (int32_t)APP_VOLUME_STEP_PERCENT));
            } else {
                int32_t next = (int32_t)app->fm.frequency_10khz +
                               ((int32_t)event->value * (int32_t)APP_FM_STEP_10KHZ);
                if (next < (int32_t)APP_FM_MIN_10KHZ) next = APP_FM_MIN_10KHZ;
                if (next > (int32_t)APP_FM_MAX_10KHZ) next = APP_FM_MAX_10KHZ;
                if ((uint16_t)next != app->fm.frequency_10khz) {
                    (void)kiku_app_tune_fm(app, (uint16_t)next);
                }
            }
        } else if (app->ui.screen == UI_SCREEN_NOW_PLAYING ||
                   app->ui.screen == UI_SCREEN_BLUETOOTH) {
            app_set_volume(app, (int32_t)app->settings.value.volume_percent +
                                ((int32_t)event->value * (int32_t)APP_VOLUME_STEP_PERCENT));
        }
        break;

    case INPUT_EVENT_BUTTON_1:
        if (app->audio.source == AUDIO_SOURCE_FM && app->ui.screen == UI_SCREEN_FM) {
            (void)kiku_app_seek_fm(app, false);
        } else if (app->ui.screen == UI_SCREEN_NOW_PLAYING &&
                   app->audio.source == AUDIO_SOURCE_LOCAL) {
            app_open_relative_local_track(app, -1, true);
        } else if (app->ui.screen == UI_SCREEN_BLUETOOTH &&
                   app->audio.source == AUDIO_SOURCE_BLUETOOTH &&
                   app->ui.bluetooth_connected) {
            app_bm83_control(app, BM83_CONTROL_PREVIOUS_TRACK);
        } else {
            /* No competing B1 short action on menus/status pages: make Back a
             * normal click instead of forcing a long hold. app_go_back() is a
             * deliberate no-op on Home and transition/sleep screens. */
            app_go_back(app);
        }
        break;

    case INPUT_EVENT_BUTTON_2:
        if (app->ui.screen == UI_SCREEN_HOME) {
            uint32_t now = app_now(app);
            if (app->home_b2_press_count == 0U ||
                (uint32_t)(now - app->home_b2_first_press_ms) > APP_THEME_GESTURE_WINDOW_MS) {
                app->home_b2_press_count = 1U;
                app->home_b2_first_press_ms = now;
            } else {
                ++app->home_b2_press_count;
            }
            if (app->home_b2_press_count >= 5U) {
                app->home_b2_press_count = 0U;
                app->ui.theme = (ui_theme_t)(((unsigned)app->ui.theme + 1U) % (unsigned)UI_THEME_COUNT);
            }
        } else if (app->ui.screen == UI_SCREEN_FM && app->audio.source == AUDIO_SOURCE_FM) {
            (void)kiku_app_seek_fm(app, true);
        } else if (app->ui.screen == UI_SCREEN_BLUETOOTH &&
                   app->audio.source == AUDIO_SOURCE_BLUETOOTH) {
            app_bm83_control(app, BM83_CONTROL_NEXT_TRACK);
        } else if (app->ui.screen == UI_SCREEN_NOW_PLAYING &&
                   app->audio.source == AUDIO_SOURCE_LOCAL) {
            app_open_relative_local_track(app, +1, true);
        }
        break;

    case INPUT_EVENT_ENCODER_PRESS:
        if (app->ui.screen == UI_SCREEN_HOME) {
            app->home_b2_press_count = 0U;
            app_open_home_selection(app);
        }
        else if (app->ui.screen == UI_SCREEN_LIBRARY) app_activate_library_selection(app);
        else if (app->ui.screen == UI_SCREEN_SETTINGS) app_activate_settings_selection(app);
        else if (app->ui.screen == UI_SCREEN_FM && app->audio.source == AUDIO_SOURCE_FM) {
            app->ui.fm_control_mode =
                app->ui.fm_control_mode == UI_FM_CONTROL_TUNE ?
                UI_FM_CONTROL_VOLUME : UI_FM_CONTROL_TUNE;
        }
        else if (app->ui.screen == UI_SCREEN_NOW_PLAYING &&
                 app->audio.source == AUDIO_SOURCE_LOCAL) {
            if (app->local.file != NULL) {
                (void)local_playback_set_playing(&app->local, !app->local.playing);
            }
        }
        else if (app->ui.screen == UI_SCREEN_BLUETOOTH &&
                 app->audio.source == AUDIO_SOURCE_BLUETOOTH) {
            app_bm83_control(app, BM83_CONTROL_PLAY_PAUSE);
        }
        break;

    case INPUT_EVENT_BUTTON_1_LONG:
        /* Long Back is only needed where B1's short click already has a primary
         * action. Menu/status pages use the short click for Back instead. */
        if ((app->ui.screen == UI_SCREEN_FM && app->audio.source == AUDIO_SOURCE_FM) ||
            (app->ui.screen == UI_SCREEN_NOW_PLAYING && app->audio.source == AUDIO_SOURCE_LOCAL) ||
            (app->ui.screen == UI_SCREEN_BLUETOOTH && app->audio.source == AUDIO_SOURCE_BLUETOOTH &&
             app->ui.bluetooth_connected)) {
            app_go_back(app);
        }
        break;
    case INPUT_EVENT_BUTTON_2_LONG:
        if (app->audio.source == AUDIO_SOURCE_FM && app->deps.radio != NULL) {
            (void)kiku_app_seek_fm(app, true);
        } else if (app->audio.source == AUDIO_SOURCE_BLUETOOTH &&
                   !app->ui.bluetooth_connected && app->deps.bluetooth != NULL) {
            (void)bm83_mmi_action(app->deps.bluetooth, 0U, BM83_MMI_FAST_PAIRING);
        }
        break;
    case INPUT_EVENT_NONE:
    default:
        break;
    }
}

void kiku_app_toggle_soft_power(kiku_app_t *app)
{
    if (app == NULL) return;
    if (app->soft_powered_off || app->ui.screen == UI_SCREEN_SLEEP) app_begin_power_on(app);
    else app_begin_power_off(app);
}

static uint16_t rd16be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t rd32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static bool app_uid_is_valid_track(const uint8_t uid[8])
{
    if (uid == NULL) return false;
    bool all_ff = true;
    for (size_t i = 0U; i < 8U; ++i) {
        if (uid[i] != 0xFFU) all_ff = false;
    }
    return !all_ff;
}

static void app_parse_bm83_notification(kiku_app_t *app, const bm83_packet_t *packet)
{
    /* BM83 event 0x1A wraps AVRCP RegisterNotification responses as:
     * database, response, 0x48, 0x00, company ID, PDU 0x31, packet type,
     * parameter length and parameters. Notification payload sizes are
     * event-specific: PlaybackStatusChanged is only 2 bytes while
     * TrackChanged is EventID + an 8-byte media identifier. */
    if (app == NULL || packet == NULL || packet->payload_len < 12U) return;
    const uint8_t *avc = &packet->payload[1];
    size_t avc_len = packet->payload_len - 1U;
    if (avc_len < 11U || (avc[0] != 0x0FU && avc[0] != 0x0DU) ||
        avc[1] != 0x48U || avc[2] != 0x00U ||
        avc[3] != 0x00U || avc[4] != 0x19U || avc[5] != 0x58U ||
        avc[6] != 0x31U || avc[7] != 0x00U) return;

    ++g_bt_metadata_diag.notification_count;
    g_bt_metadata_diag.last_notification_response = avc[0];
    size_t raw_len = avc_len < sizeof(g_bt_metadata_diag.raw_notification) ?
                     avc_len : sizeof(g_bt_metadata_diag.raw_notification);
    memset((void *)g_bt_metadata_diag.raw_notification, 0,
           sizeof(g_bt_metadata_diag.raw_notification));
    if (raw_len != 0U) memcpy((void *)g_bt_metadata_diag.raw_notification, avc, raw_len);

    size_t param_len = ((size_t)avc[8] << 8) | avc[9];
    if (param_len == 0U || param_len > avc_len - 10U) return;
    g_bt_metadata_diag.last_notification_param_len =
        param_len > 0xFFU ? 0xFFU : (uint8_t)param_len;
    g_bt_metadata_diag.last_notification_event_id = avc[10];

    /* This fielded phone/BM83 pair has repeatedly produced
     * PlaybackStatusChanged while omitting TrackChanged. Treat either CHANGED
     * event as an acceleration hint for the periodic metadata poll. A play/pause
     * transition may cause one harmless extra query; it is far safer than
     * depending on TrackChanged for freshness. */
    if (avc[10] == 0x01U) {
        if (avc[0] == 0x0DU) app_bt_metadata_force_refresh(app, app_now(app), false);
        return;
    }

    /* Other notifications (volume, position, etc.) are expected and must not
     * be mistaken for TrackChanged. */
    if (avc[10] != 0x02U || param_len < 9U) return;

    memcpy(app->bt_track_uid, &avc[11], 8U);
    app->bt_track_uid_valid = app_uid_is_valid_track(app->bt_track_uid);
    if (avc[0] == 0x0DU) {
        app->bt_track_notification_pending = true;
        ++g_bt_metadata_diag.metadata_track_change_count;
        app_bt_metadata_force_refresh(app, app_now(app), true);
    }
    memcpy((void *)g_bt_metadata_diag.track_uid, app->bt_track_uid, 8U);
    g_bt_metadata_diag.track_uid_valid = app->bt_track_uid_valid ? 1U : 0U;
    if (!app->bt_track_uid_valid) {
        ui_state_set_track(&app->ui, "", "", "");
        ui_state_clear_track_details(&app->ui);
        return;
    }

    if (app->bt_uid_counter_valid && app->bt_player_valid) {
        app->bt_browse_stage = BT_BROWSE_NEED_TRACK_ATTRIBUTES;
    }
}

static void app_parse_browse_attributes(kiku_app_t *app,
                                        const uint8_t *payload, size_t payload_len)
{
    ++g_bt_metadata_diag.item_attr_response_count;
    if (payload != NULL && payload_len >= 3U) g_bt_metadata_diag.last_item_status = payload[2];
    /* GetItemAttributes_Rsp:
     * subevent, db, status, attr_count, total_attr_len:u16, attr_list. */
    if (app == NULL || payload == NULL || payload_len < 6U ||
        payload[0] != 0x05U || payload[2] != 0x04U) return;
    uint8_t count = payload[3];
    size_t list_len = rd16be(&payload[4]);
    if (list_len > payload_len - 6U) return;

    const uint8_t *q = &payload[6];
    size_t remain = list_len;
    char title[sizeof(app->ui.track_title)] = {0};
    char artist[sizeof(app->ui.track_artist)] = {0};
    char album[sizeof(app->ui.track_album)] = {0};
    char genre[sizeof(app->ui.track_genre)] = {0};
    uint16_t track_number = 0U;
    uint16_t track_total = 0U;
    uint32_t duration_ms = 0U;
    for (uint8_t i = 0U; i < count && remain >= 8U; ++i) {
        uint32_t attr = rd32be(q);
        size_t value_len = rd16be(&q[6]);
        q += 8U;
        remain -= 8U;
        if (value_len > remain) break;
        if (attr == 1U) app_copy_metadata_text(title, sizeof(title), q, value_len);
        else if (attr == 2U) app_copy_metadata_text(artist, sizeof(artist), q, value_len);
        else if (attr == 3U) app_copy_metadata_text(album, sizeof(album), q, value_len);
        else if (attr == 4U) {
            uint32_t value = app_parse_metadata_u32(q, value_len);
            track_number = value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
        } else if (attr == 5U) {
            uint32_t value = app_parse_metadata_u32(q, value_len);
            track_total = value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
        } else if (attr == 6U) {
            app_copy_metadata_text(genre, sizeof(genre), q, value_len);
        } else if (attr == 7U) {
            duration_ms = app_parse_metadata_u32(q, value_len);
        }
        q += value_len;
        remain -= value_len;
    }
    app_cleanup_artist_text(artist);
    if (title[0] != '\0' || artist[0] != '\0' || album[0] != '\0') {
        ui_state_set_track(&app->ui, title, artist, album);
        ui_state_set_track_details(&app->ui, genre, track_number, track_total, duration_ms);
        app_copy_metadata_text((char *)g_bt_metadata_diag.title,
                               sizeof(g_bt_metadata_diag.title),
                               (const uint8_t *)title, strlen(title));
        app_copy_metadata_text((char *)g_bt_metadata_diag.artist,
                               sizeof(g_bt_metadata_diag.artist),
                               (const uint8_t *)artist, strlen(artist));
        app_copy_metadata_text((char *)g_bt_metadata_diag.album,
                               sizeof(g_bt_metadata_diag.album),
                               (const uint8_t *)album, strlen(album));
        app_copy_metadata_text((char *)g_bt_metadata_diag.genre,
                               sizeof(g_bt_metadata_diag.genre),
                               (const uint8_t *)genre, strlen(genre));
        g_bt_metadata_diag.track_number = track_number;
        g_bt_metadata_diag.track_total = track_total;
        g_bt_metadata_diag.duration_ms = duration_ms;
    }
}

static void app_copy_metadata_text(char *dst, size_t dst_size,
                                   const uint8_t *src, size_t src_len)
{
    if (dst == NULL || dst_size == 0U) return;
    size_t out = 0U;
    for (size_t i = 0U; i < src_len && out + 1U < dst_size; ++i) {
        uint8_t c = src[i];
        if (c == '\r' || c == '\n' || c == '\t') {
            dst[out++] = ' ';
        } else if (c >= 0x20U && c < 0x7FU) {
            dst[out++] = (char)c;
        } else if (c >= 0xC0U) {
            /* The on-device face is ASCII. Skip unsupported emoji/symbols
             * cleanly instead of showing a jarring '?' in otherwise valid
             * Spotify/phone metadata. Preserve word separation where useful. */
            if (out != 0U && dst[out - 1U] != ' ' && out + 1U < dst_size) dst[out++] = ' ';
            while (i + 1U < src_len && (src[i + 1U] & 0xC0U) == 0x80U) ++i;
        }
    }
    while (out != 0U && dst[out - 1U] == ' ') --out;
    dst[out] = '\0';
}

static void app_cleanup_artist_text(char *artist)
{
    if (artist == NULL) return;

    static const char *const suffixes[] = { "Video Available", "Audio Available" };
    for (size_t i = 0U; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
        char *p = strstr(artist, suffixes[i]);
        if (p != NULL) {
            char *tail = p + strlen(suffixes[i]);
            while (*tail == ' ') ++tail;
            if (*tail == '\0') {
                while (p > artist && p[-1] == ' ') --p;
                *p = '\0';
            }
        }
    }

    char *src = artist;
    char *dst = artist;
    bool previous_space = false;
    while (*src != '\0') {
        bool space = *src == ' ';
        if (!space || !previous_space) *dst++ = *src;
        previous_space = space;
        ++src;
    }
    while (dst > artist && dst[-1] == ' ') --dst;
    *dst = '\0';
}

static bool app_time_reached(uint32_t now, uint32_t target)
{
    return (int32_t)(now - target) >= 0;
}

static uint32_t app_bt_metadata_core_interval(const kiku_app_t *app)
{
    return (app != NULL && app->settings.value.power_save_enabled != 0U) ?
           APP_POWER_SAVE_BT_METADATA_CORE_POLL_MS : APP_BT_METADATA_CORE_POLL_MS;
}

static void app_bt_metadata_sync_diag(const kiku_app_t *app)
{
    if (app == NULL) return;
    g_bt_metadata_diag.metadata_inflight_kind = app->bt_metadata_request_kind;
    g_bt_metadata_diag.metadata_inflight_attribute = app->bt_metadata_requested_attribute;
    g_bt_metadata_diag.metadata_stage_seen_mask = app->bt_metadata_stage_seen_mask;
}

static void app_bt_metadata_clear_physical_request(kiku_app_t *app)
{
    if (app == NULL) return;
    app->bt_metadata_request_started_ms = 0U;
    app->bt_metadata_request_inflight = false;
    app->bt_metadata_request_kind = BT_METADATA_REQ_NONE;
    app->bt_metadata_requested_attribute = 0U;
    app->bt_metadata_command_accepted = false;
    app->bt_metadata_response_stalled = false;
    app->bt_metadata_discard_response = false;
}

static void app_bt_metadata_reset_stage(kiku_app_t *app)
{
    if (app == NULL) return;
    memset(app->bt_metadata_stage_title, 0, sizeof(app->bt_metadata_stage_title));
    memset(app->bt_metadata_stage_artist, 0, sizeof(app->bt_metadata_stage_artist));
    memset(app->bt_metadata_stage_album, 0, sizeof(app->bt_metadata_stage_album));
    app->bt_metadata_stage_seen_mask = 0U;
    app_bt_metadata_sync_diag(app);
}

static void app_bt_metadata_reset_transaction(kiku_app_t *app, uint32_t now,
                                              bool force_core)
{
    if (app == NULL) return;
    const bool stale_response_possible = app->bt_metadata_request_inflight ||
                                         app->bt_metadata_discard_response;
    app_bt_metadata_clear_physical_request(app);
    /* Resetting a profile/source can abandon a 0x4A after BM83 accepted it but
     * before its 0x5D response reached the host. Keep a short drain quarantine
     * across that boundary; otherwise a delayed old title/artist/album response
     * is indistinguishable from the first request after re-entry/reconnect. */
    app->bt_metadata_discard_response = stale_response_possible;
    app->bt_metadata_detail_phase = 0U;
    app->bt_metadata_core_next_attribute = 0U;
    app->bt_metadata_force_core = force_core;
    app->bt_metadata_identity_guard_active = false;
    app->bt_metadata_identity_guard_started_ms = 0U;
    memset(app->bt_metadata_guard_title, 0, sizeof(app->bt_metadata_guard_title));
    memset(app->bt_metadata_guard_artist, 0, sizeof(app->bt_metadata_guard_artist));
    memset(app->bt_metadata_guard_album, 0, sizeof(app->bt_metadata_guard_album));
    app->last_bt_metadata_ms = 0U;
    app->bt_metadata_next_due_ms = stale_response_possible ?
                                   now + APP_BT_METADATA_STALE_DRAIN_MS : now;
    app_bt_metadata_reset_stage(app);
}

static void app_bt_metadata_end_request(kiku_app_t *app, uint32_t now, bool success)
{
    if (app == NULL) return;
    const uint8_t kind = app->bt_metadata_request_kind;
    app_bt_metadata_clear_physical_request(app);
    if (success) {
        if (kind == BT_METADATA_REQ_CORE) {
            app->bt_metadata_core_next_attribute = 0U;
            app->last_bt_metadata_ms = now;
            app->bt_metadata_force_core = false;
            ++g_bt_metadata_diag.metadata_core_complete_count;
            app->bt_metadata_next_due_ms = now + app_bt_metadata_core_interval(app);
        } else if (kind == BT_METADATA_REQ_DETAIL && app->bt_metadata_detail_phase < 4U) {
            ++app->bt_metadata_detail_phase;
            app->bt_metadata_next_due_ms = now + APP_BT_METADATA_DETAIL_GAP_MS;
        }
    } else {
        if (kind == BT_METADATA_REQ_CORE || app->bt_metadata_core_next_attribute != 0U) {
            app->bt_metadata_core_next_attribute = 0U;
            app->bt_metadata_force_core = true;
            app_bt_metadata_reset_stage(app);
        }
        app->bt_metadata_next_due_ms = now + APP_BT_METADATA_RETRY_BACKOFF_MS;
    }
    app_bt_metadata_sync_diag(app);
}

static void app_bt_metadata_advance_core(kiku_app_t *app, uint32_t now,
                                         uint8_t completed_attribute)
{
    if (app == NULL || completed_attribute < 1U || completed_attribute >= 3U) return;
    app_bt_metadata_clear_physical_request(app);
    app->bt_metadata_core_next_attribute = (uint8_t)(completed_attribute + 1U);
    app->bt_metadata_next_due_ms = now + APP_BT_METADATA_DETAIL_GAP_MS;
    app_bt_metadata_sync_diag(app);
}

static void app_bt_metadata_force_refresh(kiku_app_t *app, uint32_t now,
                                          bool clear_visible_track)
{
    if (app == NULL) return;
    if (clear_visible_track && !app->bt_metadata_identity_guard_active &&
        (app->ui.track_title[0] != '\0' || app->ui.track_artist[0] != '\0' ||
         app->ui.track_album[0] != '\0')) {
        /* NEXT/PREV and TrackChanged clear the visible identity immediately, but
         * the remote player can still answer the first fresh 0x4A with the old
         * song for a short settling window. Remember that exact identity and do
         * not let it repopulate the UI. Repeated queued skips preserve the first
         * non-empty guard instead of replacing it with the already-cleared UI. */
        memcpy(app->bt_metadata_guard_title, app->ui.track_title,
               sizeof(app->bt_metadata_guard_title));
        memcpy(app->bt_metadata_guard_artist, app->ui.track_artist,
               sizeof(app->bt_metadata_guard_artist));
        memcpy(app->bt_metadata_guard_album, app->ui.track_album,
               sizeof(app->bt_metadata_guard_album));
        app->bt_metadata_guard_title[sizeof(app->bt_metadata_guard_title) - 1U] = '\0';
        app->bt_metadata_guard_artist[sizeof(app->bt_metadata_guard_artist) - 1U] = '\0';
        app->bt_metadata_guard_album[sizeof(app->bt_metadata_guard_album) - 1U] = '\0';
        app->bt_metadata_identity_guard_started_ms = now;
        app->bt_metadata_identity_guard_active = true;
    }
    app->bt_metadata_force_core = true;
    app->bt_metadata_detail_phase = 0U;
    app->bt_metadata_next_due_ms = now + APP_BT_METADATA_POST_CONTROL_MS;
    ++g_bt_metadata_diag.metadata_forced_refresh_count;
    /* A refresh hint means any metadata response already in flight may describe
     * the state before the change that triggered the hint. Drain that response,
     * but never commit it. This is deliberately true even for
     * PlaybackStatusChanged: a pause/resume can cost one harmless re-query, while
     * a phone-side track change that omits TrackChanged must not leak stale data. */
    if (app->bt_metadata_request_inflight) {
        app->bt_metadata_discard_response = true;
    } else if (app->bt_metadata_core_next_attribute != 0U) {
        /* A refresh can arrive in the quiet gap between title/artist/album
         * requests. The partial identity is already stale even though there is
         * no physical 0x4A on the wire at this instant, so restart the logical
         * three-attribute cycle from title. */
        app->bt_metadata_core_next_attribute = 0U;
        app_bt_metadata_reset_stage(app);
    }
    if (clear_visible_track) {
        ui_state_set_track(&app->ui, "", "", "");
        ui_state_clear_track_details(&app->ui);
        app_bt_metadata_reset_stage(app);
    }
}

static void app_parse_bm83_metadata(kiku_app_t *app, const bm83_packet_t *packet)
{
    /* BM83 event 0x1A: database index followed by the AVRCP vendor response.
     * GetElementAttributes (PDU 0x20) returns a count followed by repeated
     * {attribute:u32, charset:u16, length:u16, value} records. */
    if (app == NULL || packet == NULL || packet->payload_len < 12U) return;
    const uint8_t *avc = &packet->payload[1];
    size_t avc_len = packet->payload_len - 1U;
    const uint8_t response = avc[0];
    if ((response != 0x09U && response != 0x0CU && response != 0x0DU) ||
        avc[1] != 0x48U || avc[2] != 0x00U ||
        avc[3] != 0x00U || avc[4] != 0x19U || avc[5] != 0x58U ||
        avc[6] != 0x20U || avc[7] != 0x00U || avc_len < 11U) return;

    size_t param_len = ((size_t)avc[8] << 8) | avc[9];
    if (param_len > avc_len - 10U || param_len < 1U) return;
    const uint8_t *q = &avc[10];
    size_t remain = param_len;
    uint8_t count = *q++;
    --remain;

    char title[sizeof(app->ui.track_title)];
    char artist[sizeof(app->ui.track_artist)];
    char album[sizeof(app->ui.track_album)];
    memcpy(title, app->ui.track_title, sizeof(title));
    memcpy(artist, app->ui.track_artist, sizeof(artist));
    memcpy(album, app->ui.track_album, sizeof(album));
    title[sizeof(title) - 1U] = '\0';
    artist[sizeof(artist) - 1U] = '\0';
    album[sizeof(album) - 1U] = '\0';
    for (uint8_t i = 0U; i < count && remain >= 8U; ++i) {
        uint32_t attr = rd32be(q);
        size_t value_len = ((size_t)q[6] << 8) | q[7];
        q += 8U;
        remain -= 8U;
        if (value_len > remain) break;
        if (attr == 1U) app_copy_metadata_text(title, sizeof(title), q, value_len);
        else if (attr == 2U) app_copy_metadata_text(artist, sizeof(artist), q, value_len);
        else if (attr == 3U) app_copy_metadata_text(album, sizeof(album), q, value_len);
        q += value_len;
        remain -= value_len;
    }

    app_cleanup_artist_text(artist);

    if (title[0] != '\0' || artist[0] != '\0' || album[0] != '\0') {
        ui_state_set_track(&app->ui, title, artist, album);
    }
}

static void app_parse_bm83_metadata_v206(kiku_app_t *app,
                                         const bm83_packet_t *packet)
{
    /* UART v2.06+ event 0x5D:
     * PDU_ID, DB, Response, IsEndOfBody, Attr_Num, Total_Attr_List_Len:u16,
     * then standard AVRCP attribute-value entries. */
    if (app == NULL || packet == NULL || packet->payload_len < 7U ||
        packet->payload[0] != 0x20U) return;
    ++g_bt_metadata_diag.metadata_response_count;
    const uint8_t database_index = packet->payload[1];
    if (database_index != app->bt_database_index) return;
    const uint8_t response = packet->payload[2];
    const bool end_of_body = packet->payload[3] != 0U;
    if (response != 0x04U && response != 0x09U &&
        response != 0x0CU && response != 0x0DU) {
        if (end_of_body && app->bt_metadata_request_inflight) {
            app_bt_metadata_end_request(app, app_now(app), false);
        }
        return;
    }
    uint8_t count = packet->payload[4];
    size_t list_len = rd16be(&packet->payload[5]);
    if (list_len > packet->payload_len - 7U) {
        if (end_of_body && app->bt_metadata_request_inflight) {
            app_bt_metadata_end_request(app, app_now(app), false);
        }
        return;
    }

    /* A local NEXT/PREV can overtake an old metadata response already queued in
     * the BM83/remote AVRCP path. Do not let that late response repopulate the
     * song we intentionally cleared; simply drain it and then issue the forced
     * refresh for the new track. */
    if (app->bt_metadata_discard_response) {
        uint32_t now = app_now(app);
        /* Any fragment belonging to the stale transaction extends the drain
         * window so a continuation packet cannot be mistaken for the next
         * request merely because it arrived near the original deadline. */
        app->bt_metadata_next_due_ms = now + APP_BT_METADATA_STALE_DRAIN_MS;
        if (end_of_body) {
            if (app->bt_metadata_request_inflight) {
                app_bt_metadata_end_request(app, now, false);
            } else {
                app->bt_metadata_discard_response = false;
            }
            app->bt_metadata_force_core = true;
            app->bt_metadata_next_due_ms = now + APP_BT_METADATA_POST_CONTROL_MS;
            app_bt_metadata_sync_diag(app);
        }
        return;
    }

    /* Production 0x5D responses are accepted only while the response-driven
     * scheduler is expecting one. This prevents an unsolicited or very late
     * packet from mutating user-visible metadata after a disconnect/source
     * change. Raw packets remain available in g_bm83_diag for bench diagnosis. */
    if (!app->bt_metadata_request_inflight) return;

    const uint8_t *q = &packet->payload[7];
    size_t remain = list_len;
    char title[sizeof(app->ui.track_title)];
    char artist[sizeof(app->ui.track_artist)];
    char album[sizeof(app->ui.track_album)];
    char genre[sizeof(app->ui.track_genre)];
    const bool staged_core = app->bt_metadata_request_inflight &&
                             app->bt_metadata_request_kind == BT_METADATA_REQ_CORE;
    const bool staged_detail = app->bt_metadata_request_inflight &&
                               app->bt_metadata_request_kind == BT_METADATA_REQ_DETAIL;
    if (staged_core) {
        memcpy(title, app->bt_metadata_stage_title, sizeof(title));
        memcpy(artist, app->bt_metadata_stage_artist, sizeof(artist));
        memcpy(album, app->bt_metadata_stage_album, sizeof(album));
    } else {
        memcpy(title, app->ui.track_title, sizeof(title));
        memcpy(artist, app->ui.track_artist, sizeof(artist));
        memcpy(album, app->ui.track_album, sizeof(album));
    }
    memcpy(genre, app->ui.track_genre, sizeof(genre));
    title[sizeof(title)-1U] = '\0';
    artist[sizeof(artist)-1U] = '\0';
    album[sizeof(album)-1U] = '\0';
    genre[sizeof(genre)-1U] = '\0';
    uint16_t track_number = app->ui.track_number;
    uint16_t track_total = app->ui.track_total;
    uint32_t duration_ms = app->ui.track_duration_ms;
    bool detail_match = false;
    for (uint8_t i = 0U; i < count && remain >= 8U; ++i) {
        uint32_t attr = rd32be(q);
        size_t value_len = rd16be(&q[6]);
        q += 8U;
        remain -= 8U;
        if (value_len > remain) break;
        if (attr == 1U && !staged_detail) {
            app_copy_metadata_text(title, sizeof(title), q, value_len);
            if (staged_core) app->bt_metadata_stage_seen_mask |= 0x01U;
        } else if (attr == 2U && !staged_detail) {
            app_copy_metadata_text(artist, sizeof(artist), q, value_len);
            if (staged_core) app->bt_metadata_stage_seen_mask |= 0x02U;
        } else if (attr == 3U && !staged_detail) {
            app_copy_metadata_text(album, sizeof(album), q, value_len);
            if (staged_core) app->bt_metadata_stage_seen_mask |= 0x04U;
        }
        else if (attr == 4U && (!staged_detail || app->bt_metadata_requested_attribute == 4U)) {
            uint32_t value = app_parse_metadata_u32(q, value_len);
            track_number = value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
            if (staged_detail && app->bt_metadata_requested_attribute == 4U) detail_match = true;
        } else if (attr == 5U && (!staged_detail || app->bt_metadata_requested_attribute == 5U)) {
            uint32_t value = app_parse_metadata_u32(q, value_len);
            track_total = value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
            if (staged_detail && app->bt_metadata_requested_attribute == 5U) detail_match = true;
        } else if (attr == 6U && (!staged_detail || app->bt_metadata_requested_attribute == 6U)) {
            app_copy_metadata_text(genre, sizeof(genre), q, value_len);
            if (staged_detail && app->bt_metadata_requested_attribute == 6U) detail_match = true;
        } else if (attr == 7U && (!staged_detail || app->bt_metadata_requested_attribute == 7U)) {
            duration_ms = app_parse_metadata_u32(q, value_len);
            if (staged_detail && app->bt_metadata_requested_attribute == 7U) detail_match = true;
        }
        q += value_len;
        remain -= value_len;
    }
    app_cleanup_artist_text(artist);
    if (staged_core) {
        memcpy(app->bt_metadata_stage_title, title, sizeof(app->bt_metadata_stage_title));
        memcpy(app->bt_metadata_stage_artist, artist, sizeof(app->bt_metadata_stage_artist));
        memcpy(app->bt_metadata_stage_album, album, sizeof(app->bt_metadata_stage_album));
        app_bt_metadata_sync_diag(app);
        if (!end_of_body) return;

        const uint8_t requested_attribute = app->bt_metadata_requested_attribute;
        const uint8_t requested_mask = (requested_attribute >= 1U && requested_attribute <= 3U) ?
                                       (uint8_t)(1U << (requested_attribute - 1U)) : 0U;
        /* Production deliberately uses the field-proven one-attribute 0x4A
         * request shape. A final response which does not contain the requested
         * identity field has still completed the BM83 transaction, but it cannot
         * be allowed to advance an atomic title/artist/album cycle. */
        if (requested_mask == 0U ||
            (app->bt_metadata_stage_seen_mask & requested_mask) == 0U) {
            app_bt_metadata_end_request(app, app_now(app), false);
            return;
        }

        if (requested_attribute < 3U) {
            app_bt_metadata_advance_core(app, app_now(app), requested_attribute);
            return;
        }

        const uint32_t now = app_now(app);
        if (app->bt_metadata_identity_guard_active) {
            const bool still_old_identity =
                strcmp(app->bt_metadata_guard_title, title) == 0 &&
                strcmp(app->bt_metadata_guard_artist, artist) == 0 &&
                strcmp(app->bt_metadata_guard_album, album) == 0;
            const bool guard_live =
                (uint32_t)(now - app->bt_metadata_identity_guard_started_ms) <
                APP_BT_METADATA_IDENTITY_GUARD_MS;
            if (still_old_identity && guard_live) {
                /* This is a fresh query, but the phone has not applied the local
                 * track step yet. Keep the UI blank and try again after the normal
                 * post-control settle interval instead of flashing the old song. */
                app_bt_metadata_end_request(app, now, false);
                app->bt_metadata_next_due_ms = now + APP_BT_METADATA_POST_CONTROL_MS;
                return;
            }
            if (!still_old_identity || !guard_live) {
                app->bt_metadata_identity_guard_active = false;
                app->bt_metadata_identity_guard_started_ms = 0U;
            }
        }

        const bool changed = strcmp(app->ui.track_title, title) != 0 ||
                             strcmp(app->ui.track_artist, artist) != 0 ||
                             strcmp(app->ui.track_album, album) != 0;
        /* Commit the three user-visible identity fields together. Missing
         * attributes stay empty rather than leaking fields from the old song. */
        if (app->bt_metadata_stage_seen_mask != 0U) {
            ui_state_set_track(&app->ui, title, artist, album);
            if (changed) {
                ui_state_clear_track_details(&app->ui);
                app->bt_metadata_detail_phase = 0U;
                ++g_bt_metadata_diag.metadata_core_change_count;
            }
        }
        app_bt_metadata_end_request(app, now, true);
    } else if (staged_detail) {
        /* Detail polls must never alter title/artist/album. Those three identity
         * fields are only committed atomically by a combined core response. */
        if (!end_of_body || !detail_match) return;
        ui_state_set_track_details(&app->ui, genre, track_number, track_total, duration_ms);
        app_bt_metadata_end_request(app, app_now(app), true);
    }

    app_copy_metadata_text((char *)g_bt_metadata_diag.title,
                           sizeof(g_bt_metadata_diag.title),
                           (const uint8_t *)app->ui.track_title,
                           strlen(app->ui.track_title));
    app_copy_metadata_text((char *)g_bt_metadata_diag.artist,
                           sizeof(g_bt_metadata_diag.artist),
                           (const uint8_t *)app->ui.track_artist,
                           strlen(app->ui.track_artist));
    app_copy_metadata_text((char *)g_bt_metadata_diag.album,
                           sizeof(g_bt_metadata_diag.album),
                           (const uint8_t *)app->ui.track_album,
                           strlen(app->ui.track_album));
    app_copy_metadata_text((char *)g_bt_metadata_diag.genre,
                           sizeof(g_bt_metadata_diag.genre),
                           (const uint8_t *)app->ui.track_genre,
                           strlen(app->ui.track_genre));
    g_bt_metadata_diag.track_number = app->ui.track_number;
    g_bt_metadata_diag.track_total = app->ui.track_total;
    g_bt_metadata_diag.duration_ms = app->ui.track_duration_ms;

}


static void app_parse_browse_item_list(kiku_app_t *app,
                                       const uint8_t *payload, size_t payload_len)
{
    /* GetFolderItems_Rsp payload:
     * subevent, db, status, uidCounter:u16, items:u16, listLen:u16, itemList. */
    if (app == NULL || payload == NULL || payload_len < 9U || payload[0] != 0x00U) return;
    if (payload[2] != 0x04U || rd16be(&payload[5]) == 0U) return;
    size_t list_len = rd16be(&payload[7]);
    if (list_len > payload_len - 9U || list_len < 3U) return;

    const uint8_t *q = &payload[9];
    size_t remain = list_len;
    /* AVRCP Media Element Item: type 0x03, length:u16, then UID, media type,
     * charset, display-name and an attribute-value list. */
    if (q[0] != 0x03U) return;
    size_t item_len = rd16be(&q[1]);
    q += 3U;
    remain -= 3U;
    if (item_len > remain || item_len < 14U) return;
    size_t item_remain = item_len;

    q += 8U; item_remain -= 8U; /* UID */
    ++q; --item_remain;          /* media type */
    q += 2U; item_remain -= 2U; /* charset */
    if (item_remain < 2U) return;
    size_t name_len = rd16be(q); q += 2U; item_remain -= 2U;
    if (name_len > item_remain) return;

    char title[sizeof(app->ui.track_title)] = {0};
    char artist[sizeof(app->ui.track_artist)] = {0};
    char album[sizeof(app->ui.track_album)] = {0};
    char genre[sizeof(app->ui.track_genre)] = {0};
    uint16_t track_number = 0U;
    uint16_t track_total = 0U;
    uint32_t duration_ms = 0U;
    /* The displayable media-element name is normally the track title and is a
     * useful fallback when the remote omits attribute ID 1. */
    app_copy_metadata_text(title, sizeof(title), q, name_len);
    q += name_len; item_remain -= name_len;
    if (item_remain < 1U) return;
    uint8_t attr_count = *q++; --item_remain;

    for (uint8_t i = 0U; i < attr_count && item_remain >= 8U; ++i) {
        uint32_t attr = rd32be(q);
        size_t value_len = rd16be(&q[6]);
        q += 8U; item_remain -= 8U;
        if (value_len > item_remain) break;
        if (attr == 1U) app_copy_metadata_text(title, sizeof(title), q, value_len);
        else if (attr == 2U) app_copy_metadata_text(artist, sizeof(artist), q, value_len);
        else if (attr == 3U) app_copy_metadata_text(album, sizeof(album), q, value_len);
        else if (attr == 4U) {
            uint32_t value = app_parse_metadata_u32(q, value_len);
            track_number = value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
        } else if (attr == 5U) {
            uint32_t value = app_parse_metadata_u32(q, value_len);
            track_total = value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
        } else if (attr == 6U) {
            app_copy_metadata_text(genre, sizeof(genre), q, value_len);
        } else if (attr == 7U) {
            duration_ms = app_parse_metadata_u32(q, value_len);
        }
        q += value_len; item_remain -= value_len;
    }

    app_cleanup_artist_text(artist);
    if (title[0] != '\0' || artist[0] != '\0' || album[0] != '\0') {
        ui_state_set_track(&app->ui, title, artist, album);
        ui_state_set_track_details(&app->ui, genre, track_number, track_total, duration_ms);
    }
}

static bool app_parse_media_player_list(kiku_app_t *app,
                                        const uint8_t *payload, size_t payload_len)
{
    if (app == NULL || payload == NULL || payload_len < 9U || payload[0] != 0x00U ||
        payload[2] != 0x04U || rd16be(&payload[5]) == 0U) return false;

    size_t list_len = rd16be(&payload[7]);
    if (list_len > payload_len - 9U) return false;
    const uint8_t *q = &payload[9];
    size_t remain = list_len;
    uint16_t selected = 0U;
    unsigned selected_score = 0U;
    bool found = false;

    while (remain >= 3U) {
        uint8_t item_type = q[0];
        size_t item_len = rd16be(&q[1]);
        q += 3U;
        remain -= 3U;
        if (item_len > remain) break;

        if (item_type == 0x01U && item_len >= 28U) {
            uint16_t player_id = rd16be(q);
            uint8_t major_type = q[2];
            uint8_t play_status = q[7];
            /* Prefer the player which is actively playing. If the phone does
             * not mark one as playing, prefer an audio player over any other
             * valid media-player item, while still retaining a final fallback. */
            unsigned score = 1U;
            if ((major_type & 0x01U) != 0U || (major_type & 0x04U) != 0U) score += 2U;
            if (play_status == 0x01U) score += 8U;
            if (!found || score > selected_score) {
                selected = player_id;
                selected_score = score;
                found = true;
            }
        }

        q += item_len;
        remain -= item_len;
    }

    if (!found) return false;
    app->bt_player_id = selected;
    app->bt_player_valid = false;
    return true;
}

static void app_handle_browse_payload(kiku_app_t *app,
                                      const uint8_t *payload, size_t payload_len)
{
    if (app == NULL || payload == NULL || payload_len == 0U) return;

    if (payload[0] == 0x00U) { /* GetFolderItems_Rsp */
        if (app->bt_browse_stage == BT_BROWSE_WAIT_PLAYER_LIST) {
            if (app_parse_media_player_list(app, payload, payload_len)) {
                app->bt_browse_stage = BT_BROWSE_NEED_SET_ADDRESSED;
            } else {
                app->bt_browse_stage = BT_BROWSE_IDLE;
            }
        } else if (app->bt_browse_stage == BT_BROWSE_WAIT_NOW_PLAYING) {
            app_parse_browse_item_list(app, payload, payload_len);
            app->bt_browse_stage = BT_BROWSE_IDLE;
        } else {
            /* A late response is harmless; still allow a valid Now Playing
             * media element to refresh the display. */
            app_parse_browse_item_list(app, payload, payload_len);
        }
    } else if (payload[0] == 0x02U && payload_len >= 4U &&
               app->bt_browse_stage == BT_BROWSE_WAIT_SET_ADDRESSED) {
        /* SetAddressedPlayer_Rsp: subevent, DB, response, status. Success is
         * AVRCP status 0x04; accepted/stable/changed are all usable outcomes. */
        uint8_t response = payload[2];
        uint8_t status = payload[3];
        if (status == 0x04U &&
            (response == 0x09U || response == 0x0CU || response == 0x0DU)) {
            app->bt_browse_stage = BT_BROWSE_NEED_SET_BROWSED;
        } else {
            app->bt_player_valid = false;
            app->bt_browse_stage = BT_BROWSE_IDLE;
        }
    } else if (payload[0] == 0x03U && payload_len >= 3U &&
               app->bt_browse_stage == BT_BROWSE_WAIT_SET_BROWSED) {
        /* SetBrowsedPlayer_Rsp: subevent, DB, status, then the browsed
         * player's UID counter/folder information when successful. */
        if (payload[2] == 0x04U) {
            app->bt_player_valid = true;
            if (payload_len >= 5U) {
                app->bt_uid_counter = rd16be(&payload[3]);
                app->bt_uid_counter_valid = true;
            }
            /* A RegisterNotification/TrackChanged interim response supplies
             * the current media UID. Do not assume the phone exposes an
             * enumerable Now Playing queue: the connected phone returns
             * Range Out Of Bounds for item 0 even while streaming. */
            app->bt_browse_stage = (app->bt_track_uid_valid && app->bt_uid_counter_valid) ?
                                   BT_BROWSE_NEED_TRACK_ATTRIBUTES : BT_BROWSE_IDLE;
        } else {
            app->bt_player_valid = false;
            app->bt_browse_stage = BT_BROWSE_IDLE;
        }
    } else if (payload[0] == 0x05U) { /* GetItemAttributes_Rsp */
        app_parse_browse_attributes(app, payload, payload_len);
        if (app->bt_browse_stage == BT_BROWSE_WAIT_TRACK_ATTRIBUTES) {
            /* Status 0x05 means UID changed. Refresh the browsed player's
             * UID counter then retry the exact TrackChanged UID. */
            if (payload_len >= 3U && payload[2] == 0x05U) {
                app->bt_uid_counter_valid = false;
                app->bt_browse_stage = BT_BROWSE_NEED_SET_BROWSED;
            } else {
                app->bt_browse_stage = BT_BROWSE_IDLE;
            }
        }
    } else if (payload[0] == 0x0DU && payload_len >= 5U) { /* UIDsChanged_Notify */
        app->bt_uid_counter = rd16be(&payload[3]);
        app->bt_uid_counter_valid = true;
        if (app->bt_track_uid_valid && app->bt_player_valid) {
            app->bt_browse_stage = BT_BROWSE_NEED_TRACK_ATTRIBUTES;
        }
    }
}

static void app_parse_bm83_browsing(kiku_app_t *app, const bm83_packet_t *packet)
{
    if (app == NULL || packet == NULL || packet->payload_len < 5U) return;
    uint8_t type = packet->payload[0];
    uint16_t total = rd16be(&packet->payload[1]);
    uint16_t fragment = rd16be(&packet->payload[3]);
    if ((size_t)fragment > packet->payload_len - 5U || total > APP_BT_BROWSE_BUFFER_BYTES) {
        app->bt_browse_received = 0U;
        app->bt_browse_expected = 0U;
        return;
    }

    if (type == 0x00U) {
        if (fragment == total) app_handle_browse_payload(app, &packet->payload[5], fragment);
        return;
    }
    if (type == 0x01U) {
        app->bt_browse_expected = total;
        app->bt_browse_received = 0U;
    } else if ((type != 0x02U && type != 0x03U) || app->bt_browse_expected != total) {
        app->bt_browse_received = 0U;
        app->bt_browse_expected = 0U;
        return;
    }

    if ((uint32_t)app->bt_browse_received + fragment > APP_BT_BROWSE_BUFFER_BYTES) {
        app->bt_browse_received = 0U;
        app->bt_browse_expected = 0U;
        return;
    }
    memcpy(&app->bt_browse_buffer[app->bt_browse_received], &packet->payload[5], fragment);
    app->bt_browse_received = (uint16_t)(app->bt_browse_received + fragment);
    if (type == 0x03U) {
        if (app->bt_browse_received == app->bt_browse_expected) {
            app_handle_browse_payload(app, app->bt_browse_buffer, app->bt_browse_received);
        }
        app->bt_browse_received = 0U;
        app->bt_browse_expected = 0U;
    }
}

static void app_mark_avrcp_available(kiku_app_t *app, uint8_t database_index)
{
    if (app == NULL) return;
    const bool newly_available = !app->bt_avrcp_connected;
    app->bt_avrcp_connected = true;
    app->ui.bt_avrcp_connected = true;
    app->bt_database_index = database_index & 0x0FU;
    if (newly_available && app->bt_database_index <= 1U) {
        app->bt_player_valid = false;
        app->bt_uid_counter_valid = false;
        app->bt_track_uid_valid = false;
        app->bt_avrcp_capability_pending = true;
        /* Subscribe to TrackChanged on the AVRCP control channel. Optional
         * browsing (UART command 0x41) was only introduced in BM83 Audio UART
         * v2.03; the production module reports v2.02, so sending 0x41 on that
         * package can wedge discovery before the v2.00 GetElementAttributes
         * fallback gets a chance to run. Only enter browsing discovery when
         * the running BM83 explicitly reports a command-set version that
         * supports it. */
        app->bt_track_notification_pending = true;
        app->bt_browse_stage = bm83_uart_version_at_least(app->deps.bluetooth, 2U, 3U) ?
                               BT_BROWSE_NEED_PLAYER_LIST : BT_BROWSE_IDLE;
        app_bt_metadata_reset_transaction(app, app_now(app), true);
    }
}

static void app_bm83_packet(void *ctx, const bm83_packet_t *packet)
{
    kiku_app_t *app = (kiku_app_t *)ctx;
    if (app == NULL || packet == NULL) return;

    if (packet->id == BM83_EVENT_BTM_STATUS && packet->payload_len >= 2U) {
        const uint8_t state = packet->payload[0];
        if (state == BM83_BTM_A2DP_CONNECTED) {
            app->ui.bluetooth_connected = true;
            app->bt_database_index = packet->payload[1] & 0x0FU;
            /* Do not steal the user's selected FM/local source merely because
             * a previously paired phone reconnects in the background. */
        } else if (state == BM83_BTM_A2DP_DISCONNECTED ||
                   state == BM83_BTM_ACL_DISCONNECTED) {
            app_bt_control_queue_clear(app);
            app->ui.bluetooth_connected = false;
            app->bt_avrcp_connected = false;
            app->ui.bt_avrcp_connected = false;
            app->bt_player_valid = false;
            app->bt_uid_counter_valid = false;
            app->bt_track_uid_valid = false;
            app->bt_avrcp_capability_pending = false;
            app->bt_track_notification_pending = false;
            app->bt_browse_stage = BT_BROWSE_IDLE;
            app_bt_metadata_reset_transaction(app, app_now(app), false);
            ui_state_set_track(&app->ui, "", "", "");
            ui_state_clear_track_details(&app->ui);
        } else if (state == BM83_BTM_AVRCP_CONNECTED) {
            app_mark_avrcp_available(app, packet->payload[1]);
        } else if (state == BM83_BTM_AVRCP_DISCONNECTED) {
            app->bt_avrcp_connected = false;
            app->ui.bt_avrcp_connected = false;
            app->bt_player_valid = false;
            app->bt_uid_counter_valid = false;
            app->bt_track_uid_valid = false;
            app->bt_avrcp_capability_pending = false;
            app->bt_track_notification_pending = false;
            app->bt_browse_stage = BT_BROWSE_IDLE;
            app_bt_metadata_reset_transaction(app, app_now(app), false);
        }
    } else if (packet->id == BM83_EVENT_COMMAND_ACK && packet->payload_len >= 2U &&
               packet->payload[0] == BM83_CMD_AVRCP_VENDOR_DEPENDENT &&
               app->bt_metadata_request_inflight) {
        if (packet->payload[1] == 0U) {
            /* Command_ACK only says BTM accepted the command. The corresponding
             * 0x5D is what releases the logical AVRCP metadata transaction. Keep
             * that distinction explicit so a missing response can never turn
             * into a train of accepted, still-owned 0x4A transactions. */
            app->bt_metadata_command_accepted = true;
        } else {
            if (packet->payload[1] == 0x04U || packet->payload[1] == 0x05U) {
                ++g_bt_metadata_diag.metadata_backpressure_count;
            }
            app_bt_metadata_end_request(app, app_now(app), false);
        }
    } else if (packet->id == BM83_EVENT_TYPE_CODEC && packet->payload_len >= 2U) {
        const uint8_t sample_rate = packet->payload[0];
        const uint8_t mode = packet->payload[1];
        /* Microchip defines Report_Type_Codec as the *next* I2S state. Re-arm
         * on 0x01=Prepare, before the new clock domain starts. Re-arming again
         * on 0x04=A2DP-decode used to abort the freshly-started SPI2 transfer
         * exactly as audio began, which could produce intermittent silence. */
        if (app->audio.source == AUDIO_SOURCE_BLUETOOTH &&
            sample_rate == 0x05U &&
            mode == 0x01U) {
            (void)audio_router_rearm_current_source(&app->audio);
        }
    } else if (packet->id == BM83_EVENT_AVC_VENDOR_DEPENDENT_RESPONSE) {
        app_parse_bm83_notification(app, packet);
        app_parse_bm83_metadata(app, packet);
    } else if (packet->id == BM83_EVENT_AVRCP_VENDOR_DEPENDENT_RSP) {
        app_parse_bm83_metadata_v206(app, packet);
    } else if (packet->id == BM83_EVENT_AVRCP_BROWSING) {
        app_parse_bm83_browsing(app, packet);
    }
}

dev_status_t kiku_app_init(kiku_app_t *app, const app_deps_t *deps)
{
    if ((app == NULL) || (deps == NULL) || (deps->charger == NULL) ||
        (deps->gauge == NULL) || (deps->codec == NULL) ||
        (deps->radio == NULL) || (deps->bluetooth == NULL) ||
        (deps->audio_io == NULL) || (deps->clock.time_ms == NULL)) return DEV_EINVAL;

    memset(app, 0, sizeof(*app));
    app->deps = *deps;
    app->bm83_controls = deps->bm83_controls;

    dev_status_t st = settings_init(&app->settings, &deps->settings_storage, 0U);
    if (st != DEV_OK) return st;
    st = settings_load(&app->settings);
    if ((st != DEV_OK) && (st != DEV_ECRC) && (st != DEV_ENOTSUP) && (st != DEV_EIO)) return st;

    st = power_manager_init(&app->power, deps->charger, deps->gauge,
                            APP_LOW_BATTERY_CENTI_PERCENT,
                            APP_CRITICAL_BATTERY_CENTI_PERCENT,
                            APP_POWER_POLL_MS);
    if (st != DEV_OK) return st;
#if APP_BENCH_CHARGE_POLICY_ENABLE
    st = power_manager_apply_bench_charge_policy(&app->power,
                                                  APP_BENCH_CHARGE_INPUT_LIMIT_MA,
                                                  APP_BENCH_CHARGE_CURRENT_MA,
                                                  APP_BENCH_BQ_WATCHDOG_SECONDS);
    if (st != DEV_OK) return st;
#endif

    st = audio_router_init(&app->audio, deps->audio_io, &deps->audio_route_ops);
    if (st != DEV_OK) return st;
    app->audio.speaker_enabled_by_user = app->settings.value.speaker_enabled != 0U;
    st = audio_router_set_output(&app->audio,
                                 app->settings.value.output_mode <= (uint8_t)AUDIO_OUTPUT_BOTH ?
                                 (audio_output_t)app->settings.value.output_mode : AUDIO_OUTPUT_AUTO);
    if (st != DEV_OK) return st;
    st = audio_router_set_volume(&app->audio, app->settings.value.volume_percent);
    if (st != DEV_OK) return st;

    if ((deps->local_fs.open != NULL) && (deps->pcm_sink.write != NULL)) {
        st = local_playback_init(&app->local, &deps->local_fs, &deps->pcm_sink,
                                 &deps->mp3_decoder);
        if (st != DEV_OK) return st;
    }

    st = input_manager_init(&app->input, &deps->button1, &deps->button2,
                            &deps->encoder_push, true,
                            APP_INPUT_DEBOUNCE_MS, APP_INPUT_LONG_PRESS_MS,
                            APP_ENCODER_POWER_HOLD_MS,
                            app_input_event, app);
    if (st != DEV_OK) return st;

    fm_state_reset(&app->fm, app->settings.value.fm_frequency_10khz);
    ui_state_init(&app->ui, &app->settings.value);
    app->ui.output = app->audio.output;
    app->ui.shuffle_enabled = app->settings.value.shuffle_enabled != 0U;
    app_update_effective_brightness(app);
    app_sync_fm_preset(app);

    bm83_set_packet_callback(deps->bluetooth, app_bm83_packet, app);
    /* BM83 boot/link events may arrive before the application callback is
     * registered. The transport driver tracks them independently, so seed the
     * UI/application state from that authoritative state rather than waiting
     * for another connection transition that may never occur. */
    app->ui.bluetooth_connected = deps->bluetooth->a2dp_connected;
    app->bt_avrcp_connected = deps->bluetooth->avrcp_connected;
    app->ui.bt_avrcp_connected = deps->bluetooth->avrcp_connected;
    app->bt_database_index = deps->bluetooth->database_index;

    if (deps->si4705_power_on_init) {
        st = si4705_hw_reset(deps->radio);
        if ((st != DEV_OK) && (st != DEV_ENOTSUP)) return st;
        st = si4705_power_up_analog(deps->radio, deps->si4705_power_func,
                                    deps->si4705_power_opmode);
        if (st != DEV_OK) return st;
        st = si4705_apply_properties(deps->radio, deps->si4705_properties,
                                     deps->si4705_property_count);
        if (st != DEV_OK) return st;
    }

    if (deps->display != NULL) {
        st = st7789_startup_sequence(deps->display);
        if (st != DEV_OK) ui_state_set_error(&app->ui, APP_ERROR_DISPLAY);
    }

    audio_source_t initial = AUDIO_SOURCE_LOCAL;
    if (app->settings.value.preferred_source <= SETTINGS_SOURCE_BLUETOOTH) {
        initial = (audio_source_t)(app->settings.value.preferred_source + 1U);
    }
    st = kiku_app_select_source(app, initial);
    if (st != DEV_OK) return st;
    if ((initial == AUDIO_SOURCE_LOCAL) && (app->local.file == NULL)) {
        app_open_relative_local_track(app, 0, true);
    }
    if ((initial == AUDIO_SOURCE_FM) && deps->si4705_power_on_init) {
        st = si4705_tune_frequency(deps->radio, app->settings.value.fm_frequency_10khz);
        if (st != DEV_OK) return st;
    }

    /* The display has completed its hardware startup by this point. Show a
     * short kiku-specific boot sequence without delaying peripheral servicing. */
    app->transition_return_screen = app->ui.screen;
    app->transition_started_ms = app_now(app);
    app->ui.animation_progress = 0U;
    app->ui.screen = UI_SCREEN_BOOT;

    app->initialised = true;
    return DEV_OK;
}

dev_status_t kiku_app_select_source(kiku_app_t *app, audio_source_t source)
{
    if ((app == NULL) || (source < AUDIO_SOURCE_LOCAL) || (source > AUDIO_SOURCE_BLUETOOTH)) return DEV_EINVAL;
    if (app->audio.source == AUDIO_SOURCE_BLUETOOTH && source != AUDIO_SOURCE_BLUETOOTH) {
        app_bt_control_queue_clear(app);
        if (app->bt_metadata_request_inflight) app->bt_metadata_discard_response = true;
    }
    if ((app->audio.source == AUDIO_SOURCE_LOCAL) && (source != AUDIO_SOURCE_LOCAL)) app->local.playing = false;
    if (source != AUDIO_SOURCE_FM) {
        app->fm_seek_active = false;
        app->ui.fm_seeking = false;
    }
    dev_status_t st = audio_router_set_source(&app->audio, source);
    if (st != DEV_OK) return st;
    /* route_switch() powers/configures the Si4705 before this point. Tune only
     * after that reset/power-up sequence so the remembered station survives. */
    if (source == AUDIO_SOURCE_FM && app->deps.radio != NULL) {
        app->fm_seek_active = false;
        app->ui.fm_seeking = false;
        st = si4705_tune_frequency(app->deps.radio,
                                   app->settings.value.fm_frequency_10khz);
        if (st != DEV_OK) return st;
        /* Manual/source changes can leave groups from the previous station in
         * the Si4705 FIFO. Clear them before accepting PI/PS/RadioText for the
         * newly selected frequency. */
        si4705_rds_group_t discarded;
        (void)si4705_read_rds_group(app->deps.radio, true, &discarded);
        fm_state_reset(&app->fm, app->settings.value.fm_frequency_10khz);
        app->ui.fm = app->fm;
        app->ui.fm_control_mode = UI_FM_CONTROL_TUNE;
        app_sync_fm_preset(app);
    }
    app->ui.source = source;
    app->settings.value.preferred_source = (uint8_t)(source - 1U);
    settings_mark_dirty(&app->settings, app_now(app));
    /* Reconnect only while Bluetooth is the user-selected source. This avoids
     * background link-back attempts disrupting FM/local playback, while a
     * selected Bluetooth source recovers from phone range/power cycles. */
    if (app->deps.bluetooth != NULL) {
        bm83_set_auto_reconnect(app->deps.bluetooth, source == AUDIO_SOURCE_BLUETOOTH);
    }
    if (source == AUDIO_SOURCE_FM) app->ui.screen = UI_SCREEN_FM;
    else if (source == AUDIO_SOURCE_BLUETOOTH) {
        app->bt_power_recovery_attempted = false;
        app->ui.screen = UI_SCREEN_BLUETOOTH;
        ui_state_set_track(&app->ui, "", "", "");
        if (app->bt_avrcp_connected && app->deps.bluetooth != NULL &&
                   app->bt_database_index <= 1U) {
            app->bt_browse_stage = BT_BROWSE_IDLE;
            app_bt_metadata_reset_transaction(app, app_now(app), true);
        }
    } else {
        app->ui.screen = UI_SCREEN_NOW_PLAYING;
        /* Selecting Music is a play action, including source restoration at
         * boot. A card arriving later is handled by the normal tick retry. */
        if (app->local.file == NULL) app_open_relative_local_track(app, 0, true);
        else if (!app->local.playing) (void)local_playback_set_playing(&app->local, true);
        if (app->local.file != NULL && app->local.path[0] != '\0') {
            app_set_local_title_from_path(app, app->local.path);
        }
    }
    return DEV_OK;
}

dev_status_t kiku_app_open_local(kiku_app_t *app, const char *path)
{
    if ((app == NULL) || (path == NULL) || (app->local.fs.open == NULL)) return DEV_ENOTSUP;
    dev_status_t st = local_playback_open(&app->local, path);
    if (st != DEV_OK) {
        ui_state_set_error(&app->ui, APP_ERROR_STORAGE);
        return st;
    }
    st = kiku_app_select_source(app, AUDIO_SOURCE_LOCAL);
    if (st != DEV_OK) return st;
    app_set_local_title_from_path(app, path);
    return local_playback_set_playing(&app->local, true);
}

dev_status_t kiku_app_tune_fm(kiku_app_t *app, uint16_t frequency_10khz)
{
    if ((app == NULL) || (app->deps.radio == NULL) ||
        (frequency_10khz < APP_FM_MIN_10KHZ) ||
        (frequency_10khz > APP_FM_MAX_10KHZ)) return DEV_EINVAL;
    /* Store the requested station before selecting FM. Source selection owns
     * radio power-up/reset and will tune this value afterwards. */
    app->settings.value.fm_frequency_10khz = frequency_10khz;
    settings_mark_dirty(&app->settings, app_now(app));
    app->fm_seek_active = false;
    app->ui.fm_seeking = false;
    if (app->audio.source != AUDIO_SOURCE_FM) {
        return kiku_app_select_source(app, AUDIO_SOURCE_FM);
    }

    dev_status_t st = si4705_tune_frequency(app->deps.radio, frequency_10khz);
    if (st != DEV_OK) {
        ui_state_set_error(&app->ui, APP_ERROR_RADIO);
        return st;
    }
    si4705_rds_group_t discarded;
    (void)si4705_read_rds_group(app->deps.radio, true, &discarded);
    fm_state_reset(&app->fm, frequency_10khz);
    app->ui.fm = app->fm;
    app_sync_fm_preset(app);
    return DEV_OK;
}

void kiku_app_encoder_delta(kiku_app_t *app, int16_t delta)
{
    if (app == NULL) return;
    input_manager_encoder_delta(&app->input, delta);
}

void kiku_app_bm83_rx(kiku_app_t *app, const uint8_t *data, size_t len)
{
    if ((app == NULL) || (app->deps.bluetooth == NULL)) return;
    bm83_rx_data(app->deps.bluetooth, data, len);
}

self_test_result_t kiku_app_run_self_test(kiku_app_t *app)
{
    if (app == NULL) return (self_test_result_t){0U, 0U};
    self_test_deps_t d = {
        .charger = app->deps.charger,
        .gauge = app->deps.gauge,
        .codec = app->deps.codec,
        .radio = app->deps.si4705_power_on_init ? app->deps.radio : NULL,
        .display = app->deps.display,
        .bluetooth = app->deps.bluetooth,
        .storage_ctx = app->deps.storage_probe_ctx,
        .storage_probe = app->deps.storage_probe
    };
    app->self_test = self_test_run_non_destructive(&d);
    return app->self_test;
}

static void app_service_bt_metadata_transport(kiku_app_t *app, uint32_t now)
{
    if (app == NULL || app->deps.bluetooth == NULL || !app->bt_avrcp_connected ||
        app->bt_database_index > 1U) return;

    /* Logical AVRCP metadata transactions complete on the actual 0x5D response,
     * not on the UART Command_ACK. Never issue another auxiliary metadata/
     * browsing command while one is still outstanding. This is the key guard
     * against outrunning the BM83's small AVRCP command pool. */
    if (app->bt_metadata_request_inflight) {
        if (app->bt_metadata_response_stalled) return;
        if ((uint32_t)(now - app->bt_metadata_request_started_ms) >=
            APP_BT_METADATA_RESPONSE_TIMEOUT_MS) {
            ++g_bt_metadata_diag.metadata_timeout_count;
            if (app->bt_metadata_command_accepted) {
                /* An ACKed 0x4A with no 0x5D is still owned by the BM83. There
                 * is no transaction token (and no documented 0x4A abort) that
                 * lets the host prove it was released. Keep this physical
                 * request outstanding until its late response or a real AVRCP
                 * profile boundary arrives. Playback controls remain independent
                 * and usable while background metadata is stalled. */
                app->bt_metadata_response_stalled = true;
                app_bt_metadata_sync_diag(app);
                return;
            }
            app_bt_metadata_end_request(app, now, false);
            /* The UART command was ACKed but the AVRCP response was not seen in
             * time. There is no transaction ID in 0x5D, so quarantine the next
             * request briefly and drain any late fragments before reusing the
             * same PDU/DB tuple. */
            app->bt_metadata_discard_response = true;
            const uint32_t failure_delay =
                APP_BT_METADATA_RETRY_BACKOFF_MS > APP_BT_METADATA_STALE_DRAIN_MS ?
                APP_BT_METADATA_RETRY_BACKOFF_MS : APP_BT_METADATA_STALE_DRAIN_MS;
            app->bt_metadata_next_due_ms = now + failure_delay;
        }
        return;
    }

    if (app->bt_metadata_discard_response) {
        if (!app_time_reached(now, app->bt_metadata_next_due_ms)) return;
        app->bt_metadata_discard_response = false;
        app_bt_metadata_sync_diag(app);
    }

    /* Honour BM83 Command_ACK 0x04/0x05 exactly as documented: after rejecting
     * a 0x4A request, BTM later emits 0x4A/status-0 when its resource is ready
     * and only then may the MCU resend. This driver-level gate also leaves music
     * controls free to use the UART while metadata waits. */
    if (bm83_command_waiting_ready(app->deps.bluetooth,
                                   BM83_CMD_AVRCP_VENDOR_DEPENDENT)) return;

    /* Human transport controls are retried before background metadata in
     * kiku_app_tick(). Do not immediately occupy the command channel again if a
     * queued action still needs to be sent. */
    if (app->bt_control_queue_count != 0U) return;

    /* First capture the remote link's negotiated AVRCP feature bits. This is
     * read-only and, on the v2.02 module, is the most direct way to tell whether
     * metadata/status support exists on the control link at all. */
    if (app->bt_avrcp_capability_pending) {
        dev_status_t st = bm83_read_remote_avrcp_features(app->deps.bluetooth,
                                                          app->bt_database_index);
        if (st == DEV_OK) app->bt_avrcp_capability_pending = false;
        return;
    }

    /* RegisterNotification is one-shot: a CHANGED response consumes the
     * registration, so re-register after every track transition. Its INTERIM
     * response also gives us the UID of the track already playing at connect. */
    if (app->bt_track_notification_pending) {
        dev_status_t st = bm83_register_track_changed(app->deps.bluetooth,
                                                       app->bt_database_index);
        if (st == DEV_OK) {
            app->bt_track_notification_pending = false;
            app->last_bt_browse_request_ms = now;
            /* Give the remote target time to return the one-shot INTERIM
             * notification before starting a separate metadata transaction. */
            app->bt_metadata_next_due_ms = now + APP_BT_METADATA_POST_CONTROL_MS;
        }
        return;
    }

    bool waiting = app->bt_browse_stage == BT_BROWSE_WAIT_PLAYER_LIST ||
                   app->bt_browse_stage == BT_BROWSE_WAIT_SET_ADDRESSED ||
                   app->bt_browse_stage == BT_BROWSE_WAIT_SET_BROWSED ||
                   app->bt_browse_stage == BT_BROWSE_WAIT_NOW_PLAYING ||
                   app->bt_browse_stage == BT_BROWSE_WAIT_TRACK_ATTRIBUTES;
    if (waiting && (uint32_t)(now - app->last_bt_browse_request_ms) >= APP_BT_BROWSE_TIMEOUT_MS) {
        /* Browsing is optional even after player discovery succeeds. A lost
         * SetBrowsedPlayer/GetItemAttributes response must not trap metadata
         * in an endless rediscovery loop. Fall back to the control channel. */
        app->bt_player_valid = false;
        app->bt_uid_counter_valid = false;
        app->bt_browse_stage = BT_BROWSE_IDLE;
        app->last_bt_metadata_ms = 0U;
    }

    dev_status_t st = DEV_EBUSY;
    uint8_t next_stage = app->bt_browse_stage;
    switch (app->bt_browse_stage) {
    case BT_BROWSE_NEED_PLAYER_LIST:
        st = bm83_request_media_player_list(app->deps.bluetooth, app->bt_database_index);
        next_stage = BT_BROWSE_WAIT_PLAYER_LIST;
        break;
    case BT_BROWSE_NEED_SET_ADDRESSED:
        st = bm83_set_addressed_player(app->deps.bluetooth, app->bt_database_index,
                                       app->bt_player_id);
        next_stage = BT_BROWSE_WAIT_SET_ADDRESSED;
        break;
    case BT_BROWSE_NEED_SET_BROWSED:
        st = bm83_set_browsed_player(app->deps.bluetooth, app->bt_database_index,
                                     app->bt_player_id);
        next_stage = BT_BROWSE_WAIT_SET_BROWSED;
        break;
    case BT_BROWSE_NEED_NOW_PLAYING:
        st = bm83_request_now_playing_browse(app->deps.bluetooth, app->bt_database_index);
        next_stage = BT_BROWSE_WAIT_NOW_PLAYING;
        break;
    case BT_BROWSE_NEED_TRACK_ATTRIBUTES:
        if (!app->bt_track_uid_valid || !app->bt_uid_counter_valid || !app->bt_player_valid) {
            app->bt_browse_stage = BT_BROWSE_IDLE;
            break;
        }
        st = bm83_request_item_attributes(app->deps.bluetooth, app->bt_database_index,
                                          0x03U, app->bt_track_uid,
                                          app->bt_uid_counter);
        next_stage = BT_BROWSE_WAIT_TRACK_ATTRIBUTES;
        break;
    case BT_BROWSE_IDLE: {
        /* The production BM83 reports Audio UART v2.02, but the flashed
         * multi-speaker project implements command 0x4A and has repeatedly
         * returned real title/artist/album data through it on the live board.
         * Conversely, its nominally older 0x0B GetElementAttributes path ACKs
         * requests but never returns PDU 0x20. Treat the observed project
         * capability as authoritative instead of version-gating this command. */
        if (!app_time_reached(now, app->bt_metadata_next_due_ms)) return;

        const uint32_t core_interval = app_bt_metadata_core_interval(app);
        const bool core_due = app->bt_metadata_core_next_attribute != 0U ||
                              app->bt_metadata_force_core ||
                              app->last_bt_metadata_ms == 0U ||
                              (uint32_t)(now - app->last_bt_metadata_ms) >= core_interval;
        if (core_due) {
            if (app->bt_metadata_core_next_attribute == 0U) {
                app_bt_metadata_reset_stage(app);
                app->bt_metadata_core_next_attribute = 1U;
            }
            const uint8_t attribute = app->bt_metadata_core_next_attribute;
            st = bm83_request_current_metadata_attribute_v206(app->deps.bluetooth,
                                                              app->bt_database_index,
                                                              attribute);
            if (st == DEV_OK) {
                app->bt_metadata_request_inflight = true;
                app->bt_metadata_request_kind = BT_METADATA_REQ_CORE;
                app->bt_metadata_requested_attribute = attribute;
                app->bt_metadata_command_accepted = false;
                app->bt_metadata_response_stalled = false;
                app->bt_metadata_request_started_ms = now;
                app->bt_metadata_discard_response = false;
                ++g_bt_metadata_diag.metadata_request_count;
                app_bt_metadata_sync_diag(app);
            } else if (st != DEV_EBUSY) {
                app->bt_metadata_next_due_ms = now + APP_BT_METADATA_RETRY_BACKOFF_MS;
            }
            return;
        }

        /* Title/artist/album freshness is the production correctness path. Do
         * not start independent detail queries between core polls: on the live
         * v2.02 project those extra outstanding AVRCP requests were the traffic
         * most likely to provoke BTM Memory Full. Details remain supported by
         * direct/optional browsing responses, but can never delay core identity. */
        app->bt_metadata_next_due_ms = app->last_bt_metadata_ms + core_interval;
        return;
    }
    default:
        return;
    }

    if (st == DEV_OK) {
        app->bt_browse_stage = next_stage;
        app->last_bt_browse_request_ms = now;
    }
}

dev_status_t kiku_app_tick(kiku_app_t *app)
{
    if ((app == NULL) || !app->initialised) return DEV_ENOTREADY;
    uint32_t now = app_now(app);
    /* The transport's Read_Link_Status snapshot is authoritative and can
     * recover from missed asynchronous BTM_Status events. Mirror both profile
     * states into the application/UI every tick; otherwise the driver can know
     * A2DP is up while the Bluetooth screen remains permanently "Not connected". */
    if (app->deps.bluetooth != NULL) {
        app->ui.bluetooth_connected = app->deps.bluetooth->a2dp_connected;
        if (app->deps.bluetooth->a2dp_connected) {
            app->bt_power_recovery_attempted = false;
        }
        if (app->deps.bluetooth->avrcp_connected && !app->bt_avrcp_connected) {
            app_mark_avrcp_available(app, app->deps.bluetooth->database_index);
        } else if (!app->deps.bluetooth->avrcp_connected && app->bt_avrcp_connected) {
            app->bt_avrcp_connected = false;
            app->ui.bt_avrcp_connected = false;
            app->bt_player_valid = false;
            app->bt_uid_counter_valid = false;
            app->bt_track_uid_valid = false;
            app->bt_avrcp_capability_pending = false;
            app->bt_track_notification_pending = false;
            app->bt_browse_stage = BT_BROWSE_IDLE;
            app_bt_metadata_reset_transaction(app, now, false);
        }
    }
    g_bt_metadata_diag.player_id = app->bt_player_id;
    g_bt_metadata_diag.uid_counter = app->bt_uid_counter;
    g_bt_metadata_diag.browse_stage = app->bt_browse_stage;
    g_bt_metadata_diag.database_index = app->bt_database_index;
    g_bt_metadata_diag.uid_counter_valid = app->bt_uid_counter_valid ? 1U : 0U;
    g_bt_metadata_diag.track_uid_valid = app->bt_track_uid_valid ? 1U : 0U;
    g_bt_metadata_diag.player_valid = app->bt_player_valid ? 1U : 0U;
    g_bt_metadata_diag.avrcp_connected = app->bt_avrcp_connected ? 1U : 0U;

    input_manager_poll(&app->input, now);
    /* Input callbacks are allowed to perform hardware power sequencing. Waking
     * the ST7789 blocks for its required SLPOUT settle interval before
     * app_begin_power_on() stamps transition_started_ms. Re-sample the clock
     * after input dispatch so the first POWERING_ON service pass can never see
     * a timestamp older than the transition it is servicing (unsigned
     * subtraction would otherwise look like a multi-day elapsed interval and
     * complete the animation immediately). */
    now = app_now(app);
    app_service_ui_transition(app, now);
    if (app->soft_powered_off) return DEV_OK;
    dev_status_t st = audio_router_poll_headphone(&app->audio);
    if (st != DEV_OK) ui_state_set_error(&app->ui, APP_ERROR_AUDIO_ROUTE);
    app->ui.headphones_present = app->audio.headphones_present;
    app->ui.playing = (app->audio.source == AUDIO_SOURCE_LOCAL) ? app->local.playing : false;
    app->ui.bt_avrcp_connected = app->bt_avrcp_connected;

    st = power_manager_poll(&app->power, now);
    if ((st != DEV_OK) && (st != DEV_EBUSY)) ui_state_set_error(&app->ui, APP_ERROR_POWER);
    ui_state_sync_power(&app->ui, &app->power);

    /* A card can be inserted after boot. Once the first track appears while
     * Local is selected, start it immediately instead of silently opening track
     * one in a paused state. */
    if ((app->audio.source == AUDIO_SOURCE_LOCAL) && app->local.file == NULL &&
        app->deps.local_track_path != NULL) {
        app_open_relative_local_track(app, 0, true);
    }
    if ((app->audio.source == AUDIO_SOURCE_LOCAL) && app->local.playing) {
        st = local_playback_process(&app->local);
        if ((st != DEV_OK) && (st != DEV_EBUSY)) {
            ui_state_set_error(&app->ui, APP_ERROR_STORAGE);
            /* A removed or failing card invalidates FatFs' open object. Close
             * the local player immediately rather than retrying the stale FIL
             * every 10 ms. The platform storage service will remount/rescan a
             * reinserted card and the normal local-source path can then open a
             * fresh track cleanly. */
            local_playback_close(&app->local);
        }
        else if ((st == DEV_OK) && app->local.eof) {
            /* A portable player should continue through the library without
             * requiring a button press at every track boundary. */
            app_open_relative_local_track(app, +1, true);
        }
    }

    app->ui.playing = (app->audio.source == AUDIO_SOURCE_LOCAL) ? app->local.playing : false;

    if ((app->audio.source == AUDIO_SOURCE_FM) && (app->deps.radio != NULL)) {
        if ((uint32_t)(now - app->last_fm_status_ms) >= APP_FM_STATUS_POLL_MS) {
            /* AN332 requires GET_INT_STATUS after FM_SEEK_START; STCINT is not
             * guaranteed to assert merely by polling FM_TUNE_STATUS. The old
             * code skipped that command, which made seek completion dependent
             * on incidental tuner behaviour. */
            uint8_t interrupt_status = 0U;
            st = si4705_get_interrupt_status(app->deps.radio, &interrupt_status);
            bool seek_complete = st == DEV_OK &&
                                 (interrupt_status & SI4705_STATUS_STCINT) != 0U;
            bool seek_timed_out = app->fm_seek_active &&
                                  (uint32_t)(now - app->fm_seek_started_ms) >= APP_FM_SEEK_TIMEOUT_MS;

            if (st == DEV_OK && (!app->fm_seek_active || seek_complete || seek_timed_out)) {
                si4705_tune_status_t tune;
                st = si4705_get_tune_status(app->deps.radio, seek_timed_out, &tune);
                if (st == DEV_OK) {
                    bool was_seeking = app->fm_seek_active;
                    app->fm_seek_active = false;
                    app->ui.fm_seeking = false;

                    if (!was_seeking || tune.valid) {
                        uint16_t previous_frequency = app->fm.frequency_10khz;
                        fm_state_apply_tune_status(&app->fm, &tune);
                        if (tune.valid && tune.frequency_10khz != 0U &&
                            tune.frequency_10khz != previous_frequency) {
                            /* A seek completed on a new station. Persist the actual
                             * station and discard RDS groups queued for the old one. */
                            app->settings.value.fm_frequency_10khz = tune.frequency_10khz;
                            settings_mark_dirty(&app->settings, now);
                            si4705_rds_group_t discarded;
                            (void)si4705_read_rds_group(app->deps.radio, true, &discarded);
                            /* A newly completed seek/tune is a different station. Clear
                             * all assembled PS/RadioText immediately so stale metadata
                             * never flashes while the new RDS stream acquires. */
                            fm_state_reset(&app->fm, tune.frequency_10khz);
                        }
                    } else {
                        /* If no valid station is found, return to the remembered
                         * frequency instead of leaving the radio parked at a band
                         * edge/noise channel. */
                        (void)si4705_tune_frequency(app->deps.radio,
                                                   app->settings.value.fm_frequency_10khz);
                        fm_state_reset(&app->fm, app->settings.value.fm_frequency_10khz);
                    }
                    app_sync_fm_preset(app);
                }
            }

            si4705_signal_quality_t quality;
            if (si4705_get_signal_quality(app->deps.radio, &quality) == DEV_OK) {
                fm_state_apply_signal_quality(&app->fm, &quality);
            }
            app->ui.fm = app->fm;
            app->last_fm_status_ms = now;
        }
        if (!app->fm_seek_active &&
            (uint32_t)(now - app->last_rds_poll_ms) >= APP_FM_RDS_POLL_MS) {
            /* Drain a short bounded burst. Full-screen SPI rendering can delay
             * this foreground task long enough for several groups to queue;
             * one-read-per-poll unnecessarily increased acquisition time and
             * risked FIFO overflow on text-heavy stations. */
            for (unsigned group_index = 0U; group_index < 8U; ++group_index) {
                si4705_rds_group_t group;
                st = si4705_read_rds_group(app->deps.radio, false, &group);
                if (st != DEV_OK) break;
                fm_state_process_rds(&app->fm, &group);
                if (!group.fifo_used) break;
            }
            app->ui.fm = app->fm;
            app->last_rds_poll_ms = now;
        }
    }

    st = bm83_service(app->deps.bluetooth);
    if ((st != DEV_OK) && (st != DEV_EBUSY)) {
        ui_state_set_error(&app->ui, APP_ERROR_BLUETOOTH);
    }

    if (app->audio.source == AUDIO_SOURCE_BLUETOOTH) {
        /* Physical transport controls have priority over background metadata.
         * A queued play/pause/skip action is retried as soon as the BM83 command
         * channel is free, before a new metadata query can occupy it again. */
        app_service_bt_control_queue(app);
        dev_status_t reconnect_st = bm83_service_connection(app->deps.bluetooth);
        if (reconnect_st != DEV_OK && reconnect_st != DEV_EBUSY) {
            ui_state_set_error(&app->ui, APP_ERROR_BLUETOOTH);
        }
        /* Link-back can be ACKed indefinitely while the BM83 never establishes
         * A2DP. Once the driver has exhausted its bounded reconnect episode,
         * perform one full module power cycle through the platform. This is the
         * same recovery that live hardware proved by switching FM -> Bluetooth,
         * but it keeps the user's selected source and UI unchanged. One attempt
         * per disconnected episode prevents a failed module from power-cycling
         * forever; a real A2DP connection or a later source re-selection re-arms
         * the recovery. */
        if (!app->deps.bluetooth->a2dp_connected &&
            app->deps.bluetooth->reconnect_exhausted &&
            !app->bt_power_recovery_attempted &&
            app->deps.bluetooth_recover != NULL) {
            app->bt_power_recovery_attempted = true;
            reconnect_st = app->deps.bluetooth_recover(app->deps.bluetooth_recover_ctx);
            if (reconnect_st != DEV_OK && reconnect_st != DEV_EBUSY) {
                ui_state_set_error(&app->ui, APP_ERROR_BLUETOOTH);
            }
        }
        app_service_bt_metadata_transport(app, now);
    }

    /* Settings live on the same removable SD volume as local audio. FatFs
     * persistence performs sync-backed writes to both redundant records, which
     * can hold the foreground task long enough to drain the audio FIFO on a
     * slow card. Preserve the dirty record while local playback is active and
     * commit it as soon as playback is paused or another source is selected. */
    if (app->audio.source != AUDIO_SOURCE_LOCAL || !app->local.playing) {
        st = settings_save_if_due(&app->settings, now, APP_SETTINGS_SAVE_DELAY_MS);
        if ((st != DEV_OK) && (st != DEV_EBUSY) && (st != DEV_ENOTSUP)) return st;
    }
    return DEV_OK;
}
