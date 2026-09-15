#include "si4705.h"

#define SI_CMD_POWER_UP        0x01U
#define SI_CMD_POWER_DOWN      0x11U
#define SI_CMD_SET_PROPERTY    0x12U
#define SI_CMD_GET_PROPERTY    0x13U
#define SI_CMD_GET_INT_STATUS  0x14U
#define SI_CMD_FM_TUNE_FREQ    0x20U
#define SI_CMD_FM_SEEK_START   0x21U
#define SI_CMD_FM_TUNE_STATUS  0x22U
#define SI_CMD_FM_RSQ_STATUS   0x23U
#define SI_CMD_FM_RDS_STATUS   0x24U

#define SI_CTS                 0x80U
#define SI_ERR                 0x40U

static dev_status_t si_wait_cts(si4705_t *dev, uint8_t *status)
{
    if ((dev == NULL) || (dev->i2c.read == NULL)) return DEV_EINVAL;
    uint32_t start = (dev->clock.time_ms != NULL) ? dev->clock.time_ms(dev->clock.ctx) : 0U;
    for (;;) {
        uint8_t s = 0U;
        dev_status_t st = dev->i2c.read(dev->i2c.ctx, dev->addr7, &s, 1U, dev->timeout_ms);
        if (st != DEV_OK) return st;
        if ((s & SI_CTS) != 0U) {
            if (status != NULL) *status = s;
            return ((s & SI_ERR) != 0U) ? DEV_EIO : DEV_OK;
        }
        if (dev->clock.delay_ms != NULL) dev->clock.delay_ms(dev->clock.ctx, 1U);
        if (dev->clock.time_ms != NULL) {
            if ((uint32_t)(dev->clock.time_ms(dev->clock.ctx) - start) >= dev->timeout_ms) return DEV_ETIMEOUT;
        } else if (start++ >= dev->timeout_ms) {
            return DEV_ETIMEOUT;
        }
    }
}

static dev_status_t si_command(si4705_t *dev, const uint8_t *tx, size_t tx_len,
                               uint8_t *rx, size_t rx_len)
{
    if ((dev == NULL) || (tx == NULL) || (tx_len == 0U) || (dev->i2c.write == NULL)) return DEV_EINVAL;
    dev_status_t st = si_wait_cts(dev, NULL);
    if (st != DEV_OK) return st;
    st = dev->i2c.write(dev->i2c.ctx, dev->addr7, tx, tx_len, dev->timeout_ms);
    if (st != DEV_OK) return st;
    st = si_wait_cts(dev, NULL);
    if (st != DEV_OK) return st;
    if ((rx != NULL) && (rx_len != 0U)) {
        if (dev->i2c.read == NULL) return DEV_ENOTSUP;
        return dev->i2c.read(dev->i2c.ctx, dev->addr7, rx, rx_len, dev->timeout_ms);
    }
    return DEV_OK;
}

dev_status_t si4705_init(si4705_t *dev, const dev_i2c_bus_t *bus,
                         const dev_gpio_t *reset_n, const dev_clock_t *clock,
                         uint8_t addr7)
{
    if ((dev == NULL) || (bus == NULL) || (bus->write == NULL) || (bus->read == NULL)) return DEV_EINVAL;
    dev->i2c = *bus;
    dev->reset_n = (reset_n != NULL) ? *reset_n : (dev_gpio_t){0};
    dev->clock = (clock != NULL) ? *clock : (dev_clock_t){0};
    dev->addr7 = (addr7 == 0U) ? SI4705_I2C_ADDR_DEFAULT : addr7;
    /* POWER_UP can take up to about 110 ms before CTS. Leave margin for
     * oscillator start and I2C scheduling rather than timing out at 100 ms. */
    dev->timeout_ms = 150U;
    return DEV_OK;
}

dev_status_t si4705_hw_reset(si4705_t *dev)
{
    if ((dev == NULL) || (dev->reset_n.write == NULL) || (dev->clock.delay_ms == NULL)) return DEV_ENOTSUP;
    dev->reset_n.write(dev->reset_n.ctx, false);
    dev->clock.delay_ms(dev->clock.ctx, 2U);
    dev->reset_n.write(dev->reset_n.ctx, true);
    dev->clock.delay_ms(dev->clock.ctx, 10U);
    return DEV_OK;
}

dev_status_t si4705_power_up_analog(si4705_t *dev, uint8_t func, uint8_t opmode)
{
    uint8_t tx[3] = { SI_CMD_POWER_UP, func, opmode };
    return si_command(dev, tx, sizeof(tx), NULL, 0U);
}

dev_status_t si4705_power_down(si4705_t *dev)
{
    uint8_t tx[1] = { SI_CMD_POWER_DOWN };
    return si_command(dev, tx, sizeof(tx), NULL, 0U);
}

dev_status_t si4705_set_property(si4705_t *dev, uint16_t property, uint16_t value)
{
    uint8_t tx[6] = { SI_CMD_SET_PROPERTY, 0U,
                      (uint8_t)(property >> 8), (uint8_t)property,
                      (uint8_t)(value >> 8), (uint8_t)value };
    return si_command(dev, tx, sizeof(tx), NULL, 0U);
}

dev_status_t si4705_get_property(si4705_t *dev, uint16_t property, uint16_t *value)
{
    if (value == NULL) return DEV_EINVAL;
    uint8_t tx[4] = { SI_CMD_GET_PROPERTY, 0U, (uint8_t)(property >> 8), (uint8_t)property };
    uint8_t rx[4] = {0};
    dev_status_t st = si_command(dev, tx, sizeof(tx), rx, sizeof(rx));
    if (st == DEV_OK) *value = (uint16_t)(((uint16_t)rx[2] << 8) | rx[3]);
    return st;
}

