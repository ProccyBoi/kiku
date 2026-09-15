#ifndef KIKU_BT_RX_WATCHDOG_H
#define KIKU_BT_RX_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A pause can stop the BM83 I2S clock long enough for the STM32H5 slave RX
 * transaction to become stale. Do not treat clock absence itself as a fault:
 * only recover after an actual Bluetooth FIFO underrun has put playback into
 * rebuffer mode. Quiet retries back off to avoid stop/start thrash while the
 * phone is intentionally paused; RX-pending/OVR evidence allows a quicker retry
 * once clocks/data have returned but DMA callbacks still are not progressing. */
#define BT_RX_WATCHDOG_STALL_MS              1000U
#define BT_RX_WATCHDOG_ACTIVE_RETRY_MS        500U
#define BT_RX_WATCHDOG_QUIET_RETRY_BASE_MS   2000U
#define BT_RX_WATCHDOG_QUIET_RETRY_MAX_MS    4000U

typedef struct {
    uint32_t last_progress_count;
    uint32_t last_progress_ms;
    uint32_t last_rearm_ms;
    uint8_t rearm_attempts;
    bool recovery_armed;
} bt_rx_watchdog_t;

void bt_rx_watchdog_reset(bt_rx_watchdog_t *watchdog, uint32_t now_ms,
                          uint32_t progress_count);
void bt_rx_watchdog_arm_rebuffer(bt_rx_watchdog_t *watchdog, uint32_t now_ms,
                                 uint32_t progress_count);
bool bt_rx_watchdog_should_rearm(bt_rx_watchdog_t *watchdog, uint32_t now_ms,
                                 uint32_t progress_count, bool fifo_starved,
                                 bool tx_started, bool peripheral_rx_pending);

#ifdef __cplusplus
}
#endif

#endif
