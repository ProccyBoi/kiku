#include <stdbool.h>

#include "ff.h"
#include "diskio.h"

#include "stm32h5xx_hal.h"

extern SD_HandleTypeDef hsd1;

static volatile DSTATUS s_status = STA_NOINIT;

typedef struct {
    uint32_t init_count;
    uint32_t read_count;
    uint32_t write_count;
    uint32_t sync_count;
    uint32_t init_fail_count;
    uint32_t read_fail_count;
    uint32_t write_fail_count;
    uint32_t sync_fail_count;
    uint32_t wait_timeout_count;
    uint32_t last_tick_ms;
    uint32_t last_sector;
    uint32_t last_count;
    uint32_t last_hal_status;
    uint32_t last_hal_error;
    uint32_t last_card_state;
    uint32_t last_op;
} diskio_diag_t;

volatile diskio_diag_t g_diskio_diag;

enum {
    DISK_DIAG_OP_NONE = 0U,
    DISK_DIAG_OP_INIT,
    DISK_DIAG_OP_READ,
    DISK_DIAG_OP_WRITE,
    DISK_DIAG_OP_SYNC,
};

#define DISK_TRANSFER_BASE_TIMEOUT_MS 1000U
#define DISK_TRANSFER_PER_BLOCK_MS      20U
#define DISK_TRANSFER_MAX_TIMEOUT_MS  2000U

static void mark_not_ready(void)
{
    s_status = STA_NOINIT;
}

static uint32_t transfer_timeout_ms(UINT count)
{
    const UINT extra_blocks =
        (UINT)((DISK_TRANSFER_MAX_TIMEOUT_MS - DISK_TRANSFER_BASE_TIMEOUT_MS) /
               DISK_TRANSFER_PER_BLOCK_MS);
    if (count > extra_blocks) return DISK_TRANSFER_MAX_TIMEOUT_MS;
    return DISK_TRANSFER_BASE_TIMEOUT_MS + ((uint32_t)count * DISK_TRANSFER_PER_BLOCK_MS);
}

static bool wait_ready(uint32_t timeout_ms)
{
    const uint32_t start = HAL_GetTick();
    for (;;) {
        HAL_SD_CardStateTypeDef state = HAL_SD_GetCardState(&hsd1);
        g_diskio_diag.last_card_state = (uint32_t)state;
        g_diskio_diag.last_hal_error = hsd1.ErrorCode;
        if (state == HAL_SD_CARD_TRANSFER) return true;
        if ((HAL_GetTick() - start) >= timeout_ms) {
            ++g_diskio_diag.wait_timeout_count;
            return false;
        }
    }
}

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != 0U) {
        return STA_NOINIT;
    }
    return s_status;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0U) {
        return STA_NOINIT;
    }
    ++g_diskio_diag.init_count;
    g_diskio_diag.last_op = DISK_DIAG_OP_INIT;
    g_diskio_diag.last_tick_ms = HAL_GetTick();
    /* A removable card may have been physically replaced after a previous
     * transfer error. Return the peripheral to a known state before every
     * fresh enumeration instead of relying on stale HAL state. */
    (void)HAL_SD_DeInit(&hsd1);
    HAL_StatusTypeDef hs = HAL_SD_Init(&hsd1);
    g_diskio_diag.last_hal_status = (uint32_t)hs;
    g_diskio_diag.last_hal_error = hsd1.ErrorCode;
    if (hs != HAL_OK || !wait_ready(1000U)) {
        ++g_diskio_diag.init_fail_count;
        mark_not_ready();
        return s_status;
    }
    s_status = 0U;
    return s_status;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0U || buff == NULL || count == 0U) {
        return RES_PARERR;
    }
    if ((s_status & STA_NOINIT) != 0U) return RES_NOTRDY;
    ++g_diskio_diag.read_count;
    g_diskio_diag.last_op = DISK_DIAG_OP_READ;
    g_diskio_diag.last_tick_ms = HAL_GetTick();
    g_diskio_diag.last_sector = (uint32_t)sector;
    g_diskio_diag.last_count = count;
    HAL_StatusTypeDef hs = HAL_SD_ReadBlocks(&hsd1, buff, (uint32_t)sector, count,
                                             transfer_timeout_ms(count));
    g_diskio_diag.last_hal_status = (uint32_t)hs;
    g_diskio_diag.last_hal_error = hsd1.ErrorCode;
    if (hs != HAL_OK) {
        ++g_diskio_diag.read_fail_count;
        mark_not_ready();
        return RES_ERROR;
    }
    if (!wait_ready(1000U)) {
        ++g_diskio_diag.read_fail_count;
        mark_not_ready();
        return RES_ERROR;
    }
    return RES_OK;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0U || buff == NULL || count == 0U) {
        return RES_PARERR;
    }
    if ((s_status & STA_NOINIT) != 0U) return RES_NOTRDY;
    ++g_diskio_diag.write_count;
    g_diskio_diag.last_op = DISK_DIAG_OP_WRITE;
    g_diskio_diag.last_tick_ms = HAL_GetTick();
    g_diskio_diag.last_sector = (uint32_t)sector;
    g_diskio_diag.last_count = count;
    HAL_StatusTypeDef hs = HAL_SD_WriteBlocks(&hsd1, buff, (uint32_t)sector, count,
                                              transfer_timeout_ms(count));
    g_diskio_diag.last_hal_status = (uint32_t)hs;
    g_diskio_diag.last_hal_error = hsd1.ErrorCode;
    if (hs != HAL_OK) {
        ++g_diskio_diag.write_fail_count;
        mark_not_ready();
        return RES_ERROR;
    }
    if (!wait_ready(2000U)) {
        ++g_diskio_diag.write_fail_count;
        mark_not_ready();
        return RES_ERROR;
    }
    return RES_OK;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0U) {
        return RES_PARERR;
    }
    if ((s_status & STA_NOINIT) != 0U) {
        return RES_NOTRDY;
    }

    HAL_SD_CardInfoTypeDef info;
    switch (cmd) {
    case CTRL_SYNC:
        ++g_diskio_diag.sync_count;
        g_diskio_diag.last_op = DISK_DIAG_OP_SYNC;
        g_diskio_diag.last_tick_ms = HAL_GetTick();
        if (!wait_ready(1000U)) {
            ++g_diskio_diag.sync_fail_count;
            mark_not_ready();
            return RES_ERROR;
        }
        return RES_OK;
    case GET_SECTOR_COUNT:
        if (buff == NULL) return RES_PARERR;
        if (HAL_SD_GetCardInfo(&hsd1, &info) != HAL_OK) {
            mark_not_ready();
            return RES_ERROR;
        }
        *(LBA_t *)buff = (LBA_t)info.LogBlockNbr;
        return RES_OK;
    case GET_SECTOR_SIZE:
        if (buff == NULL) return RES_PARERR;
        *(WORD *)buff = 512U;
        return RES_OK;
    case GET_BLOCK_SIZE:
        if (buff == NULL) return RES_PARERR;
        *(DWORD *)buff = 1U;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
