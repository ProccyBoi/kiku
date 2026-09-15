#include "max17048.h"

#define MAX_REG_VCELL   0x02U
#define MAX_REG_SOC     0x04U
#define MAX_REG_MODE    0x06U
#define MAX_REG_VERSION 0x08U
#define MAX_REG_CRATE   0x16U
#define MAX_REG_STATUS  0x1AU

static dev_status_t max_read(max17048_t *dev, uint8_t reg, uint16_t *value)
{
    uint8_t rx[2];
    if ((dev == NULL) || (value == NULL) || !dev_i2c_valid(&dev->i2c)) return DEV_EINVAL;
    dev_status_t st;
    if (dev->i2c.write_read != NULL) {
        st = dev->i2c.write_read(dev->i2c.ctx, dev->addr7, &reg, 1U, rx, 2U, dev->timeout_ms);
    } else {
        st = dev->i2c.write(dev->i2c.ctx, dev->addr7, &reg, 1U, dev->timeout_ms);
        if (st == DEV_OK) st = dev->i2c.read(dev->i2c.ctx, dev->addr7, rx, 2U, dev->timeout_ms);
    }
    if (st == DEV_OK) *value = (uint16_t)(((uint16_t)rx[0] << 8) | rx[1]);
    return st;
}

dev_status_t max17048_init(max17048_t *dev, const dev_i2c_bus_t *bus, uint8_t addr7)
{
    if ((dev == NULL) || (bus == NULL) || !dev_i2c_valid(bus)) return DEV_EINVAL;
    dev->i2c = *bus;
    dev->addr7 = (addr7 == 0U) ? MAX17048_I2C_ADDR_DEFAULT : addr7;
    dev->timeout_ms = 20U;
    return DEV_OK;
}

dev_status_t max17048_read_reg(max17048_t *dev, uint8_t reg, uint16_t *value)
{
    return max_read(dev, reg, value);
}

dev_status_t max17048_write_reg(max17048_t *dev, uint8_t reg, uint16_t value)
{
    uint8_t tx[3] = { reg, (uint8_t)(value >> 8), (uint8_t)value };
    if ((dev == NULL) || (dev->i2c.write == NULL)) return DEV_EINVAL;
    return dev->i2c.write(dev->i2c.ctx, dev->addr7, tx, sizeof(tx), dev->timeout_ms);
}

dev_status_t max17048_quick_start(max17048_t *dev)
{
    return max17048_write_reg(dev, MAX_REG_MODE, 0x4000U);
}

dev_status_t max17048_get_version(max17048_t *dev, uint16_t *version)
{
    return max_read(dev, MAX_REG_VERSION, version);
}

dev_status_t max17048_read_snapshot(max17048_t *dev, max17048_snapshot_t *snapshot)
{
    if ((dev == NULL) || (snapshot == NULL)) return DEV_EINVAL;
    uint16_t vcell, soc, crate_raw, status;
    dev_status_t st;
    st = max_read(dev, MAX_REG_VCELL, &vcell); if (st != DEV_OK) return st;
    st = max_read(dev, MAX_REG_SOC, &soc); if (st != DEV_OK) return st;
    st = max_read(dev, MAX_REG_CRATE, &crate_raw); if (st != DEV_OK) return st;
    st = max_read(dev, MAX_REG_STATUS, &status); if (st != DEV_OK) return st;

    /* VCELL LSB = 78.125 uV; SOC LSB = 1/256 %. */
    snapshot->voltage_mv = (uint16_t)(((uint32_t)vcell * 78125UL + 500000UL) / 1000000UL);
    snapshot->soc_centi_percent = (uint16_t)(((uint32_t)soc * 100UL) / 256UL);
    snapshot->crate_centi_percent_per_hour = ((int32_t)(int16_t)crate_raw * 208L) / 10L;
    snapshot->status_raw = status;
    return DEV_OK;
}
