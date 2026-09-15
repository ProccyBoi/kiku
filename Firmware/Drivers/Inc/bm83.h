#ifndef BM83_H
#define BM83_H

#include "device_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BM83_FRAME_START 0xAAU
#define BM83_MAX_PAYLOAD 256U
#define BM83_CMD_MMI_ACTION 0x02U
#define BM83_CMD_EVENT_MASK_SETTING 0x03U
#define BM83_CMD_MUSIC_CONTROL 0x04U
#define BM83_CMD_BTM_PARAMETER_SETTING 0x07U
#define BM83_CMD_READ_VERSION 0x08U
#define BM83_CMD_AVC_VENDOR_DEPENDENT 0x0BU
#define BM83_CMD_READ_LINK_STATUS 0x0DU
#define BM83_CMD_READ_PAIRED_DEVICE_RECORD 0x0EU
#define BM83_CMD_EVENT_ACK 0x14U
#define BM83_CMD_READ_LINKED_DEVICE_INFO 0x16U
#define BM83_CMD_UTILITY_FUNCTION 0x13U
#define BM83_CMD_PROFILES_LINK_BACK 0x17U
#define BM83_CMD_DISCONNECT 0x18U
#define BM83_CMD_READ_EEPROM 0x3CU
#define BM83_CMD_AVRCP_BROWSING 0x41U
#define BM83_CMD_AVRCP_VENDOR_DEPENDENT 0x4AU
#define BM83_CMD_TOGGLE_AUDIO_SOURCE 0xCCU

#define BM83_EVENT_COMMAND_ACK 0x00U
#define BM83_EVENT_BTM_STATUS 0x01U
#define BM83_EVENT_READ_LINKED_DEVICE_INFO_REPLY 0x17U
#define BM83_EVENT_READ_VERSION_REPLY 0x18U
#define BM83_EVENT_AVC_VENDOR_DEPENDENT_RESPONSE 0x1AU
#define BM83_EVENT_READ_LINK_STATUS_REPLY 0x1EU
#define BM83_EVENT_READ_PAIRED_DEVICE_RECORD_REPLY 0x1FU
#define BM83_EVENT_LINK_BACK_STATUS 0x23U
#define BM83_EVENT_TYPE_CODEC 0x2DU
#define BM83_EVENT_INITIAL_STATUS 0x30U
#define BM83_EVENT_READ_EEPROM_REPLY 0x42U
#define BM83_EVENT_AVRCP_BROWSING 0x44U
#define BM83_EVENT_AVRCP_VENDOR_DEPENDENT_RSP 0x5DU

#define BM83_COMMAND_ACK_TIMEOUT_MS 200U
#define BM83_NONFATAL_READY_TIMEOUT_MS 10000U
#define BM83_RECOVERY_RESET_LOW_MS   20U
#define BM83_RECOVERY_SETTLE_MS     500U

#define BM83_MMI_POWER_ON_PRESS     0x51U
#define BM83_MMI_POWER_ON_RELEASE   0x52U
#define BM83_MMI_POWER_OFF_PRESS    0x53U
#define BM83_MMI_POWER_OFF_RELEASE  0x54U
#define BM83_MMI_SWITCH_POWER_OFF   0x5EU
#define BM83_MMI_FAST_PAIRING       0x5DU

/* Audio UART Command Set v2.10, BTM_Status event values. */
#define BM83_BTM_OFF                0x00U
#define BM83_BTM_PAIRING            0x01U
#define BM83_BTM_POWER_ON           0x02U
#define BM83_BTM_PAIRING_SUCCESS    0x03U
#define BM83_BTM_PAIRING_FAILED     0x04U
#define BM83_BTM_A2DP_CONNECTED     0x06U
#define BM83_BTM_A2DP_DISCONNECTED  0x08U
#define BM83_BTM_AVRCP_CONNECTED    0x0BU
#define BM83_BTM_AVRCP_DISCONNECTED 0x0CU
#define BM83_BTM_STANDBY            0x0FU
#define BM83_BTM_ACL_DISCONNECTED   0x11U
#define BM83_BTM_ACL_CONNECTED      0x15U
#define BM83_BTM_LINK_BACK_ACL      0x17U

