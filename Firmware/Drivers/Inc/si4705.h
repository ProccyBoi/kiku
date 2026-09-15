#ifndef SI4705_H
#define SI4705_H

#include "device_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SI4705_I2C_ADDR_DEFAULT 0x11U

/* AN332 property IDs used by the product integration. */
#define SI4705_PROP_FM_DEEMPHASIS 0x1100U
#define SI4705_PROP_FM_SEEK_BAND_BOTTOM 0x1400U
#define SI4705_PROP_FM_SEEK_BAND_TOP 0x1401U
#define SI4705_PROP_FM_SEEK_FREQ_SPACING 0x1402U
#define SI4705_PROP_FM_SEEK_TUNE_SNR_THRESHOLD 0x1403U
#define SI4705_PROP_FM_SEEK_TUNE_RSSI_THRESHOLD 0x1404U
#define SI4705_PROP_FM_RDS_CONFIG 0x1502U
#define SI4705_FM_DEEMPHASIS_50US 0x0001U
#define SI4705_FM_RDS_CONFIG_ENABLE_BLE2 0xAA01U
#define SI4705_STATUS_STCINT 0x01U

typedef struct {
    dev_i2c_bus_t i2c;
    dev_gpio_t reset_n;
    dev_clock_t clock;
    uint8_t addr7;
    uint32_t timeout_ms;
} si4705_t;

typedef struct {
    uint16_t property;
    uint16_t value;
} si4705_property_t;

typedef struct {
    uint16_t frequency_10khz;
    uint8_t rssi_dbuv;
    uint8_t snr_db;
    bool valid;
    bool stereo;
    bool rds_sync;
} si4705_tune_status_t;

typedef struct {
    bool valid;
    bool pilot_present;
    uint8_t stereo_blend_percent;
    uint8_t rssi_dbuv;
    uint8_t snr_db;
    int8_t frequency_offset_khz;
} si4705_signal_quality_t;

typedef struct {
    uint16_t block_a;
    uint16_t block_b;
    uint16_t block_c;
    uint16_t block_d;
    uint8_t ble_a;
    uint8_t ble_b;
    uint8_t ble_c;
    uint8_t ble_d;
    bool sync;
    bool fifo_used;
} si4705_rds_group_t;

dev_status_t si4705_init(si4705_t *dev, const dev_i2c_bus_t *bus,
                         const dev_gpio_t *reset_n, const dev_clock_t *clock,
                         uint8_t addr7);
dev_status_t si4705_hw_reset(si4705_t *dev);
dev_status_t si4705_power_up_analog(si4705_t *dev, uint8_t func, uint8_t opmode);
dev_status_t si4705_power_down(si4705_t *dev);
dev_status_t si4705_set_property(si4705_t *dev, uint16_t property, uint16_t value);
dev_status_t si4705_get_property(si4705_t *dev, uint16_t property, uint16_t *value);
dev_status_t si4705_apply_properties(si4705_t *dev,
                                     const si4705_property_t *properties,
                                     size_t count);
dev_status_t si4705_tune_frequency(si4705_t *dev, uint16_t frequency_10khz);
dev_status_t si4705_seek(si4705_t *dev, bool seek_up, bool wrap);
dev_status_t si4705_get_interrupt_status(si4705_t *dev, uint8_t *status);
dev_status_t si4705_get_tune_status(si4705_t *dev, bool cancel,
                                    si4705_tune_status_t *status);
dev_status_t si4705_get_signal_quality(si4705_t *dev,
                                       si4705_signal_quality_t *quality);
dev_status_t si4705_read_rds_group(si4705_t *dev, bool fifo_clear,
                                   si4705_rds_group_t *group);

#ifdef __cplusplus
}
#endif

#endif
