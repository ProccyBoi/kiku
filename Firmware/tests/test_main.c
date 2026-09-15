#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bm83.h"
#include "bt_rx_watchdog.h"
#include "bq25895.h"
#include "fm_state.h"
#include "input_manager.h"
#include "local_playback.h"
#include "settings.h"
#include "ui_renderer.h"

/* Exercise the actual application metadata scheduler and response parser. */
#include "../App/Src/app.c"

#define UI_TEST_WIDTH 320U
#define UI_TEST_HEIGHT 240U

static uint16_t s_ui_pixels[UI_TEST_WIDTH * UI_TEST_HEIGHT];
static size_t s_ui_pixel_count;
static uint16_t s_ui_window_x0;
static uint16_t s_ui_window_y0;
static uint16_t s_ui_window_x1;
static uint16_t s_ui_window_y1;
static size_t s_ui_window_cursor;

dev_status_t st7789_startup_sequence(st7789_t *lcd)
{
    (void)lcd;
    return DEV_ENOTSUP;
}

dev_status_t st7789_command(st7789_t *lcd, uint8_t command,
                           const uint8_t *data, size_t len)
{
    (void)lcd; (void)command; (void)data; (void)len;
    return DEV_ENOTSUP;
}

dev_status_t st7789_set_window(st7789_t *lcd, uint16_t x0, uint16_t y0,
                               uint16_t x1, uint16_t y1)
{
    assert(lcd != NULL);
    assert(x0 <= x1 && y0 <= y1);
    assert(x1 < lcd->width && y1 < lcd->height);
    s_ui_window_x0 = x0;
    s_ui_window_y0 = y0;
    s_ui_window_x1 = x1;
    s_ui_window_y1 = y1;
    s_ui_window_cursor = 0U;
    s_ui_pixel_count = 0U;
    return DEV_OK;
}

dev_status_t st7789_write_pixels_rgb565(st7789_t *lcd,
                                        const uint16_t *pixels, size_t count)
{
    assert(lcd != NULL && pixels != NULL);
    size_t width = (size_t)s_ui_window_x1 - s_ui_window_x0 + 1U;
    size_t height = (size_t)s_ui_window_y1 - s_ui_window_y0 + 1U;
    assert(s_ui_window_cursor + count <= width * height);
    for (size_t i = 0U; i < count; ++i) {
        size_t pos = s_ui_window_cursor + i;
        size_t x = (size_t)s_ui_window_x0 + (pos % width);
        size_t y = (size_t)s_ui_window_y0 + (pos / width);
        assert(x < lcd->width && y < lcd->height);
        s_ui_pixels[y * UI_TEST_WIDTH + x] = pixels[i];
    }
    s_ui_window_cursor += count;
    s_ui_pixel_count += count;
    return DEV_OK;
}

static uint32_t ui_frame_hash(void)
{
    uint32_t h = 2166136261UL;
    for (size_t i = 0U; i < sizeof(s_ui_pixels) / sizeof(s_ui_pixels[0]); ++i) {
        h ^= s_ui_pixels[i];
        h *= 16777619UL;
    }
    return h;
}

static uint32_t ui_structure_hash(void)
{
    /* Structural signature independent of palette: (0,0) is always untouched
     * theme paper, so hash only whether each rendered pixel differs from it. */
    uint16_t paper = s_ui_pixels[0];
    uint32_t h = 2166136261UL;
    for (size_t i = 0U; i < sizeof(s_ui_pixels) / sizeof(s_ui_pixels[0]); ++i) {
        h ^= s_ui_pixels[i] == paper ? 0U : 1U;
        h *= 16777619UL;
    }
    return h;
}

typedef struct {
    bool b1_level;
    bool b2_level;
    bool enc_level;
    unsigned encoder_long_events;
    unsigned encoder_press_events;
    unsigned b1_events;
    unsigned b2_events;
    unsigned b1_long_events;
    unsigned b2_long_events;
} input_mock_t;

static bool input_gpio_read(void *ctx)
{
    return *(bool *)ctx;
}

static void input_capture(void *ctx, const input_event_t *event)
{
    input_mock_t *m = ctx;
    if (event->type == INPUT_EVENT_ENCODER_LONG) ++m->encoder_long_events;
    else if (event->type == INPUT_EVENT_ENCODER_PRESS) ++m->encoder_press_events;
    else if (event->type == INPUT_EVENT_BUTTON_1) ++m->b1_events;
    else if (event->type == INPUT_EVENT_BUTTON_2) ++m->b2_events;
    else if (event->type == INPUT_EVENT_BUTTON_1_LONG) ++m->b1_long_events;
    else if (event->type == INPUT_EVENT_BUTTON_2_LONG) ++m->b2_long_events;
}

static void test_input_encoder_long_power_gesture(void)
{
    input_mock_t m = { .b1_level = true, .b2_level = true, .enc_level = true };
    dev_gpio_t b1 = { .ctx = &m.b1_level, .read = input_gpio_read };
    dev_gpio_t b2 = { .ctx = &m.b2_level, .read = input_gpio_read };
    dev_gpio_t enc = { .ctx = &m.enc_level, .read = input_gpio_read };
    input_manager_t mgr;
    assert(input_manager_init(&mgr, &b1, &b2, &enc, true, 25U, 900U, 4000U,
                              input_capture, &m) == DEV_OK);

    input_manager_poll(&mgr, 0U);

    /* Power is exclusively a deliberate four-second encoder hold. It fires
     * once while held and suppresses the release-time short/back press. */
    m.enc_level = false;
    input_manager_poll(&mgr, 10U);
    input_manager_poll(&mgr, 40U);
    input_manager_poll(&mgr, 4039U);
    assert(m.encoder_long_events == 0U);
    input_manager_poll(&mgr, 4040U);
    assert(m.encoder_long_events == 1U);
    input_manager_poll(&mgr, 4200U);
    assert(m.encoder_long_events == 1U);
    m.enc_level = true;
    input_manager_poll(&mgr, 4210U);
    input_manager_poll(&mgr, 4240U);
    assert(m.encoder_press_events == 0U);

    /* A normal encoder click is still emitted promptly on release; users do not
     * need to wait for the four-second power threshold to navigate back. */
    m.enc_level = false;
    input_manager_poll(&mgr, 4260U);
    input_manager_poll(&mgr, 4290U);
    m.enc_level = true;
    input_manager_poll(&mgr, 4390U);
    input_manager_poll(&mgr, 4420U);
    assert(m.encoder_press_events == 1U);

    /* Pressing both front buttons no longer has any hidden power meaning. */
    m.b1_level = false; m.b2_level = false;
    input_manager_poll(&mgr, 4500U);
    input_manager_poll(&mgr, 4530U);
    m.b1_level = true; m.b2_level = true;
    input_manager_poll(&mgr, 4550U);
    input_manager_poll(&mgr, 4580U);
    assert(m.encoder_long_events == 1U);
    assert(m.b1_events == 1U && m.b2_events == 1U);

    /* A normal isolated B1 press remains a normal short press afterwards. */
    m.b1_level = false;
    input_manager_poll(&mgr, 4650U);
    input_manager_poll(&mgr, 4680U);
    m.b1_level = true;
    input_manager_poll(&mgr, 4700U);
    input_manager_poll(&mgr, 4730U);
    assert(m.b1_events == 2U);

    /* B1 is the dedicated long-hold Back gesture. It fires once and suppresses
     * the release-time B1 short event. */
    m.b1_level = false;
    input_manager_poll(&mgr, 5800U);
    input_manager_poll(&mgr, 5830U);
    input_manager_poll(&mgr, 6729U);
    assert(m.b1_long_events == 0U);
    input_manager_poll(&mgr, 6730U);
    assert(m.b1_long_events == 1U);
    m.b1_level = true;
    input_manager_poll(&mgr, 6750U);
    input_manager_poll(&mgr, 6780U);
    assert(m.b1_events == 2U);

    /* B2 keeps its contextual long action and also suppresses its short event
     * when the long threshold was reached. */
    m.b2_level = false;
    input_manager_poll(&mgr, 6800U);
    input_manager_poll(&mgr, 6830U);
    input_manager_poll(&mgr, 7730U);
    assert(m.b2_long_events == 1U);
    m.b2_level = true;
    input_manager_poll(&mgr, 7750U);
    input_manager_poll(&mgr, 7780U);
    assert(m.b2_events == 1U);
}

typedef struct {
    uint8_t frame[512];
    size_t frame_len;
    unsigned writes;
    bool mfb;
    unsigned mfb_highs;
    unsigned mfb_lows;
    uint32_t delayed_ms;
    uint32_t now_ms;
    bool reset_n;
    unsigned reset_lows;
    unsigned reset_highs;
} bm_mock_t;

static dev_status_t mock_uart_write(void *ctx, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    bm_mock_t *m = ctx;
    (void)timeout_ms;
    assert(len <= sizeof(m->frame));
    memcpy(m->frame, data, len);
    m->frame_len = len;
    ++m->writes;
    return DEV_OK;
}

static void mock_gpio_write(void *ctx, bool level)
{
    bm_mock_t *m = ctx;
    m->mfb = level;
    if (level) ++m->mfb_highs; else ++m->mfb_lows;
}

static void mock_delay(void *ctx, uint32_t ms)
{
    bm_mock_t *m = ctx;
    m->delayed_ms += ms;
    m->now_ms += ms;
}

static uint32_t mock_time(void *ctx)
{
    return ((bm_mock_t *)ctx)->now_ms;
}

static void mock_reset_write(void *ctx, bool level)
{
    bm_mock_t *m = ctx;
    m->reset_n = level;
    if (level) ++m->reset_highs; else ++m->reset_lows;
}

typedef struct {
    unsigned calls;
    bm83_packet_t packet;
} packet_capture_t;

static void packet_cb(void *ctx, const bm83_packet_t *packet)
{
    packet_capture_t *cap = ctx;
    ++cap->calls;
    cap->packet = *packet;
}

static void make_v206_text_response(bm83_packet_t *packet, bool end_of_body,
                                    const uint8_t *attributes,
                                    const char *const *values, size_t count)
{
    assert(packet != NULL && attributes != NULL && values != NULL && count <= 7U);
    memset(packet, 0, sizeof(*packet));
    packet->id = BM83_EVENT_AVRCP_VENDOR_DEPENDENT_RSP;
    packet->payload[0] = 0x20U;
    packet->payload[1] = 0x00U;
    packet->payload[2] = 0x04U;
    packet->payload[3] = end_of_body ? 1U : 0U;
    packet->payload[4] = (uint8_t)count;

    size_t pos = 7U;
    for (size_t i = 0U; i < count; ++i) {
        size_t len = strlen(values[i]);
        assert(pos + 8U + len <= sizeof(packet->payload));
        packet->payload[pos + 0U] = 0U;
        packet->payload[pos + 1U] = 0U;
        packet->payload[pos + 2U] = 0U;
        packet->payload[pos + 3U] = attributes[i];
        packet->payload[pos + 4U] = 0U;
        packet->payload[pos + 5U] = 0x6AU;
        packet->payload[pos + 6U] = (uint8_t)(len >> 8);
        packet->payload[pos + 7U] = (uint8_t)len;
        memcpy(&packet->payload[pos + 8U], values[i], len);
        pos += 8U + len;
    }
    size_t list_len = pos - 7U;
    packet->payload[5] = (uint8_t)(list_len >> 8);
    packet->payload[6] = (uint8_t)list_len;
    packet->payload_len = (uint16_t)pos;
}

static void test_app_metadata_fallback(void)
{
    bm_mock_t m = {0};
    dev_uart_bus_t uart = { .ctx = &m, .write = mock_uart_write };
    dev_clock_t clock = { .ctx = &m, .time_ms = mock_time };
    bm83_t dev;
    assert(bm83_init(&dev, &uart, NULL, NULL, NULL, &clock) == DEV_OK);
    kiku_app_t app = {0};
    app.deps.bluetooth = &dev;
    settings_defaults(&app.settings.value);
    ui_state_init(&app.ui, &app.settings.value);
    /* An unknown/2.02 module must never enter optional browsing, but the
     * production BM83 project is live-proven to accept the 0x4A metadata path
     * despite reporting UART v2.02. */
    app_mark_avrcp_available(&app, 0U);
    assert(app.bt_browse_stage == BT_BROWSE_IDLE);
    app.bt_avrcp_capability_pending = false;
    app.bt_track_notification_pending = false;
    app_service_bt_metadata_transport(&app, 10000U);
    assert(m.frame[3] == BM83_CMD_AVRCP_VENDOR_DEPENDENT);
    assert(m.frame[4] == 0x00U);
    assert(m.frame[5] == 0x20U);
    assert(m.frame[6] == 0x01U);
    assert(m.frame[10] == 1U);
    /* This test only verifies the fallback frame here; complete the synthetic
     * request before moving on to independent parser/browsing cases. */
    app_bt_metadata_end_request(&app, 10000U, true);
    dev.awaiting_command_ack = false;

    /* Parse real legacy response framing and retain separately polled fields. */
    bm83_packet_t packet = { .id = BM83_EVENT_AVC_VENDOR_DEPENDENT_RESPONSE };
    const uint8_t title[] = {0,0x0C,0x48,0,0,0x19,0x58,0x20,0,0,13,
                             1,0,0,0,1,0,0x6A,0,4,'S','o','n','g'};
    memcpy(packet.payload, title, sizeof(title));
    packet.payload_len = sizeof(title);
    app_parse_bm83_metadata(&app, &packet);
    assert(strcmp(app.ui.track_title, "Song") == 0);
    packet.payload[15] = 2U;
    memcpy(&packet.payload[20], "Band", 4U);
    app_parse_bm83_metadata(&app, &packet);
    assert(strcmp(app.ui.track_title, "Song") == 0);
    assert(strcmp(app.ui.track_artist, "Band") == 0);

    /* Even a discovered player can lose browsing. Every wait must fall back. */
    const uint8_t waits[] = { BT_BROWSE_WAIT_PLAYER_LIST, BT_BROWSE_WAIT_SET_ADDRESSED,
        BT_BROWSE_WAIT_SET_BROWSED, BT_BROWSE_WAIT_NOW_PLAYING,
        BT_BROWSE_WAIT_TRACK_ATTRIBUTES };
    dev.uart_version_valid = true;
    dev.uart_version_major = 2U; dev.uart_version_minor = 6U;
    for (size_t i = 0; i < sizeof(waits); ++i) {
        dev.awaiting_command_ack = false;
        app_bt_metadata_reset_transaction(&app, 20000U, false);
        app.bt_browse_stage = waits[i];
        app.bt_player_id = 7U;
        app.bt_player_valid = true;
        app.last_bt_browse_request_ms = 0U;
        app_service_bt_metadata_transport(&app, 20000U);
        assert(app.bt_browse_stage == BT_BROWSE_IDLE);
        assert(!app.bt_player_valid);
        assert(m.frame[3] == BM83_CMD_AVRCP_VENDOR_DEPENDENT);
        /* This loop is testing independent browsing timeout states, not an
         * intentionally abandoned 0x4A. Finish the synthetic fallback request
         * before constructing the next case so stale-drain state cannot leak
         * between otherwise unrelated fixtures. */
        if (app.bt_metadata_request_inflight) {
            app_bt_metadata_end_request(&app, 20000U, true);
        }
        app.bt_metadata_discard_response = false;
    }
}

