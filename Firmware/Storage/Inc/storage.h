#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ff.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_MAX_PATH 256U

typedef enum {
    STORAGE_FILE_UNKNOWN = 0,
    STORAGE_FILE_WAV,
    STORAGE_FILE_MP3,
} storage_file_type_t;

typedef struct {
    char path[STORAGE_MAX_PATH];
    storage_file_type_t type;
    uint32_t size_bytes;
} storage_track_t;

typedef struct {
    bool mounted;
    FRESULT last_error;
    FRESULT last_scan_error;
    uint32_t scan_entries_seen;
    uint32_t scan_dirs_seen;
    uint32_t scan_audio_seen;
    FATFS fs;
} storage_t;

void storage_init(storage_t *storage);
bool storage_mount(storage_t *storage);
void storage_unmount(storage_t *storage);
bool storage_is_mounted(const storage_t *storage);
size_t storage_scan_music(storage_t *storage, storage_track_t *tracks, size_t capacity);
storage_file_type_t storage_file_type(const char *path);

#ifdef __cplusplus
}
#endif


