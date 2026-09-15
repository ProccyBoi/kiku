#include "settings.h"

#include "app_config.h"

#include <string.h>

#define SETTINGS_MAGIC   0x424C4F42UL /* Legacy v1 magic retained for compatibility. */
#define SETTINGS_VERSION 1U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    app_settings_t payload;
    uint32_t crc32;
} settings_record_t;

static bool settings_sanitise(app_settings_t *settings)
{
    if (settings == NULL) return false;

    bool changed = false;
    if (settings->volume_percent > 100U) {
        settings->volume_percent = 100U;
        changed = true;
    }
    if (settings->brightness_percent > 100U) {
        settings->brightness_percent = 100U;
        changed = true;
    }
    if (settings->preferred_source > SETTINGS_SOURCE_BLUETOOTH) {
        settings->preferred_source = SETTINGS_SOURCE_LOCAL;
        changed = true;
    }
    /* kiku currently ships one Australian FM band plan. Keep the field for a
     * future record version, but never let a corrupt/old value select an
     * undefined region at runtime. */
    if (settings->fm_region != 0U) {
        settings->fm_region = 0U;
        changed = true;
    }
    if (settings->fm_frequency_10khz < APP_FM_MIN_10KHZ ||
        settings->fm_frequency_10khz > APP_FM_MAX_10KHZ) {
        settings->fm_frequency_10khz = APP_FM_DEFAULT_10KHZ;
        changed = true;
    }
    if (settings->speaker_enabled > 1U) {
        settings->speaker_enabled = 1U;
        changed = true;
    }
    if (settings->output_mode > 3U) {
        settings->output_mode = 0U;
        changed = true;
    }

    uint8_t power_save = settings->power_save_enabled != 0U ? 1U : 0U;
    uint8_t shuffle = settings->shuffle_enabled != 0U ? 1U : 0U;
    if (settings->power_save_enabled != power_save) changed = true;
    if (settings->shuffle_enabled != shuffle) changed = true;
    settings->power_save_enabled = power_save;
    settings->shuffle_enabled = shuffle;

    for (size_t i = 0U; i < sizeof(settings->reserved); ++i) {
        if (settings->reserved[i] != 0U) changed = true;
        settings->reserved[i] = 0U;
    }
    return changed;
}

static uint32_t settings_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1U));
            crc = (crc >> 1) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

static bool settings_record_valid(const settings_record_t *record)
{
    if (record == NULL || record->magic != SETTINGS_MAGIC ||
        record->version != SETTINGS_VERSION ||
        record->payload_size != sizeof(app_settings_t)) return false;
    uint32_t crc = settings_crc32((const uint8_t *)record,
                                  offsetof(settings_record_t, crc32));
    return record->crc32 == crc;
}

void settings_defaults(app_settings_t *settings)
{
    if (settings == NULL) return;
    memset(settings, 0, sizeof(*settings));
    settings->volume_percent = 45U;
    settings->brightness_percent = 70U;
    settings->preferred_source = SETTINGS_SOURCE_LOCAL;
    settings->fm_region = 0U;
    settings->fm_frequency_10khz = 10170U;
    settings->speaker_enabled = 1U;
    settings->output_mode = 0U;
    settings->power_save_enabled = 0U;
    settings->shuffle_enabled = 0U;
}

dev_status_t settings_init(settings_manager_t *mgr, const settings_storage_t *storage,
                           uint32_t base_offset)
{
    if (mgr == NULL) return DEV_EINVAL;
    /* The v1 redundant layout stores the backup record immediately after the
     * primary. Reject an impossible offset up front rather than allowing the
     * uint32_t storage address to wrap back to the start of the medium. */
    if (base_offset > UINT32_MAX - (uint32_t)sizeof(settings_record_t)) return DEV_EOVERFLOW;
    memset(mgr, 0, sizeof(*mgr));
    if (storage != NULL) mgr->storage = *storage;
    mgr->base_offset = base_offset;
    settings_defaults(&mgr->value);
    return DEV_OK;
}

