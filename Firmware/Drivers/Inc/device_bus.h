#ifndef DEVICE_BUS_H
#define DEVICE_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DEV_OK = 0,
    DEV_EINVAL = -1,
    DEV_EIO = -2,
    DEV_ETIMEOUT = -3,
    DEV_EBUSY = -4,
    DEV_ECRC = -5,
    DEV_ENOTREADY = -6,
    DEV_ENOTSUP = -7,
    DEV_EOVERFLOW = -8,
    DEV_ESTATE = -9
} dev_status_t;

typedef dev_status_t (*dev_i2c_write_fn)(void *ctx, uint8_t addr7,
                                         const uint8_t *data, size_t len,
                                         uint32_t timeout_ms);
typedef dev_status_t (*dev_i2c_read_fn)(void *ctx, uint8_t addr7,
                                        uint8_t *data, size_t len,
                                        uint32_t timeout_ms);
typedef dev_status_t (*dev_i2c_write_read_fn)(void *ctx, uint8_t addr7,
                                              const uint8_t *tx, size_t tx_len,
                                              uint8_t *rx, size_t rx_len,
                                              uint32_t timeout_ms);

typedef struct {
    void *ctx;
    dev_i2c_write_fn write;
    dev_i2c_read_fn read;
    dev_i2c_write_read_fn write_read;
} dev_i2c_bus_t;

typedef dev_status_t (*dev_spi_write_fn)(void *ctx, const uint8_t *data,
                                         size_t len, uint32_t timeout_ms);
typedef struct {
    void *ctx;
    dev_spi_write_fn write;
} dev_spi_bus_t;

typedef dev_status_t (*dev_uart_write_fn)(void *ctx, const uint8_t *data,
                                          size_t len, uint32_t timeout_ms);
typedef struct {
    void *ctx;
    dev_uart_write_fn write;
} dev_uart_bus_t;

typedef bool (*dev_gpio_read_fn)(void *ctx);
typedef void (*dev_gpio_write_fn)(void *ctx, bool level);
typedef struct {
    void *ctx;
    dev_gpio_read_fn read;
    dev_gpio_write_fn write;
} dev_gpio_t;

typedef void (*dev_delay_ms_fn)(void *ctx, uint32_t ms);
typedef uint32_t (*dev_time_ms_fn)(void *ctx);
typedef struct {
    void *ctx;
    dev_delay_ms_fn delay_ms;
    dev_time_ms_fn time_ms;
} dev_clock_t;

static inline bool dev_i2c_valid(const dev_i2c_bus_t *bus)
{
    return (bus != NULL) && (bus->write != NULL) &&
           ((bus->write_read != NULL) || (bus->read != NULL));
}

#ifdef __cplusplus
}
#endif

#endif
