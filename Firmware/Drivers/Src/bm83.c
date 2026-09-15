#include "bm83.h"

#include <string.h>

volatile bm83_diag_t g_bm83_diag;

enum {
    BM_PARSE_SYNC = 0,
    BM_PARSE_LEN_HI,
    BM_PARSE_LEN_LO,
    BM_PARSE_ID,
    BM_PARSE_PAYLOAD,
    BM_PARSE_CHECKSUM
};

static void bm_parser_reset(bm83_t *dev)
{
    dev->parser_state = BM_PARSE_SYNC;
    dev->expected_len = 0U;
    dev->received_len = 0U;
    dev->checksum = 0U;
    dev->rx.id = 0U;
    dev->rx.payload_len = 0U;
}

static size_t bm_build_frame(uint8_t command_id, const uint8_t *payload,
                             uint16_t payload_len, uint8_t *frame)
{
    uint16_t wire_len = (uint16_t)(payload_len + 1U); /* ID + payload */
    frame[0] = BM83_FRAME_START;
    frame[1] = (uint8_t)(wire_len >> 8);
    frame[2] = (uint8_t)wire_len;
    frame[3] = command_id;
    if (payload_len != 0U) memcpy(&frame[4], payload, payload_len);

    uint8_t sum = (uint8_t)(frame[1] + frame[2] + frame[3]);
    for (uint16_t i = 0U; i < payload_len; ++i) sum = (uint8_t)(sum + payload[i]);
    frame[4U + payload_len] = (uint8_t)(0U - sum);
    return (size_t)payload_len + 5U;
}

static dev_status_t bm_write_frame(bm83_t *dev, const uint8_t *frame, size_t frame_len)
{
    if ((dev == NULL) || (dev->uart.write == NULL) || (frame == NULL) || frame_len == 0U) {
        return DEV_EINVAL;
    }
    /* BM83 does not expose UART_RX_IND. Audio UART Command Set v2.10 section
     * 4.1 explicitly lists UART_RX_IND as N/A for BM83 (MFB is UART_RX_IND on
     * BM62/BM64 only). Toggling MFB around UART traffic therefore generates
     * unintended button activity on BM83 and can break pairing/link-back. */
    return dev->uart.write(dev->uart.ctx, frame, frame_len, dev->timeout_ms);
}

static uint32_t bm_now(const bm83_t *dev)
{
    return (dev != NULL && dev->clock.time_ms != NULL) ?
           dev->clock.time_ms(dev->clock.ctx) : 0U;
}

static bool bm_time_reached(uint32_t now, uint32_t target)
{
    return (int32_t)(now - target) >= 0;
}

static uint32_t bm_reconnect_backoff_ms(const bm83_t *dev)
{
    uint8_t exponent = 0U;
    if (dev != NULL && dev->reconnect_attempts > 1U) {
        exponent = (uint8_t)(dev->reconnect_attempts - 1U);
        if (exponent > 5U) exponent = 5U;
    }
    uint32_t delay = BM83_RECONNECT_BASE_MS << exponent;
    return delay > BM83_RECONNECT_MAX_MS ? BM83_RECONNECT_MAX_MS : delay;
}

static bool bm_command_timeout_is_nonfatal(uint8_t command_id)
{
    /* AVRCP metadata/browsing is auxiliary to the A2DP audio transport. A lost
     * UART ACK here must not escalate into a hardware reset of an otherwise
     * healthy streaming radio. The application can retry these idempotent
     * queries later. User playback controls retain the existing reset policy. */
    return command_id == BM83_CMD_AVC_VENDOR_DEPENDENT ||
           command_id == BM83_CMD_READ_LINK_STATUS ||
           command_id == BM83_CMD_READ_LINKED_DEVICE_INFO ||
           command_id == BM83_CMD_AVRCP_BROWSING ||
           command_id == BM83_CMD_AVRCP_VENDOR_DEPENDENT;
}

static bool bm_command_is_connection_recovery(const bm83_t *dev, uint8_t command_id)
{
    if (dev == NULL) return false;
    if (command_id == BM83_CMD_PROFILES_LINK_BACK) return dev->auto_reconnect;
    if (command_id == BM83_CMD_DISCONNECT) {
        return dev->auto_reconnect && dev->reconnect_stale_cleanup_used &&
               dev->reconnect_force_address;
    }

    /* Audio UART Command Set v2.10 section 5.2.4 explicitly states that a
     * Music_Control command causes BTM to initiate AVRCP when A2DP is already
     * active. service_connection() uses STOP_SEEK for that documented side
     * effect when A2DP is up but AVRCP is not. Treat only that in-progress
     * automatic command as connection recovery; ordinary user music controls
     * keep their existing error behaviour. */
    return command_id == BM83_CMD_MUSIC_CONTROL &&
           dev->avrcp_recovery_command_pending;
}

static void bm_schedule_reconnect(bm83_t *dev, bool immediate)
{
    if (dev == NULL) return;
    dev->reconnect_in_progress = false;
    if (!dev->auto_reconnect || dev->pairing ||
        (dev->a2dp_connected && dev->avrcp_connected)) return;
    const uint32_t now = bm_now(dev);
    dev->reconnect_due_ms = immediate ? now : now + bm_reconnect_backoff_ms(dev);
}

static void bm_reset_reconnect_episode(bm83_t *dev)
{
    if (dev == NULL) return;
    dev->reconnect_in_progress = false;
    dev->avrcp_recovery_command_pending = false;
    dev->reconnect_attempts = 0U;
    dev->reconnect_stale_cleanup_used = false;
    dev->reconnect_force_address = false;
    dev->reconnect_exhausted = false;
}

static void bm_clear_nonfatal_backpressure(bm83_t *dev)
{
    if (dev == NULL) return;
    dev->nonfatal_backpressure_waiting_ready = false;
    dev->nonfatal_backpressure_command_id = 0U;
    dev->nonfatal_backpressure_since_ms = 0U;
}

