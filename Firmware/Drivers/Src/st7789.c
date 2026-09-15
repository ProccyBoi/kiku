#include "st7789.h"

#define ST_SWRESET 0x01U
#define ST_SLPIN   0x10U
#define ST_SLPOUT  0x11U
#define ST_NORON   0x13U
#define ST_INVON   0x21U
#define ST_COLMOD  0x3AU
#define ST_MADCTL  0x36U
#define ST_CASET   0x2AU
#define ST_RASET   0x2BU
#define ST_RAMWR   0x2CU
#define ST_DISPOFF 0x28U
#define ST_DISPON  0x29U

static dev_status_t lcd_select(st7789_t *lcd, bool selected)
{
    if ((lcd == NULL) || (lcd->cs_n.write == NULL)) return DEV_ENOTSUP;
    lcd->cs_n.write(lcd->cs_n.ctx, !selected);
    return DEV_OK;
}

dev_status_t st7789_init(st7789_t *lcd, const dev_spi_bus_t *spi,
                         const dev_gpio_t *cs_n, const dev_gpio_t *dc,
                         const dev_gpio_t *reset_n, const dev_clock_t *clock,
                         uint16_t width, uint16_t height,
                         uint16_t x_offset, uint16_t y_offset)
{
    if ((lcd == NULL) || (spi == NULL) || (spi->write == NULL) ||
        (cs_n == NULL) || (dc == NULL) || (cs_n->write == NULL) || (dc->write == NULL)) return DEV_EINVAL;
    lcd->spi = *spi;
    lcd->cs_n = *cs_n;
    lcd->dc = *dc;
    lcd->reset_n = (reset_n != NULL) ? *reset_n : (dev_gpio_t){0};
    lcd->clock = (clock != NULL) ? *clock : (dev_clock_t){0};
    lcd->width = width;
    lcd->height = height;
    lcd->x_offset = x_offset;
    lcd->y_offset = y_offset;
    lcd->timeout_ms = 100U;
    lcd->cs_n.write(lcd->cs_n.ctx, true);
    return DEV_OK;
}

dev_status_t st7789_hw_reset(st7789_t *lcd)
{
    if ((lcd == NULL) || (lcd->reset_n.write == NULL) || (lcd->clock.delay_ms == NULL)) return DEV_ENOTSUP;
    lcd->reset_n.write(lcd->reset_n.ctx, true);
    lcd->clock.delay_ms(lcd->clock.ctx, 5U);
    lcd->reset_n.write(lcd->reset_n.ctx, false);
    lcd->clock.delay_ms(lcd->clock.ctx, 20U);
    lcd->reset_n.write(lcd->reset_n.ctx, true);
    lcd->clock.delay_ms(lcd->clock.ctx, 120U);
    return DEV_OK;
}

dev_status_t st7789_command(st7789_t *lcd, uint8_t command,
                            const uint8_t *data, size_t len)
{
    if ((lcd == NULL) || ((data == NULL) && (len != 0U))) return DEV_EINVAL;
    dev_status_t st = lcd_select(lcd, true);
    if (st != DEV_OK) return st;
    lcd->dc.write(lcd->dc.ctx, false);
    st = lcd->spi.write(lcd->spi.ctx, &command, 1U, lcd->timeout_ms);
    if ((st == DEV_OK) && (len != 0U)) {
        lcd->dc.write(lcd->dc.ctx, true);
        st = lcd->spi.write(lcd->spi.ctx, data, len, lcd->timeout_ms);
    }
    (void)lcd_select(lcd, false);
    return st;
}

dev_status_t st7789_startup_sequence(st7789_t *lcd)
{
    if (lcd == NULL) return DEV_EINVAL;
    dev_status_t st = st7789_hw_reset(lcd);
    if ((st != DEV_OK) && (st != DEV_ENOTSUP)) return st;
    st = st7789_command(lcd, ST_SWRESET, NULL, 0U); if (st != DEV_OK) return st;
    if (lcd->clock.delay_ms != NULL) lcd->clock.delay_ms(lcd->clock.ctx, 150U);
    st = st7789_command(lcd, ST_SLPOUT, NULL, 0U); if (st != DEV_OK) return st;
    if (lcd->clock.delay_ms != NULL) lcd->clock.delay_ms(lcd->clock.ctx, 120U);
    uint8_t colmod = 0x55U; /* 16-bit RGB565 */
    st = st7789_command(lcd, ST_COLMOD, &colmod, 1U); if (st != DEV_OK) return st;
    /* Mounted HS20HS072RX: swap the native 240x320 axes and reverse X
     * for upright 320x240 content with the controls below the display. */
    uint8_t madctl = 0x60U; /* MX | MV, RGB */
    st = st7789_command(lcd, ST_MADCTL, &madctl, 1U); if (st != DEV_OK) return st;
    st = st7789_command(lcd, ST_INVON, NULL, 0U); if (st != DEV_OK) return st;
    st = st7789_command(lcd, ST_NORON, NULL, 0U); if (st != DEV_OK) return st;
    return st7789_command(lcd, ST_DISPON, NULL, 0U);
}