static void test_app_metadata_response_scheduler(void)
{
    bm_mock_t m = { .now_ms = 1000U };
    dev_uart_bus_t uart = { .ctx = &m, .write = mock_uart_write };
    dev_clock_t clock = { .ctx = &m, .time_ms = mock_time };
    bm83_t dev;
    assert(bm83_init(&dev, &uart, NULL, NULL, NULL, &clock) == DEV_OK);

    kiku_app_t app = {0};
    app.deps.bluetooth = &dev;
    app.deps.clock = clock;
    app.audio.source = AUDIO_SOURCE_BLUETOOTH;
    settings_defaults(&app.settings.value);
    ui_state_init(&app.ui, &app.settings.value);
    ui_state_set_track(&app.ui, "Old title", "Old artist", "Old album");
    bm83_set_packet_callback(&dev, app_bm83_packet, &app);

    uint32_t requests_before = g_bt_metadata_diag.metadata_request_count;
    uint32_t completes_before = g_bt_metadata_diag.metadata_core_complete_count;
    uint32_t changes_before = g_bt_metadata_diag.metadata_core_change_count;
    uint32_t timeouts_before = g_bt_metadata_diag.metadata_timeout_count;

    app_mark_avrcp_available(&app, 0U);
    app.bt_avrcp_capability_pending = false;
    app.bt_track_notification_pending = false;
    app_service_bt_metadata_transport(&app, m.now_ms);
    assert(app.bt_metadata_request_inflight);
    assert(app.bt_metadata_request_kind == BT_METADATA_REQ_CORE);
    assert(m.frame[3] == BM83_CMD_AVRCP_VENDOR_DEPENDENT);
    assert(m.frame[6] == 0x01U && m.frame[10] == 0x01U);
    assert(g_bt_metadata_diag.metadata_request_count == requests_before + 1U);

    /* A Command_ACK releases the UART slot, but not the logical metadata
     * transaction. No second 0x4A may be issued before the actual 0x5D body. */
    const uint8_t metadata_ack[] = {0xAA,0x00,0x03,0x00,0x4A,0x00,0xB3};
    bm83_rx_data(&dev, metadata_ack, sizeof(metadata_ack));
    unsigned writes_after_ack = m.writes;
    m.now_ms += 100U;
    app_service_bt_metadata_transport(&app, m.now_ms);
    assert(app.bt_metadata_request_inflight);
    assert(m.writes == writes_after_ack);

    /* Each field-proven 0x4A physical transaction requests exactly one identity
     * attribute. The logical title/artist/album cycle stages all three and only
     * commits after the album response, so the UI can never cross-mix tracks. */
    bm83_packet_t fragment;
    const uint8_t title_attr[] = {1U};
    const char *const title_value[] = {"Song"};
    make_v206_text_response(&fragment, true, title_attr, title_value, 1U);
    app_parse_bm83_metadata_v206(&app, &fragment);
    assert(strcmp(app.ui.track_title, "Old title") == 0);
    assert(strcmp(app.ui.track_artist, "Old artist") == 0);
    assert(strcmp(app.ui.track_album, "Old album") == 0);
    assert(!app.bt_metadata_request_inflight);
    assert(app.bt_metadata_core_next_attribute == 2U);
    assert(app.bt_metadata_stage_seen_mask == 0x01U);

    m.now_ms = app.bt_metadata_next_due_ms;
    app_service_bt_metadata_transport(&app, m.now_ms);
    assert(app.bt_metadata_request_inflight);
    assert(m.frame[6] == 0x01U && m.frame[10] == 0x02U);
    bm83_rx_data(&dev, metadata_ack, sizeof(metadata_ack));
    const uint8_t artist_attr[] = {2U};
    const char *const artist_value[] = {"Band"};
    make_v206_text_response(&fragment, true, artist_attr, artist_value, 1U);
    app_parse_bm83_metadata_v206(&app, &fragment);
    assert(!app.bt_metadata_request_inflight);
    assert(app.bt_metadata_core_next_attribute == 3U);
    assert(app.bt_metadata_stage_seen_mask == 0x03U);
    assert(strcmp(app.ui.track_title, "Old title") == 0);
    assert(strcmp(app.ui.track_artist, "Old artist") == 0);
    assert(strcmp(app.ui.track_album, "Old album") == 0);

    m.now_ms = app.bt_metadata_next_due_ms;
    app_service_bt_metadata_transport(&app, m.now_ms);
    assert(app.bt_metadata_request_inflight);
    assert(m.frame[6] == 0x01U && m.frame[10] == 0x03U);
    bm83_rx_data(&dev, metadata_ack, sizeof(metadata_ack));
    const uint8_t album_attr[] = {3U};
    const char *const album_value[] = {"Album"};
    make_v206_text_response(&fragment, true, album_attr, album_value, 1U);
    app_parse_bm83_metadata_v206(&app, &fragment);
    assert(strcmp(app.ui.track_title, "Song") == 0);
    assert(strcmp(app.ui.track_artist, "Band") == 0);
    assert(strcmp(app.ui.track_album, "Album") == 0);
    assert(!app.bt_metadata_request_inflight);
    assert(app.bt_metadata_core_next_attribute == 0U);
    assert(!app.bt_metadata_force_core);
    assert(g_bt_metadata_diag.metadata_core_complete_count == completes_before + 1U);
    assert(g_bt_metadata_diag.metadata_core_change_count == changes_before + 1U);

    /* Notifications are accelerators, not correctness dependencies. With no
     * notification at all, the periodic core deadline must issue another
     * atomic identity cycle. */
    m.now_ms += APP_BT_METADATA_CORE_POLL_MS - 1U;
    unsigned writes_before_periodic = m.writes;
    app_service_bt_metadata_transport(&app, m.now_ms);
    assert(m.writes == writes_before_periodic);
    ++m.now_ms;
    app_service_bt_metadata_transport(&app, m.now_ms);
    assert(app.bt_metadata_request_inflight);
    assert(m.frame[3] == BM83_CMD_AVRCP_VENDOR_DEPENDENT &&
           m.frame[6] == 0x01U && m.frame[10] == 0x01U);

    /* A Command_ACK means BTM owns the physical AVRCP transaction. If its 0x5D
     * goes missing, never compound the fault by submitting another 0x4A. Keep
     * the accepted transaction stalled until its late response or a real profile
     * boundary proves the old transaction can no longer complete. */
    bm83_rx_data(&dev, metadata_ack, sizeof(metadata_ack));
    const uint32_t timed_request_started = app.bt_metadata_request_started_ms;
    m.now_ms = timed_request_started + APP_BT_METADATA_RESPONSE_TIMEOUT_MS;
    app_service_bt_metadata_transport(&app, m.now_ms);
    assert(app.bt_metadata_request_inflight);
    assert(app.bt_metadata_command_accepted);
    assert(app.bt_metadata_response_stalled);
    assert(g_bt_metadata_diag.metadata_timeout_count == timeouts_before + 1U);
    unsigned writes_after_timeout = m.writes;
    m.now_ms += APP_BT_METADATA_RETRY_BACKOFF_MS * 4U;
    app_service_bt_metadata_transport(&app, m.now_ms);
    assert(m.writes == writes_after_timeout);

    /* A late response still closes the exact accepted physical transaction and
     * advances the logical cycle without exposing a partial identity. */
    const char *const late_title_value[] = {"Late title"};
    make_v206_text_response(&fragment, true, title_attr, late_title_value, 1U);
    app_parse_bm83_metadata_v206(&app, &fragment);
    assert(!app.bt_metadata_request_inflight);
    assert(!app.bt_metadata_response_stalled);
    assert(app.bt_metadata_core_next_attribute == 2U);
    assert(strcmp(app.ui.track_title, "Song") == 0);

    /* A local track step can overtake a metadata response after the 0x4A UART
     * ACK. Clear the visible identity immediately and discard the late old-song
     * response instead of repopulating it. */
    m.now_ms = app.bt_metadata_next_due_ms;
    app_service_bt_metadata_transport(&app, m.now_ms);
    assert(app.bt_metadata_request_inflight && app.bt_metadata_requested_attribute == 2U);
    bm83_rx_data(&dev, metadata_ack, sizeof(metadata_ack));
    ui_state_set_track(&app.ui, "Current", "Artist", "Record");
    assert(kiku_app_bluetooth_music_action(&app, BM83_MUSIC_NEXT) == DEV_OK);
    assert(app.bt_metadata_discard_response);
    assert(app.ui.track_title[0] == '\0');
    const uint8_t old_artist_attr[] = {2U};
    const char *const old_artist_value[] = {"Wrong"};
    make_v206_text_response(&fragment, true, old_artist_attr, old_artist_value, 1U);
    app_parse_bm83_metadata_v206(&app, &fragment);
    assert(app.ui.track_title[0] == '\0');
    assert(app.ui.track_artist[0] == '\0');
    assert(app.ui.track_album[0] == '\0');
    assert(!app.bt_metadata_request_inflight);
    assert(app.bt_metadata_force_core);

    /* Finish the NEXT UART transaction and issue the fresh request. Even though
     * this is no longer the pre-NEXT transaction, the phone can briefly return
     * the same old identity while its player is still settling. That identity
     * must remain quarantined rather than flashing back onto the screen. */
    const uint8_t music_ack[] = {0xAA,0x00,0x03,0x00,0x04,0x00,0xF9};
    bm83_rx_data(&dev, music_ack, sizeof(music_ack));
    m.now_ms = app.bt_metadata_next_due_ms;
    app_service_bt_metadata_transport(&app, m.now_ms);
    assert(app.bt_metadata_request_inflight);
    bm83_rx_data(&dev, metadata_ack, sizeof(metadata_ack));

    uint32_t guarded_changes = g_bt_metadata_diag.metadata_core_change_count;
    const char *const guarded_title[] = {"Current"};
    const char *const guarded_artist[] = {"Artist"};
    const char *const guarded_album[] = {"Record"};
    make_v206_text_response(&fragment, true, title_attr, guarded_title, 1U);
    app_parse_bm83_metadata_v206(&app, &fragment);
    m.now_ms = app.bt_metadata_next_due_ms;
    app_service_bt_metadata_transport(&app, m.now_ms);
    bm83_rx_data(&dev, metadata_ack, sizeof(metadata_ack));
    make_v206_text_response(&fragment, true, artist_attr, guarded_artist, 1U);
    app_parse_bm83_metadata_v206(&app, &fragment);
    m.now_ms = app.bt_metadata_next_due_ms;
    app_service_bt_metadata_transport(&app, m.now_ms);
    bm83_rx_data(&dev, metadata_ack, sizeof(metadata_ack));
    make_v206_text_response(&fragment, true, album_attr, guarded_album, 1U);
    app_parse_bm83_metadata_v206(&app, &fragment);
    assert(app.ui.track_title[0] == '\0');
    assert(app.ui.track_artist[0] == '\0');
    assert(app.ui.track_album[0] == '\0');
    assert(app.bt_metadata_identity_guard_active);
    assert(g_bt_metadata_diag.metadata_core_change_count == guarded_changes);

    m.now_ms = app.bt_metadata_next_due_ms;
    app_service_bt_metadata_transport(&app, m.now_ms);
    assert(app.bt_metadata_request_inflight);
    bm83_rx_data(&dev, metadata_ack, sizeof(metadata_ack));
    const char *const next_title[] = {"Next song"};
    const char *const next_artist[] = {"New artist"};
    const char *const next_album[] = {"New album"};
    make_v206_text_response(&fragment, true, title_attr, next_title, 1U);
    app_parse_bm83_metadata_v206(&app, &fragment);
    m.now_ms = app.bt_metadata_next_due_ms;
    app_service_bt_metadata_transport(&app, m.now_ms);
    bm83_rx_data(&dev, metadata_ack, sizeof(metadata_ack));
    make_v206_text_response(&fragment, true, artist_attr, next_artist, 1U);
    app_parse_bm83_metadata_v206(&app, &fragment);
    m.now_ms = app.bt_metadata_next_due_ms;
    app_service_bt_metadata_transport(&app, m.now_ms);
    bm83_rx_data(&dev, metadata_ack, sizeof(metadata_ack));
    make_v206_text_response(&fragment, true, album_attr, next_album, 1U);
    app_parse_bm83_metadata_v206(&app, &fragment);
    assert(strcmp(app.ui.track_title, "Next song") == 0);
    assert(strcmp(app.ui.track_artist, "New artist") == 0);
    assert(strcmp(app.ui.track_album, "New album") == 0);
    assert(!app.bt_metadata_identity_guard_active);
    assert(g_bt_metadata_diag.metadata_core_change_count == guarded_changes + 1U);

    /* Start another core request so a profile-loss reset abandons a real
     * accepted 0x4A response. The stale-drain quarantine must survive both
     * disconnect and immediate AVRCP re-entry. */
    m.now_ms = app.bt_metadata_next_due_ms;
    app_service_bt_metadata_transport(&app, m.now_ms);
    assert(app.bt_metadata_request_inflight);
    bm83_rx_data(&dev, metadata_ack, sizeof(metadata_ack));

    bm83_packet_t disconnect = { .id = BM83_EVENT_BTM_STATUS, .payload_len = 2U };
    disconnect.payload[0] = BM83_BTM_A2DP_DISCONNECTED;
    disconnect.payload[1] = 0U;
    app_bm83_packet(&app, &disconnect);
    assert(!app.bt_metadata_request_inflight);
    assert(app.bt_metadata_discard_response);
    assert(!app.bt_avrcp_connected);

    app_mark_avrcp_available(&app, 0U);
    app.bt_avrcp_capability_pending = false;
    app.bt_track_notification_pending = false;
    assert(app.bt_metadata_discard_response);
    unsigned writes_before_reentry = m.writes;
    app_service_bt_metadata_transport(&app, m.now_ms);
    assert(m.writes == writes_before_reentry);

    /* Drain the abandoned pre-disconnect result. It must not become the first
     * song after reconnect, and only after that drain may a fresh core query run. */
    const char *const stale_title[] = {"Stale"};
    make_v206_text_response(&fragment, true, title_attr, stale_title, 1U);
    app_parse_bm83_metadata_v206(&app, &fragment);
    assert(!app.bt_metadata_discard_response);
    assert(app.ui.track_title[0] == '\0');
    m.now_ms = app.bt_metadata_next_due_ms;
    app_service_bt_metadata_transport(&app, m.now_ms);
    assert(app.bt_metadata_request_inflight);

    /* Exercise Memory Full through the actual application callback, not just the
     * driver. The rejected 0x4A ends this logical attempt, but the driver must
     * hold the exact command behind the documented ready gate until BM83 later
     * emits 0x4A/status-0. Human transport controls remain usable meanwhile. */
    bm83_set_packet_callback(&dev, app_bm83_packet, &app);
    uint32_t backpressure_before = g_bt_metadata_diag.metadata_backpressure_count;
    const uint8_t memory_full_ack[] = {0xAA,0x00,0x03,0x00,0x4A,0x05,0xAE};
    bm83_rx_data(&dev, memory_full_ack, sizeof(memory_full_ack));
    assert(!app.bt_metadata_request_inflight);
    assert(app.bt_metadata_force_core);
    assert(app.bt_metadata_next_due_ms == m.now_ms + APP_BT_METADATA_RETRY_BACKOFF_MS);
    assert(g_bt_metadata_diag.metadata_backpressure_count == backpressure_before + 1U);
    assert(!dev.awaiting_command_ack);
    assert(bm83_command_waiting_ready(&dev, BM83_CMD_AVRCP_VENDOR_DEPENDENT));
    assert(!dev.command_error_latched);

    /* Passing the application's ordinary retry deadline must not generate a
     * polling storm while BM83 still owns the backpressure gate. */
    unsigned writes_while_backpressured = m.writes;
    m.now_ms = app.bt_metadata_next_due_ms;
    app_service_bt_metadata_transport(&app, m.now_ms);
    assert(m.writes == writes_while_backpressured);
    assert(!app.bt_metadata_request_inflight);

    /* The gate is command-specific: an unrelated playback control is allowed
     * through immediately and can be ACKed independently. */
    assert(kiku_app_bluetooth_music_action(&app, BM83_MUSIC_PLAY) == DEV_OK);
    assert(dev.awaiting_command_ack && dev.pending_command_id == BM83_CMD_MUSIC_CONTROL);
    const uint8_t play_ack[] = {0xAA,0x00,0x03,0x00,0x04,0x00,0xF9};
    bm83_rx_data(&dev, play_ack, sizeof(play_ack));
    assert(!dev.awaiting_command_ack);
    assert(bm83_command_waiting_ready(&dev, BM83_CMD_AVRCP_VENDOR_DEPENDENT));

    /* BM83's later matching status-0 ACK is a readiness notification, not the
     * completion of the rejected metadata packet. Only after it arrives may the
     * scheduler construct a fresh single-attribute 0x4A request. */
    uint32_t ready_before = g_bm83_diag.nonfatal_ready_ack_count;
    bm83_rx_data(&dev, metadata_ack, sizeof(metadata_ack));
    assert(!bm83_command_waiting_ready(&dev, BM83_CMD_AVRCP_VENDOR_DEPENDENT));
    assert(g_bm83_diag.nonfatal_ready_ack_count == ready_before + 1U);
    unsigned writes_before_ready_retry = m.writes;
    app_service_bt_metadata_transport(&app, m.now_ms);
    assert(app.bt_metadata_request_inflight);
    assert(m.writes == writes_before_ready_retry + 1U);
    assert(m.frame[3] == BM83_CMD_AVRCP_VENDOR_DEPENDENT &&
           m.frame[6] == 0x01U && m.frame[10] == 0x01U);
}

