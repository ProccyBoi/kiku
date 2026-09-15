#ifndef SELF_TEST_H
#define SELF_TEST_H

#include "bm83.h"
#include "bq25895.h"
#include "max17048.h"
#include "si4705.h"
#include "st7789.h"
#include "tlv320aic3104.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SELF_TEST_BQ25895   = 1UL << 0,
    SELF_TEST_MAX17048  = 1UL << 1,
    SELF_TEST_CODEC     = 1UL << 2,
    SELF_TEST_SI4705    = 1UL << 3,
    SELF_TEST_DISPLAY   = 1UL << 4,
    SELF_TEST_BM83_GPIO = 1UL << 5,
    SELF_TEST_STORAGE   = 1UL << 6
};

typedef dev_status_t (*self_test_storage_probe_fn)(void *ctx);

typedef struct {
    bq25895_t *charger;
    max17048_t *gauge;
    tlv320aic3104_t *codec;
    si4705_t *radio;
    st7789_t *display;
    bm83_t *bluetooth;
    void *storage_ctx;
    self_test_storage_probe_fn storage_probe;
} self_test_deps_t;

typedef struct {
    uint32_t tested_mask;
    uint32_t failed_mask;
} self_test_result_t;

self_test_result_t self_test_run_non_destructive(const self_test_deps_t *deps);

#ifdef __cplusplus
}
#endif

#endif
