#include "bq25895.h"

#define BQ_REG_INPUT_SRC_CTRL   0x00U
#define BQ_REG_ADC_CTRL         0x02U
#define BQ_REG_PWR_ON_CFG       0x03U
#define BQ_REG_CHG_CURRENT      0x04U
#define BQ_REG_TERM_TIMER       0x07U
#define BQ_REG_STATUS           0x0BU
#define BQ_REG_FAULT            0x0CU
#define BQ_REG_BAT_ADC          0x0EU
#define BQ_REG_SYS_ADC          0x0FU
#define BQ_REG_VBUS_ADC         0x11U
#define BQ_REG_ICHG_ADC         0x12U

#define BQ_CHG_CONFIG_MASK      0x10U
#define BQ_WDT_RESET_MASK       0x40U
#define BQ_WATCHDOG_MASK        0x30U
#define BQ_IINLIM_MASK          0x3FU
#define BQ_ICHG_MASK            0x7FU
#define BQ_CONV_RATE_MASK       0x40U
#define BQ_HVDCP_EN_MASK        0x08U
#define BQ_MAXC_EN_MASK         0x04U
#define BQ_FORCE_DPDM_MASK      0x02U
#define BQ_AUTO_DPDM_EN_MASK    0x01U

static dev_status_t bq_read(bq25895_t *dev, uint8_t reg, uint8_t *value)
{
    if ((dev == NULL) || (value == NULL) || !dev_i2c_valid(&dev->i2c)) {
        return DEV_EINVAL;
    }
    if (dev->i2c.write_read != NULL) {
        return dev->i2c.write_read(dev->i2c.ctx, dev->addr7, &reg, 1U,
                                   value, 1U, dev->timeout_ms);
    }
    dev_status_t st = dev->i2c.write(dev->i2c.ctx, dev->addr7, &reg, 1U,
                                     dev->timeout_ms);
    if (st != DEV_OK) {
        return st;
    }
    return dev->i2c.read(dev->i2c.ctx, dev->addr7, value, 1U, dev->timeout_ms);
}

dev_status_t bq25895_init(bq25895_t *dev, const dev_i2c_bus_t *bus, uint8_t addr7)
{
    if ((dev == NULL) || (bus == NULL) || !dev_i2c_valid(bus)) {
        return DEV_EINVAL;
    }
    dev->i2c = *bus;
    dev->addr7 = (addr7 == 0U) ? BQ25895_I2C_ADDR_DEFAULT : addr7;
    dev->timeout_ms = 20U;
    return DEV_OK;
}

dev_status_t bq25895_read_reg(bq25895_t *dev, uint8_t reg, uint8_t *value)
{
    return bq_read(dev, reg, value);
}

dev_status_t bq25895_write_reg(bq25895_t *dev, uint8_t reg, uint8_t value)
{
    uint8_t frame[2] = { reg, value };
    if ((dev == NULL) || (dev->i2c.write == NULL)) {
        return DEV_EINVAL;
    }
    return dev->i2c.write(dev->i2c.ctx, dev->addr7, frame, sizeof(frame), dev->timeout_ms);
}

dev_status_t bq25895_update_bits(bq25895_t *dev, uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t current = 0U;
    dev_status_t st = bq_read(dev, reg, &current);
    if (st != DEV_OK) {
        return st;
    }
    current = (uint8_t)((current & (uint8_t)~mask) | (value & mask));
    return bq25895_write_reg(dev, reg, current);
}

dev_status_t bq25895_set_charge_enable(bq25895_t *dev, bool enable)
{
    /* CHG_CONFIG = 01 enables charging; 00 disables. */
    return bq25895_update_bits(dev, BQ_REG_PWR_ON_CFG, BQ_CHG_CONFIG_MASK,
                               enable ? 0x10U : 0x00U);
}

dev_status_t bq25895_set_input_current_limit_ma(bq25895_t *dev, uint16_t ma)
{
    uint16_t limited = ma;
    if (limited < 100U) limited = 100U;
    if (limited > 3250U) limited = 3250U;
    uint8_t code = (uint8_t)((limited - 100U) / 50U);
    return bq25895_update_bits(dev, BQ_REG_INPUT_SRC_CTRL, BQ_IINLIM_MASK, code);
}

dev_status_t bq25895_set_charge_current_ma(bq25895_t *dev, uint16_t ma)
{
    uint16_t limited = (ma > 5056U) ? 5056U : ma;
    uint8_t code = (uint8_t)(limited / 64U);
    return bq25895_update_bits(dev, BQ_REG_CHG_CURRENT, BQ_ICHG_MASK, code);
}

