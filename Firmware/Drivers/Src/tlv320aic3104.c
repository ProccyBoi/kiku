#include "tlv320aic3104.h"

#define TLV_PAGE_SELECT_REG 0x00U
#define TLV_SW_RESET_REG    0x01U

static dev_status_t tlv_select_page(tlv320aic3104_t *dev, uint8_t page)
{
    if (dev->current_page == page) return DEV_OK;
    uint8_t tx[2] = { TLV_PAGE_SELECT_REG, page };
    dev_status_t st = dev->i2c.write(dev->i2c.ctx, dev->addr7, tx, sizeof(tx), dev->timeout_ms);
    if (st == DEV_OK) dev->current_page = page;
    return st;
}

dev_status_t tlv320aic3104_init(tlv320aic3104_t *dev, const dev_i2c_bus_t *bus,
                                const dev_gpio_t *reset_n, const dev_clock_t *clock,
                                uint8_t addr7)
{
    if ((dev == NULL) || (bus == NULL) || !dev_i2c_valid(bus)) return DEV_EINVAL;
    dev->i2c = *bus;
    dev->reset_n = (reset_n != NULL) ? *reset_n : (dev_gpio_t){0};
    dev->clock = (clock != NULL) ? *clock : (dev_clock_t){0};
    dev->addr7 = (addr7 == 0U) ? TLV320AIC3104_I2C_ADDR_DEFAULT : addr7;
    dev->current_page = 0xFFU;
    dev->timeout_ms = 20U;
    return DEV_OK;
}

dev_status_t tlv320aic3104_hw_reset(tlv320aic3104_t *dev)
{
    if ((dev == NULL) || (dev->reset_n.write == NULL) || (dev->clock.delay_ms == NULL)) return DEV_ENOTSUP;
    dev->reset_n.write(dev->reset_n.ctx, false);
    dev->clock.delay_ms(dev->clock.ctx, 2U);
    dev->reset_n.write(dev->reset_n.ctx, true);
    dev->clock.delay_ms(dev->clock.ctx, 2U);
    dev->current_page = 0xFFU;
    return DEV_OK;
}

dev_status_t tlv320aic3104_sw_reset(tlv320aic3104_t *dev)
{
    return tlv320aic3104_write_reg(dev, 0U, TLV_SW_RESET_REG, 0x80U);
}

dev_status_t tlv320aic3104_write_reg(tlv320aic3104_t *dev, uint8_t page,
                                     uint8_t reg, uint8_t value)
{
    if ((dev == NULL) || (dev->i2c.write == NULL)) return DEV_EINVAL;
    dev_status_t st = tlv_select_page(dev, page);
    if (st != DEV_OK) return st;
    uint8_t tx[2] = { reg, value };
    return dev->i2c.write(dev->i2c.ctx, dev->addr7, tx, sizeof(tx), dev->timeout_ms);
}

dev_status_t tlv320aic3104_read_reg(tlv320aic3104_t *dev, uint8_t page,
                                    uint8_t reg, uint8_t *value)
{
    if ((dev == NULL) || (value == NULL)) return DEV_EINVAL;
    dev_status_t st = tlv_select_page(dev, page);
    if (st != DEV_OK) return st;
    if (dev->i2c.write_read != NULL) {
        return dev->i2c.write_read(dev->i2c.ctx, dev->addr7, &reg, 1U,
                                   value, 1U, dev->timeout_ms);
    }
    st = dev->i2c.write(dev->i2c.ctx, dev->addr7, &reg, 1U, dev->timeout_ms);
    if (st != DEV_OK) return st;
    return dev->i2c.read(dev->i2c.ctx, dev->addr7, value, 1U, dev->timeout_ms);
}

dev_status_t tlv320aic3104_apply_script(tlv320aic3104_t *dev,
                                        const tlv320_reg_write_t *script,
                                        size_t count)
{
    if ((dev == NULL) || ((script == NULL) && (count != 0U))) return DEV_EINVAL;
    for (size_t i = 0; i < count; ++i) {
        dev_status_t st = tlv320aic3104_write_reg(dev, script[i].page,
                                                  script[i].reg, script[i].value);
        if (st != DEV_OK) return st;
    }
    return DEV_OK;
}
