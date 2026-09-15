#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include "bq25895.h"
#include "max17048.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POWER_LEVEL_UNKNOWN = 0,
    POWER_LEVEL_NORMAL,
    POWER_LEVEL_LOW,
    POWER_LEVEL_CRITICAL
} power_level_t;

typedef struct {
    bq25895_t *charger;
    max17048_t *gauge;
    bq25895_snapshot_t charger_status;
    max17048_snapshot_t gauge_status;
    power_level_t level;
    uint16_t low_threshold_centi_percent;
    uint16_t critical_threshold_centi_percent;
    uint32_t poll_interval_ms;
    uint32_t last_poll_ms;
    uint32_t last_watchdog_kick_ms;
    uint16_t watchdog_seconds;
    bool charger_valid;
    bool gauge_valid;
    bool gauge_consistent;
    bool using_charger_fallback;
    uint16_t effective_battery_mv;
    uint16_t effective_soc_centi_percent;
} power_manager_t;

dev_status_t power_manager_init(power_manager_t *mgr, bq25895_t *charger,
                                max17048_t *gauge, uint16_t low_threshold,
                                uint16_t critical_threshold, uint32_t poll_interval_ms);
dev_status_t power_manager_apply_bench_charge_policy(power_manager_t *mgr,
                                                     uint16_t input_limit_ma,
                                                     uint16_t charge_current_ma,
                                                     uint16_t watchdog_seconds);
dev_status_t power_manager_poll(power_manager_t *mgr, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
