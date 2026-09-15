#ifndef ST7789_H
#define ST7789_H

#include "device_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    dev_spi_bus_t spi;
    dev_gpio_t cs_n;
    dev_gpio_t dc;
    dev_gpio_t reset_n;
    dev_clock_t clock;
    uint16_t width;
    uint16_t height;
    uint16_t x_offset;
    uint16_t y_offset;
    uint32_t timeout_ms;
} st7789_t;

dev_status_t st7789_init(st7789_t *lcd, const dev_spi_bus_t *spi,
                         const dev_gpio_t *cs_n, const dev_gpio_t *dc,
                         const dev_gpio_t *reset_n, const dev_clock_t *clock,
                         uint16_t width, uint16_t height,
                         uint16_t x_offset, uint16_t y_offset);
dev_status_t st7789_hw_reset(st7789_t *lcd);
dev_status_t st7789_command(st7789_t *lcd, uint8_t command,
                            const uint8_t *data, size_t len);
dev_status_t st7789_startup_sequence(st7789_t *lcd);
dev_status_t st7789_sleep(st7789_t *lcd);
dev_status_t st7789_wake(st7789_t *lcd);
dev_status_t st7789_set_window(st7789_t *lcd, uint16_t x0, uint16_t y0,
                               uint16_t x1, uint16_t y1);
dev_status_t st7789_write_pixels_rgb565(st7789_t *lcd,
                                        const uint16_t *pixels, size_t count);
dev_status_t st7789_fill(st7789_t *lcd, uint16_t rgb565);

#ifdef __cplusplus
}
#endif

#endif
