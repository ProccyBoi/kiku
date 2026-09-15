#ifndef TLV320AIC3104_H
#define TLV320AIC3104_H

#include "device_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TLV320AIC3104_I2C_ADDR_DEFAULT 0x18U

typedef struct {
    uint8_t page;
    uint8_t reg;
    uint8_t value;
} tlv320_reg_write_t;

typedef struct {
    dev_i2c_bus_t i2c;
    dev_gpio_t reset_n;
    dev_clock_t clock;
    uint8_t addr7;
    uint8_t current_page;
    uint32_t timeout_ms;
} tlv320aic3104_t;

dev_status_t tlv320aic3104_init(tlv320aic3104_t *dev, const dev_i2c_bus_t *bus,
                                const dev_gpio_t *reset_n, const dev_clock_t *clock,
                                uint8_t addr7);
dev_status_t tlv320aic3104_hw_reset(tlv320aic3104_t *dev);
dev_status_t tlv320aic3104_sw_reset(tlv320aic3104_t *dev);
dev_status_t tlv320aic3104_read_reg(tlv320aic3104_t *dev, uint8_t page,
                                    uint8_t reg, uint8_t *value);
dev_status_t tlv320aic3104_write_reg(tlv320aic3104_t *dev, uint8_t page,
                                     uint8_t reg, uint8_t value);
dev_status_t tlv320aic3104_apply_script(tlv320aic3104_t *dev,
                                        const tlv320_reg_write_t *script,
                                        size_t count);

/*
 * Board-level audio configuration is intentionally table-driven. Codec clock,
 * PLL, serial-format and analogue routing values depend on the final I2S clock
 * plan and should be supplied as a reviewed register script rather than hidden
 * as magic constants in this driver.
 */

#ifdef __cplusplus
}
#endif

#endif