static void test_app_metadata_refresh_hints_and_track_steps(void)
{
    bm_mock_t m = { .now_ms = 4000U };
    dev_uart_bus_t uart = { .ctx = &m, .write = mock_uart_write };
    dev_clock_t clock = { .ctx = &m, .time_ms = mock_time };
    bm83_t dev;
    assert(bm83_init(&dev, &uart, NULL, NULL, NULL, &clock) == DEV_OK);

    kiku_app_t app = {0};
    app.deps.bluetooth = &dev;
    app.deps.clock = clock;
    app.audio.source = AUDIO_SOURCE_BLUETOOTH;
    settings_defaults(&app.settings.value);
    ui_state_init(&app.ui, &app.settings.value);
    app_mark_avrcp_available(&app, 0U);
    app.bt_avrcp_capability_pending = false;
    app.bt_track_notification_pending = false;
    app.bt_metadata_force_core = false;
    app.bt_metadata_next_due_ms = m.now_ms + 5000U;
    ui_state_set_track(&app.ui, "Existing", "Existing artist", "Existing album");

    /* PlaybackStatusChanged is observed reliably on the fielded phone even when
     * TrackChanged is omitted. CHANGED accelerates metadata without clearing a
     * still-valid identity while INTERIM must not force a poll. */
    bm83_packet_t notification = { .id = BM83_EVENT_AVC_VENDOR_DEPENDENT_RESPONSE };
    notification.payload[0] = 0U;
    notification.payload[1] = 0x0FU; /* INTERIM */
    notification.payload[2] = 0x48U;
    notification.payload[3] = 0x00U;
    notification.payload[4] = 0x00U;
    notification.payload[5] = 0x19U;
    notification.payload[6] = 0x58U;
    notification.payload[7] = 0x31U;
    notification.payload[8] = 0x00U;
    notification.payload[9] = 0x00U;
    notification.payload[10] = 0x02U;
    notification.payload[11] = 0x01U; /* PlaybackStatusChanged */
    notification.payload[12] = 0x01U;
    notification.payload_len = 13U;
    uint32_t forced_before = g_bt_metadata_diag.metadata_forced_refresh_count;
    app_parse_bm83_notification(&app, &notification);
    assert(!app.bt_metadata_force_core);
    assert(g_bt_metadata_diag.metadata_forced_refresh_count == forced_before);

    notification.payload[1] = 0x0DU; /* CHANGED */
    app_parse_bm83_notification(&app, &notification);
    assert(app.bt_metadata_force_core);
    assert(app.bt_metadata_next_due_ms == m.now_ms + APP_BT_METADATA_POST_CONTROL_MS);
    assert(strcmp(app.ui.track_title, "Existing") == 0);
    assert(g_bt_metadata_diag.metadata_forced_refresh_count == forced_before + 1U);

    unsigned writes_before_hint = m.writes;
    m.now_ms = app.bt_metadata_next_due_ms - 1U;
    app_service_bt_metadata_transport(&app, m.now_ms);
    assert(m.writes == writes_before_hint);
    ++m.now_ms;
    app_service_bt_metadata_transport(&app, m.now_ms);
    assert(app.bt_metadata_request_inflight);
    assert(m.frame[3] == BM83_CMD_AVRCP_VENDOR_DEPENDENT &&
           m.frame[6] == 0x01U && m.frame[10] == 0x01U);
    const uint8_t metadata_ack[] = {0xAA,0x00,0x03,0x00,0x4A,0x00,0xB3};
    bm83_rx_data(&dev, metadata_ack, sizeof(metadata_ack));

    /* TrackChanged is a stronger identity boundary: clear the visible song,
     * re-register the one-shot notification and discard any pre-change 0x5D. */
    memset(&notification, 0, sizeof(notification));
    notification.id = BM83_EVENT_AVC_VENDOR_DEPENDENT_RESPONSE;
    notification.payload[0] = 0U;
    notification.payload[1] = 0x0DU;
    notification.payload[2] = 0x48U;
    notification.payload[3] = 0x00U;
    notification.payload[4] = 0x00U;
    notification.payload[5] = 0x19U;
    notification.payload[6] = 0x58U;
    notification.payload[7] = 0x31U;
    notification.payload[8] = 0x00U;
    notification.payload[9] = 0x00U;
    notification.payload[10] = 0x09U;
    notification.payload[11] = 0x02U; /* TrackChanged */
    for (size_t i = 0U; i < 8U; ++i) notification.payload[12U + i] = (uint8_t)(i + 1U);
    notification.payload_len = 20U;
    uint32_t track_changes_before = g_bt_metadata_diag.metadata_track_change_count;
    app_parse_bm83_notification(&app, &notification);
    assert(app.bt_track_notification_pending);
    assert(app.bt_metadata_discard_response);
    assert(app.ui.track_title[0] == '\0');
    assert(g_bt_metadata_diag.metadata_track_change_count == track_changes_before + 1U);

    bm83_packet_t stale;
    const uint8_t attrs[] = {1U, 2U, 3U};
    const char *const stale_values[] = {"Old phone title", "Old phone artist", "Old phone album"};
    make_v206_text_response(&stale, true, attrs, stale_values, 3U);
    app_parse_bm83_metadata_v206(&app, &stale);
    assert(app.ui.track_title[0] == '\0');
    assert(app.ui.track_artist[0] == '\0');
    assert(app.ui.track_album[0] == '\0');

    /* Both local skip directions use the same old-response quarantine. Exercise
     * each explicitly so PREVIOUS cannot regress behind NEXT-only coverage. */
    const bm83_music_action_t steps[] = { BM83_MUSIC_NEXT, BM83_MUSIC_PREVIOUS };
    const uint8_t music_ack[] = {0xAA,0x00,0x03,0x00,0x04,0x00,0xF9};
    for (size_t i = 0U; i < sizeof(steps) / sizeof(steps[0]); ++i) {
        dev.awaiting_command_ack = false;
        app.bt_track_notification_pending = false;
        app.bt_metadata_discard_response = false;
        app_bt_metadata_reset_transaction(&app, m.now_ms, true);
        app.bt_metadata_discard_response = false;
        app_service_bt_metadata_transport(&app, m.now_ms);
        assert(app.bt_metadata_request_inflight);
        bm83_rx_data(&dev, metadata_ack, sizeof(metadata_ack));
        ui_state_set_track(&app.ui, "Current", "Artist", "Album");
        assert(kiku_app_bluetooth_music_action(&app, steps[i]) == DEV_OK);
        assert(app.bt_metadata_discard_response);
        assert(app.ui.track_title[0] == '\0');
        make_v206_text_response(&stale, true, attrs, stale_values, 3U);
        app_parse_bm83_metadata_v206(&app, &stale);
        assert(app.ui.track_title[0] == '\0');
        assert(app.ui.track_artist[0] == '\0');
        assert(app.ui.track_album[0] == '\0');
        bm83_rx_data(&dev, music_ack, sizeof(music_ack));
        m.now_ms += APP_BT_METADATA_POST_CONTROL_MS;
    }
}

