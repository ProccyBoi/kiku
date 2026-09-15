#include "fm_state.h"

#include <string.h>

static bool rds_block_ok(uint8_t ble)
{
    /* Si4705 BLE=0/1/2 are usable (none, 1-2, or 3-5 corrected errors).
     * BLE=3 is uncorrectable. The tuner is configured to accept BLE2, so
     * throwing BLE2 away here made weaker-but-valid Sydney RDS stations much
     * slower or impossible to assemble into complete PS/RadioText strings. */
    return ble <= 2U;
}

static bool text_has_visible(const char *text, size_t max_len)
{
    if (text == NULL) return false;
    for (size_t i = 0U; i < max_len && text[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c > (unsigned char)' ' && c < 0x7FU) return true;
    }
    return false;
}

static void clear_rtplus(fm_state_t *state)
{
    state->rtplus_title[0] = '\0';
    state->rtplus_artist[0] = '\0';
}

static bool copy_rt_span(char *dst, size_t dst_size, const fm_state_t *state,
                         uint8_t start, uint8_t length_minus_one)
{
    if (dst == NULL || dst_size == 0U || state == NULL) return false;
    size_t len = (size_t)length_minus_one + 1U;
    if ((size_t)start >= 64U || (size_t)start + len > 64U) return false;
    if (len >= dst_size) len = dst_size - 1U;

    /* Unreceived RadioText positions are NUL. Do not publish a partial RT+
     * tag until every character in its span has arrived. */
    for (size_t i = 0U; i < len; ++i) {
        if (state->radio_text[(size_t)start + i] == '\0') return false;
    }

    memcpy(dst, &state->radio_text[start], len);
    dst[len] = '\0';
    while (len != 0U && (dst[len - 1U] == ' ' || dst[len - 1U] == '\r' || dst[len - 1U] == '\n')) {
        dst[--len] = '\0';
    }
    return len != 0U;
}

static void apply_rtplus_tags(fm_state_t *state)
{
    if (state == NULL || !state->rtplus_group_valid || !state->rtplus_item_running) return;

    char first[65] = {0};
    char second[65] = {0};
    bool have_first = copy_rt_span(first, sizeof(first), state,
                                   state->rtplus_start1, state->rtplus_len1);
    bool have_second = copy_rt_span(second, sizeof(second), state,
                                    state->rtplus_start2, state->rtplus_len2);

    /* IEC 62106 RT+ content types: 1=ITEM.TITLE, 4=ITEM.ARTIST. */
    if (state->rtplus_type1 == 1U && have_first) {
        (void)strncpy(state->rtplus_title, first, sizeof(state->rtplus_title) - 1U);
        state->rtplus_title[sizeof(state->rtplus_title) - 1U] = '\0';
    } else if (state->rtplus_type1 == 4U && have_first) {
        (void)strncpy(state->rtplus_artist, first, sizeof(state->rtplus_artist) - 1U);
        state->rtplus_artist[sizeof(state->rtplus_artist) - 1U] = '\0';
    }

    if (state->rtplus_type2 == 1U && have_second) {
        (void)strncpy(state->rtplus_title, second, sizeof(state->rtplus_title) - 1U);
        state->rtplus_title[sizeof(state->rtplus_title) - 1U] = '\0';
    } else if (state->rtplus_type2 == 4U && have_second) {
        (void)strncpy(state->rtplus_artist, second, sizeof(state->rtplus_artist) - 1U);
        state->rtplus_artist[sizeof(state->rtplus_artist) - 1U] = '\0';
    }
}

void fm_state_reset(fm_state_t *state, uint16_t frequency_10khz)
{
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    state->frequency_10khz = frequency_10khz;
    memset(state->program_service, ' ', 8U);
    state->program_service[8] = '\0';
}

void fm_state_apply_tune_status(fm_state_t *state, const si4705_tune_status_t *status)
{
    if ((state == NULL) || (status == NULL)) return;
    if (state->frequency_10khz != status->frequency_10khz) {
        uint16_t freq = status->frequency_10khz;
        fm_state_reset(state, freq);
    }
    state->frequency_10khz = status->frequency_10khz;
    state->rssi_dbuv = status->rssi_dbuv;
    state->snr_db = status->snr_db;
    state->stereo = status->stereo;
    state->rds_sync = status->rds_sync;
}

void fm_state_apply_signal_quality(fm_state_t *state, const si4705_signal_quality_t *quality)
{
    if ((state == NULL) || (quality == NULL)) return;
    state->rssi_dbuv = quality->rssi_dbuv;
    state->snr_db = quality->snr_db;
    state->stereo = quality->pilot_present && (quality->stereo_blend_percent != 0U);
}

