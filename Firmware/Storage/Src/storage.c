#include "storage.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define STORAGE_MAX_SCAN_DEPTH 8U


static bool ext_eq(const char *path, const char *ext)
{
    const char *dot = strrchr(path, '.');
    if (dot == NULL) return false;
    while (*dot != '\0' && *ext != '\0') {
        if (tolower((unsigned char)*dot) != tolower((unsigned char)*ext)) return false;
        ++dot;
        ++ext;
    }
    return (*dot == '\0' && *ext == '\0');
}

storage_file_type_t storage_file_type(const char *path)
{
    if (path == NULL) return STORAGE_FILE_UNKNOWN;
    if (ext_eq(path, ".wav")) return STORAGE_FILE_WAV;
    if (ext_eq(path, ".mp3")) return STORAGE_FILE_MP3;
    return STORAGE_FILE_UNKNOWN;
}

void storage_init(storage_t *storage)
{
    if (storage == NULL) return;
    memset(storage, 0, sizeof(*storage));
    storage->last_error = FR_NOT_READY;
    storage->last_scan_error = FR_NOT_READY;
}

bool storage_mount(storage_t *storage)
{
    if (storage == NULL) return false;
    storage->last_error = f_mount(&storage->fs, "0:", 1);
    storage->mounted = (storage->last_error == FR_OK);
    return storage->mounted;
}

void storage_unmount(storage_t *storage)
{
    if (storage == NULL) return;
    (void)f_mount(NULL, "0:", 0);
    storage->mounted = false;
}

bool storage_is_mounted(const storage_t *storage)
{
    return (storage != NULL && storage->mounted);
}

static size_t scan_dir(storage_t *storage, const char *dir_path,
                       storage_track_t *tracks, size_t capacity,
                       size_t count, unsigned depth)
{
    if (depth > STORAGE_MAX_SCAN_DEPTH || count >= capacity) return count;

    DIR dir;
    FILINFO info;
    FRESULT fr = f_opendir(&dir, dir_path);
    if (fr != FR_OK) {
        storage->last_scan_error = fr;
        return count;
    }

    for (;;) {
        fr = f_readdir(&dir, &info);
        if (fr != FR_OK) {
            storage->last_scan_error = fr;
            break;
        }
        if (info.fname[0] == '\0') break;
        if (info.fname[0] == '.') continue;

        ++storage->scan_entries_seen;

        char path[STORAGE_MAX_PATH];
        const bool root = (strcmp(dir_path, "0:/") == 0);
        int written;
        if (root) {
            written = snprintf(path, sizeof(path), "0:/%s", info.fname);
        } else {
            written = snprintf(path, sizeof(path), "%s/%s", dir_path, info.fname);
        }
        if (written <= 0 || (size_t)written >= sizeof(path)) continue;

        if ((info.fattrib & AM_DIR) != 0U) {
            ++storage->scan_dirs_seen;
            count = scan_dir(storage, path, tracks, capacity, count, depth + 1U);
            if (count >= capacity) break;
            continue;
        }

        const storage_file_type_t type = storage_file_type(path);
        if (type == STORAGE_FILE_UNKNOWN || count >= capacity) continue;

        memset(&tracks[count], 0, sizeof(tracks[count]));
        (void)snprintf(tracks[count].path, sizeof(tracks[count].path), "%s", path);
        tracks[count].type = type;
        tracks[count].size_bytes = (uint32_t)info.fsize;
        ++storage->scan_audio_seen;
        ++count;
    }

    (void)f_closedir(&dir);
    return count;
}

size_t storage_scan_music(storage_t *storage, storage_track_t *tracks, size_t capacity)
{
    if (storage == NULL || tracks == NULL || capacity == 0U || !storage->mounted) return 0U;
    storage->last_scan_error = FR_OK;
    storage->scan_entries_seen = 0U;
    storage->scan_dirs_seen = 0U;
    storage->scan_audio_seen = 0U;
    return scan_dir(storage, "0:/", tracks, capacity, 0U, 0U);
}



