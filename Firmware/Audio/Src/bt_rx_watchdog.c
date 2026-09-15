#include "bt_rx_watchdog.h"

#include <stddef.h>

void bt_rx_watchdog_reset(bt_rx_watchdog_t *watchdog, uint32_t now_ms,
                          uint32_t progress_count)
{
    if (watchdog == NULL) return;
    watchdog->last_progress_count = progress_count;
    watchdog->last_progress_ms = now_ms;
    watchdog->last_rearm_ms = now_ms;
    watchdog->rearm_attempts = 0U;
    watchdog->recovery_armed = false;
}

void bt_rx_watchdog_arm_rebuffer(bt_rx_watchdog_t *watchdog, uint32_t now_ms,
                                 uint32_t progress_count)
{
    if (watchdog == NULL) return;
    watchdog->last_progress_count = progress_count;
    watchdog->last_progress_ms = now_ms;
    watchdog->last_rearm_ms = now_ms;
    watchdog->rearm_attempts = 0U;
    watchdog->recovery_armed = true;
}

bool bt_rx_watchdog_should_rearm(bt_rx_watchdog_t *watchdog, uint32_t now_ms,
                                 uint32_t progress_count, bool fifo_starved,
                                 bool tx_started, bool peripheral_rx_pending)
{
    if (watchdog == NULL) return false;

    if (progress_count != watchdog->last_progress_count) {
        /* Progress resets the stall timer/backoff but does not end recovery by
         * itself. A transaction can deliver one half-buffer and then wedge
         * again; the owner disarms this watchdog only after the FIFO reaches its
         * start threshold and codec TX has actually restarted. */
        watchdog->last_progress_count = progress_count;
        watchdog->last_progress_ms = now_ms;
        watchdog->rearm_attempts = 0U;
        return false;
    }

    if (!watchdog->recovery_armed || !fifo_starved || tx_started) return false;
    if ((uint32_t)(now_ms - watchdog->last_progress_ms) < BT_RX_WATCHDOG_STALL_MS) {
        return false;
    }

    uint32_t retry_ms = 0U;
    if (watchdog->rearm_attempts != 0U) {
        if (peripheral_rx_pending) {
            retry_ms = BT_RX_WATCHDOG_ACTIVE_RETRY_MS;
        } else if (watchdog->rearm_attempts == 1U) {
            retry_ms = BT_RX_WATCHDOG_QUIET_RETRY_BASE_MS;
        } else {
            retry_ms = BT_RX_WATCHDOG_QUIET_RETRY_MAX_MS;
        }
        if ((uint32_t)(now_ms - watchdog->last_rearm_ms) < retry_ms) return false;
    }

    watchdog->last_rearm_ms = now_ms;
    if (watchdog->rearm_attempts != UINT8_MAX) ++watchdog->rearm_attempts;
    return true;
}