dev_status_t si4705_apply_properties(si4705_t *dev,
                                     const si4705_property_t *properties,
                                     size_t count)
{
    if ((dev == NULL) || ((properties == NULL) && (count != 0U))) return DEV_EINVAL;
    for (size_t i = 0U; i < count; ++i) {
        dev_status_t st = si4705_set_property(dev, properties[i].property,
                                              properties[i].value);
        if (st != DEV_OK) return st;
    }
    return DEV_OK;
}

dev_status_t si4705_tune_frequency(si4705_t *dev, uint16_t frequency_10khz)
{
    uint8_t tx[5] = { SI_CMD_FM_TUNE_FREQ, 0U,
                      (uint8_t)(frequency_10khz >> 8), (uint8_t)frequency_10khz, 0U };
    return si_command(dev, tx, sizeof(tx), NULL, 0U);
}

dev_status_t si4705_seek(si4705_t *dev, bool seek_up, bool wrap)
{
    uint8_t arg = (uint8_t)((seek_up ? 0x08U : 0U) | (wrap ? 0x04U : 0U));
    uint8_t tx[2] = { SI_CMD_FM_SEEK_START, arg };
    return si_command(dev, tx, sizeof(tx), NULL, 0U);
}

dev_status_t si4705_get_interrupt_status(si4705_t *dev, uint8_t *status)
{
    if (dev == NULL || status == NULL || dev->i2c.write == NULL) return DEV_EINVAL;

    /* GET_INT_STATUS is a one-byte status response. Capture the byte returned
     * by the post-command CTS poll directly; doing another I2C read afterwards
     * can consume a later status byte and lose the latched STCINT indication. */
    dev_status_t st = si_wait_cts(dev, NULL);
    if (st != DEV_OK) return st;
    uint8_t tx = SI_CMD_GET_INT_STATUS;
    st = dev->i2c.write(dev->i2c.ctx, dev->addr7, &tx, 1U, dev->timeout_ms);
    if (st != DEV_OK) return st;
    return si_wait_cts(dev, status);
}

dev_status_t si4705_get_tune_status(si4705_t *dev, bool cancel,
                                    si4705_tune_status_t *status)
{
    if (status == NULL) return DEV_EINVAL;
    /* INTACK (bit0) must be asserted when consuming STCINT. Without it a
     * completed tune/seek remains latched indefinitely, so a later seek can
     * appear to do nothing because the application keeps seeing the old
     * completion/frequency. CANCEL is bit1. */
    uint8_t tx[2] = { SI_CMD_FM_TUNE_STATUS,
                      (uint8_t)(0x01U | (cancel ? 0x02U : 0x00U)) };
    uint8_t rx[8] = {0};
    dev_status_t st = si_command(dev, tx, sizeof(tx), rx, sizeof(rx));
    if (st != DEV_OK) return st;
    status->valid = (rx[1] & 0x01U) != 0U;
    status->frequency_10khz = (uint16_t)(((uint16_t)rx[2] << 8) | rx[3]);
    status->rssi_dbuv = rx[4];
    status->snr_db = rx[5];
    /* Stereo/pilot state belongs to FM_RSQ_STATUS, not FM_TUNE_STATUS. */
    status->stereo = false;
    status->rds_sync = false;
    return DEV_OK;
}

dev_status_t si4705_get_signal_quality(si4705_t *dev,
                                       si4705_signal_quality_t *quality)
{
    if (quality == NULL) return DEV_EINVAL;
    uint8_t tx[2] = { SI_CMD_FM_RSQ_STATUS, 0x00U };
    uint8_t rx[8] = {0};
    dev_status_t st = si_command(dev, tx, sizeof(tx), rx, sizeof(rx));
    if (st != DEV_OK) return st;
    quality->valid = (rx[2] & 0x01U) != 0U;
    quality->pilot_present = (rx[3] & 0x80U) != 0U;
    quality->stereo_blend_percent = (uint8_t)(rx[3] & 0x7FU);
    quality->rssi_dbuv = rx[4];
    quality->snr_db = rx[5];
    quality->frequency_offset_khz = (int8_t)rx[7];
    return DEV_OK;
}

dev_status_t si4705_read_rds_group(si4705_t *dev, bool fifo_clear,
                                   si4705_rds_group_t *group)
{
    if (group == NULL) return DEV_EINVAL;
    /* ARG1 bit1 = MTFIFO. Bit0 (INTACK) is intentionally left clear here. */
    uint8_t tx[2] = { SI_CMD_FM_RDS_STATUS, fifo_clear ? 0x02U : 0x00U };
    uint8_t rx[13] = {0};
    dev_status_t st = si_command(dev, tx, sizeof(tx), rx, sizeof(rx));
    if (st != DEV_OK) return st;
    group->sync = (rx[2] & 0x01U) != 0U;
    group->fifo_used = rx[3] != 0U;
    group->block_a = (uint16_t)(((uint16_t)rx[4] << 8) | rx[5]);
    group->block_b = (uint16_t)(((uint16_t)rx[6] << 8) | rx[7]);
    group->block_c = (uint16_t)(((uint16_t)rx[8] << 8) | rx[9]);
    group->block_d = (uint16_t)(((uint16_t)rx[10] << 8) | rx[11]);
    group->ble_a = (uint8_t)((rx[12] >> 6) & 0x03U);
    group->ble_b = (uint8_t)((rx[12] >> 4) & 0x03U);
    group->ble_c = (uint8_t)((rx[12] >> 2) & 0x03U);
    group->ble_d = (uint8_t)(rx[12] & 0x03U);
    return DEV_OK;
}