static void bm_update_connection_state(bm83_t *dev, const bm83_packet_t *packet)
{
    if (dev == NULL || packet == NULL) return;

    if (packet->id == BM83_EVENT_INITIAL_STATUS && packet->payload_len >= 1U) {
        if (packet->payload[0] == 0x00U) dev->initial_status_received = true;
        return;
    }

    if (packet->id == BM83_EVENT_BTM_STATUS && packet->payload_len >= 1U) {
        const uint8_t state = packet->payload[0];
        dev->btm_status_received = true;
        dev->btm_state = state;
        if (packet->payload_len >= 2U) dev->database_index = (uint8_t)(packet->payload[1] & 0x0FU);

        switch (state) {
        case BM83_BTM_PAIRING:
            dev->pairing = true;
            dev->reconnect_in_progress = false;
            break;
        case BM83_BTM_PAIRING_SUCCESS:
            dev->pairing = false;
            bm_reset_reconnect_episode(dev);
            bm_schedule_reconnect(dev, true);
            break;
        case BM83_BTM_PAIRING_FAILED:
            dev->pairing = false;
            bm_schedule_reconnect(dev, false);
            break;
        case BM83_BTM_A2DP_CONNECTED:
            dev->a2dp_connected = true;
            dev->pairing = false;
            bm_reset_reconnect_episode(dev);
            /* A2DP can become ready before its companion AVRCP control link.
             * Give normal BM83/phone profile setup one backoff interval, then
             * service_connection() will use the documented Music_Control
             * AVRCP-link trigger if the 0x0B event still has not arrived. */
            if (!dev->avrcp_connected) bm_schedule_reconnect(dev, false);
            break;
        case BM83_BTM_A2DP_DISCONNECTED:
        case BM83_BTM_ACL_DISCONNECTED:
            dev->a2dp_connected = false;
            dev->avrcp_connected = false;
            bm_clear_nonfatal_backpressure(dev);
            bm_schedule_reconnect(dev, false);
            break;
        case BM83_BTM_AVRCP_CONNECTED:
            dev->avrcp_connected = true;
            bm_reset_reconnect_episode(dev);
            if (!dev->a2dp_connected) bm_schedule_reconnect(dev, false);
            break;
        case BM83_BTM_AVRCP_DISCONNECTED:
            dev->avrcp_connected = false;
            bm_clear_nonfatal_backpressure(dev);
            bm_schedule_reconnect(dev, false);
            break;
        case BM83_BTM_POWER_ON:
            dev->pairing = false;
            dev->a2dp_connected = false;
            dev->avrcp_connected = false;
            bm_clear_nonfatal_backpressure(dev);
            bm_reset_reconnect_episode(dev);
            bm_schedule_reconnect(dev, true);
            break;
        case BM83_BTM_STANDBY:
            /* Standby is definitive evidence that a previous page/link-back is
             * no longer making progress. Some BM83 projects return here without
             * first emitting A2DP/ACL-disconnected or Report_Link_Back_Status.
             * Clear stale profile state and end the attempt immediately instead
             * of waiting the full 12 second reconnect watchdog. */
            dev->pairing = false;
            dev->a2dp_connected = false;
            dev->avrcp_connected = false;
            bm_clear_nonfatal_backpressure(dev);
            if (dev->reconnect_in_progress || dev->reconnect_attempts != 0U) {
                ++g_bm83_diag.reconnect_standby_retry_count;
                bm_schedule_reconnect(dev, false);
            } else {
                bm_schedule_reconnect(dev, true);
            }
            break;
        default:
            break;
        }
        return;
    }

    /* Some BM83 firmware packages establish a usable AVRCP session without
     * emitting BTM_Status 0x0B. Receipt of an AVRCP-specific response is
     * stronger evidence than that optional status indication: the remote
     * control channel is necessarily up if it can carry this traffic. Keep
     * the connection state in sync so metadata recovery does not remain
     * permanently gated after a successful A2DP reconnect. */
    if (dev->a2dp_connected &&
        (packet->id == BM83_EVENT_AVC_VENDOR_DEPENDENT_RESPONSE ||
         packet->id == BM83_EVENT_AVRCP_VENDOR_DEPENDENT_RSP ||
         packet->id == BM83_EVENT_AVRCP_BROWSING)) {
        dev->avrcp_connected = true;
        bm_reset_reconnect_episode(dev);
        return;
    }

    if (packet->id == BM83_EVENT_READ_LINK_STATUS_REPLY && packet->payload_len >= 7U) {
        /* Read_Link_Status is the authoritative snapshot for BM83 projects
         * which occasionally omit one of the asynchronous BTM_Status events.
         * Bits 0/1 are A2DP signalling/stream and bit2 is AVRCP. Reconcile the
         * driver's state so a missed connect event cannot cause needless link
         * backs, and a missed disconnect event cannot suppress recovery. */
        const uint8_t device_state = packet->payload[0];
        const uint8_t db0 = packet->payload[1];
        const uint8_t db1 = packet->payload[2];
        const bool db0_a2dp = (db0 & 0x03U) != 0U;
        const bool db1_a2dp = (db1 & 0x03U) != 0U;
        const bool db0_avrcp = (db0 & 0x04U) != 0U;
        const bool db1_avrcp = (db1 & 0x04U) != 0U;
        const bool a2dp = db0_a2dp || db1_a2dp;
        const bool avrcp = db0_avrcp || db1_avrcp;

        if ((db1_a2dp || db1_avrcp) && !(db0_a2dp || db0_avrcp)) {
            dev->database_index = 1U;
        } else if (db0_a2dp || db0_avrcp) {
            dev->database_index = 0U;
        }
        dev->pairing = device_state == 0x01U;
        dev->a2dp_connected = a2dp;
        dev->avrcp_connected = avrcp;

        /* This authoritative snapshot also closes any metadata transaction
         * resource gate left behind by a missed asynchronous disconnect event.
         * A readiness ACK belongs only to the AVRCP link on which 0x04/0x05 was
         * reported; it must never survive loss of either A2DP or AVRCP. */
        if (!a2dp || !avrcp) bm_clear_nonfatal_backpressure(dev);

        if (a2dp && avrcp) {
            bm_reset_reconnect_episode(dev);
        } else if (dev->auto_reconnect && !dev->pairing) {
            if (device_state == 0x02U && dev->reconnect_in_progress) {
                bm_schedule_reconnect(dev, false);
            } else if (!dev->reconnect_in_progress) {
                bm_schedule_reconnect(dev, false);
            }
        }
        return;
    }

    if (packet->id == BM83_EVENT_LINK_BACK_STATUS && packet->payload_len >= 2U) {
        const uint8_t status = packet->payload[0];
        const uint8_t result = packet->payload[1];
        g_bm83_diag.last_link_back_status = status;
        g_bm83_diag.last_link_back_result = result;
        if (status == BM83_LINK_BACK_STATUS_ACL && result != BM83_LINK_BACK_ACL_FAILED) {
            ++g_bm83_diag.link_back_acl_success_count;
        } else if (status == BM83_LINK_BACK_STATUS_A2DP) {
            if (result == BM83_LINK_BACK_PROFILE_OK) {
                ++g_bm83_diag.link_back_a2dp_success_count;
            } else {
                ++g_bm83_diag.link_back_a2dp_fail_count;
            }
        }
        const bool failed =
            (status == BM83_LINK_BACK_STATUS_ACL && result == BM83_LINK_BACK_ACL_FAILED) ||
            (status == BM83_LINK_BACK_STATUS_A2DP && result != BM83_LINK_BACK_PROFILE_OK);
        if (failed) bm_schedule_reconnect(dev, false);
    }
}

static dev_status_t bm_send_event_ack_raw(bm83_t *dev, uint8_t event_id)
{
    uint8_t frame[6U];
    size_t frame_len = bm_build_frame(BM83_CMD_EVENT_ACK, &event_id, 1U, frame);
    return bm_write_frame(dev, frame, frame_len);
}

dev_status_t bm83_init(bm83_t *dev, const dev_uart_bus_t *uart,
                       const dev_gpio_t *reset_n, const dev_gpio_t *mfb,
                       const dev_gpio_t *tx_ind, const dev_clock_t *clock)
{
    if ((dev == NULL) || (uart == NULL) || (uart->write == NULL)) return DEV_EINVAL;
    memset(dev, 0, sizeof(*dev));
    memset((void *)&g_bm83_diag, 0, sizeof(g_bm83_diag));
    dev->uart = *uart;
    if (reset_n != NULL) dev->reset_n = *reset_n;
    if (mfb != NULL) dev->mfb = *mfb;
    if (tx_ind != NULL) dev->tx_ind = *tx_ind;
    if (clock != NULL) dev->clock = *clock;
    dev->timeout_ms = 100U;
    bm_parser_reset(dev);
    return DEV_OK;
}

void bm83_set_packet_callback(bm83_t *dev, bm83_packet_cb_t cb, void *ctx)
{
    if (dev == NULL) return;
    dev->packet_cb = cb;
    dev->packet_cb_ctx = ctx;
}