dev_status_t settings_load(settings_manager_t *mgr)
{
    if (mgr == NULL) return DEV_EINVAL;
    if (mgr->storage.read == NULL) {
        settings_defaults(&mgr->value);
        return DEV_ENOTSUP;
    }
    settings_record_t record;
    bool used_backup = false;
    dev_status_t primary_st = mgr->storage.read(mgr->storage.ctx, mgr->base_offset,
                                                &record, sizeof(record));
    bool valid = primary_st == DEV_OK && settings_record_valid(&record);
    if (!valid) {
        /* Keep a second complete CRC-protected record immediately after the
         * original v1 record. Existing single-copy files remain compatible.
         * A torn primary write can therefore recover the already-synchronised
         * backup on the next boot instead of silently losing all preferences. */
        dev_status_t backup_st = mgr->storage.read(mgr->storage.ctx,
                                                   mgr->base_offset + sizeof(record),
                                                   &record, sizeof(record));
        valid = backup_st == DEV_OK && settings_record_valid(&record);
        if (!valid) {
            settings_defaults(&mgr->value);
            if (primary_st != DEV_OK) return primary_st;
            return DEV_ECRC;
        }
        used_backup = true;
    }
    mgr->value = record.payload;
    /* CRC proves integrity of the bytes, not that their enum/range values are
     * meaningful to this firmware version. Canonicalise before any caller can
     * use the settings. If repair was necessary, persist the canonical record
     * through the normal delayed-save path rather than carrying bad state for
     * the rest of the boot. */
    mgr->dirty = used_backup || settings_sanitise(&mgr->value);
    mgr->dirty_since_ms = 0U;
    return DEV_OK;
}

dev_status_t settings_save(settings_manager_t *mgr)
{
    if (mgr == NULL) return DEV_EINVAL;
    if (mgr->storage.write == NULL) return DEV_ENOTSUP;
    (void)settings_sanitise(&mgr->value);
    settings_record_t record;
    memset(&record, 0, sizeof(record));
    record.magic = SETTINGS_MAGIC;
    record.version = SETTINGS_VERSION;
    record.payload_size = (uint16_t)sizeof(app_settings_t);
    record.payload = mgr->value;
    record.crc32 = settings_crc32((const uint8_t *)&record,
                                  offsetof(settings_record_t, crc32));
    /* Commit backup first. If power/media fails during this write, the old
     * primary remains authoritative. Only after the backup is fully committed
     * do we replace the primary; a torn primary then recovers from the backup. */
    dev_status_t st = mgr->storage.write(mgr->storage.ctx,
                                         mgr->base_offset + sizeof(record),
                                         &record, sizeof(record));
    if (st != DEV_OK) return st;
    st = mgr->storage.write(mgr->storage.ctx, mgr->base_offset,
                            &record, sizeof(record));
    if (st == DEV_OK) mgr->dirty = false;
    return st;
}

void settings_mark_dirty(settings_manager_t *mgr, uint32_t now_ms)
{
    if (mgr == NULL) return;
    if (!mgr->dirty) mgr->dirty_since_ms = now_ms;
    mgr->dirty = true;
}

dev_status_t settings_save_if_due(settings_manager_t *mgr, uint32_t now_ms, uint32_t delay_ms)
{
    if (mgr == NULL) return DEV_EINVAL;
    if (!mgr->dirty) return DEV_OK;
    if ((uint32_t)(now_ms - mgr->dirty_since_ms) < delay_ms) return DEV_EBUSY;
    dev_status_t st = settings_save(mgr);
    if (st != DEV_OK) {
        /* Missing/removed media and transient write failures must not turn the
         * 10 ms application loop into a continuous FAT/SD retry storm. Keep the
         * change dirty, but apply the same delay before the next attempt. */
        mgr->dirty = true;
        mgr->dirty_since_ms = now_ms;
    }
    return st;
}