static void test_bm83(void)
{
    bm_mock_t m = {0};
    dev_uart_bus_t uart = { .ctx = &m, .write = mock_uart_write };
    dev_gpio_t mfb = { .ctx = &m, .read = NULL, .write = mock_gpio_write };
    dev_gpio_t reset_n = { .ctx = &m, .read = NULL, .write = mock_reset_write };
    dev_clock_t clock = { .ctx = &m, .delay_ms = mock_delay, .time_ms = mock_time };
    bm83_t dev;
    assert(bm83_init(&dev, &uart, &reset_n, &mfb, NULL, &clock) == DEV_OK);

    assert(!bm83_uart_version_at_least(&dev, 2U, 0U));
    const uint8_t uart_v202[] = {0xAA,0x00,0x04,0x18,0x00,0x02,0x02,0xE0};
    bm83_rx_data(&dev, uart_v202, sizeof(uart_v202));
    assert(dev.uart_version_valid);
    assert(dev.uart_version_major == 2U && dev.uart_version_minor == 2U);
    assert(bm83_uart_version_at_least(&dev, 2U, 0U));
    assert(bm83_uart_version_at_least(&dev, 2U, 2U));
    assert(!bm83_uart_version_at_least(&dev, 2U, 3U));
    assert(!bm83_uart_version_at_least(&dev, 3U, 0U));

    assert(bm83_unmask_all_events(&dev) == DEV_OK);
    const uint8_t expected_event_mask[] = {
        0xAA,0x00,0x05,0x03,0x00,0x00,0x00,0x00,0xF8
    };
    assert(m.frame_len == sizeof(expected_event_mask));
    assert(memcmp(m.frame, expected_event_mask, sizeof(expected_event_mask)) == 0);
    const uint8_t event_mask_ack[] = {0xAA,0x00,0x03,0x00,0x03,0x00,0xFA};
    bm83_rx_data(&dev, event_mask_ack, sizeof(event_mask_ack));
    assert(!dev.awaiting_command_ack);

    assert(bm83_music_control(&dev, BM83_MUSIC_TOGGLE) == DEV_OK);
    const uint8_t expected_music[] = {0xAA,0x00,0x03,0x04,0x00,0x07,0xF2};
    assert(m.frame_len == sizeof(expected_music));
    assert(memcmp(m.frame, expected_music, sizeof(expected_music)) == 0);
    /* BM83 has no UART_RX_IND input. Normal UART traffic must never pulse MFB;
     * on this module MFB is a real button/control pin. */
    assert(m.mfb_highs == 0U && m.mfb_lows == 0U && !m.mfb);
    assert(m.delayed_ms == 0U);

    /* Every normal command is serialized until its Command_ACK arrives. */
    assert(bm83_request_now_playing_metadata(&dev, 0U) == DEV_EBUSY);
    const uint8_t command_ack[] = {0xAA,0x00,0x03,0x00,0x04,0x00,0xF9};
    bm83_rx_data(&dev, command_ack, sizeof(command_ack));
    assert(!dev.awaiting_command_ack);

    assert(bm83_read_paired_device_record(&dev) == DEV_OK);
    const uint8_t expected_paired_read[] = {0xAA,0x00,0x02,0x0E,0x00,0xF0};
    assert(m.frame_len == sizeof(expected_paired_read));
    assert(memcmp(m.frame, expected_paired_read, sizeof(expected_paired_read)) == 0);
    const uint8_t paired_ack[] = {0xAA,0x00,0x03,0x00,0x0E,0x00,0xEF};
    bm83_rx_data(&dev, paired_ack, sizeof(paired_ack));
    const uint8_t paired_reply[] = {
        0xAA,0x00,0x10,0x1F,0x02,
        0x02,0x66,0x55,0x44,0x33,0x22,0x11,
        0x01,0xA6,0xA5,0xA4,0xA3,0xA2,0xA1,
        0x92
    };
    bm83_rx_data(&dev, paired_reply, sizeof(paired_reply));
    assert(g_bm83_diag.paired_device_count == 2U);
    assert(g_bm83_diag.paired_newest_valid == 1U);
    const uint8_t newest_addr[] = {0xA6,0xA5,0xA4,0xA3,0xA2,0xA1};
    assert(memcmp((const void *)g_bm83_diag.paired_newest_address,
                  newest_addr, sizeof(newest_addr)) == 0);
    assert(dev.paired_device_count == 2U);
    assert(dev.paired_newest_valid);
    assert(dev.paired_newest_record_index == 1U);
    assert(memcmp(dev.paired_newest_address, newest_addr, sizeof(newest_addr)) == 0);

    assert(bm83_link_back_address_a2dp(&dev, 1U, newest_addr) == DEV_OK);
    const uint8_t expected_address_back[] = {
        0xAA,0x00,0x0A,0x17,0x05,0x01,0x04,
        0xA6,0xA5,0xA4,0xA3,0xA2,0xA1,0x00
    };
    assert(m.frame_len == sizeof(expected_address_back));
    assert(memcmp(m.frame, expected_address_back, sizeof(expected_address_back)) == 0);
    const uint8_t address_back_ack[] = {0xAA,0x00,0x03,0x00,0x17,0x00,0xE6};
    bm83_rx_data(&dev, address_back_ack, sizeof(address_back_ack));

    assert(bm83_set_supported_classic_profiles(
               &dev,
               (uint8_t)(BM83_PROFILE_A2DP | BM83_PROFILE_AVRCP_CT | BM83_PROFILE_AVRCP_TG)) == DEV_OK);
    const uint8_t expected_profiles[] = {0xAA,0x00,0x03,0x07,0x04,0x1C,0xD6};
    assert(m.frame_len == sizeof(expected_profiles));
    assert(memcmp(m.frame, expected_profiles, sizeof(expected_profiles)) == 0);
    const uint8_t profiles_ack[] = {0xAA,0x00,0x03,0x00,0x07,0x00,0xF6};
    bm83_rx_data(&dev, profiles_ack, sizeof(profiles_ack));
    assert(!dev.awaiting_command_ack);

    assert(bm83_toggle_audio_source(&dev) == DEV_OK);
    const uint8_t expected_toggle[] = {0xAA,0x00,0x02,0xCC,0x01,0x31};
    assert(m.frame_len == sizeof(expected_toggle));
    assert(memcmp(m.frame, expected_toggle, sizeof(expected_toggle)) == 0);
    const uint8_t toggle_ack[] = {0xAA,0x00,0x03,0x00,0xCC,0x00,0x31};
    bm83_rx_data(&dev, toggle_ack, sizeof(toggle_ack));
    assert(!dev.awaiting_command_ack);

    assert(bm83_read_eeprom(&dev, 0x1234U, 16U) == DEV_OK);
    const uint8_t expected_read[] = {0xAA,0x00,0x04,0x3C,0x12,0x34,0x10,0x6A};
    assert(m.frame_len == sizeof(expected_read));
    assert(memcmp(m.frame, expected_read, sizeof(expected_read)) == 0);
    const uint8_t read_ack[] = {0xAA,0x00,0x03,0x00,0x3C,0x00,0xC1};
    bm83_rx_data(&dev, read_ack, sizeof(read_ack));
    assert(!dev.awaiting_command_ack);

    assert(bm83_read_link_status(&dev) == DEV_OK);
    const uint8_t expected_link[] = {0xAA,0x00,0x02,0x0D,0x00,0xF1};
    assert(m.frame_len == sizeof(expected_link));
    assert(memcmp(m.frame, expected_link, sizeof(expected_link)) == 0);
    const uint8_t link_ack[] = {0xAA,0x00,0x03,0x00,0x0D,0x00,0xF0};
    bm83_rx_data(&dev, link_ack, sizeof(link_ack));
    assert(!dev.awaiting_command_ack);

    assert(bm83_read_remote_avrcp_features(&dev, 0U) == DEV_OK);
    const uint8_t expected_avrcp_features[] = {0xAA,0x00,0x03,0x16,0x00,0x03,0xE4};
    assert(m.frame_len == sizeof(expected_avrcp_features));
    assert(memcmp(m.frame, expected_avrcp_features, sizeof(expected_avrcp_features)) == 0);
    const uint8_t avrcp_info_ack[] = {0xAA,0x00,0x03,0x00,0x16,0x00,0xE7};
    bm83_rx_data(&dev, avrcp_info_ack, sizeof(avrcp_info_ack));
    assert(!dev.awaiting_command_ack);
    const uint8_t avrcp_info[] = {0xAA,0x00,0x04,0x17,0x00,0x03,0x01,0xE1};
    bm83_rx_data(&dev, avrcp_info, sizeof(avrcp_info));
    assert(g_bm83_diag.remote_avrcp_features_valid == 1U);
    assert(g_bm83_diag.remote_avrcp_features == 0x01U);

    assert(bm83_link_back_last_device(&dev) == DEV_OK);
    const uint8_t expected_link_back[] = {0xAA,0x00,0x02,0x17,0x00,0xE7};
    assert(m.frame_len == sizeof(expected_link_back));
    assert(memcmp(m.frame, expected_link_back, sizeof(expected_link_back)) == 0);
    const uint8_t link_back_ack[] = {0xAA,0x00,0x03,0x00,0x17,0x00,0xE6};
    bm83_rx_data(&dev, link_back_ack, sizeof(link_back_ack));
    assert(!dev.awaiting_command_ack);

    assert(bm83_link_back_last_a2dp(&dev) == DEV_OK);
    const uint8_t expected_a2dp_back[] = {0xAA,0x00,0x02,0x17,0x02,0xE5};
    assert(m.frame_len == sizeof(expected_a2dp_back));
    assert(memcmp(m.frame, expected_a2dp_back, sizeof(expected_a2dp_back)) == 0);
    bm83_rx_data(&dev, link_back_ack, sizeof(link_back_ack));
    assert(!dev.awaiting_command_ack);

    assert(bm83_request_now_playing_metadata(&dev, 0U) == DEV_OK);
    const uint8_t expected_metadata[] = {
        0xAA,0x00,0x13,0x0B,0x00,0x20,0x00,0x00,0x0D,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,
        0x00,0x00,0x00,0x01,0xB3,
    };
    assert(m.frame_len == sizeof(expected_metadata));
    assert(memcmp(m.frame, expected_metadata, sizeof(expected_metadata)) == 0);
    const uint8_t metadata_ack[] = {0xAA,0x00,0x03,0x00,0x0B,0x00,0xF2};
    bm83_rx_data(&dev, metadata_ack, sizeof(metadata_ack));
    assert(!dev.awaiting_command_ack);

    assert(bm83_register_track_changed(&dev, 0U) == DEV_OK);
    const uint8_t expected_track_notify[] = {
        0xAA,0x00,0x0B,0x0B,0x00,0x31,0x00,0x00,0x05,
        0x02,0x00,0x00,0x00,0x00,0xB2
    };
    assert(m.frame_len == sizeof(expected_track_notify));
    assert(memcmp(m.frame, expected_track_notify, sizeof(expected_track_notify)) == 0);
    bm83_rx_data(&dev, metadata_ack, sizeof(metadata_ack));
    assert(!dev.awaiting_command_ack);

    assert(bm83_request_event_capabilities(&dev, 0U) == DEV_OK);
    const uint8_t expected_caps[] = { 0xAA,0x00,0x07,0x0B,0x00,0x10,0x00,0x00,0x01,0x03,0xDA };
    assert(m.frame_len == sizeof(expected_caps));
    assert(memcmp(m.frame, expected_caps, sizeof(expected_caps)) == 0);
    bm83_rx_data(&dev, metadata_ack, sizeof(metadata_ack));
    assert(!dev.awaiting_command_ack);

    assert(bm83_request_now_playing_attribute(&dev, 0U, 2U) == DEV_OK);
    assert(m.frame_len == sizeof(expected_metadata));
    assert(m.frame[21] == 0x02U);
    bm83_rx_data(&dev, metadata_ack, sizeof(metadata_ack));
    assert(!dev.awaiting_command_ack);

    assert(bm83_request_now_playing_attribute(&dev, 0U, 3U) == DEV_OK);
    assert(m.frame_len == sizeof(expected_metadata));
    assert(m.frame[21] == 0x03U);
    bm83_rx_data(&dev, metadata_ack, sizeof(metadata_ack));
    assert(!dev.awaiting_command_ack);

    assert(bm83_request_current_metadata_v206(&dev, 0U) == DEV_OK);
    assert(m.frame[3] == BM83_CMD_AVRCP_VENDOR_DEPENDENT);
    assert(m.frame[4] == 0x00U && m.frame[5] == 0x20U && m.frame[6] == 0x03U);
    assert(m.frame[10] == 0x01U && m.frame[14] == 0x02U && m.frame[18] == 0x03U);
    const uint8_t modern_ack[] = {0xAA,0x00,0x03,0x00,0x4A,0x00,0xB3};
    bm83_rx_data(&dev, modern_ack, sizeof(modern_ack));
    assert(!dev.awaiting_command_ack);

    assert(bm83_request_current_metadata_attribute_v206(&dev, 0U, 7U) == DEV_OK);
    assert(m.frame[3] == BM83_CMD_AVRCP_VENDOR_DEPENDENT);
    assert(m.frame[4] == 0x00U && m.frame[5] == 0x20U && m.frame[6] == 0x01U);
    assert(m.frame[10] == 0x07U);
    bm83_rx_data(&dev, modern_ack, sizeof(modern_ack));
    assert(!dev.awaiting_command_ack);

    assert(bm83_request_media_player_list(&dev, 0U) == DEV_OK);
    assert(m.frame[3] == BM83_CMD_AVRCP_BROWSING);
    assert(m.frame[4] == 0x00U && m.frame[5] == 0x00U && m.frame[6] == 0x00U);
    assert(m.frame[14] == 0x00U && m.frame[15] == 0x00U);
    const uint8_t browse_ack[] = {0xAA,0x00,0x03,0x00,0x41,0x00,0xBC};
    bm83_rx_data(&dev, browse_ack, sizeof(browse_ack));
    assert(!dev.awaiting_command_ack);

    assert(bm83_set_addressed_player(&dev, 0U, 0x1234U) == DEV_OK);
    assert(m.frame[3] == BM83_CMD_AVRCP_BROWSING);
    assert(m.frame[4] == 0x02U && m.frame[5] == 0x00U &&
           m.frame[6] == 0x12U && m.frame[7] == 0x34U);
    bm83_rx_data(&dev, browse_ack, sizeof(browse_ack));
    assert(!dev.awaiting_command_ack);

    assert(bm83_set_browsed_player(&dev, 0U, 0x1234U) == DEV_OK);
    assert(m.frame[3] == BM83_CMD_AVRCP_BROWSING);
    assert(m.frame[4] == 0x03U && m.frame[5] == 0x00U &&
           m.frame[6] == 0x12U && m.frame[7] == 0x34U);
    bm83_rx_data(&dev, browse_ack, sizeof(browse_ack));
    assert(!dev.awaiting_command_ack);

    assert(bm83_request_now_playing_browse(&dev, 0U) == DEV_OK);
    const uint8_t expected_browse[] = {
        0xAA,0x00,0x19,0x41,0x00,0x00,0x03,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,
        0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x02,
        0x00,0x00,0x00,0x03,0x9A
    };
    assert(m.frame_len == sizeof(expected_browse));
    assert(memcmp(m.frame, expected_browse, sizeof(expected_browse)) == 0);
    bm83_rx_data(&dev, browse_ack, sizeof(browse_ack));
    assert(!dev.awaiting_command_ack);

    const uint8_t track_uid[8] = {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF};
    assert(bm83_request_item_attributes(&dev, 0U, 0x03U, track_uid, 0x1357U) == DEV_OK);
    assert(m.frame[3] == BM83_CMD_AVRCP_BROWSING);
    assert(m.frame[4] == 0x05U && m.frame[5] == 0x00U && m.frame[6] == 0x03U);
    assert(memcmp(&m.frame[7], track_uid, sizeof(track_uid)) == 0);
    assert(m.frame[15] == 0x13U && m.frame[16] == 0x57U);
    assert(m.frame[17] == 0x07U);
    assert(m.frame[21] == 0x01U && m.frame[25] == 0x02U && m.frame[29] == 0x03U);
    assert(m.frame[33] == 0x04U && m.frame[37] == 0x05U && m.frame[41] == 0x06U &&
           m.frame[45] == 0x07U);
    bm83_rx_data(&dev, browse_ack, sizeof(browse_ack));
    assert(!dev.awaiting_command_ack);

    packet_capture_t cap = {0};
    bm83_set_packet_callback(&dev, packet_cb, &cap);
    const uint8_t event[] = {0xAA,0x00,0x03,0x01,0x06,0x00,0xF6};
    unsigned writes_before = m.writes;
    bm83_rx_data(&dev, event, sizeof(event));
    assert(cap.calls == 1U);
    assert(cap.packet.id == BM83_EVENT_BTM_STATUS);
    assert(cap.packet.payload_len == 2U && cap.packet.payload[0] == 0x06U);
    assert(m.writes == writes_before + 1U); /* Event_ACK */
    const uint8_t expected_ack[] = {0xAA,0x00,0x02,0x14,0x01,0xE9};
    assert(m.frame_len == sizeof(expected_ack));
    assert(memcmp(m.frame, expected_ack, sizeof(expected_ack)) == 0);

    /* Missing ACK: retry the exact frame at 200 ms, then follow the documented
     * hardware reset sequence after a second 200 ms timeout. */
    bm_mock_t retry = {0};
    dev_uart_bus_t retry_uart = { .ctx = &retry, .write = mock_uart_write };
    dev_gpio_t retry_mfb = { .ctx = &retry, .write = mock_gpio_write };
    dev_gpio_t retry_reset = { .ctx = &retry, .write = mock_reset_write };
    dev_clock_t retry_clock = { .ctx = &retry, .delay_ms = mock_delay, .time_ms = mock_time };
    bm83_t retry_dev;
    assert(bm83_init(&retry_dev, &retry_uart, &retry_reset, &retry_mfb, NULL, &retry_clock) == DEV_OK);
    assert(bm83_music_control(&retry_dev, BM83_MUSIC_NEXT) == DEV_OK);
    const unsigned first_writes = retry.writes;
    retry.now_ms += BM83_COMMAND_ACK_TIMEOUT_MS;
    assert(bm83_service(&retry_dev) == DEV_EBUSY);
    assert(retry.writes == first_writes + 1U);
    retry.now_ms += BM83_COMMAND_ACK_TIMEOUT_MS;
    assert(bm83_service(&retry_dev) == DEV_ETIMEOUT);
    assert(retry.reset_lows == 1U && retry.reset_highs == 1U);
    assert(!retry_dev.awaiting_command_ack);

    /* Metadata is auxiliary to A2DP. Losing both ACK attempts for an AVRCP
     * query must abandon that query without resetting a healthy streaming
     * BM83; a later poll may retry it. */
    bm_mock_t metadata_retry = {0};
    dev_uart_bus_t metadata_uart = { .ctx = &metadata_retry, .write = mock_uart_write };
    dev_gpio_t metadata_mfb = { .ctx = &metadata_retry, .write = mock_gpio_write };
    dev_gpio_t metadata_reset = { .ctx = &metadata_retry, .write = mock_reset_write };
    dev_clock_t metadata_clock = {
        .ctx = &metadata_retry, .delay_ms = mock_delay, .time_ms = mock_time
    };
    bm83_t metadata_dev;
    assert(bm83_init(&metadata_dev, &metadata_uart, &metadata_reset,
                     &metadata_mfb, NULL, &metadata_clock) == DEV_OK);
    assert(bm83_request_current_metadata_attribute_v206(&metadata_dev, 0U, 1U) == DEV_OK);
    unsigned metadata_writes = metadata_retry.writes;
    metadata_retry.now_ms += BM83_COMMAND_ACK_TIMEOUT_MS;
    assert(bm83_service(&metadata_dev) == DEV_EBUSY);
    assert(metadata_retry.writes == metadata_writes + 1U);
    metadata_retry.now_ms += BM83_COMMAND_ACK_TIMEOUT_MS;
    assert(bm83_service(&metadata_dev) == DEV_EBUSY);
    assert(!metadata_dev.awaiting_command_ack);
    assert(metadata_retry.reset_lows == 0U && metadata_retry.reset_highs == 0U);
    assert(g_bm83_diag.command_timeout_count == 1U);
    assert(g_bm83_diag.nonfatal_command_timeout_count == 1U);

    /* Explicit BM83 Busy/Memory Full on an auxiliary 0x4A query is a
     * readiness handshake, not a timer-based retry. The exact command is gated
     * until the later matching status-0 ACK while unrelated controls still work. */
    bm_mock_t busy = {0};
    dev_uart_bus_t busy_uart = { .ctx = &busy, .write = mock_uart_write };
    dev_clock_t busy_clock = { .ctx = &busy, .time_ms = mock_time };
    bm83_t busy_dev;
    assert(bm83_init(&busy_dev, &busy_uart, NULL, NULL, NULL, &busy_clock) == DEV_OK);
    uint32_t busy_before = g_bm83_diag.nonfatal_busy_ack_count;
    uint32_t ready_before = g_bm83_diag.nonfatal_ready_ack_count;
    assert(bm83_request_current_metadata_v206(&busy_dev, 0U) == DEV_OK);
    const uint8_t memory_full_ack[] = {0xAA,0x00,0x03,0x00,0x4A,0x05,0xAE};
    bm83_rx_data(&busy_dev, memory_full_ack, sizeof(memory_full_ack));
    assert(!busy_dev.awaiting_command_ack);
    assert(bm83_command_waiting_ready(&busy_dev, BM83_CMD_AVRCP_VENDOR_DEPENDENT));
    assert(g_bm83_diag.nonfatal_busy_ack_count == busy_before + 1U);
    unsigned busy_writes = busy.writes;
    busy.now_ms += BM83_COMMAND_ACK_TIMEOUT_MS;
    assert(bm83_service(&busy_dev) == DEV_OK);
    assert(busy.writes == busy_writes);
    assert(bm83_request_current_metadata_v206(&busy_dev, 0U) == DEV_EBUSY);
    assert(busy.writes == busy_writes);

    assert(bm83_music_control(&busy_dev, BM83_MUSIC_PLAY) == DEV_OK);
    assert(busy_dev.awaiting_command_ack);
    assert(busy_dev.pending_command_id == BM83_CMD_MUSIC_CONTROL);

    /* Deliver readiness while the unrelated control still awaits its own ACK.
     * Matching by command ID must clear only the metadata gate. */
    const uint8_t ready_ack[] = {0xAA,0x00,0x03,0x00,0x4A,0x00,0xB3};
    bm83_rx_data(&busy_dev, ready_ack, sizeof(ready_ack));
    assert(!bm83_command_waiting_ready(&busy_dev, BM83_CMD_AVRCP_VENDOR_DEPENDENT));
    assert(g_bm83_diag.nonfatal_ready_ack_count == ready_before + 1U);
    assert(busy_dev.awaiting_command_ack);
    assert(busy_dev.pending_command_id == BM83_CMD_MUSIC_CONTROL);

    const uint8_t play_ack[] = {0xAA,0x00,0x03,0x00,0x04,0x00,0xF9};
    bm83_rx_data(&busy_dev, play_ack, sizeof(play_ack));
    assert(!busy_dev.awaiting_command_ack);
    assert(bm83_request_current_metadata_v206(&busy_dev, 0U) == DEV_OK);

    /* A fielded BM83 v2.02 build can omit the documented delayed readiness ACK.
     * After a long quiet interval the driver must permit exactly one new probe,
     * not auto-transmit and not enter a fixed-rate retry loop. */
    bm_mock_t fallback = {0};
    dev_uart_bus_t fallback_uart = { .ctx = &fallback, .write = mock_uart_write };
    dev_clock_t fallback_clock = { .ctx = &fallback, .time_ms = mock_time };
    bm83_t fallback_dev;
    assert(bm83_init(&fallback_dev, &fallback_uart, NULL, NULL, NULL,
                     &fallback_clock) == DEV_OK);
    uint32_t ready_timeout_before = g_bm83_diag.nonfatal_ready_timeout_count;
    assert(bm83_request_current_metadata_v206(&fallback_dev, 0U) == DEV_OK);
    bm83_rx_data(&fallback_dev, memory_full_ack, sizeof(memory_full_ack));
    assert(bm83_command_waiting_ready(&fallback_dev,
                                      BM83_CMD_AVRCP_VENDOR_DEPENDENT));
    unsigned fallback_writes = fallback.writes;
    fallback.now_ms += BM83_NONFATAL_READY_TIMEOUT_MS - 1U;
    assert(bm83_service(&fallback_dev) == DEV_OK);
    assert(bm83_command_waiting_ready(&fallback_dev,
                                      BM83_CMD_AVRCP_VENDOR_DEPENDENT));
    assert(fallback.writes == fallback_writes);

    fallback.now_ms += 1U;
    assert(bm83_service(&fallback_dev) == DEV_OK);
    assert(!bm83_command_waiting_ready(&fallback_dev,
                                       BM83_CMD_AVRCP_VENDOR_DEPENDENT));
    assert(g_bm83_diag.nonfatal_ready_timeout_count == ready_timeout_before + 1U);
    assert(fallback.writes == fallback_writes);
    assert(bm83_request_current_metadata_v206(&fallback_dev, 0U) == DEV_OK);
    assert(fallback.writes == fallback_writes + 1U);
}