/* Documented Report_Link_Back_Status (0x23) values. */
#define BM83_LINK_BACK_STATUS_ACL   0x00U
#define BM83_LINK_BACK_STATUS_A2DP  0x02U
#define BM83_LINK_BACK_ACL_FAILED   0xFFU
#define BM83_LINK_BACK_PROFILE_OK   0x00U

#define BM83_RECONNECT_BASE_MS      750U
#define BM83_RECONNECT_MAX_MS       8000U
#define BM83_RECONNECT_TIMEOUT_MS   12000U
#define BM83_RECONNECT_STATUS_POLL_MS 1000U
#define BM83_RECONNECT_FULL_ATTEMPTS_BEFORE_A2DP_FALLBACK 2U
#define BM83_RECONNECT_STALE_CLEANUP_ATTEMPT 4U
#define BM83_RECONNECT_MAX_ATTEMPTS 7U

/* Disconnect (0x18), Audio UART Command Set v2.10 section 5.2.22. */
#define BM83_DISCONNECT_CANCEL_PAGE (1U << 0)
#define BM83_DISCONNECT_A2DP        (1U << 2)

/* BTM_Parameter_Setting (0x07), Parameter 0x04 classic-profile mask. */
#define BM83_PROFILE_A2DP           (1U << 2)
#define BM83_PROFILE_AVRCP_CT       (1U << 3)
#define BM83_PROFILE_AVRCP_TG       (1U << 4)

typedef struct {
    uint8_t id;
    uint16_t payload_len;
    uint8_t payload[BM83_MAX_PAYLOAD];
} bm83_packet_t;

typedef void (*bm83_packet_cb_t)(void *ctx, const bm83_packet_t *packet);


typedef struct {
    uint32_t valid_packets;
    uint32_t checksum_errors;
    uint8_t last_event_id;
    uint8_t last_btm_state;
    uint8_t last_database_index;
    uint8_t codec_sample_rate_code;
    uint8_t codec_mode;
    uint8_t last_command_ack_id;
    uint8_t last_command_ack_status;
    uint8_t uart_version[2];
    uint8_t fw_version[2];
    uint8_t eeprom_version[2];
    uint8_t fw_detail_version[4];
    uint8_t dsp_version[4];
    uint8_t project_target[3];
    uint8_t audio_source_state;
    uint8_t link_device_state;
    uint8_t link_db0_connect;
    uint8_t link_db1_connect;
    uint8_t link_db0_play;
    uint8_t link_db1_play;
    uint8_t link_db0_stream;
    uint8_t link_db1_stream;
    uint8_t remote_avrcp_features;
    uint8_t remote_avrcp_features_valid;
    uint8_t last_link_back_status;
    uint8_t last_link_back_result;
    uint8_t paired_device_count;
    uint8_t paired_newest_valid;
    uint8_t paired_newest_address[6];
    uint8_t eeprom_read_len;
    uint8_t eeprom_read_data[16];
    uint32_t avrc_response_count;
    uint32_t metadata_response_count;
    uint16_t metadata_last_payload_len;
    uint8_t metadata_last_response_type;
    uint8_t metadata_last_pdu;
    uint8_t metadata_last_packet_type;
    uint8_t metadata_preview_len;
    uint8_t metadata_preview[32];
    uint32_t capabilities_response_count;
    uint16_t capabilities_last_payload_len;
    uint8_t capabilities_preview_len;
    uint8_t capabilities_preview[32];
    uint32_t browsing_response_count;
    uint32_t command_timeout_count;
    uint32_t nonfatal_command_timeout_count;
    uint32_t nonfatal_busy_ack_count;
    uint32_t nonfatal_ready_ack_count;
    uint32_t nonfatal_ready_timeout_count;
    uint32_t avrcp_v206_response_count;
    uint32_t event_ack_attempt_count;
    uint32_t event_ack_success_count;
    uint32_t event_ack_failure_count;
    uint32_t event_ack_v206_success_count;
    uint32_t avrcp_v206_nonfinal_count;
    uint32_t avrcp_v206_final_count;
    uint16_t avrcp_v206_last_payload_len;
    uint16_t avrcp_v206_last_total_attr_list_len;
    uint16_t avrcp_v206_max_payload_len;
    uint8_t event_ack_last_event_id;
    uint8_t event_ack_last_status;
    uint8_t avrcp_v206_last_pdu;
    uint8_t avrcp_v206_last_database_index;
    uint8_t avrcp_v206_last_response;
    uint8_t avrcp_v206_last_end_of_body;
    uint8_t avrcp_v206_last_attr_count;
    uint32_t event_command_ack_count;
    uint32_t event_command_ack_4a_count;
    uint32_t event_command_ack_14_count;
    uint32_t event_btm_status_count;
    uint32_t event_btm_status_82_count;
    uint32_t event_type_codec_count;
    uint32_t event_avc_vendor_count;
    uint32_t event_other_count;
    uint32_t reconnect_request_count;
    uint32_t reconnect_a2dp_fallback_count;
    uint32_t reconnect_address_fallback_count;
    uint32_t reconnect_stale_cleanup_count;
    uint32_t reconnect_exhausted_count;
    uint32_t link_back_acl_success_count;
    uint32_t link_back_a2dp_success_count;
    uint32_t link_back_a2dp_fail_count;
    uint32_t reconnect_power_cycle_count;
    uint32_t reconnect_status_poll_count;
    uint32_t reconnect_standby_retry_count;
    uint16_t browsing_last_payload_len;
    uint8_t browsing_preview_len;
    uint8_t browsing_preview[48];
} bm83_diag_t;