dev_status_t bm83_send_packet(bm83_t *dev, uint8_t command_id,
                              const uint8_t *payload, uint16_t payload_len)
{
    if ((dev == NULL) || (dev->uart.write == NULL) ||
        ((payload == NULL) && (payload_len != 0U)) ||
        (payload_len > BM83_MAX_PAYLOAD)) return DEV_EINVAL;

    if (command_id == BM83_CMD_EVENT_ACK) {
        uint8_t frame[BM83_MAX_PAYLOAD + 5U];
        size_t frame_len = bm_build_frame(command_id, payload, payload_len, frame);
        return bm_write_frame(dev, frame, frame_len);
    }
    /* Command_ACK status 0x04/0x05 is not a request to poll/retry. Audio UART
     * Command Set v2.10 section 7.1 says BTM will emit a later Command_ACK with
     * status 0x00 for that same command ID once the resource is available, and
     * only then should the MCU resend. Keep other command IDs usable (transport
     * controls must not be starved by metadata backpressure), but reject a
     * premature resend of the exact backpressured command. */
    if (dev->nonfatal_backpressure_waiting_ready &&
        command_id == dev->nonfatal_backpressure_command_id) return DEV_EBUSY;
    if (dev->awaiting_command_ack) return DEV_EBUSY;

    size_t frame_len = bm_build_frame(command_id, payload, payload_len,
                                      dev->pending_frame);
    dev_status_t st = bm_write_frame(dev, dev->pending_frame, frame_len);
    if (st == DEV_OK) {
        dev->pending_frame_len = (uint16_t)frame_len;
        dev->pending_command_id = command_id;
        dev->retry_count = 0U;
        dev->last_command_ack_status = 0U;
        dev->pending_since_ms = (dev->clock.time_ms != NULL) ?
                                dev->clock.time_ms(dev->clock.ctx) : 0U;
        dev->awaiting_command_ack = true;
        dev->command_error_latched = false;
    }
    return st;
}

bool bm83_command_waiting_ready(const bm83_t *dev, uint8_t command_id)
{
    return dev != NULL && dev->nonfatal_backpressure_waiting_ready &&
           dev->nonfatal_backpressure_command_id == command_id;
}

dev_status_t bm83_music_control(bm83_t *dev, bm83_music_action_t action)
{
    if ((uint8_t)action > (uint8_t)BM83_MUSIC_PREVIOUS) return DEV_EINVAL;
    const uint8_t payload[2] = { 0x00U, (uint8_t)action };
    return bm83_send_packet(dev, BM83_CMD_MUSIC_CONTROL, payload, sizeof(payload));
}

dev_status_t bm83_set_supported_classic_profiles(bm83_t *dev, uint8_t profile_mask)
{
    /* Audio UART Command Set v2.10 section 5.2.7, Parameter 0x04.
     * This setting is runtime-only by default; unlike Parameters 0x01/0x02/0x06
     * the documentation does not mark it as stored in EEPROM. */
    const uint8_t payload[2] = { 0x04U, profile_mask };
    return bm83_send_packet(dev, BM83_CMD_BTM_PARAMETER_SETTING,
                            payload, (uint16_t)sizeof(payload));
}

dev_status_t bm83_unmask_all_events(bm83_t *dev)
{
    /* Audio UART Command Set v2.10 section 5.2.3: a set mask bit suppresses
     * the corresponding UART event. In particular byte 2 bit 2 masks event
     * 0x1A (AVC_Vendor_Dependent_Response), which is how AVRCP 1.3 title,
     * artist and album data returns on the v2.02 command set used by this BM83.
     * The command is runtime-only; explicitly restoring the documented all-zero
     * default prevents a module/config image from accepting metadata commands
     * while silently withholding their response events. */
    static const uint8_t mask[4] = { 0U, 0U, 0U, 0U };
    return bm83_send_packet(dev, BM83_CMD_EVENT_MASK_SETTING, mask, sizeof(mask));
}

dev_status_t bm83_generate_tone(bm83_t *dev, uint8_t tone_type)
{
    /* Audio UART Command Set v2.10, BTM_Utility_Function (0x13), subtype
     * 0x02: generate a one-shot built-in tone. 0x01..0x26 are documented
     * ROM tones; 0x80..0x93 are stored voice prompts. This is runtime-only
     * and does not modify EEPROM or factory configuration. */
    if (!((tone_type >= 0x01U && tone_type <= 0x26U) ||
          (tone_type >= 0x80U && tone_type <= 0x93U))) return DEV_EINVAL;
    const uint8_t payload[2] = { 0x02U, tone_type };
    return bm83_send_packet(dev, BM83_CMD_UTILITY_FUNCTION, payload, sizeof(payload));
}

dev_status_t bm83_read_version(bm83_t *dev, uint8_t type)
{
    if (type > 0x05U) return DEV_EINVAL;
    return bm83_send_packet(dev, BM83_CMD_READ_VERSION, &type, 1U);
}

bool bm83_uart_version_at_least(const bm83_t *dev, uint8_t major, uint8_t minor)
{
    if (dev == NULL || !dev->uart_version_valid) return false;
    return dev->uart_version_major > major ||
           (dev->uart_version_major == major && dev->uart_version_minor >= minor);
}

dev_status_t bm83_read_link_status(bm83_t *dev)
{
    /* Reserved byte is documented as don't-care. Zero is the vendor examples'
     * conventional value and this command is supported from UART v2.00. */
    const uint8_t reserved = 0x00U;
    return bm83_send_packet(dev, BM83_CMD_READ_LINK_STATUS, &reserved, 1U);
}

dev_status_t bm83_read_paired_device_record(bm83_t *dev)
{
    /* Audio UART Command Set v2.10 section 5.2.13. The response reports the
     * Classic paired-device count followed by seven bytes per record: link
     * priority then the six-byte BD address. This is read-only and is useful
     * when a phone claims "connected" while A2DP/AVRCP link-back silently does
     * nothing: it distinguishes a stale phone-side/LE link from a valid Classic
     * pairing record without deleting or rewriting any pairing data. */
    const uint8_t reserved = 0x00U;
    return bm83_send_packet(dev, BM83_CMD_READ_PAIRED_DEVICE_RECORD, &reserved, 1U);
}

dev_status_t bm83_read_remote_avrcp_features(bm83_t *dev, uint8_t database_index)
{
    if (database_index > 1U) return DEV_EINVAL;
    /* Read_Linked_Device_Information type 0x03 is explicitly defined as the
     * read-only AVRCP capability query. Its returned bit0 tells us whether the
     * phone negotiated media-player status notification support; bit1 is
     * absolute volume. This lets live diagnostics distinguish a bad UART
     * metadata path from a link that never negotiated AVRCP 1.3 features. */
    const uint8_t payload[2] = { database_index, 0x03U };
    return bm83_send_packet(dev, BM83_CMD_READ_LINKED_DEVICE_INFO,
                            payload, sizeof(payload));
}

dev_status_t bm83_link_back_last_device(bm83_t *dev)
{
    /* Type 0x00: reconnect the last paired device using the profiles stored
     * in BM83's existing paired-device record. This changes no NVM. */
    const uint8_t type = 0x00U;
    return bm83_send_packet(dev, BM83_CMD_PROFILES_LINK_BACK, &type, 1U);
}

dev_status_t bm83_link_back_last_a2dp(bm83_t *dev)
{
    /* Type 0x02: initiate A2DP connection to the last A2DP device. This is
     * preferable for kiku because it is an A2DP sink, not a headset. */
    const uint8_t type = 0x02U;
    return bm83_send_packet(dev, BM83_CMD_PROFILES_LINK_BACK, &type, 1U);
}

dev_status_t bm83_link_back_address_a2dp(bm83_t *dev, uint8_t device_index,
                                         const uint8_t address[6])
{
    if (dev == NULL || address == NULL || device_index > 7U) return DEV_EINVAL;
    /* Audio UART Command Set v2.10 section 5.2.21, type 0x05: connect to an
     * explicit Bluetooth address. Profile bit2 is A2DP. The paired-record event
     * returns BD addresses in the same low-byte-first wire order expected here.
     * AVRCP is recovered afterwards through the documented Music_Control path. */
    uint8_t payload[9] = { 0x05U, device_index, 0x04U, 0U, 0U, 0U, 0U, 0U, 0U };
    memcpy(&payload[3], address, 6U);
    return bm83_send_packet(dev, BM83_CMD_PROFILES_LINK_BACK,
                            payload, (uint16_t)sizeof(payload));
}

void bm83_set_auto_reconnect(bm83_t *dev, bool enable)
{
    if (dev == NULL) return;
    if (dev->auto_reconnect == enable) return;
    dev->auto_reconnect = enable;
    bm_reset_reconnect_episode(dev);
    if (enable && !dev->a2dp_connected && !dev->pairing) {
        dev->reconnect_due_ms = bm_now(dev);
    }
}