static void test_bm83_reconnect(void)
{
    bm_mock_t m = {0};
    dev_uart_bus_t uart = { .ctx = &m, .write = mock_uart_write };
    dev_gpio_t mfb = { .ctx = &m, .write = mock_gpio_write };
    dev_gpio_t reset_n = { .ctx = &m, .write = mock_reset_write };
    dev_clock_t clock = { .ctx = &m, .delay_ms = mock_delay, .time_ms = mock_time };
    bm83_t dev;
    assert(bm83_init(&dev, &uart, &reset_n, &mfb, NULL, &clock) == DEV_OK);

    bm83_set_auto_reconnect(&dev, true);
    assert(bm83_service_connection(&dev) == DEV_OK);
    /* Normal reconnect uses the paired-device profile record, rather than an
     * A2DP-only link-back which can leave AVRCP (and therefore metadata) down. */
    const uint8_t expected_device_back[] = {0xAA,0x00,0x02,0x17,0x00,0xE7};
    assert(m.frame_len == sizeof(expected_device_back));
    assert(memcmp(m.frame, expected_device_back, sizeof(expected_device_back)) == 0);
    assert(dev.reconnect_in_progress && dev.reconnect_attempts == 1U);
    assert(m.mfb_highs == 0U && m.mfb_lows == 0U);

    const uint8_t link_back_ack[] = {0xAA,0x00,0x03,0x00,0x17,0x00,0xE6};
    bm83_rx_data(&dev, link_back_ack, sizeof(link_back_ack));
    assert(!dev.awaiting_command_ack);

    /* A2DP link-back failure schedules bounded backoff instead of tight-looping. */
    const uint8_t link_fail[] = {0xAA,0x00,0x03,0x23,0x02,0x01,0xD7};
    bm83_rx_data(&dev, link_fail, sizeof(link_fail));
    assert(g_bm83_diag.last_link_back_status == BM83_LINK_BACK_STATUS_A2DP);
    assert(g_bm83_diag.last_link_back_result == 0x01U);
    assert(g_bm83_diag.link_back_a2dp_fail_count == 1U);
    assert(!dev.reconnect_in_progress);
    unsigned writes_after_failure = m.writes;
    assert(bm83_service_connection(&dev) == DEV_EBUSY);
    assert(m.writes == writes_after_failure);
    m.now_ms += BM83_RECONNECT_BASE_MS - 1U;
    assert(bm83_service_connection(&dev) == DEV_EBUSY);
    assert(m.writes == writes_after_failure);
    ++m.now_ms;
    assert(bm83_service_connection(&dev) == DEV_OK);
    assert(dev.reconnect_attempts == 2U);
    assert(memcmp(m.frame, expected_device_back, sizeof(expected_device_back)) == 0);

    bm83_rx_data(&dev, link_back_ack, sizeof(link_back_ack));
    /* If the composite last-device path fails twice, the next attempt uses the
     * documented A2DP-only link-back. Once A2DP is up the existing AVRCP
     * recovery path completes the control/metadata profile. */
    bm83_rx_data(&dev, link_fail, sizeof(link_fail));
    assert(!dev.reconnect_in_progress);
    m.now_ms += (BM83_RECONNECT_BASE_MS * 2U) - 1U;
    assert(bm83_service_connection(&dev) == DEV_EBUSY);
    ++m.now_ms;
    assert(bm83_service_connection(&dev) == DEV_OK);
    const uint8_t expected_a2dp_back[] = {0xAA,0x00,0x02,0x17,0x02,0xE5};
    assert(m.frame_len == sizeof(expected_a2dp_back));
    assert(memcmp(m.frame, expected_a2dp_back, sizeof(expected_a2dp_back)) == 0);
    assert(dev.reconnect_attempts == 3U);
    assert(g_bm83_diag.reconnect_a2dp_fallback_count == 1U);
    bm83_rx_data(&dev, link_back_ack, sizeof(link_back_ack));

    const uint8_t a2dp_connected[] = {0xAA,0x00,0x03,0x01,0x06,0x00,0xF6};
    bm83_rx_data(&dev, a2dp_connected, sizeof(a2dp_connected));
    assert(dev.a2dp_connected);
    assert(!dev.reconnect_in_progress && dev.reconnect_attempts == 0U);
    unsigned writes_connected = m.writes;
    /* A2DP may arrive without AVRCP. Give the companion profile its normal
     * setup window, then use documented Music_Control/STOP_SEEK to make BM83
     * initiate AVRCP without changing playback state. */
    assert(bm83_service_connection(&dev) == DEV_EBUSY);
    assert(m.writes == writes_connected);
    m.now_ms += BM83_RECONNECT_BASE_MS;
    assert(bm83_service_connection(&dev) == DEV_OK);
    const uint8_t expected_avrcp_probe[] = {0xAA,0x00,0x03,0x04,0x00,0x00,0xF9};
    assert(m.frame_len == sizeof(expected_avrcp_probe));
    assert(memcmp(m.frame, expected_avrcp_probe, sizeof(expected_avrcp_probe)) == 0);
    const uint8_t music_ack[] = {0xAA,0x00,0x03,0x00,0x04,0x00,0xF9};
    bm83_rx_data(&dev, music_ack, sizeof(music_ack));
    /* The fielded BM83 package can omit BTM_Status/AVRCP_CONNECTED even after
     * the recovery command succeeds. Any real AVRCP response proves that the
     * control channel is usable and must release the metadata gate. */
    const uint8_t avrcp_response[] = {0xAA,0x00,0x02,0x1A,0x00,0xE4};
    bm83_rx_data(&dev, avrcp_response, sizeof(avrcp_response));
    assert(dev.avrcp_connected);
    assert(!dev.reconnect_in_progress && dev.reconnect_attempts == 0U);
    writes_connected = m.writes;
    m.now_ms += BM83_RECONNECT_MAX_MS + 1000U;
    assert(bm83_service_connection(&dev) == DEV_OK);
    assert(m.writes == writes_connected);

    /* Losing AVRCP alone must recover the control/metadata profile while the
     * A2DP stream remains connected. */
    const uint8_t avrcp_disconnected[] = {0xAA,0x00,0x03,0x01,0x0C,0x00,0xF0};
    bm83_rx_data(&dev, avrcp_disconnected, sizeof(avrcp_disconnected));
    assert(dev.a2dp_connected && !dev.avrcp_connected);
    unsigned writes_avrcp_down = m.writes;
    assert(bm83_service_connection(&dev) == DEV_EBUSY);
    assert(m.writes == writes_avrcp_down);
    m.now_ms += BM83_RECONNECT_BASE_MS;
    assert(bm83_service_connection(&dev) == DEV_OK);
    assert(m.frame_len == sizeof(expected_avrcp_probe));
    assert(memcmp(m.frame, expected_avrcp_probe, sizeof(expected_avrcp_probe)) == 0);
    bm83_rx_data(&dev, music_ack, sizeof(music_ack));
    const uint8_t avrcp_connected[] = {0xAA,0x00,0x03,0x01,0x0B,0x00,0xF1};
    bm83_rx_data(&dev, avrcp_connected, sizeof(avrcp_connected));
    assert(dev.avrcp_connected);

    /* A real disconnect re-arms link-back after the base delay. */
    const uint8_t a2dp_disconnected[] = {0xAA,0x00,0x03,0x01,0x08,0x00,0xF4};
    bm83_rx_data(&dev, a2dp_disconnected, sizeof(a2dp_disconnected));
    assert(!dev.a2dp_connected);
    unsigned writes_disconnected = m.writes;
    assert(bm83_service_connection(&dev) == DEV_EBUSY);
    m.now_ms += BM83_RECONNECT_BASE_MS;
    assert(bm83_service_connection(&dev) == DEV_OK);
    assert(m.writes == writes_disconnected + 1U);
    bm83_rx_data(&dev, link_back_ack, sizeof(link_back_ack));

    /* Returning to standby terminates the page immediately. Do not sit in the
     * reconnect-in-progress state for the full watchdog timeout. */
    const uint8_t standby[] = {0xAA,0x00,0x03,0x01,0x0F,0x00,0xED};
    bm83_rx_data(&dev, standby, sizeof(standby));
    assert(!dev.reconnect_in_progress);
    assert(!dev.a2dp_connected && !dev.avrcp_connected);
    assert(g_bm83_diag.reconnect_standby_retry_count == 1U);
    unsigned writes_after_standby = m.writes;
    assert(bm83_service_connection(&dev) == DEV_EBUSY);
    assert(m.writes == writes_after_standby);
    m.now_ms += BM83_RECONNECT_BASE_MS;
    assert(bm83_service_connection(&dev) == DEV_OK);
    assert(memcmp(m.frame, expected_device_back, sizeof(expected_device_back)) == 0);
    bm83_rx_data(&dev, link_back_ack, sizeof(link_back_ack));

    /* While a page is in progress, poll Read_Link_Status so missed BTM_Status
     * events cannot leave the driver blind for 12 seconds. The reply is also
     * authoritative enough to reconcile both A2DP and AVRCP state. */
    m.now_ms += BM83_RECONNECT_STATUS_POLL_MS;
    assert(bm83_service_connection(&dev) == DEV_EBUSY);
    const uint8_t expected_link_status[] = {0xAA,0x00,0x02,0x0D,0x00,0xF1};
    assert(m.frame_len == sizeof(expected_link_status));
    assert(memcmp(m.frame, expected_link_status, sizeof(expected_link_status)) == 0);
    assert(g_bm83_diag.reconnect_status_poll_count == 1U);
    const uint8_t link_status_ack[] = {0xAA,0x00,0x03,0x00,0x0D,0x00,0xF0};
    bm83_rx_data(&dev, link_status_ack, sizeof(link_status_ack));
    const uint8_t linked_status[] = {
        0xAA,0x00,0x08,0x1E,0x06,0x05,0x00,0x00,0x00,0x00,0x00,0xCF
    };
    bm83_rx_data(&dev, linked_status, sizeof(linked_status));
    assert(dev.a2dp_connected && dev.avrcp_connected);
    assert(dev.database_index == 0U);
    assert(!dev.reconnect_in_progress && dev.reconnect_attempts == 0U);

    /* Command-disallowed is normal during a connection race. It schedules a
     * retry and must not latch a fatal transport error into the application. */
    bm83_rx_data(&dev, a2dp_disconnected, sizeof(a2dp_disconnected));
    m.now_ms += BM83_RECONNECT_BASE_MS;
    assert(bm83_service_connection(&dev) == DEV_OK);
    const uint8_t link_disallowed[] = {0xAA,0x00,0x03,0x00,0x17,0x01,0xE5};
    bm83_rx_data(&dev, link_disallowed, sizeof(link_disallowed));
    assert(!dev.awaiting_command_ack);
    assert(!dev.reconnect_in_progress);
    assert(bm83_service(&dev) == DEV_OK);

    /* A reconnect which gets no ACK twice is backed off rather than resetting
     * BM83 into a state that would require another explicit power-on MMI. */
    m.now_ms += BM83_RECONNECT_MAX_MS;
    assert(bm83_service_connection(&dev) == DEV_OK);
    assert(dev.awaiting_command_ack);
    unsigned resets_before_timeout = m.reset_lows;
    m.now_ms += BM83_COMMAND_ACK_TIMEOUT_MS;
    assert(bm83_service(&dev) == DEV_EBUSY); /* exact retransmission */
    m.now_ms += BM83_COMMAND_ACK_TIMEOUT_MS;
    assert(bm83_service(&dev) == DEV_EBUSY); /* backoff, no reset */
    assert(!dev.awaiting_command_ack && !dev.reconnect_in_progress);
    assert(m.reset_lows == resets_before_timeout);

    bm83_set_auto_reconnect(&dev, false);
    unsigned writes_disabled = m.writes;
    m.now_ms += BM83_RECONNECT_MAX_MS + 1000U;
    assert(bm83_service_connection(&dev) == DEV_OK);
    assert(m.writes == writes_disabled);

    /* If normal and exact-address link-back are both accepted but make no
     * progress, perform one documented stale-page/A2DP cleanup and then force
     * the next retry to the stored address. Finally stop paging after a bounded
     * number of failures instead of repeating forever. */
    bm_mock_t stale_m = {0};
    dev_uart_bus_t stale_uart = { .ctx = &stale_m, .write = mock_uart_write };
    dev_gpio_t stale_mfb = { .ctx = &stale_m, .write = mock_gpio_write };
    dev_gpio_t stale_reset = { .ctx = &stale_m, .write = mock_reset_write };
    dev_clock_t stale_clock = {
        .ctx = &stale_m, .delay_ms = mock_delay, .time_ms = mock_time
    };
    bm83_t stale;
    assert(bm83_init(&stale, &stale_uart, &stale_reset, &stale_mfb, NULL,
                     &stale_clock) == DEV_OK);
    stale.paired_newest_valid = true;
    stale.paired_newest_record_index = 0U;
    const uint8_t stale_addr[6] = {0xA6,0xA5,0xA4,0xA3,0xA2,0xA1};
    memcpy(stale.paired_newest_address, stale_addr, sizeof(stale_addr));
    bm83_set_auto_reconnect(&stale, true);
    stale.reconnect_attempts = BM83_RECONNECT_STALE_CLEANUP_ATTEMPT;

    assert(bm83_service_connection(&stale) == DEV_OK);
    const uint8_t expected_cleanup[] = {0xAA,0x00,0x02,0x18,0x05,0xE1};
    assert(stale_m.frame_len == sizeof(expected_cleanup));
    assert(memcmp(stale_m.frame, expected_cleanup, sizeof(expected_cleanup)) == 0);
    assert(stale.reconnect_stale_cleanup_used && stale.reconnect_force_address);
    assert(g_bm83_diag.reconnect_stale_cleanup_count == 1U);
    const uint8_t cleanup_ack[] = {0xAA,0x00,0x03,0x00,0x18,0x00,0xE5};
    bm83_rx_data(&stale, cleanup_ack, sizeof(cleanup_ack));
    assert(!stale.awaiting_command_ack);

    stale_m.now_ms += BM83_RECOVERY_SETTLE_MS - 1U;
    assert(bm83_service_connection(&stale) == DEV_EBUSY);
    ++stale_m.now_ms;
    assert(bm83_service_connection(&stale) == DEV_OK);
    const uint8_t expected_stale_address_back[] = {
        0xAA,0x00,0x0A,0x17,0x05,0x00,0x04,
        0xA6,0xA5,0xA4,0xA3,0xA2,0xA1,0x01
    };
    assert(stale_m.frame_len == sizeof(expected_stale_address_back));
    assert(memcmp(stale_m.frame, expected_stale_address_back,
                  sizeof(expected_stale_address_back)) == 0);
    assert(!stale.reconnect_force_address);

    bm83_rx_data(&stale, link_back_ack, sizeof(link_back_ack));
    stale.reconnect_in_progress = false;
    stale.reconnect_attempts = BM83_RECONNECT_MAX_ATTEMPTS;
    stale.reconnect_due_ms = stale_m.now_ms;
    unsigned writes_before_exhausted = stale_m.writes;
    assert(bm83_service_connection(&stale) == DEV_OK);
    assert(stale.reconnect_exhausted);
    assert(g_bm83_diag.reconnect_exhausted_count == 1U);
    stale_m.now_ms += BM83_RECONNECT_MAX_MS * 4U;
    assert(bm83_service_connection(&stale) == DEV_OK);
    assert(stale_m.writes == writes_before_exhausted);

    bm83_set_auto_reconnect(&stale, false);
    bm83_set_auto_reconnect(&stale, true);
    assert(!stale.reconnect_exhausted && !stale.reconnect_stale_cleanup_used);
    assert(stale.reconnect_attempts == 0U);
}