extern volatile bm83_diag_t g_bm83_diag;

typedef struct {
    dev_uart_bus_t uart;
    dev_gpio_t reset_n;
    dev_gpio_t mfb;
    dev_gpio_t tx_ind;
    dev_clock_t clock;
    bm83_packet_cb_t packet_cb;
    void *packet_cb_ctx;
    uint32_t timeout_ms;
    uint8_t parser_state;
    uint16_t expected_len;
    uint16_t received_len;
    uint8_t checksum;
    bm83_packet_t rx;
    uint8_t pending_frame[BM83_MAX_PAYLOAD + 5U];
    uint16_t pending_frame_len;
    uint8_t pending_command_id;
    uint8_t retry_count;
    uint8_t last_command_ack_status;
    uint32_t pending_since_ms;
    bool awaiting_command_ack;
    bool command_error_latched;
    bool initial_status_received;
    bool btm_status_received;
    uint8_t btm_state;
    uint8_t database_index;
    uint8_t uart_version_major;
    uint8_t uart_version_minor;
    bool uart_version_valid;
    bool pairing;
    bool a2dp_connected;
    bool avrcp_connected;
    bool auto_reconnect;
    bool reconnect_in_progress;
    bool avrcp_recovery_command_pending;
    bool paired_newest_valid;
    bool reconnect_stale_cleanup_used;
    bool reconnect_force_address;
    bool reconnect_exhausted;
    bool nonfatal_backpressure_waiting_ready;
    uint8_t reconnect_attempts;
    uint8_t nonfatal_backpressure_command_id;
    uint8_t paired_device_count;
    uint8_t paired_newest_record_index;
    uint8_t paired_newest_address[6];
    uint32_t reconnect_due_ms;
    uint32_t reconnect_started_ms;
    uint32_t reconnect_status_due_ms;
    uint32_t nonfatal_backpressure_since_ms;
} bm83_t;

typedef struct {
    /*
     * Command IDs vary with the BM83 firmware/configuration package. Leave
     * entries at zero when not explicitly verified against the final package.
     * No metadata/control opcode is fabricated by this driver.
     */
    uint8_t play_pause;
    uint8_t next_track;
    uint8_t previous_track;
    uint8_t volume_up;
    uint8_t volume_down;
} bm83_control_ids_t;

typedef enum {
    BM83_CONTROL_PLAY_PAUSE = 0,
    BM83_CONTROL_NEXT_TRACK,
    BM83_CONTROL_PREVIOUS_TRACK,
    BM83_CONTROL_VOLUME_UP,
    BM83_CONTROL_VOLUME_DOWN
} bm83_control_t;

typedef enum {
    BM83_MUSIC_STOP_SEEK = 0x00U,
    BM83_MUSIC_FAST_FORWARD = 0x01U,
    BM83_MUSIC_FAST_FORWARD_REPEAT = 0x02U,
    BM83_MUSIC_REWIND = 0x03U,
    BM83_MUSIC_REWIND_REPEAT = 0x04U,
    BM83_MUSIC_PLAY = 0x05U,
    BM83_MUSIC_PAUSE = 0x06U,
    BM83_MUSIC_TOGGLE = 0x07U,
    BM83_MUSIC_STOP = 0x08U,
    BM83_MUSIC_NEXT = 0x09U,
    BM83_MUSIC_PREVIOUS = 0x0AU
} bm83_music_action_t;

