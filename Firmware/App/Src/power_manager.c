#include "power_manager.h"

#include <string.h>

#define POWER_GAUGE_VOLTAGE_MISMATCH_MV 250U

typedef struct {
    uint16_t mv;
    uint16_t centi_percent;
} battery_curve_point_t;

/* Conservative 1-cell Li-ion open-circuit approximation used only when the
 * MAX17048 CELL measurement is demonstrably inconsistent with the charger's
 * direct BAT ADC. It is deliberately not presented as fuel-gauge accuracy. */
static uint16_t estimate_soc_from_voltage(uint16_t mv, bool charge_done)
{
    if (charge_done && mv >= 4100U) return 10000U;

    static const battery_curve_point_t curve[] = {
        {3300U,     0U},
        {3500U,   500U},
        {3600U,  1000U},
        {3700U,  2000U},
        {3800U,  4000U},
        {3900U,  6000U},
        {4000U,  7800U},
        {4100U,  9000U},
        {4200U, 10000U},
    };

    if (mv <= curve[0].mv) return curve[0].centi_percent;
    for (size_t i = 1U; i < sizeof(curve) / sizeof(curve[0]); ++i) {
        if (mv <= curve[i].mv) {
            uint32_t span_mv = (uint32_t)curve[i].mv - curve[i - 1U].mv;
            uint32_t span_soc = (uint32_t)curve[i].centi_percent - curve[i - 1U].centi_percent;
            uint32_t offset_mv = (uint32_t)mv - curve[i - 1U].mv;
            return (uint16_t)(curve[i - 1U].centi_percent +
                              ((span_soc * offset_mv) / span_mv));
        }
    }
    return 10000U;
}

dev_status_t power_manager_init(power_manager_t *mgr, bq25895_t *charger,
                                max17048_t *gauge, uint16_t low_threshold,
                                uint16_t critical_threshold, uint32_t poll_interval_ms)
{
    if ((mgr == NULL) || (charger == NULL) || (gauge == NULL) ||
        (critical_threshold > low_threshold)) return DEV_EINVAL;
    memset(mgr, 0, sizeof(*mgr));
    mgr->charger = charger;
    mgr->gauge = gauge;
    mgr->low_threshold_centi_percent = low_threshold;
    mgr->critical_threshold_centi_percent = critical_threshold;
    mgr->poll_interval_ms = poll_interval_ms;
    mgr->level = POWER_LEVEL_UNKNOWN;
    return DEV_OK;
}

dev_status_t power_manager_apply_bench_charge_policy(power_manager_t *mgr,
                                                     uint16_t input_limit_ma,
                                                     uint16_t charge_current_ma,
                                                     uint16_t watchdog_seconds)
{
    if ((mgr == NULL) || (mgr->charger == NULL)) return DEV_EINVAL;
    dev_status_t st = bq25895_set_input_current_limit_ma(mgr->charger, input_limit_ma);
    if (st != DEV_OK) return st;
    st = bq25895_set_charge_current_ma(mgr->charger, charge_current_ma);
    if (st != DEV_OK) return st;
    st = bq25895_set_watchdog_seconds(mgr->charger, watchdog_seconds);
    if (st != DEV_OK) return st;
    mgr->watchdog_seconds = watchdog_seconds;
    return DEV_OK;
}

dev_status_t power_manager_poll(power_manager_t *mgr, uint32_t now_ms)
{
    if ((mgr == NULL) || (mgr->charger == NULL) || (mgr->gauge == NULL)) return DEV_EINVAL;
    if ((mgr->charger_valid || mgr->gauge_valid) &&
        ((uint32_t)(now_ms - mgr->last_poll_ms) < mgr->poll_interval_ms)) return DEV_EBUSY;
    mgr->last_poll_ms = now_ms;

    dev_status_t a = bq25895_read_snapshot(mgr->charger, &mgr->charger_status);
    mgr->charger_valid = (a == DEV_OK);
    dev_status_t b = max17048_read_snapshot(mgr->gauge, &mgr->gauge_status);
    mgr->gauge_valid = (b == DEV_OK);

    mgr->gauge_consistent = mgr->gauge_valid;
    mgr->using_charger_fallback = false;
    if (mgr->gauge_valid && mgr->charger_valid && mgr->charger_status.battery_mv >= 2500U) {
        uint16_t ga = mgr->gauge_status.voltage_mv;
        uint16_t ch = mgr->charger_status.battery_mv;
        uint16_t diff = (ga > ch) ? (uint16_t)(ga - ch) : (uint16_t)(ch - ga);
        mgr->gauge_consistent = diff <= POWER_GAUGE_VOLTAGE_MISMATCH_MV;
    }

    if (mgr->gauge_valid && mgr->gauge_consistent) {
        mgr->effective_battery_mv = mgr->gauge_status.voltage_mv;
        mgr->effective_soc_centi_percent = mgr->gauge_status.soc_centi_percent;
    } else if (mgr->charger_valid && mgr->charger_status.battery_mv >= 2500U) {
        const uint8_t charge_state = (uint8_t)((mgr->charger_status.status_raw >> 3U) & 0x03U);
        const bool charge_done = charge_state == 0x03U;
        mgr->effective_battery_mv = mgr->charger_status.battery_mv;
        mgr->effective_soc_centi_percent =
            estimate_soc_from_voltage(mgr->effective_battery_mv, charge_done);
        mgr->using_charger_fallback = true;
    } else {
        mgr->effective_battery_mv = 0U;
        mgr->effective_soc_centi_percent = 0U;
    }

    if ((mgr->gauge_valid && mgr->gauge_consistent) || mgr->using_charger_fallback) {
        uint16_t soc = mgr->effective_soc_centi_percent;
        if (soc <= mgr->critical_threshold_centi_percent) mgr->level = POWER_LEVEL_CRITICAL;
        else if (soc <= mgr->low_threshold_centi_percent) mgr->level = POWER_LEVEL_LOW;
        else mgr->level = POWER_LEVEL_NORMAL;
    } else {
        mgr->level = POWER_LEVEL_UNKNOWN;
    }

    if ((mgr->watchdog_seconds != 0U) &&
        ((uint32_t)(now_ms - mgr->last_watchdog_kick_ms) >=
         ((uint32_t)mgr->watchdog_seconds * 500U))) {
        if (bq25895_kick_watchdog(mgr->charger) == DEV_OK) mgr->last_watchdog_kick_ms = now_ms;
    }

    if ((a != DEV_OK) && (b != DEV_OK)) return DEV_EIO;
    return DEV_OK;
}