dev_status_t bm83_service_connection(bm83_t *dev)
{
    if (dev == NULL) return DEV_EINVAL;
    if (!dev->auto_reconnect || dev->pairing ||
        (dev->a2dp_connected && dev->avrcp_connected)) return DEV_OK;
    if (dev->reconnect_exhausted) return DEV_OK;
    if (dev->clock.time_ms == NULL) return DEV_ENOTSUP;

    const uint32_t now = bm_now(dev);
    if (dev->reconnect_in_progress) {
        if ((uint32_t)(now - dev->reconnect_started_ms) < BM83_RECONNECT_TIMEOUT_MS) {
            /* Do not rely solely on optional asynchronous BTM_Status events.
             * Poll the documented connection snapshot while paging so a link
             * which actually succeeded is recognised promptly, and standby is
             * recognised as a failed attempt without a 12 s blind wait. */
            if (!dev->awaiting_command_ack &&
                bm_time_reached(now, dev->reconnect_status_due_ms)) {
                dev_status_t status_st = bm83_read_link_status(dev);
                if (status_st == DEV_OK) {
                    ++g_bm83_diag.reconnect_status_poll_count;
                    dev->reconnect_status_due_ms = now + BM83_RECONNECT_STATUS_POLL_MS;
                    return DEV_EBUSY;
                }
                if (status_st != DEV_EBUSY) return status_st;
            }
            return DEV_EBUSY;
        }
        bm_schedule_reconnect(dev, false);
    }
    if (!bm_time_reached(now, dev->reconnect_due_ms)) return DEV_EBUSY;
    if (dev->awaiting_command_ack) return DEV_EBUSY;

    const bool avrcp_recovery = dev->a2dp_connected;
    dev_status_t st;
    if (avrcp_recovery) {
        /* There is no documented Additional_Profile_Link_Setup value for
         * AVRCP: v2.10 section 5.2.19 only defines HF/HS, A2DP and iAP/SPP.
         * Section 5.2.4 does explicitly define how to recover this partial-link
         * state: sending Music_Control while A2DP is active makes BTM initiate
         * AVRCP. STOP_SEEK is the least intrusive action and is inert unless a
         * fast-forward/rewind operation is already active. */
        st = bm83_music_control(dev, BM83_MUSIC_STOP_SEEK);
    } else {
        /* A phone can retain a stale controller/Classic-visible connection while
         * BM83 itself is definitively in standby with no A2DP database bits.
         * After several accepted-but-unsuccessful link-backs, use the documented
         * Disconnect command once to cancel any outstanding page and clear A2DP,
         * then force the next attempt to the exact stored paired-device address.
         * This is deliberately one-shot per reconnect episode: repeated cleanup
         * commands can otherwise become a second infinite recovery loop. */
        if (dev->paired_newest_valid &&
            dev->reconnect_attempts >= BM83_RECONNECT_STALE_CLEANUP_ATTEMPT &&
            !dev->reconnect_stale_cleanup_used) {
            const uint8_t disconnect_flags =
                (uint8_t)(BM83_DISCONNECT_CANCEL_PAGE | BM83_DISCONNECT_A2DP);
            st = bm83_send_packet(dev, BM83_CMD_DISCONNECT, &disconnect_flags, 1U);
            if (st == DEV_OK) {
                dev->reconnect_stale_cleanup_used = true;
                dev->reconnect_force_address = true;
                dev->reconnect_in_progress = false;
                dev->reconnect_due_ms = now + BM83_RECOVERY_SETTLE_MS;
                ++g_bm83_diag.reconnect_stale_cleanup_count;
                return DEV_OK;
            }
            if (st != DEV_EBUSY) bm_schedule_reconnect(dev, false);
            return st;
        }

        /* Bound a failed recovery episode. The phone can still initiate a link,
         * and changing away from/back to Bluetooth re-arms auto-reconnect, but
         * firmware no longer pages forever (the live failure reached >90 tries). */
        if (dev->reconnect_stale_cleanup_used && !dev->reconnect_force_address &&
            dev->reconnect_attempts >= BM83_RECONNECT_MAX_ATTEMPTS) {
            dev->reconnect_exhausted = true;
            dev->reconnect_in_progress = false;
            ++g_bm83_diag.reconnect_exhausted_count;
            return DEV_OK;
        }

        /* Type 0x00 is the preferred last-device path because it uses the
         * paired-device profile record. If two complete attempts fail, alternate
         * a documented type-0x02 A2DP-only request with the normal path. This
         * gives phones which reject the composite link-back another standards-
         * defined way to restore the audio profile; once A2DP is up, the normal
         * Music_Control recovery below initiates AVRCP. */
        const bool use_a2dp_fallback =
            dev->reconnect_attempts >= BM83_RECONNECT_FULL_ATTEMPTS_BEFORE_A2DP_FALLBACK &&
            ((dev->reconnect_attempts - BM83_RECONNECT_FULL_ATTEMPTS_BEFORE_A2DP_FALLBACK) & 1U) == 0U;
        const bool use_address_fallback = dev->paired_newest_valid &&
            (dev->reconnect_force_address || use_a2dp_fallback);
        if (use_address_fallback) {
            st = bm83_link_back_address_a2dp(dev, dev->paired_newest_record_index,
                                             dev->paired_newest_address);
        } else {
            st = use_a2dp_fallback ? bm83_link_back_last_a2dp(dev) :
                                     bm83_link_back_last_device(dev);
        }
        if (st == DEV_OK && use_a2dp_fallback) {
            ++g_bm83_diag.reconnect_a2dp_fallback_count;
        }
        if (st == DEV_OK && use_address_fallback) {
            ++g_bm83_diag.reconnect_address_fallback_count;
            dev->reconnect_force_address = false;
        }
    }
    if (st == DEV_OK) {
        ++g_bm83_diag.reconnect_request_count;
        dev->reconnect_in_progress = true;
        dev->avrcp_recovery_command_pending = avrcp_recovery;
        dev->reconnect_started_ms = now;
        dev->reconnect_status_due_ms = now + BM83_RECONNECT_STATUS_POLL_MS;
        if (dev->reconnect_attempts != UINT8_MAX) ++dev->reconnect_attempts;
        return DEV_OK;
    }
    if (st != DEV_EBUSY) bm_schedule_reconnect(dev, false);
    return st;
}

dev_status_t bm83_read_eeprom(bm83_t *dev, uint16_t offset, uint8_t length)
{
    if (length == 0U || length > 16U) return DEV_EINVAL;
    const uint8_t payload[3] = {
        (uint8_t)(offset >> 8), (uint8_t)offset, length
    };
    return bm83_send_packet(dev, BM83_CMD_READ_EEPROM, payload, sizeof(payload));
}

dev_status_t bm83_toggle_audio_source(bm83_t *dev)
{
    /* Documented runtime-only BM83 command. Bit0 toggles between AUX and A2DP;
     * it does not alter factory configuration or EEPROM. */
    const uint8_t operation = 0x01U;
    return bm83_send_packet(dev, BM83_CMD_TOGGLE_AUDIO_SOURCE, &operation, 1U);
}

dev_status_t bm83_mmi_action(bm83_t *dev, uint8_t database_index, uint8_t action)
{
    uint8_t payload[2] = { database_index, action };
    return bm83_send_packet(dev, BM83_CMD_MMI_ACTION, payload, sizeof(payload));
}

