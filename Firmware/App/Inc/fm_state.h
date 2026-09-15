#ifndef FM_STATE_H
#define FM_STATE_H

#include "si4705.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FM_RTPLUS_AID 0x4BD7U

typedef struct {
    uint16_t frequency_10khz;
    uint16_t pi;
    uint8_t pty;
    char program_service[9];
    char radio_text[65];
    char rtplus_title[65];
    char rtplus_artist[65];
    uint8_t ps_valid_mask;
    uint16_t rt_valid_mask;
    uint8_t rtplus_group_code;
    uint8_t rtplus_type1;
    uint8_t rtplus_start1;
    uint8_t rtplus_len1;
    uint8_t rtplus_type2;
    uint8_t rtplus_start2;
    uint8_t rtplus_len2;
    bool radio_text_ab;
    bool radio_text_version_b;
    bool stereo;
    uint8_t rssi_dbuv;
    uint8_t snr_db;
    bool rds_sync;
    bool rtplus_group_valid;
    bool rtplus_toggle_valid;
    bool rtplus_item_toggle;
    bool rtplus_item_running;
} fm_state_t;

void fm_state_reset(fm_state_t *state, uint16_t frequency_10khz);
void fm_state_apply_tune_status(fm_state_t *state, const si4705_tune_status_t *status);
void fm_state_apply_signal_quality(fm_state_t *state, const si4705_signal_quality_t *quality);
void fm_state_process_rds(fm_state_t *state, const si4705_rds_group_t *group);
bool fm_state_has_partial_program_service(const fm_state_t *state);
bool fm_state_has_program_service(const fm_state_t *state);
bool fm_state_has_radio_text(const fm_state_t *state);
bool fm_state_has_rtplus_title(const fm_state_t *state);
bool fm_state_has_rtplus_artist(const fm_state_t *state);

#ifdef __cplusplus
}
#endif

#endif