static void test_bt_rx_watchdog(void)
{
    bt_rx_watchdog_t watchdog;
    bt_rx_watchdog_reset(&watchdog, 100U, 10U);

    /* Merely selecting Bluetooth with no I2S activity is not a recovery
     * condition. This prevents periodic DMA stop/start while a phone is paused
     * before playback has ever begun. */
    assert(!bt_rx_watchdog_should_rearm(&watchdog, 10000U, 10U,
                                        true, false, false));

    bt_rx_watchdog_arm_rebuffer(&watchdog, 200U, 10U);
    assert(!bt_rx_watchdog_should_rearm(&watchdog, 1199U, 10U,
                                        true, false, false));
    assert(bt_rx_watchdog_should_rearm(&watchdog, 1200U, 10U,
                                       true, false, false));
    assert(watchdog.rearm_attempts == 1U);

    /* During an intentional pause, retries are rate-limited and back off. */
    assert(!bt_rx_watchdog_should_rearm(&watchdog, 3199U, 10U,
                                        true, false, false));
    assert(bt_rx_watchdog_should_rearm(&watchdog, 3200U, 10U,
                                       true, false, false));
    assert(watchdog.rearm_attempts == 2U);
    assert(!bt_rx_watchdog_should_rearm(&watchdog, 7199U, 10U,
                                        true, false, false));
    assert(bt_rx_watchdog_should_rearm(&watchdog, 7200U, 10U,
                                       true, false, false));

    /* Once SPI2 reports received data/OVR, a stale DMA can be retried quickly
     * rather than waiting the full quiet-pause backoff interval. */
    assert(!bt_rx_watchdog_should_rearm(&watchdog, 7699U, 10U,
                                        true, false, true));
    assert(bt_rx_watchdog_should_rearm(&watchdog, 7700U, 10U,
                                       true, false, true));

    /* One callback resets stall timing/backoff but does not by itself prove a
     * complete recovery: the owner keeps the watch armed until codec TX starts. */
    assert(!bt_rx_watchdog_should_rearm(&watchdog, 7710U, 11U,
                                        true, false, true));
    assert(watchdog.recovery_armed);
    assert(watchdog.rearm_attempts == 0U);
    assert(watchdog.last_progress_ms == 7710U);
    assert(bt_rx_watchdog_should_rearm(&watchdog, 8710U, 11U,
                                       true, false, true));

    bt_rx_watchdog_reset(&watchdog, 8720U, 11U);
    assert(!watchdog.recovery_armed);

    /* A FIFO which has reached the start threshold or a running codec side is
     * not the starved/rebuffer state that this watchdog is allowed to disturb. */
    bt_rx_watchdog_arm_rebuffer(&watchdog, 8000U, 11U);
    assert(!bt_rx_watchdog_should_rearm(&watchdog, 9000U, 11U,
                                        false, false, true));
    assert(!bt_rx_watchdog_should_rearm(&watchdog, 9000U, 11U,
                                        true, true, true));
}

typedef struct {
    uint8_t regs[0x15];
    unsigned writes;
} bq_mock_t;

static dev_status_t mock_i2c_write(void *ctx, uint8_t addr7, const uint8_t *data,
                                   size_t len, uint32_t timeout_ms)
{
    bq_mock_t *m = ctx;
    (void)timeout_ms;
    assert(addr7 == BQ25895_I2C_ADDR_DEFAULT);
    assert(data != NULL && len == 2U);
    assert(data[0] < sizeof(m->regs));
    m->regs[data[0]] = data[1];
    ++m->writes;
    return DEV_OK;
}

static dev_status_t mock_i2c_read(void *ctx, uint8_t addr7, uint8_t *data,
                                  size_t len, uint32_t timeout_ms)
{
    (void)ctx; (void)addr7; (void)data; (void)len; (void)timeout_ms;
    return DEV_ENOTSUP;
}

static dev_status_t mock_i2c_write_read(void *ctx, uint8_t addr7,
                                        const uint8_t *tx, size_t tx_len,
                                        uint8_t *rx, size_t rx_len,
                                        uint32_t timeout_ms)
{
    bq_mock_t *m = ctx;
    (void)timeout_ms;
    assert(addr7 == BQ25895_I2C_ADDR_DEFAULT);
    assert(tx != NULL && tx_len == 1U && rx != NULL && rx_len == 1U);
    assert(tx[0] < sizeof(m->regs));
    rx[0] = m->regs[tx[0]];
    return DEV_OK;
}

static void test_bq25895_5v_input_profile(void)
{
    bq_mock_t m = {0};
    /* Datasheet reset values relevant to this test: REG00 = EN_ILIM + 500 mA;
     * REG02 = BOOST_FREQ + ICO + HVDCP + MAXC + AUTO_DPDM. */
    m.regs[0x00] = 0x48U;
    m.regs[0x02] = 0x3DU;
    m.regs[0x03] = 0x30U;
    m.regs[0x07] = 0x9DU;
    dev_i2c_bus_t bus = {
        .ctx = &m,
        .write = mock_i2c_write,
        .read = mock_i2c_read,
        .write_read = mock_i2c_write_read,
    };
    bq25895_t dev;
    assert(bq25895_init(&dev, &bus, BQ25895_I2C_ADDR_DEFAULT) == DEV_OK);
    assert(bq25895_configure_5v_only_input(&dev, 500U) == DEV_OK);
    assert((m.regs[0x02] & 0x0FU) == 0U); /* no HVDCP/MAXC/FORCE/AUTO DPDM */
    assert((m.regs[0x00] & 0x3FU) == 0x08U); /* fixed 500-mA IINLIM */
    assert((m.regs[0x00] & 0x40U) != 0U); /* retain hardware ILIM protection */

    assert(bq25895_set_adc_continuous(&dev, true) == DEV_OK);
    assert((m.regs[0x02] & 0x40U) != 0U);
    assert((m.regs[0x02] & 0x0FU) == 0U); /* ADC enable must not re-enable DPDM */

    /* Host-mode writes start the BQ watchdog. kiku Rev A must disable it so
     * expiry cannot restore automatic high-voltage adapter negotiation. */
    assert(bq25895_set_watchdog_seconds(&dev, 0U) == DEV_OK);
    assert((m.regs[0x07] & 0x30U) == 0U);

    /* CHG_CONFIG is bit 4 only. Disabling charging must leave OTG_CONFIG (bit 5)
     * untouched rather than clearing the whole two-bit field. */
    assert(bq25895_set_charge_enable(&dev, false) == DEV_OK);
    assert((m.regs[0x03] & 0x10U) == 0U);
    assert((m.regs[0x03] & 0x20U) != 0U);
}

typedef struct {
    uint8_t bytes[128];
    unsigned writes;
    bool fail_writes;
} settings_mem_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    app_settings_t payload;
    uint32_t crc32;
} settings_test_record_t;

static uint32_t test_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0U; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1U));
            crc = (crc >> 1) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

static dev_status_t mem_read(void *ctx, uint32_t offset, void *data, size_t len)
{
    settings_mem_t *m = ctx;
    if ((size_t)offset + len > sizeof(m->bytes)) return DEV_EIO;
    memcpy(data, &m->bytes[offset], len);
    return DEV_OK;
}

static dev_status_t mem_write(void *ctx, uint32_t offset, const void *data, size_t len)
{
    settings_mem_t *m = ctx;
    if ((size_t)offset + len > sizeof(m->bytes)) return DEV_EIO;
    ++m->writes;
    if (m->fail_writes) return DEV_EIO;
    memcpy(&m->bytes[offset], data, len);
    return DEV_OK;
}

static void test_settings(void)
{
    settings_mem_t memory = {0};
    settings_storage_t storage = { .ctx=&memory, .read=mem_read, .write=mem_write };
    settings_manager_t a, b;
    assert(settings_init(&a, &storage,
                         UINT32_MAX - (uint32_t)sizeof(settings_test_record_t) + 1U) ==
           DEV_EOVERFLOW);
    assert(settings_init(&a, &storage, 8U) == DEV_OK);
    a.value.volume_percent = 73U;
    a.value.preferred_source = SETTINGS_SOURCE_FM;
    a.value.fm_frequency_10khz = 10410U;
    a.value.output_mode = 3U;
    a.value.power_save_enabled = 1U;
    a.value.shuffle_enabled = 1U;
    assert(settings_save(&a) == DEV_OK);

    assert(settings_init(&b, &storage, 8U) == DEV_OK);
    assert(settings_load(&b) == DEV_OK);
    assert(b.value.volume_percent == 73U);
    assert(b.value.preferred_source == SETTINGS_SOURCE_FM);
    assert(b.value.fm_frequency_10khz == 10410U);
    assert(b.value.output_mode == 3U);
    assert(b.value.power_save_enabled == 1U);
    assert(b.value.shuffle_enabled == 1U);

    /* A corrupt/torn primary must recover the synchronised secondary copy and
     * schedule a later repair of the primary. */
    memory.bytes[12] ^= 0x55U;
    assert(settings_load(&b) == DEV_OK);
    assert(b.value.volume_percent == 73U);
    assert(b.dirty);

    /* If neither CRC-protected copy is usable, fall back to safe defaults. */
    memory.bytes[8U + sizeof(settings_test_record_t) + 4U] ^= 0x55U;
    assert(settings_load(&b) == DEV_ECRC);
    assert(b.value.volume_percent == 45U); /* safe defaults restored */

    /* A record can have a perfectly valid CRC while containing stale/corrupt
     * enum and range values. Loading it must never expose those values to the
     * application, and the repaired record should be scheduled for persistence. */
    settings_test_record_t invalid = {0};
    invalid.magic = 0x424C4F42UL;
    invalid.version = 1U;
    invalid.payload_size = (uint16_t)sizeof(app_settings_t);
    settings_defaults(&invalid.payload);
    invalid.payload.volume_percent = 255U;
    invalid.payload.brightness_percent = 200U;
    invalid.payload.preferred_source = 99U;
    invalid.payload.fm_region = 7U;
    invalid.payload.fm_frequency_10khz = 1U;
    invalid.payload.speaker_enabled = 9U;
    invalid.payload.output_mode = 8U;
    invalid.payload.power_save_enabled = 4U;
    invalid.payload.shuffle_enabled = 2U;
    memset(invalid.payload.reserved, 0xA5, sizeof(invalid.payload.reserved));
    invalid.crc32 = test_crc32((const uint8_t *)&invalid,
                               offsetof(settings_test_record_t, crc32));
    memset(&memory, 0, sizeof(memory));
    memcpy(&memory.bytes[8], &invalid, sizeof(invalid));
    assert(settings_init(&b, &storage, 8U) == DEV_OK);
    assert(settings_load(&b) == DEV_OK);
    assert(b.value.volume_percent == 100U);
    assert(b.value.brightness_percent == 100U);
    assert(b.value.preferred_source == SETTINGS_SOURCE_LOCAL);
    assert(b.value.fm_region == 0U);
    assert(b.value.fm_frequency_10khz == APP_FM_DEFAULT_10KHZ);
    assert(b.value.speaker_enabled == 1U);
    assert(b.value.output_mode == 0U);
    assert(b.value.power_save_enabled == 1U);
    assert(b.value.shuffle_enabled == 1U);
    for (size_t i = 0U; i < sizeof(b.value.reserved); ++i) assert(b.value.reserved[i] == 0U);
    assert(b.dirty);

    /* Saving also canonicalises values created by a future runtime bug before
     * they become a CRC-valid invalid record on disk. */
    b.value.preferred_source = 250U;
    b.value.fm_frequency_10khz = UINT16_MAX;
    b.value.speaker_enabled = 3U;
    assert(settings_save(&b) == DEV_OK);
    assert(b.value.preferred_source == SETTINGS_SOURCE_LOCAL);
    assert(b.value.fm_frequency_10khz == APP_FM_DEFAULT_10KHZ);
    assert(b.value.speaker_enabled == 1U);

    /* A failed delayed save remains dirty but backs off instead of retrying on
     * every 10 ms application tick while the SD card is absent/faulted. */
    b.value.volume_percent = 55U;
    settings_mark_dirty(&b, 100U);
    memory.fail_writes = true;
    unsigned writes_before = memory.writes;
    assert(settings_save_if_due(&b, 1600U, 1500U) == DEV_EIO);
    assert(b.dirty && b.dirty_since_ms == 1600U);
    assert(memory.writes == writes_before + 1U);
    assert(settings_save_if_due(&b, 1610U, 1500U) == DEV_EBUSY);
    assert(memory.writes == writes_before + 1U);
    assert(settings_save_if_due(&b, 3100U, 1500U) == DEV_EIO);
    assert(memory.writes == writes_before + 2U);
}