dev_status_t bm83_request_now_playing_attribute(bm83_t *dev, uint8_t database_index,
                                                uint32_t attribute_id)
{
    if (database_index > 1U || attribute_id < 1U || attribute_id > 7U) return DEV_EINVAL;

    /* AVRCP GetElementAttributes (PDU 0x20), current playing element (UID 0).
     * Request one attribute per transaction. Some phones/controllers do not
     * reliably return a combined title+artist+album response, while the small
     * single-attribute response also stays well below the BM83 UART packet
     * buffer and avoids AVRCP continuation packets. */
    uint8_t payload[] = {
        database_index,
        0x20U, 0x00U, 0x00U, 0x0DU,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x01U,
        0x00U, 0x00U, 0x00U, 0x00U
    };
    payload[17] = (uint8_t)(attribute_id & 0xFFU);
    return bm83_send_packet(dev, BM83_CMD_AVC_VENDOR_DEPENDENT,
                            payload, (uint16_t)sizeof(payload));
}

dev_status_t bm83_request_now_playing_metadata(bm83_t *dev, uint8_t database_index)
{
    return bm83_request_now_playing_attribute(dev, database_index, 1U);
}

dev_status_t bm83_request_current_metadata_v206(bm83_t *dev, uint8_t database_index)
{
    if (dev == NULL || database_index > 1U) return DEV_EINVAL;
    /* UART command-set v2.06+ AVRCP_Vendor_Dependent_Cmd (0x4A),
     * GetElementAttributes (PDU 0x20). Unlike the older raw AVC command this
     * API implicitly targets the currently playing element; the parameter is
     * only Attribute_Count followed by the requested 32-bit attribute IDs. */
    const uint8_t payload[] = {
        database_index, 0x20U, 0x03U,
        0x00U,0x00U,0x00U,0x01U,
        0x00U,0x00U,0x00U,0x02U,
        0x00U,0x00U,0x00U,0x03U
    };
    return bm83_send_packet(dev, BM83_CMD_AVRCP_VENDOR_DEPENDENT,
                            payload, (uint16_t)sizeof(payload));
}

dev_status_t bm83_request_current_metadata_attribute_v206(bm83_t *dev,
                                                          uint8_t database_index,
                                                          uint8_t attribute_id)
{
    if (dev == NULL || database_index > 1U || attribute_id < 1U || attribute_id > 7U) {
        return DEV_EINVAL;
    }
    const uint8_t payload[] = {
        database_index, 0x20U, 0x01U,
        0x00U, 0x00U, 0x00U, attribute_id
    };
    return bm83_send_packet(dev, BM83_CMD_AVRCP_VENDOR_DEPENDENT,
                            payload, (uint16_t)sizeof(payload));
}

dev_status_t bm83_register_track_changed(bm83_t *dev, uint8_t database_index)
{
    if (dev == NULL || database_index > 1U) return DEV_EINVAL;
    /* AVRCP RegisterNotification (PDU 0x31), EventID 0x02 = TrackChanged.
     * Playback interval is reserved for this event and must be zero. */
    const uint8_t payload[] = {
        database_index,
        0x31U, 0x00U, 0x00U, 0x05U,
        0x02U, 0x00U, 0x00U, 0x00U, 0x00U
    };
    return bm83_send_packet(dev, BM83_CMD_AVC_VENDOR_DEPENDENT,
                            payload, (uint16_t)sizeof(payload));
}

dev_status_t bm83_request_event_capabilities(bm83_t *dev, uint8_t database_index)
{
    if (dev == NULL || database_index > 1U) return DEV_EINVAL;
    /* AVRCP GetCapabilities PDU 0x10, capability 0x03 = supported event IDs. */
    const uint8_t payload[] = { database_index, 0x10U, 0x00U, 0x00U, 0x01U, 0x03U };
    return bm83_send_packet(dev, BM83_CMD_AVC_VENDOR_DEPENDENT,
                            payload, (uint16_t)sizeof(payload));
}

dev_status_t bm83_request_media_player_list(bm83_t *dev, uint8_t database_index)
{
    if (database_index > 1U) return DEV_EINVAL;

    /* AVRCP Browsing GetFolderItems, scope 0x00 = Media Player List. Request
     * item zero first. Some TGs reject an End_Item beyond their current list
     * length instead of simply returning the shorter available range. */
    const uint8_t payload[] = {
        0x00U,             /* Sub-opcode: GetFolderItems. */
        database_index,
        0x00U,             /* Scope: Media Player List. */
        0x00U, 0x00U, 0x00U, 0x00U, /* Start item = 0. */
        0x00U, 0x00U, 0x00U, 0x00U, /* End item = 0. */
        0x00U              /* All attributes; no Attribute List follows. */
    };
    return bm83_send_packet(dev, BM83_CMD_AVRCP_BROWSING,
                            payload, (uint16_t)sizeof(payload));
}

dev_status_t bm83_set_addressed_player(bm83_t *dev, uint8_t database_index,
                                      uint16_t player_id)
{
    if (database_index > 1U) return DEV_EINVAL;
    const uint8_t payload[] = {
        0x02U, database_index,
        (uint8_t)(player_id >> 8), (uint8_t)player_id
    };
    return bm83_send_packet(dev, BM83_CMD_AVRCP_BROWSING,
                            payload, (uint16_t)sizeof(payload));
}

dev_status_t bm83_set_browsed_player(bm83_t *dev, uint8_t database_index,
                                    uint16_t player_id)
{
    if (database_index > 1U) return DEV_EINVAL;
    const uint8_t payload[] = {
        0x03U, database_index,
        (uint8_t)(player_id >> 8), (uint8_t)player_id
    };
    return bm83_send_packet(dev, BM83_CMD_AVRCP_BROWSING,
                            payload, (uint16_t)sizeof(payload));
}



dev_status_t bm83_request_now_playing_browse(bm83_t *dev, uint8_t database_index)
{
    if (database_index > 1U) return DEV_EINVAL;

    /* AVRCP_Browsing_Cmd/GetFolderItems. Scope 0x03 is the addressed player's
     * Now Playing queue. Request only item zero and the three attributes used
     * by the UI: title, artist and album. All multibyte AVRCP fields are
     * network-order. */
    const uint8_t payload[] = {
        0x00U,             /* Sub-opcode: GetFolderItems. */
        database_index,
        0x03U,             /* Scope: Now Playing. */
        0x00U, 0x00U, 0x00U, 0x00U, /* Start item = 0. */
        0x00U, 0x00U, 0x00U, 0x00U, /* End item = 0. */
        0x03U,
        0x00U, 0x00U, 0x00U, 0x01U,
        0x00U, 0x00U, 0x00U, 0x02U,
        0x00U, 0x00U, 0x00U, 0x03U
    };
    return bm83_send_packet(dev, BM83_CMD_AVRCP_BROWSING,
                            payload, (uint16_t)sizeof(payload));
}

dev_status_t bm83_request_item_attributes(bm83_t *dev, uint8_t database_index,
                                          uint8_t scope, const uint8_t uid[8],
                                          uint16_t uid_counter)
{
    if (dev == NULL || uid == NULL || database_index > 1U || scope > 3U) {
        return DEV_EINVAL;
    }

    /* AVRCP Browsing/GetItemAttributes (sub-opcode 0x05). Attribute 0x08 is
     * cover art, but the BM83 UART specification explicitly marks it NOT
     * SUPPORTED. Request every useful textual/numeric field the module can
     * actually return: title, artist, album, track number/count, genre and
     * playing time. */
    uint8_t payload[42] = {
        0x05U, database_index, scope,
        0,0,0,0,0,0,0,0,               /* UID */
        (uint8_t)(uid_counter >> 8), (uint8_t)uid_counter,
        0x07U,                           /* Attributes_Num */
        0x00U,0x00U,0x00U,0x01U,       /* Title */
        0x00U,0x00U,0x00U,0x02U,       /* Artist */
        0x00U,0x00U,0x00U,0x03U,       /* Album */
        0x00U,0x00U,0x00U,0x04U,       /* Track number */
        0x00U,0x00U,0x00U,0x05U,       /* Total tracks */
        0x00U,0x00U,0x00U,0x06U,       /* Genre */
        0x00U,0x00U,0x00U,0x07U        /* Playing time (ms) */
    };
    memcpy(&payload[3], uid, 8U);
    return bm83_send_packet(dev, BM83_CMD_AVRCP_BROWSING,
                            payload, (uint16_t)sizeof(payload));
}

