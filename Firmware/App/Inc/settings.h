#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include "device_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SETTINGS_SOURCE_LOCAL = 0,
    SETTINGS_SOURCE_FM,
    SETTINGS_SOURCE_BLUETOOTH
} settings_source_t;

typedef struct {
    uint8_t volume_percent;
    uint8_t brightness_percent;
    uint8_t preferred_source;
    uint8_t fm_region;
    uint16_t fm_frequency_10khz;
    uint8_t speaker_enabled;
    /* 0=AUTO, 1=HEADPHONES, 2=SPEAKER, 3=BOTH. This consumes the first byte
     * of the old reserved area, so the v1 on-disk record size stays stable
     * and existing files naturally migrate to AUTO (reserved byte was zero). */
    uint8_t output_mode;
    /* These consume two more bytes of the original reserved area. Keeping the
     * payload size unchanged makes existing v1 settings migrate as OFF. */
    uint8_t power_save_enabled;
    uint8_t shuffle_enabled;
    uint8_t reserved[4];
} app_settings_t;

typedef dev_status_t (*settings_read_fn)(void *ctx, uint32_t offset, void *data, size_t len);
typedef dev_status_t (*settings_write_fn)(void *ctx, uint32_t offset, const void *data, size_t len);

typedef struct {
    void *ctx;
    settings_read_fn read;
    settings_write_fn write;
} settings_storage_t;

typedef struct {
    settings_storage_t storage;
    uint32_t base_offset;
    app_settings_t value;
    bool dirty;
    uint32_t dirty_since_ms;
} settings_manager_t;

dev_status_t settings_init(settings_manager_t *mgr, const settings_storage_t *storage,
                           uint32_t base_offset);
dev_status_t settings_load(settings_manager_t *mgr);
dev_status_t settings_save(settings_manager_t *mgr);
void settings_defaults(app_settings_t *settings);
void settings_mark_dirty(settings_manager_t *mgr, uint32_t now_ms);
dev_status_t settings_save_if_due(settings_manager_t *mgr, uint32_t now_ms, uint32_t delay_ms);

#ifdef __cplusplus
}
#endif

#endif