typedef struct {
    const uint8_t *bytes;
    size_t size;
    uint32_t pos;
    bool fail_reads;
} playback_file_mock_t;

static dev_status_t playback_open(void *ctx, const char *path, void **file)
{
    playback_file_mock_t *m = ctx;
    (void)path;
    if (m == NULL || file == NULL) return DEV_EINVAL;
    m->pos = 0U;
    *file = m;
    return DEV_OK;
}

static dev_status_t playback_read(void *ctx, void *file, void *data,
                                  size_t requested, size_t *read_count)
{
    playback_file_mock_t *m = ctx;
    assert(file == m);
    if (m == NULL || data == NULL || read_count == NULL) return DEV_EINVAL;
    if (m->fail_reads) return DEV_EIO;
    if (m->pos >= m->size) {
        *read_count = 0U;
        return DEV_OK;
    }
    size_t available = m->size - m->pos;
    size_t n = requested < available ? requested : available;
    memcpy(data, &m->bytes[m->pos], n);
    m->pos += (uint32_t)n;
    *read_count = n;
    return DEV_OK;
}

static dev_status_t playback_seek(void *ctx, void *file, uint32_t offset)
{
    playback_file_mock_t *m = ctx;
    assert(file == m);
    if (m == NULL) return DEV_EINVAL;
    m->pos = offset;
    return DEV_OK;
}

static void playback_close(void *ctx, void *file)
{
    assert(file == ctx);
}

static dev_status_t playback_sink(void *ctx, const int16_t *pcm, size_t frames,
                                  uint16_t channels, uint32_t sample_rate_hz)
{
    (void)ctx; (void)pcm; (void)frames; (void)channels; (void)sample_rate_hz;
    return DEV_OK;
}

typedef struct {
    unsigned calls;
    unsigned busy_writes;
    unsigned accepted;
    int16_t accepted_first_sample[4];
} playback_sink_mock_t;

static dev_status_t playback_sink_with_backpressure(void *ctx, const int16_t *pcm,
                                                     size_t frames, uint16_t channels,
                                                     uint32_t sample_rate_hz)
{
    playback_sink_mock_t *m = ctx;
    assert(m != NULL && pcm != NULL && frames != 0U);
    assert(channels == 2U && sample_rate_hz == 48000U);
    ++m->calls;
    if (m->busy_writes != 0U) {
        --m->busy_writes;
        return DEV_EBUSY;
    }
    assert(m->accepted < sizeof(m->accepted_first_sample) / sizeof(m->accepted_first_sample[0]));
    m->accepted_first_sample[m->accepted++] = pcm[0];
    return DEV_OK;
}

typedef struct {
    unsigned calls;
} playback_mp3_mock_t;

static dev_status_t playback_mp3_reset(void *ctx)
{
    playback_mp3_mock_t *m = ctx;
    if (m != NULL) m->calls = 0U;
    return DEV_OK;
}

static dev_status_t playback_mp3_decode(void *ctx, const uint8_t *input, size_t input_len,
                                        size_t *consumed, int16_t *pcm,
                                        size_t pcm_capacity_frames, size_t *frames,
                                        local_audio_format_t *format)
{
    playback_mp3_mock_t *m = ctx;
    assert(m != NULL && input != NULL && consumed != NULL && pcm != NULL &&
           frames != NULL && format != NULL);
    assert(pcm_capacity_frames >= 1U);
    ++m->calls;
    *consumed = 0U;
    *frames = 0U;
    if (input_len < 4U || input[0] != 0xAAU) return DEV_EBUSY;

    *consumed = 4U;
    *frames = 1U;
    format->channels = 2U;
    format->sample_rate_hz = 48000U;
    format->bits_per_sample = 16U;
    pcm[0] = (int16_t)input[1];
    pcm[1] = (int16_t)input[1];
    return DEV_OK;
}

static void put_u32le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

static void test_local_playback_rejects_malformed_wav(void)
{
    local_pcm_sink_t sink = { .ctx = NULL, .write = playback_sink };
    local_fs_t fs = {
        .open = playback_open,
        .read = playback_read,
        .seek = playback_seek,
        .close = playback_close,
    };

    uint8_t overflow_wav[20] = {0};
    memcpy(&overflow_wav[0], "RIFF", 4U);
    memcpy(&overflow_wav[8], "WAVE", 4U);
    memcpy(&overflow_wav[12], "JUNK", 4U);
    put_u32le(&overflow_wav[16], 0xFFFFFFF8UL);
    playback_file_mock_t overflow_file = {
        .bytes = overflow_wav,
        .size = sizeof(overflow_wav),
    };
    fs.ctx = &overflow_file;
    local_playback_t player;
    assert(local_playback_init(&player, &fs, &sink, NULL) == DEV_OK);
    assert(local_playback_open(&player, "bad.wav") == DEV_EOVERFLOW);
    assert(player.file == NULL);

    uint8_t rate_wav[44] = {0};
    memcpy(&rate_wav[0], "RIFF", 4U);
    memcpy(&rate_wav[8], "WAVE", 4U);
    memcpy(&rate_wav[12], "fmt ", 4U);
    put_u32le(&rate_wav[16], 16U);
    rate_wav[20] = 1U;  /* PCM */
    rate_wav[22] = 2U;  /* stereo */
    put_u32le(&rate_wav[24], 192000U);
    rate_wav[32] = 4U;  /* block align */
    rate_wav[34] = 16U; /* bits/sample */
    memcpy(&rate_wav[36], "data", 4U);
    playback_file_mock_t rate_file = { .bytes = rate_wav, .size = sizeof(rate_wav) };
    fs.ctx = &rate_file;
    assert(local_playback_init(&player, &fs, &sink, NULL) == DEV_OK);
    assert(local_playback_open(&player, "too-fast.wav") == DEV_ENOTSUP);
    assert(player.file == NULL);

    /* Once an already-open card disappears, one processing failure closes the
     * stale stream rather than retrying the invalid FatFs object every tick. */
    put_u32le(&rate_wav[24], 48000U);
    put_u32le(&rate_wav[40], 4U);
    uint8_t playable_wav[48];
    memcpy(playable_wav, rate_wav, sizeof(rate_wav));
    playable_wav[44] = 0x01U;
    playable_wav[45] = 0x00U;
    playable_wav[46] = 0x01U;
    playable_wav[47] = 0x00U;
    playback_file_mock_t removed_file = {
        .bytes = playable_wav,
        .size = sizeof(playable_wav),
    };
    fs.ctx = &removed_file;
    assert(local_playback_init(&player, &fs, &sink, NULL) == DEV_OK);
    assert(local_playback_open(&player, "removed.wav") == DEV_OK);
    assert(local_playback_set_playing(&player, true) == DEV_OK);
    removed_file.fail_reads = true;
    assert(local_playback_process(&player) == DEV_EIO);
    assert(player.file == NULL);
    assert(!player.playing);
}

static void test_local_mp3_streaming_eof_and_backpressure(void)
{
    /* Two synthetic decoder frames followed by a short non-audio tail. This
     * reproduces the important state transitions without depending on a large
     * binary MP3 fixture: sink back-pressure must not consume/drop decoded PCM,
     * and an undecodable tail after physical EOF must finish rather than spin
     * DEV_EBUSY forever. */
    static const uint8_t file_bytes[] = {
        0xAAU, 0x11U, 0x00U, 0x00U,
        0xAAU, 0x22U, 0x00U, 0x00U,
        'T', 'A', 'G'
    };
    playback_file_mock_t file = {
        .bytes = file_bytes,
        .size = sizeof(file_bytes),
    };
    local_fs_t fs = {
        .ctx = &file,
        .open = playback_open,
        .read = playback_read,
        .seek = playback_seek,
        .close = playback_close,
    };
    playback_sink_mock_t sink_state = { .busy_writes = 1U };
    local_pcm_sink_t sink = {
        .ctx = &sink_state,
        .write = playback_sink_with_backpressure,
    };
    playback_mp3_mock_t decoder_state = {0};
    local_mp3_decoder_t decoder = {
        .ctx = &decoder_state,
        .reset = playback_mp3_reset,
        .decode = playback_mp3_decode,
    };

    local_playback_t player;
    assert(sizeof(player.input_buffer) >= 16U * 1024U);
    assert(local_playback_init(&player, &fs, &sink, &decoder) == DEV_OK);
    assert(local_playback_open(&player, "stream.mp3") == DEV_OK);
    assert(local_playback_set_playing(&player, true) == DEV_OK);

    /* First decoded frame hits a full FIFO. It must remain pending. */
    assert(local_playback_process(&player) == DEV_EBUSY);
    assert(decoder_state.calls == 1U);
    assert(player.pending_frames == 1U);
    assert(player.playing && !player.eof);

    /* Retrying flushes exactly that frame without decoding/consuming another. */
    assert(local_playback_process(&player) == DEV_OK);
    assert(decoder_state.calls == 1U);
    assert(player.pending_frames == 0U);
    assert(sink_state.accepted == 1U);
    assert(sink_state.accepted_first_sample[0] == 0x11);

    assert(local_playback_process(&player) == DEV_OK);
    assert(sink_state.accepted == 2U);
    assert(sink_state.accepted_first_sample[1] == 0x22);

    /* The final TAG bytes cannot make a frame. Once the file read has reached
     * EOF this is a clean track end, not a permanent decoder-starvation state. */
    assert(local_playback_process(&player) == DEV_OK);
    assert(player.eof);
    assert(!player.playing);
    assert(player.file != NULL); /* app owns track transition/close */
}

static void test_ui_shuffle_is_local_to_sd_menu(void)
{
    app_settings_t settings;
    settings_defaults(&settings);
    ui_state_t ui;
    ui_state_init(&ui, &settings);
    st7789_t lcd = { .width = UI_TEST_WIDTH, .height = UI_TEST_HEIGHT };

    /* Global Settings must not change visually when only SD shuffle changes. */
    ui.screen = UI_SCREEN_SETTINGS;
    ui.menu_index = 0U;
    ui.shuffle_enabled = false;
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    assert(s_ui_pixel_count == UI_TEST_WIDTH * UI_TEST_HEIGHT);
    uint32_t settings_shuffle_off = ui_frame_hash();
    ui.shuffle_enabled = true;
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    uint32_t settings_shuffle_on = ui_frame_hash();
    assert(settings_shuffle_off == settings_shuffle_on);

    /* The SD/library menu owns the shuffle control and must show its state. */
    ui.screen = UI_SCREEN_LIBRARY;
    ui.menu_index = 1U;
    ui.storage_mounted = true;
    ui_state_set_track(&ui, "Test Track", "SD CARD", "");
    ui.shuffle_enabled = false;
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    uint32_t library_shuffle_off = ui_frame_hash();
    ui.shuffle_enabled = true;
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    uint32_t library_shuffle_on = ui_frame_hash();
    assert(library_shuffle_off != library_shuffle_on);
}

static void test_ui_power_animation_is_continuous(void)
{
    app_settings_t settings;
    settings_defaults(&settings);
    ui_state_t ui;
    ui_state_init(&ui, &settings);
    st7789_t lcd = { .width = UI_TEST_WIDTH, .height = UI_TEST_HEIGHT };

    ui_renderer_invalidate();
    ui.screen = UI_SCREEN_POWERING_ON;
    const uint16_t samples[] = { 0U, 100U, 250U, 400U, 550U, 700U, 1000U };
    uint32_t hashes[sizeof(samples) / sizeof(samples[0])];
    for (size_t i = 0U; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        ui.animation_progress = samples[i];
        assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
        if (i == 0U) {
            assert(s_ui_pixel_count == UI_TEST_WIDTH * UI_TEST_HEIGHT);
            /* The first powered-on frame must already contain the full mark;
             * luminance fading is handled by the physical backlight. This
             * guards against the old double-fade that looked like no animation. */
            size_t paper = 0U, ink = 0U;
            for (size_t px = 0U; px < UI_TEST_WIDTH * UI_TEST_HEIGHT; ++px) {
                if (s_ui_pixels[px] == 0xFFFFU) ++paper;
                else if (s_ui_pixels[px] == 0x0000U) ++ink;
            }
            assert(paper > 0U && ink > 0U);
        }
        else assert(s_ui_pixel_count > 0U && s_ui_pixel_count < UI_TEST_WIDTH * UI_TEST_HEIGHT);
        hashes[i] = ui_frame_hash();
        if (i != 0U) assert(hashes[i] != hashes[i - 1U]);
    }

    /* Shutdown is the true visual reverse: a fully-visible frame collapses
     * smoothly to the blank black sleep field. */
    ui.screen = UI_SCREEN_POWERING_OFF;
    ui.animation_progress = 0U;
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    uint32_t off_start = ui_frame_hash();
    ui.animation_progress = 1000U;
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    uint32_t off_end = ui_frame_hash();
    assert(off_start != off_end);
}

static void test_ui_music_transport_glyph_is_action_not_state(void)
{
    app_settings_t settings;
    settings_defaults(&settings);
    ui_state_t ui;
    ui_state_init(&ui, &settings);
    st7789_t lcd = { .width = UI_TEST_WIDTH, .height = UI_TEST_HEIGHT };

    ui.screen = UI_SCREEN_NOW_PLAYING;
    ui.source = AUDIO_SOURCE_LOCAL;
    ui.storage_mounted = true;
    ui_state_set_track(&ui, "Test Track", "Artist", "Album");

    /* While audio is playing the control should advertise PAUSE. The second
     * pause bar occupies x=25 on the first icon row. */
    ui_renderer_invalidate();
    memset(s_ui_pixels, 0xFF, sizeof(s_ui_pixels));
    ui.playing = true;
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    assert(s_ui_pixels[174U * UI_TEST_WIDTH + 25U] == 0x0000U);

    /* While paused the control should advertise PLAY. The triangle's first row
     * is one pixel wide, so the former second-bar location must be paper. */
    ui.playing = false;
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    assert(s_ui_pixels[174U * UI_TEST_WIDTH + 25U] == 0xFFFFU);
}

static void test_ui_metadata_is_render_significant(void)
{
    app_settings_t settings;
    settings_defaults(&settings);
    ui_state_t ui;
    ui_state_init(&ui, &settings);
    st7789_t lcd = { .width = UI_TEST_WIDTH, .height = UI_TEST_HEIGHT };

    /* Metadata that has reached UI state must materially change the rendered
     * Bluetooth and FM screens. */
    ui.screen = UI_SCREEN_BLUETOOTH;
    ui.source = AUDIO_SOURCE_BLUETOOTH;
    ui.bluetooth_connected = true;
    ui.bt_avrcp_connected = true;
    ui_renderer_invalidate();
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    uint32_t bt_empty = ui_frame_hash();
    ui_state_set_track(&ui, "Levitating", "Dua Lipa", "Future Nostalgia");
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    assert(ui_frame_hash() != bt_empty);

    ui.screen = UI_SCREEN_FM;
    ui.source = AUDIO_SOURCE_FM;
    fm_state_reset(&ui.fm, 10170U);
    ui_renderer_invalidate();
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    uint32_t fm_empty = ui_frame_hash();
    strcpy(ui.fm.rtplus_title, "Levitating");
    strcpy(ui.fm.rtplus_artist, "Dua Lipa");
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    assert(ui_frame_hash() != fm_empty);

    memset(ui.fm.rtplus_title, 0, sizeof(ui.fm.rtplus_title));
    memset(ui.fm.rtplus_artist, 0, sizeof(ui.fm.rtplus_artist));
    strcpy(ui.fm.radio_text, "Levitating - Dua Lipa");
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    assert(ui_frame_hash() != fm_empty);
}