dev_status_t bm83_send_configured_control(bm83_t *dev,
                                          const bm83_control_ids_t *ids,
                                          bm83_control_t control,
                                          const uint8_t *payload,
                                          uint16_t payload_len)
{
    if (ids == NULL) return DEV_EINVAL;
    uint8_t configured_command_id = 0U;
    switch (control) {
    case BM83_CONTROL_PLAY_PAUSE: configured_command_id = ids->play_pause; break;
    case BM83_CONTROL_NEXT_TRACK: configured_command_id = ids->next_track; break;
    case BM83_CONTROL_PREVIOUS_TRACK: configured_command_id = ids->previous_track; break;
    case BM83_CONTROL_VOLUME_UP: configured_command_id = ids->volume_up; break;
    case BM83_CONTROL_VOLUME_DOWN: configured_command_id = ids->volume_down; break;
    default: return DEV_EINVAL;
    }
    if (configured_command_id == 0U) return DEV_ENOTSUP;
    return bm83_send_packet(dev, configured_command_id, payload, payload_len);
}

dev_status_t bm83_hw_reset(bm83_t *dev, uint32_t low_ms, uint32_t settle_ms)
{
    if ((dev == NULL) || (dev->reset_n.write == NULL) || (dev->clock.delay_ms == NULL)) return DEV_ENOTSUP;
    if (dev->mfb.write != NULL) dev->mfb.write(dev->mfb.ctx, true);
    dev->reset_n.write(dev->reset_n.ctx, false);
    dev->clock.delay_ms(dev->clock.ctx, low_ms);
    dev->reset_n.write(dev->reset_n.ctx, true);
    dev->clock.delay_ms(dev->clock.ctx, settle_ms);
    if (dev->mfb.write != NULL) dev->mfb.write(dev->mfb.ctx, false);
    bm_parser_reset(dev);
    dev->awaiting_command_ack = false;
    dev->pending_frame_len = 0U;
    dev->retry_count = 0U;
    bm_clear_nonfatal_backpressure(dev);
    dev->initial_status_received = false;
    dev->btm_status_received = false;
    dev->btm_state = BM83_BTM_OFF;
    dev->pairing = false;
    dev->a2dp_connected = false;
    dev->avrcp_connected = false;
    bm_reset_reconnect_episode(dev);
    if (dev->auto_reconnect) bm_schedule_reconnect(dev, false);
    return DEV_OK;
}

dev_status_t bm83_mfb_pulse(bm83_t *dev, uint32_t active_ms)
{
    if ((dev == NULL) || (dev->mfb.write == NULL) || (dev->clock.delay_ms == NULL)) return DEV_ENOTSUP;
    dev->mfb.write(dev->mfb.ctx, true);
    dev->clock.delay_ms(dev->clock.ctx, active_ms);
    dev->mfb.write(dev->mfb.ctx, false);
    return DEV_OK;
}

