#include "storage_adapters.h"

#include <limits.h>
#include <string.h>

static FIL s_audio_file;
static bool s_audio_file_open;

static bool storage_media_fault(FRESULT fr)
{
    return fr == FR_DISK_ERR || fr == FR_NOT_READY || fr == FR_NO_FILESYSTEM ||
           fr == FR_INVALID_OBJECT;
}

static void storage_record_result(storage_t *storage, FRESULT fr)
{
    if (storage == NULL || fr == FR_OK) return;
    storage->last_error = fr;
    /* There is no card-detect GPIO on this PCB. A media-level FatFs failure is
     * therefore our reliable hot-removal signal. Mark the volume unavailable
     * so the platform's periodic mount path can enumerate a reinserted card. */
    if (storage_media_fault(fr)) storage->mounted = false;
}

static dev_status_t fs_open(void *ctx, const char *path, void **file)
{
    storage_t *storage = (storage_t *)ctx;
    if (storage == NULL || path == NULL || file == NULL || !storage_is_mounted(storage)) {
        return DEV_ENOTREADY;
    }
    if (s_audio_file_open) {
        (void)f_close(&s_audio_file);
        s_audio_file_open = false;
    }
    FRESULT fr = f_open(&s_audio_file, path, FA_READ);
    if (fr != FR_OK) {
        storage_record_result(storage, fr);
        return DEV_EIO;
    }
    s_audio_file_open = true;
    *file = &s_audio_file;
    return DEV_OK;
}

static dev_status_t fs_read(void *ctx, void *file, void *data, size_t requested, size_t *read_count)
{
    storage_t *storage = (storage_t *)ctx;
    if (file == NULL || data == NULL || read_count == NULL || requested > UINT_MAX) {
        return DEV_EINVAL;
    }
    UINT got = 0U;
    FRESULT fr = f_read((FIL *)file, data, (UINT)requested, &got);
    *read_count = got;
    storage_record_result(storage, fr);
    return fr == FR_OK ? DEV_OK : DEV_EIO;
}

static dev_status_t fs_seek(void *ctx, void *file, uint32_t offset)
{
    storage_t *storage = (storage_t *)ctx;
    if (file == NULL) return DEV_EINVAL;
    FRESULT fr = f_lseek((FIL *)file, offset);
    storage_record_result(storage, fr);
    return fr == FR_OK ? DEV_OK : DEV_EIO;
}

static dev_status_t fs_tell(void *ctx, void *file, uint32_t *offset)
{
    (void)ctx;
    if (file == NULL || offset == NULL) return DEV_EINVAL;
    *offset = (uint32_t)f_tell((FIL *)file);
    return DEV_OK;
}

static void fs_close(void *ctx, void *file)
{
    (void)ctx;
    if (file != NULL) (void)f_close((FIL *)file);
    s_audio_file_open = false;
}

local_fs_t storage_local_fs_adapter(storage_t *storage)
{
    return (local_fs_t){
        .ctx = storage,
        .open = fs_open,
        .read = fs_read,
        .seek = fs_seek,
        .tell = fs_tell,
        .close = fs_close,
    };
}

static dev_status_t settings_read(void *ctx, uint32_t offset, void *data, size_t len)
{
    storage_t *storage = (storage_t *)ctx;
    if (storage == NULL || data == NULL || len > UINT_MAX) {
        return DEV_EINVAL;
    }
    if (!storage_is_mounted(storage)) return DEV_ENOTSUP;
    FIL file;
    /* kiku is the product name. Fall back to the old filename so existing
     * development cards keep their settings after the branding change. */
    FRESULT open_result = f_open(&file, "0:/KIKU.CFG", FA_READ);
    if (open_result != FR_OK) open_result = f_open(&file, "0:/BLOBJECT.CFG", FA_READ);
    if (open_result != FR_OK) {
        storage_record_result(storage, open_result);
        return DEV_EIO;
    }
    FRESULT fr = f_lseek(&file, offset);
    UINT got = 0U;
    if (fr == FR_OK) fr = f_read(&file, data, (UINT)len, &got);
    (void)f_close(&file);
    storage_record_result(storage, fr);
    return (fr == FR_OK && got == (UINT)len) ? DEV_OK : DEV_EIO;
}

static dev_status_t settings_write(void *ctx, uint32_t offset, const void *data, size_t len)
{
    storage_t *storage = (storage_t *)ctx;
    if (storage == NULL || data == NULL || len > UINT_MAX) {
        return DEV_EINVAL;
    }
    if (!storage_is_mounted(storage)) return DEV_ENOTSUP;
    FIL file;
    FRESULT fr = f_open(&file, "0:/KIKU.CFG", FA_READ | FA_WRITE | FA_OPEN_ALWAYS);
    if (fr != FR_OK) {
        storage_record_result(storage, fr);
        return DEV_EIO;
    }
    fr = f_lseek(&file, offset);
    UINT written = 0U;
    if (fr == FR_OK) fr = f_write(&file, data, (UINT)len, &written);
    if (fr == FR_OK) fr = f_sync(&file);
    (void)f_close(&file);
    storage_record_result(storage, fr);
    return (fr == FR_OK && written == (UINT)len) ? DEV_OK : DEV_EIO;
}

settings_storage_t storage_settings_adapter(storage_t *storage)
{
    return (settings_storage_t){ .ctx = storage, .read = settings_read, .write = settings_write };
}

dev_status_t storage_self_test_probe(void *ctx)
{
    storage_t *storage = (storage_t *)ctx;
    return storage_is_mounted(storage) ? DEV_OK : DEV_ENOTREADY;
}