dev_status_t bq25895_set_watchdog_seconds(bq25895_t *dev, uint16_t seconds)
{
    uint8_t field;
    if (seconds == 0U) field = 0x00U;
    else if (seconds <= 40U) field = 0x10U;
    else if (seconds <= 80U) field = 0x20U;
    else field = 0x30U;
    return bq25895_update_bits(dev, BQ_REG_TERM_TIMER, BQ_WATCHDOG_MASK, field);
}

dev_status_t bq25895_kick_watchdog(bq25895_t *dev)
{
    return bq25895_update_bits(dev, BQ_REG_PWR_ON_CFG, BQ_WDT_RESET_MASK, BQ_WDT_RESET_MASK);
}

dev_status_t bq25895_set_adc_continuous(bq25895_t *dev, bool enable)
{
    /* REG02.CONV_RATE=1 refreshes BAT/SYS/VBUS/ICHG monitor registers once
     * per second. This is enabled while the player is awake so battery UI and
     * diagnostics never report the reset-value ADC codes as real voltages. */
    return bq25895_update_bits(dev, BQ_REG_ADC_CTRL, BQ_CONV_RATE_MASK,
                               enable ? BQ_CONV_RATE_MASK : 0U);
}

dev_status_t bq25895_configure_5v_only_input(bq25895_t *dev, uint16_t input_limit_ma)
{
    if (dev == NULL) return DEV_EINVAL;

    /* BQ25895 reset defaults enable AUTO_DPDM, HVDCP and MaxCharge. A detected
     * DCP may therefore be asked to raise VBUS to 9/12 V without any host
     * command. kiku Rev A deliberately uses a 5-V TVS and a 6-V-rated
     * 750-mA PTC upstream of VBUS, so high-voltage negotiation is forbidden.
     *
     * Disable automatic/forced D+/D- detection as well as both adjustable-high-
     * voltage handshakes. This also prevents source detection from later
     * overwriting REG00.IINLIM with 1.5-3.25 A. USB-C CC has no current-sense
     * controller on this board, so a fixed conservative current is preferable. */
    const uint8_t dpdm_mask = BQ_HVDCP_EN_MASK | BQ_MAXC_EN_MASK |
                              BQ_FORCE_DPDM_MASK | BQ_AUTO_DPDM_EN_MASK;
    dev_status_t st = bq25895_update_bits(dev, BQ_REG_ADC_CTRL, dpdm_mask, 0U);
    if (st != DEV_OK) return st;

    return bq25895_set_input_current_limit_ma(dev, input_limit_ma);
}

dev_status_t bq25895_read_snapshot(bq25895_t *dev, bq25895_snapshot_t *snapshot)
{
    if ((dev == NULL) || (snapshot == NULL)) return DEV_EINVAL;
    uint8_t st_raw, fault, bat, sys, vbus, ichg;
    dev_status_t st;
    st = bq_read(dev, BQ_REG_STATUS, &st_raw); if (st != DEV_OK) return st;
    st = bq_read(dev, BQ_REG_FAULT, &fault); if (st != DEV_OK) return st;
    st = bq_read(dev, BQ_REG_BAT_ADC, &bat); if (st != DEV_OK) return st;
    st = bq_read(dev, BQ_REG_SYS_ADC, &sys); if (st != DEV_OK) return st;
    st = bq_read(dev, BQ_REG_VBUS_ADC, &vbus); if (st != DEV_OK) return st;
    st = bq_read(dev, BQ_REG_ICHG_ADC, &ichg); if (st != DEV_OK) return st;

    snapshot->status_raw = st_raw;
    snapshot->fault_raw = fault;
    snapshot->battery_mv = (uint16_t)(2304U + ((uint16_t)(bat & 0x7FU) * 20U));
    snapshot->system_mv = (uint16_t)(2304U + ((uint16_t)(sys & 0x7FU) * 20U));
    snapshot->vbus_mv = (uint16_t)(2600U + ((uint16_t)(vbus & 0x7FU) * 100U));
    snapshot->charge_current_ma = (uint16_t)((uint16_t)(ichg & 0x7FU) * 50U);
    snapshot->power_good = (st_raw & 0x04U) != 0U;
    snapshot->watchdog_fault = (fault & 0x80U) != 0U;
    return DEV_OK;
}
