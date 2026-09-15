#pragma once

#include "local_playback.h"
#include "settings.h"
#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

local_fs_t storage_local_fs_adapter(storage_t *storage);
settings_storage_t storage_settings_adapter(storage_t *storage);
dev_status_t storage_self_test_probe(void *ctx);

#ifdef __cplusplus
}
#endif
