#ifndef BQ25895_H
#define BQ25895_H

#include "device_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BQ25895_I2C_ADDR_DEFAULT 0x6AU

typedef struct {
    dev_i2c_bus_t i2c;
    uint8_t addr7;
    uint32_t timeout_ms;
} bq25895_t;

typedef struct {
    uint8_t status_raw;
    uint8_t fault_raw;
    uint16_t battery_mv;
    uint16_t system_mv;
    uint16_t vbus_mv;
    uint16_t charge_current_ma;
    bool power_good;
    bool watchdog_fault;
} bq25895_snapshot_t;

dev_status_t bq25895_init(bq25895_t *dev, const dev_i2c_bus_t *bus, uint8_t addr7);
dev_status_t bq25895_read_reg(bq25895_t *dev, uint8_t reg, uint8_t *value);
dev_status_t bq25895_write_reg(bq25895_t *dev, uint8_t reg, uint8_t value);
dev_status_t bq25895_update_bits(bq25895_t *dev, uint8_t reg, uint8_t mask, uint8_t value);
dev_status_t bq25895_read_snapshot(bq25895_t *dev, bq25895_snapshot_t *snapshot);
dev_status_t bq25895_set_adc_continuous(bq25895_t *dev, bool enable);
/* Force the charger's input front end into a conservative USB-C sink profile:
 * no D+/D- source detection/high-voltage handshake and a fixed IINLIM. This is
 * required by kiku Rev A, whose upstream PTC/TVS network is 5-V class. */
dev_status_t bq25895_configure_5v_only_input(bq25895_t *dev, uint16_t input_limit_ma);
dev_status_t bq25895_set_charge_enable(bq25895_t *dev, bool enable);
dev_status_t bq25895_set_input_current_limit_ma(bq25895_t *dev, uint16_t ma);
dev_status_t bq25895_set_charge_current_ma(bq25895_t *dev, uint16_t ma);
dev_status_t bq25895_set_watchdog_seconds(bq25895_t *dev, uint16_t seconds);
dev_status_t bq25895_kick_watchdog(bq25895_t *dev);

#ifdef __cplusplus
}
#endif

#endif