static void test_ui_long_metadata_fits_without_overlap(void)
{
    app_settings_t settings;
    settings_defaults(&settings);
    ui_state_t ui;
    ui_state_init(&ui, &settings);
    st7789_t lcd = { .width = UI_TEST_WIDTH, .height = UI_TEST_HEIGHT };

    /* Exercise the longest strings the production metadata buffers can expose.
     * The ST7789 mocks assert every draw window stays on-panel, while a full
     * redraw followed by a dirty redraw proves truncation is deterministic. */
    ui.source = AUDIO_SOURCE_BLUETOOTH;
    ui.screen = UI_SCREEN_BLUETOOTH;
    ui.bluetooth_connected = true;
    ui.bt_avrcp_connected = true;
    ui.volume_percent = 100U;
    ui.track_number = 999U;
    ui.track_total = 999U;
    ui.track_duration_ms = 3599999U;
    memset(ui.track_title, 'T', sizeof(ui.track_title) - 1U);
    memset(ui.track_artist, 'A', sizeof(ui.track_artist) - 1U);
    memset(ui.track_album, 'L', sizeof(ui.track_album) - 1U);
    memset(ui.track_genre, 'G', sizeof(ui.track_genre) - 1U);
    ui.track_title[sizeof(ui.track_title) - 1U] = '\0';
    ui.track_artist[sizeof(ui.track_artist) - 1U] = '\0';
    ui.track_album[sizeof(ui.track_album) - 1U] = '\0';
    ui.track_genre[sizeof(ui.track_genre) - 1U] = '\0';

    ui_renderer_invalidate();
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    assert(s_ui_pixel_count == UI_TEST_WIDTH * UI_TEST_HEIGHT);
    const uint32_t bluetooth_hash = ui_frame_hash();
    s_ui_pixel_count = 0U;
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    assert(s_ui_pixel_count == 0U);

    /* Local now-playing uses the same dense lower metadata region but adds the
     * play/pause glyph and optional shuffle label. It must remain bounds-safe
     * and visually distinct from the Bluetooth screen at maximum text length. */
    ui.source = AUDIO_SOURCE_LOCAL;
    ui.screen = UI_SCREEN_NOW_PLAYING;
    ui.storage_mounted = true;
    ui.playing = true;
    ui.shuffle_enabled = true;
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    assert(ui_frame_hash() != bluetooth_hash);
}
static void test_ui_all_screens_and_dirty_render_equivalence(void)
{
    app_settings_t settings;
    settings_defaults(&settings);
    ui_state_t ui;
    ui_state_init(&ui, &settings);
    st7789_t lcd = { .width = UI_TEST_WIDTH, .height = UI_TEST_HEIGHT };

    ui.battery_mv = 3980U;
    ui.battery_centi_percent = 7340U;
    ui.source = AUDIO_SOURCE_FM;
    ui.output = AUDIO_OUTPUT_AUTO;
    ui.volume_percent = 42U;
    ui.storage_mounted = true;
    ui.bluetooth_connected = true;
    ui.bt_avrcp_connected = true;
    ui.fm.frequency_10khz = 9690U;
    ui.fm.stereo = true;
    ui.fm.rssi_dbuv = 18U;
    ui.animation_progress = 500U;
    ui_state_set_track(&ui, "Production Test", "kiku", "Walkman");

    /* Every production screen must fit the 320x240 target without an invalid
     * draw/window request. A forced first frame is deliberately full-screen;
     * an unchanged second frame must issue no LCD transfer at all. */
    for (unsigned screen = (unsigned)UI_SCREEN_HOME;
         screen <= (unsigned)UI_SCREEN_ERROR; ++screen) {
        ui.screen = (ui_screen_t)screen;
        ui_renderer_invalidate();
        s_ui_pixel_count = 0U;
        assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
        assert(s_ui_pixel_count == UI_TEST_WIDTH * UI_TEST_HEIGHT);
        s_ui_pixel_count = 0U;
        assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
        assert(s_ui_pixel_count == 0U);
    }

    /* Partial updates must produce exactly the same final panel contents as a
     * clean full-frame render of that state. This catches dirty-rectangle bugs
     * that otherwise leave old menu text or transition pixels behind. */
    ui.screen = UI_SCREEN_HOME;
    ui_renderer_invalidate();
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    ui.screen = UI_SCREEN_FM;
    ui.animation_progress = 0U;
    s_ui_pixel_count = 0U;
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    assert(s_ui_pixel_count > 0U);
    uint32_t incremental_hash = ui_frame_hash();

    ui_renderer_invalidate();
    s_ui_pixel_count = 0U;
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    assert(s_ui_pixel_count == UI_TEST_WIDTH * UI_TEST_HEIGHT);
    assert(ui_frame_hash() == incremental_hash);
}

static void test_ui_themes_and_fm_control_mode(void)
{
    app_settings_t settings;
    settings_defaults(&settings);
    ui_state_t ui;
    ui_state_init(&ui, &settings);
    st7789_t lcd = { .width = UI_TEST_WIDTH, .height = UI_TEST_HEIGHT };

    ui.screen = UI_SCREEN_HOME;
    ui.theme = UI_THEME_KIKU;
    ui_renderer_invalidate();
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    uint32_t kiku_hash = ui_frame_hash();

    ui.theme = UI_THEME_POLAROID;
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    uint32_t polaroid_hash = ui_frame_hash();
    size_t polaroid_colour_pixels = 0U;
    for (size_t i = 0U; i < UI_TEST_WIDTH * UI_TEST_HEIGHT; ++i) {
        if (s_ui_pixels[i] != 0x0000U && s_ui_pixels[i] != 0xFFFFU) ++polaroid_colour_pixels;
    }
    assert(polaroid_hash != kiku_hash);
    assert(polaroid_colour_pixels > 0U);

    ui.theme = UI_THEME_WALKMAN;
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    uint32_t walkman_hash = ui_frame_hash();
    assert(walkman_hash != polaroid_hash && walkman_hash != kiku_hash);

    /* Every hidden theme deliberately changes layout, not just RGB565 palette.
     * Compare palette-independent occupancy signatures pairwise. */
    const ui_theme_t structural_themes[] = {
        UI_THEME_POLAROID,
        UI_THEME_WALKMAN,
        UI_THEME_INSTRUMENT,
        UI_THEME_MINIDISC,
        UI_THEME_TERMINAL,
        UI_THEME_AQUA,
    };
    uint32_t structures[sizeof(structural_themes) / sizeof(structural_themes[0])];
    for (size_t i = 0U; i < sizeof(structural_themes) / sizeof(structural_themes[0]); ++i) {
        ui.theme = structural_themes[i];
        ui.menu_index = 2U;
        assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
        structures[i] = ui_structure_hash();
        for (size_t j = 0U; j < i; ++j) assert(structures[i] != structures[j]);
    }
    assert(UI_THEME_COUNT == 7);

    /* MiniDisc's lower-left control is a filled ink tab. Its action label must
     * be inverse/paper text; drawing it in ink makes PREV/BACK/SEEK- disappear
     * completely. Keep a framebuffer-level contrast regression for the exact
     * region that previously rendered as one solid colour. */
    ui.theme = UI_THEME_MINIDISC;
    ui.screen = UI_SCREEN_NOW_PLAYING;
    ui.playing = true;
    ui_renderer_invalidate();
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    const uint16_t minidisc_tab_fill = s_ui_pixels[220U * UI_TEST_WIDTH + 10U];
    bool minidisc_tab_has_contrast = false;
    for (size_t y = 220U; y <= 234U && !minidisc_tab_has_contrast; ++y) {
        for (size_t x = 10U; x <= 45U; ++x) {
            if (s_ui_pixels[y * UI_TEST_WIDTH + x] != minidisc_tab_fill) {
                minidisc_tab_has_contrast = true;
                break;
            }
        }
    }
    assert(minidisc_tab_has_contrast);

    /* FM knob mode is intentionally explicit and render-significant: pressing
     * the encoder toggles between frequency and 2%-step volume control. Only
     * the footer hint changes; the FM body no longer repeats that mode. */
    ui.theme = UI_THEME_KIKU;
    ui.screen = UI_SCREEN_FM;
    ui.fm.frequency_10khz = 9690U;
    ui.fm_control_mode = UI_FM_CONTROL_TUNE;
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    uint32_t tune_hash = ui_frame_hash();
    ui.fm_control_mode = UI_FM_CONTROL_VOLUME;
    assert(ui_renderer_draw(&lcd, &ui) == DEV_OK);
    assert(ui_frame_hash() != tune_hash);
}

static void test_rds_program_service(void)
{
    fm_state_t state;
    fm_state_reset(&state, 10170U);
    const char name[9] = "TRIPLE J";
    for (uint8_t seg = 0U; seg < 4U; ++seg) {
        si4705_rds_group_t g = {0};
        g.sync = true;
        g.fifo_used = true;
        g.block_a = 0x1234U;
        g.block_b = seg; /* group 0A, PS segment */
        g.block_d = (uint16_t)(((uint16_t)(uint8_t)name[seg*2U] << 8) |
                               (uint8_t)name[seg*2U+1U]);
        fm_state_process_rds(&state, &g);
        if (seg == 0U) {
            assert(fm_state_has_partial_program_service(&state));
            assert(!fm_state_has_program_service(&state));
        }
    }
    assert(state.ps_valid_mask == 0x0FU);
    assert(strcmp(state.program_service, name) == 0);
    assert(fm_state_has_program_service(&state));

    /* An empty FM_RDS_STATUS read must not look like a PI change and erase the
     * completed PS. BLE2 is corrected/usable and should be accepted too. */
    si4705_rds_group_t empty = {0};
    empty.sync = true;
    fm_state_process_rds(&state, &empty);
    assert(strcmp(state.program_service, name) == 0);
    assert(fm_state_has_program_service(&state));

    fm_state_reset(&state, 10170U);
    for (uint8_t seg = 0U; seg < 4U; ++seg) {
        si4705_rds_group_t g = {0};
        g.sync = true;
        g.fifo_used = true;
        g.ble_a = 2U;
        g.ble_b = 2U;
        g.ble_d = 2U;
        g.block_a = 0x1234U;
        g.block_b = seg;
        g.block_d = (uint16_t)(((uint16_t)(uint8_t)name[seg*2U] << 8) |
                               (uint8_t)name[seg*2U+1U]);
        fm_state_process_rds(&state, &g);
    }
    assert(fm_state_has_program_service(&state));
}

static void test_rds_repeated_and_invalid_text(void)
{
    fm_state_t state;
    fm_state_reset(&state, 10170U);
    si4705_rds_group_t g = {0};
    g.sync = true; g.fifo_used = true; g.block_a = 0x1234U;
    /* Receive 2B segments backwards, then repeat the first pair. */
    g.block_b = 0x2801U; g.block_d = 0x4E47U; /* NG */
    fm_state_process_rds(&state, &g);
    g.block_b = 0x2800U; g.block_d = 0x534FU; /* SO */
    fm_state_process_rds(&state, &g);
    assert(strcmp(state.radio_text, "SONG") == 0);
    fm_state_process_rds(&state, &g);
    assert(strcmp(state.radio_text, "SONG") == 0);
    /* Corrupt payload with a new A/B flag must not erase usable text. */
    g.block_b = 0x2810U; g.ble_d = 3U;
    fm_state_process_rds(&state, &g);
    assert(strcmp(state.radio_text, "SONG") == 0);
    g.ble_d = 0U; g.block_d = 0x4E45U; /* NE, valid new text */
    fm_state_process_rds(&state, &g);
    assert(strcmp(state.radio_text, "NE") == 0);
    /* Changing 2B to 2A must discard incompatible segment positions. */
    g.block_b = 0x2011U; g.block_c = 0x5445U; g.block_d = 0x5854U;
    fm_state_process_rds(&state, &g);
    assert(!fm_state_has_radio_text(&state));
    g.block_b = 0x2010U; g.block_c = 0x4E45U; g.block_d = 0x5720U;
    fm_state_process_rds(&state, &g);
    assert(strcmp(state.radio_text, "NEW TEXT") == 0);
}

static void test_rds_rtplus(void)
{
    fm_state_t state;
    fm_state_reset(&state, 10410U);

    /* Group 3A announces RT+ AID 0x4BD7 on group 11A (group code 0x16). */
    si4705_rds_group_t oda = {0};
    oda.sync = true; oda.fifo_used = true; oda.block_a = 0x5678U; oda.block_b = 0x3016U; oda.block_d = FM_RTPLUS_AID;
    fm_state_process_rds(&state, &oda);
    assert(state.rtplus_group_valid && state.rtplus_group_code == 0x16U);

    const char text[] = "ARTIST   GREAT SONG TITLE";
    for (uint8_t seg = 0U; seg < 7U; ++seg) {
        char c[4] = {' ', ' ', ' ', ' '};
        for (uint8_t i = 0U; i < 4U; ++i) {
            size_t pos = (size_t)seg * 4U + i;
            if (pos < sizeof(text) - 1U) c[i] = text[pos];
        }
        si4705_rds_group_t rt = {0};
        rt.sync = true; rt.fifo_used = true; rt.block_a = 0x5678U; rt.block_b = (uint16_t)(0x2000U | seg);
        rt.block_c = (uint16_t)(((uint16_t)(uint8_t)c[0] << 8) | (uint8_t)c[1]);
        rt.block_d = (uint16_t)(((uint16_t)(uint8_t)c[2] << 8) | (uint8_t)c[3]);
        fm_state_process_rds(&state, &rt);
    }

    /* item-running=1; title type1/start9/len16; artist type4/start0/len6. */
    si4705_rds_group_t tags = {0};
    tags.sync = true; tags.fifo_used = true; tags.block_a = 0x5678U; tags.block_b = 0xB008U;
    tags.block_c = 0x249EU; tags.block_d = 0x2005U;
    fm_state_process_rds(&state, &tags);
    assert(fm_state_has_rtplus_title(&state));
    assert(fm_state_has_rtplus_artist(&state));
    assert(strcmp(state.rtplus_title, "GREAT SONG TITLE") == 0);
    assert(strcmp(state.rtplus_artist, "ARTIST") == 0);
}

int main(void)
{
    test_input_encoder_long_power_gesture();
    test_bm83();
    test_app_metadata_fallback();
    test_app_metadata_response_scheduler();
    test_app_metadata_refresh_hints_and_track_steps();
    test_bm83_reconnect();
    test_bt_rx_watchdog();
    test_bq25895_5v_input_profile();
    test_settings();
    test_local_playback_rejects_malformed_wav();
    test_local_mp3_streaming_eof_and_backpressure();
    test_ui_shuffle_is_local_to_sd_menu();
    test_ui_power_animation_is_continuous();
    test_ui_music_transport_glyph_is_action_not_state();
    test_ui_metadata_is_render_significant();
    test_ui_long_metadata_fits_without_overlap();
    test_ui_all_screens_and_dirty_render_equivalence();
    test_ui_themes_and_fm_control_mode();
    test_rds_program_service();
    test_rds_repeated_and_invalid_text();
    test_rds_rtplus();
    puts("kiku host tests: PASS");
    return 0;
}