void fm_state_process_rds(fm_state_t *state, const si4705_rds_group_t *g)
{
    if ((state == NULL) || (g == NULL)) return;
    state->rds_sync = g->sync;
    /* FM_RDS_STATUS returns receiver status even when its FIFO has no group.
     * The block fields are not a new RDS group in that case. Processing them
     * used to treat the empty/stale block A as a PI change and repeatedly wipe
     * accumulated station text between real groups. */
    if (!g->fifo_used) return;
    if (!g->sync || !rds_block_ok(g->ble_a) || !rds_block_ok(g->ble_b)) return;

    if ((state->pi != 0U) && (state->pi != g->block_a)) {
        uint16_t freq = state->frequency_10khz;
        fm_state_reset(state, freq);
    }
    state->pi = g->block_a;
    state->pty = (uint8_t)((g->block_b >> 5) & 0x1FU);

    uint8_t group_type = (uint8_t)((g->block_b >> 12) & 0x0FU);
    bool version_b = ((g->block_b >> 11) & 1U) != 0U;
    uint8_t group_code = (uint8_t)((group_type << 1) | (version_b ? 1U : 0U));

    /* Group 3A announces Open Data Applications. RT+ uses AID 0x4BD7 and
     * dynamically assigns one of the xA groups to carry the title/artist tags. */
    if (group_type == 3U && !version_b && rds_block_ok(g->ble_d) && g->block_d == FM_RTPLUS_AID) {
        state->rtplus_group_code = (uint8_t)(g->block_b & 0x1FU);
        state->rtplus_group_valid = true;
        return;
    }

    if (group_type == 0U && rds_block_ok(g->ble_d)) {
        uint8_t seg = (uint8_t)(g->block_b & 0x03U);
        state->program_service[2U * seg] = (char)(g->block_d >> 8);
        state->program_service[2U * seg + 1U] = (char)g->block_d;
        state->ps_valid_mask |= (uint8_t)(1U << seg);
        state->program_service[8] = '\0';
    } else if (group_type == 2U) {
        /* A damaged text block must not change A/B state and erase a song. */
        if (!rds_block_ok(g->ble_d) || (!version_b && !rds_block_ok(g->ble_c))) return;
        bool ab = ((g->block_b >> 4) & 1U) != 0U;
        if ((state->rt_valid_mask != 0U) &&
            (ab != state->radio_text_ab || version_b != state->radio_text_version_b)) {
            memset(state->radio_text, 0, sizeof(state->radio_text));
            state->rt_valid_mask = 0U;
            clear_rtplus(state);
        }
        state->radio_text_ab = ab;
        state->radio_text_version_b = version_b;
        uint8_t seg = (uint8_t)(g->block_b & 0x0FU);
        if (!version_b && rds_block_ok(g->ble_c) && rds_block_ok(g->ble_d)) {
            size_t o = (size_t)seg * 4U;
            state->radio_text[o] = (char)(g->block_c >> 8);
            state->radio_text[o + 1U] = (char)g->block_c;
            state->radio_text[o + 2U] = (char)(g->block_d >> 8);
            state->radio_text[o + 3U] = (char)g->block_d;
            state->rt_valid_mask |= (uint16_t)(1U << seg);
            state->radio_text[64] = '\0';
        } else if (version_b && rds_block_ok(g->ble_d)) {
            size_t o = (size_t)seg * 2U;
            state->radio_text[o] = (char)(g->block_d >> 8);
            state->radio_text[o + 1U] = (char)g->block_d;
            state->rt_valid_mask |= (uint16_t)(1U << seg);
            /* Segments can arrive out of order or repeat. Terminating after
             * each pair erased the next segment on every repeated 2B group. */
            state->radio_text[32] = '\0';
        }
        for (size_t i = 0U; i < 64U; ++i) {
            if ((unsigned char)state->radio_text[i] == 0x0DU) {
                state->radio_text[i] = '\0';
                break;
            }
        }
        apply_rtplus_tags(state);
    } else if (state->rtplus_group_valid && group_code == state->rtplus_group_code &&
               !version_b && rds_block_ok(g->ble_c) && rds_block_ok(g->ble_d)) {
        /* RT+ ODA group xA packs 37 bits after the group/version/PTY header:
         * item-toggle, item-running, then two {type,start,length} tags. */
        uint64_t bits = ((uint64_t)(g->block_b & 0x1FU) << 32) |
                        ((uint64_t)g->block_c << 16) | (uint64_t)g->block_d;
        bool toggle = ((bits >> 36) & 1U) != 0U;
        bool running = ((bits >> 35) & 1U) != 0U;
        if (!state->rtplus_toggle_valid || toggle != state->rtplus_item_toggle) {
            clear_rtplus(state);
        }
        state->rtplus_toggle_valid = true;
        state->rtplus_item_toggle = toggle;
        state->rtplus_item_running = running;
        state->rtplus_type1 = (uint8_t)((bits >> 29) & 0x3FU);
        state->rtplus_start1 = (uint8_t)((bits >> 23) & 0x3FU);
        state->rtplus_len1 = (uint8_t)((bits >> 17) & 0x3FU);
        state->rtplus_type2 = (uint8_t)((bits >> 11) & 0x3FU);
        state->rtplus_start2 = (uint8_t)((bits >> 5) & 0x3FU);
        state->rtplus_len2 = (uint8_t)(bits & 0x1FU);
        if (!running) clear_rtplus(state);
        else apply_rtplus_tags(state);
    }
}

bool fm_state_has_program_service(const fm_state_t *state)
{
    if (state == NULL || state->ps_valid_mask != 0x0FU) return false;
    return text_has_visible(state->program_service, 8U);
}

bool fm_state_has_partial_program_service(const fm_state_t *state)
{
    /* PS is transmitted as four independent two-character segments. Waiting
     * for all four makes the station name feel unnecessarily latent even when
     * the beginning of the name is already reliable. Only expose a partial
     * name once segment 0 has arrived, avoiding a confusing string made solely
     * from later segments with leading blanks. */
    if (state == NULL || (state->ps_valid_mask & 0x01U) == 0U) return false;
    return text_has_visible(state->program_service, 8U);
}

bool fm_state_has_radio_text(const fm_state_t *state)
{
    return state != NULL && text_has_visible(state->radio_text, 64U);
}

bool fm_state_has_rtplus_title(const fm_state_t *state)
{
    return state != NULL && text_has_visible(state->rtplus_title, 64U);
}

bool fm_state_has_rtplus_artist(const fm_state_t *state)
{
    return state != NULL && text_has_visible(state->rtplus_artist, 64U);
}
