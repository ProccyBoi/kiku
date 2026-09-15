#ifndef MAX17048_H
#define MAX17048_H

#include "device_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX17048_I2C_ADDR_DEFAULT 0x36U

typedef struct {
    dev_i2c_bus_t i2c;
    uint8_t addr7;
    uint32_t timeout_ms;
} max17048_t;

typedef struct {
    uint16_t voltage_mv;
    uint16_t soc_centi_percent;
    int32_t crate_centi_percent_per_hour;
    uint16_t status_raw;
} max17048_snapshot_t;

dev_status_t max17048_init(max17048_t *dev, const dev_i2c_bus_t *bus, uint8_t addr7);
dev_status_t max17048_read_reg(max17048_t *dev, uint8_t reg, uint16_t *value);
dev_status_t max17048_write_reg(max17048_t *dev, uint8_t reg, uint16_t value);
dev_status_t max17048_read_snapshot(max17048_t *dev, max17048_snapshot_t *snapshot);
dev_status_t max17048_quick_start(max17048_t *dev);
dev_status_t max17048_get_version(max17048_t *dev, uint16_t *version);

#ifdef __cplusplus
}
#endif

#endif