dev_status_t bm83_init(bm83_t *dev, const dev_uart_bus_t *uart,
                       const dev_gpio_t *reset_n, const dev_gpio_t *mfb,
                       const dev_gpio_t *tx_ind, const dev_clock_t *clock);
void bm83_set_packet_callback(bm83_t *dev, bm83_packet_cb_t cb, void *ctx);
dev_status_t bm83_send_packet(bm83_t *dev, uint8_t command_id,
                              const uint8_t *payload, uint16_t payload_len);
bool bm83_command_waiting_ready(const bm83_t *dev, uint8_t command_id);
dev_status_t bm83_unmask_all_events(bm83_t *dev);
dev_status_t bm83_music_control(bm83_t *dev, bm83_music_action_t action);
dev_status_t bm83_generate_tone(bm83_t *dev, uint8_t tone_type);
dev_status_t bm83_set_supported_classic_profiles(bm83_t *dev, uint8_t profile_mask);
dev_status_t bm83_read_version(bm83_t *dev, uint8_t type);
bool bm83_uart_version_at_least(const bm83_t *dev, uint8_t major, uint8_t minor);
dev_status_t bm83_read_link_status(bm83_t *dev);
dev_status_t bm83_read_paired_device_record(bm83_t *dev);
dev_status_t bm83_read_remote_avrcp_features(bm83_t *dev, uint8_t database_index);
dev_status_t bm83_link_back_last_device(bm83_t *dev);
dev_status_t bm83_link_back_last_a2dp(bm83_t *dev);
dev_status_t bm83_link_back_address_a2dp(bm83_t *dev, uint8_t device_index,
                                         const uint8_t address[6]);
void bm83_set_auto_reconnect(bm83_t *dev, bool enable);
dev_status_t bm83_service_connection(bm83_t *dev);
dev_status_t bm83_read_eeprom(bm83_t *dev, uint16_t offset, uint8_t length);
dev_status_t bm83_toggle_audio_source(bm83_t *dev);
dev_status_t bm83_mmi_action(bm83_t *dev, uint8_t database_index, uint8_t action);
dev_status_t bm83_request_now_playing_metadata(bm83_t *dev, uint8_t database_index);
dev_status_t bm83_request_now_playing_attribute(bm83_t *dev, uint8_t database_index,
                                                uint32_t attribute_id);
dev_status_t bm83_request_current_metadata_v206(bm83_t *dev, uint8_t database_index);
dev_status_t bm83_request_current_metadata_attribute_v206(bm83_t *dev,
                                                          uint8_t database_index,
                                                          uint8_t attribute_id);
dev_status_t bm83_register_track_changed(bm83_t *dev, uint8_t database_index);
dev_status_t bm83_request_event_capabilities(bm83_t *dev, uint8_t database_index);
dev_status_t bm83_request_media_player_list(bm83_t *dev, uint8_t database_index);
dev_status_t bm83_set_addressed_player(bm83_t *dev, uint8_t database_index,
                                      uint16_t player_id);
dev_status_t bm83_set_browsed_player(bm83_t *dev, uint8_t database_index,
                                    uint16_t player_id);
dev_status_t bm83_request_now_playing_browse(bm83_t *dev, uint8_t database_index);
dev_status_t bm83_request_item_attributes(bm83_t *dev, uint8_t database_index,
                                          uint8_t scope, const uint8_t uid[8],
                                          uint16_t uid_counter);
dev_status_t bm83_send_configured_control(bm83_t *dev,
                                          const bm83_control_ids_t *ids,
                                          bm83_control_t control,
                                          const uint8_t *payload,
                                          uint16_t payload_len);
dev_status_t bm83_hw_reset(bm83_t *dev, uint32_t low_ms, uint32_t settle_ms);
dev_status_t bm83_mfb_pulse(bm83_t *dev, uint32_t active_ms);
void bm83_rx_byte(bm83_t *dev, uint8_t byte);
void bm83_rx_data(bm83_t *dev, const uint8_t *data, size_t len);
/* Call regularly from the host task. It performs the documented 200 ms
 * retransmit and resets BM83 after a second missing Command_ACK. */
dev_status_t bm83_service(bm83_t *dev);

#ifdef __cplusplus
}
#endif

#endif