dev_status_t st7789_sleep(st7789_t *lcd)
{
    if (lcd == NULL) return DEV_EINVAL;
    dev_status_t st = st7789_command(lcd, ST_DISPOFF, NULL, 0U);
    if (st != DEV_OK) return st;
    if (lcd->clock.delay_ms != NULL) lcd->clock.delay_ms(lcd->clock.ctx, 20U);
    st = st7789_command(lcd, ST_SLPIN, NULL, 0U);
    if (st == DEV_OK && lcd->clock.delay_ms != NULL) lcd->clock.delay_ms(lcd->clock.ctx, 120U);
    return st;
}

dev_status_t st7789_wake(st7789_t *lcd)
{
    if (lcd == NULL) return DEV_EINVAL;
    dev_status_t st = st7789_command(lcd, ST_SLPOUT, NULL, 0U);
    if (st != DEV_OK) return st;
    if (lcd->clock.delay_ms != NULL) lcd->clock.delay_ms(lcd->clock.ctx, 120U);
    return st7789_command(lcd, ST_DISPON, NULL, 0U);
}

dev_status_t st7789_set_window(st7789_t *lcd, uint16_t x0, uint16_t y0,
                               uint16_t x1, uint16_t y1)
{
    if ((lcd == NULL) || (x0 > x1) || (y0 > y1) || (x1 >= lcd->width) || (y1 >= lcd->height)) return DEV_EINVAL;
    x0 = (uint16_t)(x0 + lcd->x_offset); x1 = (uint16_t)(x1 + lcd->x_offset);
    y0 = (uint16_t)(y0 + lcd->y_offset); y1 = (uint16_t)(y1 + lcd->y_offset);
    uint8_t data[4];
    data[0] = (uint8_t)(x0 >> 8); data[1] = (uint8_t)x0;
    data[2] = (uint8_t)(x1 >> 8); data[3] = (uint8_t)x1;
    dev_status_t st = st7789_command(lcd, ST_CASET, data, sizeof(data));
    if (st != DEV_OK) return st;
    data[0] = (uint8_t)(y0 >> 8); data[1] = (uint8_t)y0;
    data[2] = (uint8_t)(y1 >> 8); data[3] = (uint8_t)y1;
    st = st7789_command(lcd, ST_RASET, data, sizeof(data));
    if (st != DEV_OK) return st;
    return st7789_command(lcd, ST_RAMWR, NULL, 0U);
}

dev_status_t st7789_write_pixels_rgb565(st7789_t *lcd,
                                        const uint16_t *pixels, size_t count)
{
    if ((lcd == NULL) || ((pixels == NULL) && (count != 0U))) return DEV_EINVAL;
    uint8_t buf[128];
    dev_status_t st = lcd_select(lcd, true);
    if (st != DEV_OK) return st;
    lcd->dc.write(lcd->dc.ctx, true);
    size_t i = 0U;
    while (i < count) {
        size_t n = count - i;
        if (n > (sizeof(buf) / 2U)) n = sizeof(buf) / 2U;
        for (size_t j = 0U; j < n; ++j) {
            uint16_t p = pixels[i + j];
            buf[2U * j] = (uint8_t)(p >> 8);
            buf[2U * j + 1U] = (uint8_t)p;
        }
        st = lcd->spi.write(lcd->spi.ctx, buf, n * 2U, lcd->timeout_ms);
        if (st != DEV_OK) break;
        i += n;
    }
    (void)lcd_select(lcd, false);
    return st;
}

dev_status_t st7789_fill(st7789_t *lcd, uint16_t rgb565)
{
    if ((lcd == NULL) || (lcd->width == 0U) || (lcd->height == 0U)) return DEV_EINVAL;
    dev_status_t st = st7789_set_window(lcd, 0U, 0U,
                                        (uint16_t)(lcd->width - 1U),
                                        (uint16_t)(lcd->height - 1U));
    if (st != DEV_OK) return st;
    uint16_t line[64];
    for (size_t i = 0U; i < 64U; ++i) line[i] = rgb565;
    size_t remaining = (size_t)lcd->width * lcd->height;
    while (remaining != 0U) {
        size_t n = (remaining > 64U) ? 64U : remaining;
        st = st7789_write_pixels_rgb565(lcd, line, n);
        if (st != DEV_OK) return st;
        remaining -= n;
    }
    return DEV_OK;
}
