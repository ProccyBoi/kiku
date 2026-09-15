#include "self_test.h"

static void record(self_test_result_t *r, uint32_t bit, dev_status_t st)
{
    r->tested_mask |= bit;
    if (st != DEV_OK) r->failed_mask |= bit;
}

self_test_result_t self_test_run_non_destructive(const self_test_deps_t *deps)
{
    self_test_result_t r = {0U, 0U};
    if (deps == NULL) return r;

    if (deps->charger != NULL) {
        uint8_t value = 0U;
        record(&r, SELF_TEST_BQ25895, bq25895_read_reg(deps->charger, 0x14U, &value));
    }
    if (deps->gauge != NULL) {
        uint16_t version = 0U;
        record(&r, SELF_TEST_MAX17048, max17048_get_version(deps->gauge, &version));
    }
    if (deps->codec != NULL) {
        uint8_t page = 0U;
        record(&r, SELF_TEST_CODEC, tlv320aic3104_read_reg(deps->codec, 0U, 0U, &page));
    }
    if (deps->radio != NULL) {
        si4705_tune_status_t status;
        record(&r, SELF_TEST_SI4705, si4705_get_tune_status(deps->radio, false, &status));
    }
    if (deps->display != NULL) {
        /* Write-only panel: this verifies the host SPI/GPIO path, not panel readback. */
        record(&r, SELF_TEST_DISPLAY, st7789_command(deps->display, 0x00U, NULL, 0U));
    }
    if ((deps->bluetooth != NULL) && (deps->bluetooth->tx_ind.read != NULL)) {
        (void)deps->bluetooth->tx_ind.read(deps->bluetooth->tx_ind.ctx);
        record(&r, SELF_TEST_BM83_GPIO, DEV_OK);
    }
    if (deps->storage_probe != NULL) {
        record(&r, SELF_TEST_STORAGE, deps->storage_probe(deps->storage_ctx));
    }
    return r;
}