void bm83_rx_byte(bm83_t *dev, uint8_t byte)
{
    if (dev == NULL) return;
    switch (dev->parser_state) {
    case BM_PARSE_SYNC:
        if (byte == BM83_FRAME_START) bm_parser_reset(dev), dev->parser_state = BM_PARSE_LEN_HI;
        break;
    case BM_PARSE_LEN_HI:
        dev->expected_len = (uint16_t)((uint16_t)byte << 8);
        dev->checksum = byte;
        dev->parser_state = BM_PARSE_LEN_LO;
        break;
    case BM_PARSE_LEN_LO:
        dev->expected_len = (uint16_t)(dev->expected_len | byte);
        dev->checksum = (uint8_t)(dev->checksum + byte);
        if ((dev->expected_len == 0U) || (dev->expected_len > (BM83_MAX_PAYLOAD + 1U))) {
            bm_parser_reset(dev);
        } else {
            dev->parser_state = BM_PARSE_ID;
        }
        break;
    case BM_PARSE_ID:
        dev->rx.id = byte;
        dev->checksum = (uint8_t)(dev->checksum + byte);
        dev->rx.payload_len = (uint16_t)(dev->expected_len - 1U);
        dev->received_len = 0U;
        dev->parser_state = (dev->rx.payload_len == 0U) ? BM_PARSE_CHECKSUM : BM_PARSE_PAYLOAD;
        break;
    case BM_PARSE_PAYLOAD:
        dev->rx.payload[dev->received_len++] = byte;
        dev->checksum = (uint8_t)(dev->checksum + byte);
        if (dev->received_len >= dev->rx.payload_len) dev->parser_state = BM_PARSE_CHECKSUM;
        break;
    case BM_PARSE_CHECKSUM:
        if ((uint8_t)(dev->checksum + byte) == 0U) {
            const uint8_t event_id = dev->rx.id;
            ++g_bm83_diag.valid_packets;
            g_bm83_diag.last_event_id = event_id;
            if (event_id == BM83_EVENT_COMMAND_ACK) {
                ++g_bm83_diag.event_command_ack_count;
                if (dev->rx.payload_len >= 1U) {
                    if (dev->rx.payload[0] == BM83_CMD_AVRCP_VENDOR_DEPENDENT) {
                        ++g_bm83_diag.event_command_ack_4a_count;
                    } else if (dev->rx.payload[0] == BM83_CMD_EVENT_ACK) {
                        ++g_bm83_diag.event_command_ack_14_count;
                    }
                }
            } else if (event_id == BM83_EVENT_BTM_STATUS) {
                ++g_bm83_diag.event_btm_status_count;
                if (dev->rx.payload_len >= 1U && dev->rx.payload[0] == 0x82U) {
                    ++g_bm83_diag.event_btm_status_82_count;
                }
            } else if (event_id == BM83_EVENT_TYPE_CODEC) {
                ++g_bm83_diag.event_type_codec_count;
            } else if (event_id == BM83_EVENT_AVC_VENDOR_DEPENDENT_RESPONSE) {
                ++g_bm83_diag.event_avc_vendor_count;
            } else if (event_id != BM83_EVENT_AVRCP_VENDOR_DEPENDENT_RSP) {
                ++g_bm83_diag.event_other_count;
            }
            bm_update_connection_state(dev, &dev->rx);
            if (event_id == BM83_EVENT_BTM_STATUS && dev->rx.payload_len >= 2U) {
                g_bm83_diag.last_btm_state = dev->rx.payload[0];
                g_bm83_diag.last_database_index = (uint8_t)(dev->rx.payload[1] & 0x0FU);
                if (dev->rx.payload[0] >= 0x80U && dev->rx.payload[0] <= 0x82U) {
                    g_bm83_diag.audio_source_state = dev->rx.payload[0];
                }
            } else if (event_id == BM83_EVENT_TYPE_CODEC && dev->rx.payload_len >= 2U) {
                /* Report_Type_Codec: sampling-frequency code, then I2S mode. */
                g_bm83_diag.codec_sample_rate_code = dev->rx.payload[0];
                g_bm83_diag.codec_mode = dev->rx.payload[1];
            } else if (event_id == BM83_EVENT_READ_VERSION_REPLY &&
                       dev->rx.payload_len >= 3U) {
                const uint8_t type = dev->rx.payload[0];
                if (type == 0x00U) {
                    dev->uart_version_major = dev->rx.payload[1];
                    dev->uart_version_minor = dev->rx.payload[2];
                    dev->uart_version_valid = true;
                    g_bm83_diag.uart_version[0] = dev->rx.payload[1];
                    g_bm83_diag.uart_version[1] = dev->rx.payload[2];
                } else if (type == 0x01U) {
                    g_bm83_diag.fw_version[0] = dev->rx.payload[1];
                    g_bm83_diag.fw_version[1] = dev->rx.payload[2];
                } else if (type == 0x02U) {
                    g_bm83_diag.eeprom_version[0] = dev->rx.payload[1];
                    g_bm83_diag.eeprom_version[1] = dev->rx.payload[2];
                } else if (type == 0x03U && dev->rx.payload_len >= 5U) {
                    memcpy((void *)g_bm83_diag.fw_detail_version, &dev->rx.payload[1], 4U);
                } else if (type == 0x04U && dev->rx.payload_len >= 5U) {
                    memcpy((void *)g_bm83_diag.dsp_version, &dev->rx.payload[1], 4U);
                } else if (type == 0x05U && dev->rx.payload_len >= 4U) {
                    memcpy((void *)g_bm83_diag.project_target, &dev->rx.payload[1], 3U);
                }
            } else if (event_id == BM83_EVENT_READ_LINK_STATUS_REPLY &&
                       dev->rx.payload_len >= 7U) {
                g_bm83_diag.link_device_state = dev->rx.payload[0];
                g_bm83_diag.link_db0_connect = dev->rx.payload[1];
                g_bm83_diag.link_db1_connect = dev->rx.payload[2];
                g_bm83_diag.link_db0_play = dev->rx.payload[3];
                g_bm83_diag.link_db1_play = dev->rx.payload[4];
                g_bm83_diag.link_db0_stream = dev->rx.payload[5];
                g_bm83_diag.link_db1_stream = dev->rx.payload[6];
            } else if (event_id == BM83_EVENT_READ_PAIRED_DEVICE_RECORD_REPLY &&
                       dev->rx.payload_len >= 1U) {
                const uint8_t count = dev->rx.payload[0];
                dev->paired_device_count = count;
                dev->paired_newest_valid = false;
                dev->paired_newest_record_index = 0U;
                memset(dev->paired_newest_address, 0, sizeof(dev->paired_newest_address));
                g_bm83_diag.paired_device_count = count;
                g_bm83_diag.paired_newest_valid = 0U;
                memset((void *)g_bm83_diag.paired_newest_address, 0,
                       sizeof(g_bm83_diag.paired_newest_address));
                const uint16_t available_records =
                    (uint16_t)((dev->rx.payload_len - 1U) / 7U);
                const uint16_t records = count < available_records ? count : available_records;
                for (uint16_t record = 0U; record < records; ++record) {
                    const uint16_t offset = (uint16_t)(1U + record * 7U);
                    if (dev->rx.payload[offset] == 1U) {
                        dev->paired_newest_record_index = (uint8_t)record;
                        memcpy(dev->paired_newest_address, &dev->rx.payload[offset + 1U], 6U);
                        dev->paired_newest_valid = true;
                        memcpy((void *)g_bm83_diag.paired_newest_address,
                               &dev->rx.payload[offset + 1U], 6U);
                        g_bm83_diag.paired_newest_valid = 1U;
                        break;
                    }
                }
            } else if (event_id == BM83_EVENT_READ_LINKED_DEVICE_INFO_REPLY &&
                       dev->rx.payload_len >= 3U && dev->rx.payload[1] == 0x03U) {
                g_bm83_diag.remote_avrcp_features = dev->rx.payload[2];
                g_bm83_diag.remote_avrcp_features_valid = 1U;
            } else if (event_id == BM83_EVENT_READ_EEPROM_REPLY) {
                uint8_t n = dev->rx.payload_len > 16U ? 16U : (uint8_t)dev->rx.payload_len;
                g_bm83_diag.eeprom_read_len = n;
                if (n != 0U) memcpy((void *)g_bm83_diag.eeprom_read_data, dev->rx.payload, n);
            } else if (event_id == BM83_EVENT_AVC_VENDOR_DEPENDENT_RESPONSE) {
                ++g_bm83_diag.avrc_response_count;
                /* Keep the last raw AVC response even when it is not the PDU
                 * we expected. This makes phone/BM83 interoperability issues
                 * diagnosable over SWD instead of silently discarding the one
                 * response that explains what the remote actually returned. */
                g_bm83_diag.metadata_last_payload_len = dev->rx.payload_len;
                g_bm83_diag.metadata_last_response_type =
                    dev->rx.payload_len > 1U ? dev->rx.payload[1] : 0U;
                g_bm83_diag.metadata_last_pdu =
                    dev->rx.payload_len > 7U ? dev->rx.payload[7] : 0U;
                g_bm83_diag.metadata_last_packet_type =
                    dev->rx.payload_len > 8U ? dev->rx.payload[8] : 0U;
                uint8_t n = dev->rx.payload_len > 32U ? 32U : (uint8_t)dev->rx.payload_len;
                g_bm83_diag.metadata_preview_len = n;
                if (n != 0U) memcpy((void *)g_bm83_diag.metadata_preview, dev->rx.payload, n);
                if (dev->rx.payload_len > 7U && dev->rx.payload[7] == 0x20U) {
                    ++g_bm83_diag.metadata_response_count;
                } else if (dev->rx.payload_len > 7U && dev->rx.payload[7] == 0x10U) {
                    ++g_bm83_diag.capabilities_response_count;
                    g_bm83_diag.capabilities_last_payload_len = dev->rx.payload_len;
                    uint8_t cap_n = dev->rx.payload_len > 32U ? 32U : (uint8_t)dev->rx.payload_len;
                    g_bm83_diag.capabilities_preview_len = cap_n;
                    if (cap_n != 0U) {
                        memcpy((void *)g_bm83_diag.capabilities_preview,
                               dev->rx.payload, cap_n);
                    }
                }
            } else if (event_id == BM83_EVENT_AVRCP_BROWSING) {
                ++g_bm83_diag.browsing_response_count;
                g_bm83_diag.browsing_last_payload_len = dev->rx.payload_len;
                uint8_t n = dev->rx.payload_len > 48U ? 48U : (uint8_t)dev->rx.payload_len;
                g_bm83_diag.browsing_preview_len = n;
                if (n != 0U) memcpy((void *)g_bm83_diag.browsing_preview, dev->rx.payload, n);
            } else if (event_id == BM83_EVENT_AVRCP_VENDOR_DEPENDENT_RSP) {
                ++g_bm83_diag.avrcp_v206_response_count;
                g_bm83_diag.avrcp_v206_last_payload_len = dev->rx.payload_len;
                if (dev->rx.payload_len > g_bm83_diag.avrcp_v206_max_payload_len) {
                    g_bm83_diag.avrcp_v206_max_payload_len = dev->rx.payload_len;
                }
                if (dev->rx.payload_len >= 7U) {
                    g_bm83_diag.avrcp_v206_last_pdu = dev->rx.payload[0];
                    g_bm83_diag.avrcp_v206_last_database_index = dev->rx.payload[1];
                    g_bm83_diag.avrcp_v206_last_response = dev->rx.payload[2];
                    g_bm83_diag.avrcp_v206_last_end_of_body = dev->rx.payload[3];
                    g_bm83_diag.avrcp_v206_last_attr_count = dev->rx.payload[4];
                    g_bm83_diag.avrcp_v206_last_total_attr_list_len =
                        (uint16_t)(((uint16_t)dev->rx.payload[5] << 8) |
                                   dev->rx.payload[6]);
                    if (dev->rx.payload[3] != 0U) {
                        ++g_bm83_diag.avrcp_v206_final_count;
                    } else {
                        ++g_bm83_diag.avrcp_v206_nonfinal_count;
                    }
                }
            } else if (event_id == BM83_EVENT_COMMAND_ACK && dev->rx.payload_len >= 2U) {
                g_bm83_diag.last_command_ack_id = dev->rx.payload[0];
                g_bm83_diag.last_command_ack_status = dev->rx.payload[1];
            }
            /* BM83 queues subsequent events until the host acknowledges the
             * current event. Event_Ack is deliberately sent outside the
             * tracked command transaction so it cannot overwrite a command
             * awaiting its Command_ACK. */
            ++g_bm83_diag.event_ack_attempt_count;
            g_bm83_diag.event_ack_last_event_id = event_id;
            dev_status_t event_ack_st = bm_send_event_ack_raw(dev, event_id);
            g_bm83_diag.event_ack_last_status = (uint8_t)event_ack_st;
            if (event_ack_st == DEV_OK) {
                ++g_bm83_diag.event_ack_success_count;
                if (event_id == BM83_EVENT_AVRCP_VENDOR_DEPENDENT_RSP) {
                    ++g_bm83_diag.event_ack_v206_success_count;
                }
            } else {
                ++g_bm83_diag.event_ack_failure_count;
            }

            /* A delayed status-0 ACK following status 0x04/0x05 is BTM's
             * documented resource-ready indication, not completion of the
             * packet which was previously rejected. Release the backpressure
             * gate; the application will construct and resend its idempotent
             * request on the next scheduler pass. */
            if (event_id == BM83_EVENT_COMMAND_ACK && dev->rx.payload_len >= 2U &&
                dev->nonfatal_backpressure_waiting_ready &&
                dev->rx.payload[0] == dev->nonfatal_backpressure_command_id &&
                dev->rx.payload[1] == 0U) {
                bm_clear_nonfatal_backpressure(dev);
                ++g_bm83_diag.nonfatal_ready_ack_count;
            }

            if ((event_id == BM83_EVENT_COMMAND_ACK) &&
                (dev->rx.payload_len >= 2U) && dev->awaiting_command_ack &&
                (dev->rx.payload[0] == dev->pending_command_id)) {
                const uint8_t ack_status = dev->rx.payload[1];
                dev->last_command_ack_status = ack_status;
                if (ack_status == 0U) {
                    if (dev->pending_command_id == BM83_CMD_MUSIC_CONTROL) {
                        dev->avrcp_recovery_command_pending = false;
                    }
                    dev->awaiting_command_ack = false;
                    dev->pending_frame_len = 0U;
                    dev->retry_count = 0U;
                } else if (ack_status <= 3U || ack_status > 5U) {
                    /* Disallowed/unknown/bad-parameter are terminal. Surface
                     * one DEV_EIO from bm83_service() rather than silently
                     * discarding the failed command. */
                    const bool connection_recovery =
                        bm_command_is_connection_recovery(dev, dev->pending_command_id);
                    if (dev->pending_command_id == BM83_CMD_MUSIC_CONTROL) {
                        dev->avrcp_recovery_command_pending = false;
                    }
                    dev->awaiting_command_ack = false;
                    dev->pending_frame_len = 0U;
                    if (connection_recovery) {
                        /* Link recovery can be disallowed transiently while the
                         * BM83 is already creating a connection. Treat it as a
                         * normal reconnect failure rather than a fatal app error. */
                        bm_schedule_reconnect(dev, false);
                    } else {
                        dev->command_error_latched = true;
                    }
                } else {
                    /* Busy/memory-full on auxiliary AVRCP queries is explicit
                     * backpressure, not a lost UART ACK. Retrying the exact
                     * metadata frame 200 ms later can keep the BM83's AVRCP
                     * command pool permanently full. Release the UART command
                     * slot and let the application's response-aware scheduler
                     * retry after a longer bounded backoff. Playback controls
                     * and other state-changing commands retain the historical
                     * exact-frame retry behaviour. */
                    if (bm_command_timeout_is_nonfatal(dev->pending_command_id)) {
                        dev->nonfatal_backpressure_waiting_ready = true;
                        dev->nonfatal_backpressure_command_id = dev->pending_command_id;
                        dev->nonfatal_backpressure_since_ms = bm_now(dev);
                        dev->awaiting_command_ack = false;
                        dev->pending_frame_len = 0U;
                        dev->retry_count = 0U;
                        ++g_bm83_diag.nonfatal_busy_ack_count;
                    } else {
                        dev->pending_since_ms = (dev->clock.time_ms != NULL) ?
                                                dev->clock.time_ms(dev->clock.ctx) :
                                                dev->pending_since_ms;
                    }
                }
            }
            if (dev->packet_cb != NULL) dev->packet_cb(dev->packet_cb_ctx, &dev->rx);
        } else {
            ++g_bm83_diag.checksum_errors;
        }
        bm_parser_reset(dev);
        break;
    default:
        bm_parser_reset(dev);
        break;
    }
}

void bm83_rx_data(bm83_t *dev, const uint8_t *data, size_t len)
{
    if ((dev == NULL) || ((data == NULL) && (len != 0U))) return;
    for (size_t i = 0U; i < len; ++i) bm83_rx_byte(dev, data[i]);
}

dev_status_t bm83_service(bm83_t *dev)
{
    if (dev == NULL) return DEV_EINVAL;
    if (dev->command_error_latched) {
        dev->command_error_latched = false;
        return DEV_EIO;
    }
    if (dev->clock.time_ms == NULL) {
        return (dev->awaiting_command_ack || dev->nonfatal_backpressure_waiting_ready) ?
               DEV_ENOTSUP : DEV_OK;
    }

    const uint32_t now = dev->clock.time_ms(dev->clock.ctx);
    if (dev->nonfatal_backpressure_waiting_ready &&
        (uint32_t)(now - dev->nonfatal_backpressure_since_ms) >=
        BM83_NONFATAL_READY_TIMEOUT_MS) {
        /* Some fielded BM83 builds accept 0x4A despite reporting Audio UART
         * v2.02, but do not always emit the documented delayed readiness ACK.
         * Never wedge auxiliary metadata forever: after a long quiet interval,
         * release the gate for one fresh application-level probe. A repeated
         * 0x04/0x05 immediately re-enters this wait state, so this cannot form a
         * tight retry loop. */
        bm_clear_nonfatal_backpressure(dev);
        ++g_bm83_diag.nonfatal_ready_timeout_count;
    }
    if (!dev->awaiting_command_ack) return DEV_OK;
    if ((uint32_t)(now - dev->pending_since_ms) < BM83_COMMAND_ACK_TIMEOUT_MS) {
        return DEV_EBUSY;
    }

    if (dev->retry_count == 0U) {
        dev_status_t st = bm_write_frame(dev, dev->pending_frame,
                                         dev->pending_frame_len);
        if (st != DEV_OK) return st;
        dev->retry_count = 1U;
        dev->pending_since_ms = (dev->clock.time_ms != NULL) ?
                                dev->clock.time_ms(dev->clock.ctx) : now;
        return DEV_EBUSY;
    }

    /* Connection recovery is different from an ordinary command transaction:
     * the module can already be paging/connecting autonomously while our
     * request is in flight. A second missing ACK must not reset a potentially
     * healthy A2DP stream; a bare hardware reset also requires a fresh power-on
     * MMI sequence before link-back is meaningful. Back off and retry instead. */
    const bool connection_recovery_timeout =
        bm_command_is_connection_recovery(dev, dev->pending_command_id);
    const bool nonfatal_timeout = bm_command_timeout_is_nonfatal(dev->pending_command_id);
    ++g_bm83_diag.command_timeout_count;
    if (dev->pending_command_id == BM83_CMD_MUSIC_CONTROL) {
        dev->avrcp_recovery_command_pending = false;
    }
    dev->awaiting_command_ack = false;
    dev->pending_frame_len = 0U;
    if (connection_recovery_timeout) {
        bm_schedule_reconnect(dev, false);
        return DEV_EBUSY;
    }
    if (nonfatal_timeout) {
        ++g_bm83_diag.nonfatal_command_timeout_count;
        return DEV_EBUSY;
    }

    /* The Host MCU guide permits resetting BM83 after an ordinary repeated
     * command also receives no ACK. Its reset sequence is RST_N low while MFB
     * is high, 20 ms before releasing reset, then 500 ms before command traffic. */
    dev_status_t reset_st = bm83_hw_reset(dev, BM83_RECOVERY_RESET_LOW_MS,
                                          BM83_RECOVERY_SETTLE_MS);
    return (reset_st == DEV_OK) ? DEV_ETIMEOUT : reset_st;
}
