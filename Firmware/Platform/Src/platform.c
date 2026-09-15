#include "platform.h"

#include "audio_stream.h"
#include "board.h"
#include "mp3_adapter.h"
#include "storage_adapters.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} gpio_ctx_t;

static gpio_ctx_t s_gpio_button1 = { PUSH_1_GPIO_Port, PUSH_1_Pin };
static gpio_ctx_t s_gpio_button2 = { PUSH_2_GPIO_Port, PUSH_2_Pin };
static gpio_ctx_t s_gpio_enc_push = { ENC_PUSH_GPIO_Port, ENC_PUSH_Pin };
static gpio_ctx_t s_gpio_hp_det = { HP_DET_GPIO_Port, HP_DET_Pin };
static gpio_ctx_t s_gpio_speaker = { AMP_SD_MODE_GPIO_Port, AMP_SD_MODE_Pin };
static gpio_ctx_t s_gpio_tlv_rst = { TLV_RST_GPIO_Port, TLV_RST_Pin };
static gpio_ctx_t s_gpio_si_rst = { SI4705_RST_GPIO_Port, SI4705_RST_Pin };
static gpio_ctx_t s_gpio_bm_rst = { BM83_RST_GPIO_Port, BM83_RST_Pin };
static gpio_ctx_t s_gpio_bm_mfb = { BM83_MFB_GPIO_Port, BM83_MFB_Pin };
static gpio_ctx_t s_gpio_bm_tx_ind = { BM83_TX_IND_GPIO_Port, BM83_TX_IND_Pin };
static gpio_ctx_t s_gpio_lcd_cs = { LCD_CS_GPIO_Port, LCD_CS_Pin };
static gpio_ctx_t s_gpio_lcd_dc = { LCD_DC_GPIO_Port, LCD_DC_Pin };
static gpio_ctx_t s_gpio_lcd_rst = { LCD_RST_GPIO_Port, LCD_RST_Pin };

static uint32_t s_encoder_last;
static int32_t s_encoder_residual;
static bool s_radio_powered;
static bool s_fm_adc_enabled;
static bool s_soft_power_bm83_off;
static bool s_soft_power_codec_reset;
static bool s_soft_power_pll2_off;
static bool s_soft_power_hsi_off;
static bool s_soft_power_pwm_off;
static bool s_soft_power_encoder_off;
static bool s_codec_dac_enabled = true;
static bool s_codec_headphones_enabled = true;
static uint32_t s_shuffle_rng = 0x4B494B55UL; /* "KIKU" seed; mixed with runtime tick below. */

/* 48-kHz, 16-bit I2S, codec as BCLK/WCLK slave. Kept at file scope so the
 * exact reviewed power-up profile can be replayed after deep soft-off reset. */
static const tlv320_reg_write_t s_codec_boot[] = {
    {0U, 3U,   0x10U}, /* Q=2. */
    {0U, 101U, 0x01U}, /* CODEC_CLKIN = CLKDIV_OUT. */
    {0U, 102U, 0x02U}, /* CLKDIV_IN = MCLK. */
    {0U, 7U,  0x0AU}, /* 48-kHz ref; left->left and right->right DAC. */
    {0U, 8U,  0x00U}, /* BCLK/WCLK inputs: STM32 is audio-clock master. */
    {0U, 9U,  0x00U}, /* I2S, 16-bit words. */
    {0U, 15U, 0x80U}, {0U, 16U, 0x80U}, /* ADC PGAs initially muted. */
    {0U, 17U, 0xFFU}, {0U, 18U, 0xFFU}, /* LINE2 initially disconnected. */
    {0U, 19U, 0x78U}, {0U, 22U, 0x78U}, /* ADCs initially powered down. */
    {0U, 37U, 0xC0U}, /* Power left and right DACs. */
    {0U, 43U, 0x00U}, {0U, 44U, 0x00U}, /* DAC digital volume unmuted at 0 dB. */
    {0U, 47U, 0x80U}, /* DAC_L1 -> HPLOUT at 0 dB. */
    {0U, 64U, 0x80U}, /* DAC_R1 -> HPROUT at 0 dB. */
    {0U, 51U, 0x0DU}, /* HPLOUT unmuted, powered, high-Z when off. */
    {0U, 65U, 0x0DU}, /* HPROUT unmuted, powered, high-Z when off. */
};

volatile platform_diag_t g_platform_diag;
volatile uint32_t g_bench_bm83_toggle_audio_request;
volatile int32_t g_bench_bm83_toggle_audio_result = DEV_ENOTREADY;
volatile uint32_t g_bench_bm83_play_request;
volatile int32_t g_bench_bm83_play_result = DEV_ENOTREADY;
volatile uint32_t g_bench_bm83_capabilities_request;
volatile int32_t g_bench_bm83_capabilities_result = DEV_ENOTREADY;
volatile uint32_t g_bench_bm83_metadata_v206_request;
volatile int32_t g_bench_bm83_metadata_v206_result = DEV_ENOTREADY;
volatile uint32_t g_bench_bm83_eeprom_read_request;
volatile uint32_t g_bench_bm83_eeprom_read_offset;
volatile uint32_t g_bench_bm83_eeprom_read_length = 16U;
volatile int32_t g_bench_bm83_eeprom_read_result = DEV_ENOTREADY;
volatile uint32_t g_bench_bm83_link_status_request;
volatile int32_t g_bench_bm83_link_status_result = DEV_ENOTREADY;
volatile uint32_t g_bench_bm83_link_back_request;
volatile int32_t g_bench_bm83_link_back_result = DEV_ENOTREADY;
volatile uint32_t g_bench_bm83_tone_request;
volatile int32_t g_bench_bm83_tone_result = DEV_ENOTREADY;
volatile uint32_t g_bench_app_source_request;
volatile int32_t g_bench_app_source_result = DEV_ENOTREADY;
volatile uint32_t g_bench_app_output_request;
volatile int32_t g_bench_app_output_result = DEV_ENOTREADY;
volatile uint32_t g_bench_local_play_request;
volatile int32_t g_bench_local_play_result = DEV_ENOTREADY;
volatile uint32_t g_bench_speaker_direct_request;
volatile int32_t g_bench_speaker_direct_result = DEV_ENOTREADY;
static uint32_t s_diag_last_ms;
static uint32_t s_storage_last_probe_ms;
static uint32_t s_speaker_test_start_ms;
static uint32_t s_speaker_test_phase;

/* A full-screen render and removable-media service can hold task context for
 * tens of milliseconds while USART3 IRQs continue. 4 KiB gives ~350 ms of
 * 115200-baud burst headroom so a large AVRCP response cannot corrupt framing
 * merely because the UI/storage path was busy. */
#define BM83_UART_RX_RING_SIZE 4096U
static uint8_t s_bm83_irq_byte;
static uint8_t s_bm83_rx_ring[BM83_UART_RX_RING_SIZE];
static volatile uint16_t s_bm83_rx_head;
static volatile uint16_t s_bm83_rx_tail;

static void bm83_drain_to_driver(bm83_t *dev)
{
    if (dev == NULL) return;
    while (s_bm83_rx_tail != s_bm83_rx_head) {
        uint8_t byte = s_bm83_rx_ring[s_bm83_rx_tail];
        s_bm83_rx_tail = (uint16_t)((s_bm83_rx_tail + 1U) % BM83_UART_RX_RING_SIZE);
        bm83_rx_byte(dev, byte);
    }
}

static dev_status_t bm83_wait_command_ack(bm83_t *dev, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    while (dev != NULL && dev->awaiting_command_ack) {
        bm83_drain_to_driver(dev);
        dev_status_t st = bm83_service(dev);
        if (st != DEV_OK && st != DEV_EBUSY) return st;
        if ((uint32_t)(HAL_GetTick() - start) >= timeout_ms) return DEV_ETIMEOUT;
        HAL_Delay(1U);
    }
    if (dev == NULL) return DEV_EINVAL;
    bm83_drain_to_driver(dev);
    dev_status_t st = bm83_service(dev);
    return (st == DEV_EBUSY) ? DEV_OK : st;
}

static void bm83_wait_initial_status(bm83_t *dev, uint32_t timeout_ms)
{
    if (dev == NULL) return;
    uint32_t start = HAL_GetTick();
    while (!dev->initial_status_received &&
           (uint32_t)(HAL_GetTick() - start) < timeout_ms) {
        bm83_drain_to_driver(dev);
        HAL_Delay(1U);
    }
    bm83_drain_to_driver(dev);
}

static dev_status_t bm83_power_on_sequence(bm83_t *dev)
{
    if (dev == NULL) return DEV_EINVAL;
    dev_status_t last = DEV_EIO;
    for (unsigned attempt = 0U; attempt < 2U; ++attempt) {
        if (attempt != 0U) {
            last = bm83_hw_reset(dev, BM83_RECOVERY_RESET_LOW_MS,
                                 BM83_RECOVERY_SETTLE_MS);
            if (last != DEV_OK) return last;
            bm83_wait_initial_status(dev, 1000U);
        }

        last = bm83_mmi_action(dev, 0U, BM83_MMI_POWER_ON_PRESS);
        if (last == DEV_OK) last = bm83_wait_command_ack(dev, 1000U);
        if (last != DEV_OK) continue;

        last = bm83_mmi_action(dev, 0U, BM83_MMI_POWER_ON_RELEASE);
        if (last == DEV_OK) last = bm83_wait_command_ack(dev, 1000U);
        if (last == DEV_OK) return DEV_OK;
    }
    return last;
}

static dev_status_t bm83_power_off_sequence(bm83_t *dev, bool *release_sent)
{
    if (dev == NULL) return DEV_EINVAL;
    if (release_sent != NULL) *release_sent = false;

    dev_status_t st = DEV_OK;
    if (dev->awaiting_command_ack) {
        st = bm83_wait_command_ack(dev, 300U);
        if (st != DEV_OK) return st;
    }
    st = bm83_mmi_action(dev, 0U, BM83_MMI_POWER_OFF_PRESS);
    if (st == DEV_OK) st = bm83_wait_command_ack(dev, 500U);
    if (st != DEV_OK) return st;

    st = bm83_mmi_action(dev, 0U, BM83_MMI_POWER_OFF_RELEASE);
    if (st != DEV_OK) return st;
    if (release_sent != NULL) *release_sent = true;
    return bm83_wait_command_ack(dev, 500U);
}

static dev_status_t platform_bm83_ensure_powered(platform_devices_t *platform, bool powered)
{
    if (platform == NULL) return DEV_EINVAL;
    if (powered) {
        if (!s_soft_power_bm83_off) return DEV_OK;
        dev_status_t st = bm83_power_on_sequence(&platform->bluetooth);
        if (st == DEV_OK) s_soft_power_bm83_off = false;
        return st;
    }

    if (s_soft_power_bm83_off) return DEV_OK;
    bool release_sent = false;
    dev_status_t st = bm83_power_off_sequence(&platform->bluetooth, &release_sent);
    /* Once the release command was accepted, the module may already be
     * shutting down even if its final ACK disappears with the UART. Treat it
     * as off for the next explicit power-on sequence. */
    if (release_sent) s_soft_power_bm83_off = true;
    return st;
}

static dev_status_t platform_bm83_recover_connection(void *ctx)
{
    platform_devices_t *platform = (platform_devices_t *)ctx;
    if (platform == NULL) return DEV_EINVAL;
    bm83_t *bt = &platform->bluetooth;

    /* Stop the MCU-side pager before deliberately cycling the module. A soft
     * BM83 power-off, unlike a bare MCU/reset pulse, cleanly tears down the
     * stale controller-side connection which live testing showed could leave
     * the phone reporting "connected" while BM83 had no Classic profile. */
    bm83_set_auto_reconnect(bt, false);
    dev_status_t st = platform_bm83_ensure_powered(platform, false);
    if (st != DEV_OK && !s_soft_power_bm83_off) return st;
    HAL_Delay(BM83_RECOVERY_SETTLE_MS);

    st = platform_bm83_ensure_powered(platform, true);
    if (st != DEV_OK) return st;

    /* Restore the same volatile runtime setup applied at cold boot before any
     * new link-back. This preserves the production 0x4A metadata path and the
     * A2DP + AVRCP CT/TG role mask across a recovery power cycle. */
    st = bm83_unmask_all_events(bt);
    if (st != DEV_OK) return st;
    if ((st = bm83_wait_command_ack(bt, 1000U)) != DEV_OK) return st;
    st = bm83_set_supported_classic_profiles(
        bt, (uint8_t)(BM83_PROFILE_A2DP | BM83_PROFILE_AVRCP_CT | BM83_PROFILE_AVRCP_TG));
    if (st != DEV_OK) return st;
    if ((st = bm83_wait_command_ack(bt, 1000U)) != DEV_OK) return st;

    ++g_bm83_diag.reconnect_power_cycle_count;
    bm83_set_auto_reconnect(bt, true);
    return DEV_OK;
}

static dev_status_t platform_codec_boot(platform_devices_t *platform)
{
    if (platform == NULL) return DEV_EINVAL;
    dev_status_t st = tlv320aic3104_hw_reset(&platform->codec);
    if (st != DEV_OK) return st;
    st = tlv320aic3104_sw_reset(&platform->codec);
    if (st != DEV_OK) return st;
    HAL_Delay(2U);
    st = tlv320aic3104_apply_script(&platform->codec, s_codec_boot,
                                    sizeof(s_codec_boot) / sizeof(s_codec_boot[0]));
    if (st == DEV_OK) {
        s_codec_dac_enabled = true;
        s_codec_headphones_enabled = true;
        s_fm_adc_enabled = false;
    }
    return st;
}

static dev_status_t status_from_hal(HAL_StatusTypeDef status)
{
    switch (status) {
    case HAL_OK: return DEV_OK;
    case HAL_BUSY: return DEV_EBUSY;
    case HAL_TIMEOUT: return DEV_ETIMEOUT;
    case HAL_ERROR:
    default: return DEV_EIO;
    }
}

static bool i2c_recovery_pins(I2C_HandleTypeDef *i2c, GPIO_TypeDef **port,
                              uint16_t *scl, uint16_t *sda)
{
    if (i2c == NULL || port == NULL || scl == NULL || sda == NULL) return false;
    if (i2c->Instance == I2C1) {
        *port = SYS_I2C_SCL_GPIO_Port;
        *scl = SYS_I2C_SCL_Pin;
        *sda = SYS_I2C_SDA_Pin;
        return true;
    }
    if (i2c->Instance == I2C3) {
        *port = FM_SCL_GPIO_Port;
        *scl = FM_SCL_Pin;
        *sda = FM_SDA_Pin;
        return true;
    }
    return false;
}

static bool i2c_recover_bus(I2C_HandleTypeDef *i2c)
{
    GPIO_TypeDef *port = NULL;
    uint16_t scl = 0U;
    uint16_t sda = 0U;
    if (!i2c_recovery_pins(i2c, &port, &scl, &sda)) return false;

    /* Recover a slave that was reset or interrupted mid-byte. Deinitialising
     * first releases the peripheral's ownership of SCL/SDA, after which nine
     * open-drain clocks allow a slave to finish the outstanding byte. A STOP
     * then returns every compliant target to its idle state. This is only used
     * after BUSY/timeout/bus errors; a normal address NACK does not disturb the
     * bus and is deliberately not recovered this way. */
    if (HAL_I2C_DeInit(i2c) != HAL_OK) return false;

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = scl | sda;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port, &gpio);
    HAL_GPIO_WritePin(port, scl | sda, GPIO_PIN_SET);
    HAL_Delay(1U);

    for (unsigned pulse = 0U;
         pulse < 9U && HAL_GPIO_ReadPin(port, sda) == GPIO_PIN_RESET;
         ++pulse) {
        HAL_GPIO_WritePin(port, scl, GPIO_PIN_RESET);
        HAL_Delay(1U);
        HAL_GPIO_WritePin(port, scl, GPIO_PIN_SET);
        HAL_Delay(1U);
    }

    /* Explicit STOP: SDA low while SCL is low, raise SCL, then release SDA. */
    HAL_GPIO_WritePin(port, scl, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(port, sda, GPIO_PIN_RESET);
    HAL_Delay(1U);
    HAL_GPIO_WritePin(port, scl, GPIO_PIN_SET);
    HAL_Delay(1U);
    HAL_GPIO_WritePin(port, sda, GPIO_PIN_SET);
    HAL_Delay(1U);

    if (HAL_I2C_Init(i2c) != HAL_OK) return false;
    if (HAL_I2CEx_ConfigAnalogFilter(i2c, I2C_ANALOGFILTER_ENABLE) != HAL_OK) return false;
    if (HAL_I2CEx_ConfigDigitalFilter(i2c, 0U) != HAL_OK) return false;

    if (i2c->Instance == I2C1) ++g_platform_diag.i2c1_recovery_count;
    else if (i2c->Instance == I2C3) ++g_platform_diag.i2c3_recovery_count;
    return true;
}

static bool i2c_should_recover(I2C_HandleTypeDef *i2c, HAL_StatusTypeDef status)
{
    if (i2c == NULL || status == HAL_OK) return false;
    if (status == HAL_BUSY || status == HAL_TIMEOUT) return true;
    if (status != HAL_ERROR) return false;

    uint32_t error = HAL_I2C_GetError(i2c);
    /* AF is a normal address/data NACK (for example an absent optional device).
     * Recover for any bus-level error, and also for an unexplained HAL_ERROR. */
    return error == HAL_I2C_ERROR_NONE || (error & ~HAL_I2C_ERROR_AF) != 0U;
}

static dev_status_t i2c_write(void *ctx, uint8_t addr7, const uint8_t *data,
                              size_t len, uint32_t timeout_ms)
{
    if (ctx == NULL || data == NULL || len > UINT16_MAX) return DEV_EINVAL;
    I2C_HandleTypeDef *i2c = (I2C_HandleTypeDef *)ctx;
    HAL_StatusTypeDef hs = HAL_I2C_Master_Transmit(i2c, (uint16_t)addr7 << 1,
                                                   (uint8_t *)data, (uint16_t)len,
                                                   timeout_ms);
    if (i2c_should_recover(i2c, hs) && i2c_recover_bus(i2c)) {
        hs = HAL_I2C_Master_Transmit(i2c, (uint16_t)addr7 << 1,
                                     (uint8_t *)data, (uint16_t)len, timeout_ms);
    }
    return status_from_hal(hs);
}

static dev_status_t i2c_read(void *ctx, uint8_t addr7, uint8_t *data,
                             size_t len, uint32_t timeout_ms)
{
    if (ctx == NULL || data == NULL || len > UINT16_MAX) return DEV_EINVAL;
    I2C_HandleTypeDef *i2c = (I2C_HandleTypeDef *)ctx;
    HAL_StatusTypeDef hs = HAL_I2C_Master_Receive(i2c, (uint16_t)addr7 << 1,
                                                  data, (uint16_t)len, timeout_ms);
    if (i2c_should_recover(i2c, hs) && i2c_recover_bus(i2c)) {
        hs = HAL_I2C_Master_Receive(i2c, (uint16_t)addr7 << 1,
                                    data, (uint16_t)len, timeout_ms);
    }
    return status_from_hal(hs);
}

static dev_status_t i2c_write_read(void *ctx, uint8_t addr7, const uint8_t *tx,
                                   size_t tx_len, uint8_t *rx, size_t rx_len,
                                   uint32_t timeout_ms)
{
    if (ctx == NULL || tx == NULL || rx == NULL || tx_len == 0U ||
        rx_len == 0U || rx_len > UINT16_MAX) return DEV_EINVAL;

    /* Register-addressed devices on this board use an 8-bit register pointer.
     * HAL_I2C_Mem_Read emits the repeated START required by the AIC3104. A
     * STOP/START pair makes its pointer advance, so the old implementation
     * returned register N+1 when N was requested. */
    if (tx_len == 1U) {
        I2C_HandleTypeDef *i2c = (I2C_HandleTypeDef *)ctx;
        HAL_StatusTypeDef hs = HAL_I2C_Mem_Read(i2c, (uint16_t)addr7 << 1,
                                                tx[0], I2C_MEMADD_SIZE_8BIT,
                                                rx, (uint16_t)rx_len, timeout_ms);
        if (i2c_should_recover(i2c, hs) && i2c_recover_bus(i2c)) {
            hs = HAL_I2C_Mem_Read(i2c, (uint16_t)addr7 << 1,
                                  tx[0], I2C_MEMADD_SIZE_8BIT,
                                  rx, (uint16_t)rx_len, timeout_ms);
        }
        return status_from_hal(hs);
    }

    dev_status_t st = i2c_write(ctx, addr7, tx, tx_len, timeout_ms);
    return st == DEV_OK ? i2c_read(ctx, addr7, rx, rx_len, timeout_ms) : st;
}

static dev_status_t spi_write(void *ctx, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    if (ctx == NULL || data == NULL) return DEV_EINVAL;
    SPI_HandleTypeDef *spi = (SPI_HandleTypeDef *)ctx;
    while (len != 0U) {
        uint16_t chunk = len > UINT16_MAX ? UINT16_MAX : (uint16_t)len;
        dev_status_t st = status_from_hal(HAL_SPI_Transmit(spi, (uint8_t *)data, chunk, timeout_ms));
        if (st != DEV_OK) return st;
        if (spi->Instance == SPI4) {
            ++g_platform_diag.display_spi_write_calls;
            g_platform_diag.display_spi_write_bytes += chunk;
        }
        data += chunk;
        len -= chunk;
    }
    return DEV_OK;
}

/* FreeRTOS already enters ordinary Cortex-M33 Sleep (WFI) whenever the app task
 * blocks. In soft-off no peripheral below needs to make progress while the CPU
 * is asleep: the display/audio/radio/Bluetooth paths have been stopped and the
 * charger is autonomous. Disable only their RCC *Sleep* clocks, not their
 * normal enable bits or HAL state. A SysTick/TIM6 wake restores the ordinary
 * peripheral clocks automatically, so the polled PA2 encoder button remains a
 * safe wake path and there is no UART/DMA reinitialisation to threaten the
 * proven audio paths. TIM6, IWDG, SRAM and core clocks are deliberately left
 * alone. */
static void platform_set_soft_off_sleep_clocks(bool enable)
{
    if (enable) {
        __HAL_RCC_GPDMA1_CLK_SLEEP_ENABLE();
        __HAL_RCC_GPIOA_CLK_SLEEP_ENABLE();
        __HAL_RCC_GPIOB_CLK_SLEEP_ENABLE();
        __HAL_RCC_GPIOC_CLK_SLEEP_ENABLE();
        __HAL_RCC_GPIOD_CLK_SLEEP_ENABLE();
        __HAL_RCC_GPIOE_CLK_SLEEP_ENABLE();
        __HAL_RCC_SDMMC1_CLK_SLEEP_ENABLE();
        __HAL_RCC_TIM1_CLK_SLEEP_ENABLE();
        __HAL_RCC_TIM2_CLK_SLEEP_ENABLE();
        __HAL_RCC_SPI1_CLK_SLEEP_ENABLE();
        __HAL_RCC_SPI2_CLK_SLEEP_ENABLE();
        __HAL_RCC_SPI4_CLK_SLEEP_ENABLE();
        __HAL_RCC_USART3_CLK_SLEEP_ENABLE();
        __HAL_RCC_I2C1_CLK_SLEEP_ENABLE();
        __HAL_RCC_I2C3_CLK_SLEEP_ENABLE();
    } else {
        __HAL_RCC_GPDMA1_CLK_SLEEP_DISABLE();
        __HAL_RCC_GPIOA_CLK_SLEEP_DISABLE();
        __HAL_RCC_GPIOB_CLK_SLEEP_DISABLE();
        __HAL_RCC_GPIOC_CLK_SLEEP_DISABLE();
        __HAL_RCC_GPIOD_CLK_SLEEP_DISABLE();
        __HAL_RCC_GPIOE_CLK_SLEEP_DISABLE();
        __HAL_RCC_SDMMC1_CLK_SLEEP_DISABLE();
        __HAL_RCC_TIM1_CLK_SLEEP_DISABLE();
        __HAL_RCC_TIM2_CLK_SLEEP_DISABLE();
        __HAL_RCC_SPI1_CLK_SLEEP_DISABLE();
        __HAL_RCC_SPI2_CLK_SLEEP_DISABLE();
        __HAL_RCC_SPI4_CLK_SLEEP_DISABLE();
        __HAL_RCC_USART3_CLK_SLEEP_DISABLE();
        __HAL_RCC_I2C1_CLK_SLEEP_DISABLE();
        __HAL_RCC_I2C3_CLK_SLEEP_DISABLE();
    }
}

static dev_status_t uart_write(void *ctx, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    if (ctx == NULL || data == NULL || len > UINT16_MAX) return DEV_EINVAL;
    return status_from_hal(HAL_UART_Transmit((UART_HandleTypeDef *)ctx,
                                             (uint8_t *)data, (uint16_t)len,
                                             timeout_ms));
}

static bool gpio_read(void *ctx)
{
    gpio_ctx_t *gpio = (gpio_ctx_t *)ctx;
    return gpio != NULL && HAL_GPIO_ReadPin(gpio->port, gpio->pin) == GPIO_PIN_SET;
}

static void gpio_write(void *ctx, bool level)
{
    gpio_ctx_t *gpio = (gpio_ctx_t *)ctx;
    if (gpio != NULL) HAL_GPIO_WritePin(gpio->port, gpio->pin, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void delay_ms(void *ctx, uint32_t ms) { (void)ctx; HAL_Delay(ms); }
static uint32_t time_ms(void *ctx) { (void)ctx; return HAL_GetTick(); }

static dev_gpio_t make_gpio(gpio_ctx_t *ctx, bool output)
{
    return (dev_gpio_t){ .ctx = ctx, .read = gpio_read, .write = output ? gpio_write : NULL };
}

static dev_status_t pcm_write(void *ctx, const int16_t *pcm, size_t frames,
                              uint16_t channels, uint32_t sample_rate_hz)
{
    (void)ctx;
    if (pcm == NULL || frames == 0U || channels == 0U || channels > 2U ||
        sample_rate_hz < 8000U || sample_rate_hz > 96000U) return DEV_EINVAL;

    /* Keep the board I2S/codec clock domain at 48 kHz.  A nearest-neighbour
     * resampler avoids reprogramming clock trees between mixed local files and
     * is deterministic enough for the first firmware release. */
    enum { OUT_FRAMES = 192 };
    int16_t out[OUT_FRAMES * 2U];
    const uint64_t total_out = ((uint64_t)frames * BOARD_AUDIO_SAMPLE_RATE_HZ + sample_rate_hz - 1U) /
                               sample_rate_hz;
    /* Do not block the one foreground task waiting for FIFO space: it also
     * services storage, UI, controls and the watchdog. local_playback retains
     * the decoded PCM on DEV_EBUSY and retries it unchanged on the next tick. */
    if (total_out > audio_stream_free_frames()) return DEV_EBUSY;
    uint64_t produced = 0U;
    while (produced < total_out) {
        size_t batch = (size_t)(total_out - produced);
        if (batch > OUT_FRAMES) batch = OUT_FRAMES;
        for (size_t i = 0U; i < batch; ++i) {
            uint64_t src_frame = ((produced + i) * sample_rate_hz) / BOARD_AUDIO_SAMPLE_RATE_HZ;
            if (src_frame >= frames) src_frame = frames - 1U;
            int16_t left = pcm[src_frame * channels];
            int16_t right = channels == 2U ? pcm[src_frame * 2U + 1U] : left;
            out[i * 2U] = left;
            out[i * 2U + 1U] = right;
        }
        dev_status_t st = audio_stream_write_stereo(out, batch);
        if (st != DEV_OK) return st;
        produced += batch;
    }
    return DEV_OK;
}

static dev_status_t route_switch(void *ctx, audio_source_t source)
{
    platform_devices_t *platform = (platform_devices_t *)ctx;
    if (platform == NULL) return DEV_EINVAL;
    if (source == AUDIO_SOURCE_BLUETOOTH) {
        dev_status_t bt_st = platform_bm83_ensure_powered(platform, true);
        if (bt_st != DEV_OK) return bt_st;
    } else if (source == AUDIO_SOURCE_LOCAL || source == AUDIO_SOURCE_FM) {
        /* Bluetooth is the largest avoidable always-on digital load. Keep the
         * receiver genuinely off while the user is listening to SD or FM, and
         * wake it lazily only when Bluetooth is selected. Failure to shut it
         * down must never prevent switching away from Bluetooth. */
        (void)platform_bm83_ensure_powered(platform, false);
    }
    if (source == AUDIO_SOURCE_FM && !s_radio_powered) {
        /* PCB X1 is the Si4705 32.768 kHz crystal. AN332 requires XOSCEN=1
         * (ARG1 bit4) and OPMODE=0x05 for analogue L/R output. */
        dev_status_t st = si4705_hw_reset(&platform->radio);
        if (st != DEV_OK) return st;
        st = si4705_power_up_analog(&platform->radio, 0x10U, 0x05U);
        if (st != DEV_OK) return st;
        /* AN332: allow the enabled 32.768 kHz crystal at least 500 ms to
         * settle before the first tune command. CTS alone is insufficient. */
        HAL_Delay(500U);
        /* Australia uses 50 us FM de-emphasis. Enable RDS and accept up to
         * two corrected errors per RDS block (BLE thresholds = 2/2/2/2). */
        st = si4705_set_property(&platform->radio, SI4705_PROP_FM_DEEMPHASIS,
                                 SI4705_FM_DEEMPHASIS_50US);
        if (st != DEV_OK) return st;
        st = si4705_set_property(&platform->radio, SI4705_PROP_FM_RDS_CONFIG,
                                 SI4705_FM_RDS_CONFIG_ENABLE_BLE2);
        if (st != DEV_OK) return st;
        /* The SI4705 default seek RSSI threshold is too conservative for the
         * compact kiku antenna path (known-good Sydney stations can be around
         * the low teens dBuV). Keep the Australian 87.5-108.0 MHz / 100 kHz
         * raster explicit and use a modest threshold that still rejects the
         * ~0 dB SNR noise floor seen on weak channels. */
        st = si4705_set_property(&platform->radio, SI4705_PROP_FM_SEEK_BAND_BOTTOM,
                                 APP_FM_MIN_10KHZ);
        if (st != DEV_OK) return st;
        st = si4705_set_property(&platform->radio, SI4705_PROP_FM_SEEK_BAND_TOP,
                                 APP_FM_MAX_10KHZ);
        if (st != DEV_OK) return st;
        st = si4705_set_property(&platform->radio, SI4705_PROP_FM_SEEK_FREQ_SPACING,
                                 APP_FM_STEP_10KHZ);
        if (st != DEV_OK) return st;
        st = si4705_set_property(&platform->radio, SI4705_PROP_FM_SEEK_TUNE_SNR_THRESHOLD,
                                 3U);
        if (st != DEV_OK) return st;
        st = si4705_set_property(&platform->radio, SI4705_PROP_FM_SEEK_TUNE_RSSI_THRESHOLD,
                                 8U);
        if (st != DEV_OK) return st;
        s_radio_powered = true;
    } else if (source != AUDIO_SOURCE_FM && s_radio_powered) {
        /* Do not leave the tuner/XOSC running while another source (or soft
         * power-off) is selected. It will be reset/configured on the next FM
         * selection, which also guarantees a clean RDS FIFO. */
        /* Leaving a broken/removed tuner must not prevent the user switching to
         * SD or Bluetooth. A later FM selection always hardware-resets it. */
        (void)si4705_power_down(&platform->radio);
        s_radio_powered = false;
    }
    return audio_stream_set_source(source);
}

static dev_status_t route_rearm(void *ctx, audio_source_t source)
{
    (void)ctx;
    if (source != AUDIO_SOURCE_BLUETOOTH) return DEV_OK;
    return audio_stream_rearm_bluetooth_rx();
}

static dev_status_t codec_profile(void *ctx, audio_source_t source,
                                  bool headphones_enabled, uint8_t volume_percent)
{
    platform_devices_t *platform = (platform_devices_t *)ctx;
    if (platform == NULL) return DEV_EINVAL;

    audio_stream_set_volume(volume_percent);

    if (source == AUDIO_SOURCE_NONE && s_codec_dac_enabled) {
        dev_status_t dac_st = tlv320aic3104_write_reg(&platform->codec, 0U, 37U, 0x00U);
        if (dac_st != DEV_OK) return dac_st;
        s_codec_dac_enabled = false;
    } else if (source != AUDIO_SOURCE_NONE && !s_codec_dac_enabled) {
        dev_status_t dac_st = tlv320aic3104_write_reg(&platform->codec, 0U, 37U, 0xC0U);
        if (dac_st != DEV_OK) return dac_st;
        s_codec_dac_enabled = true;
    }

    /* TLV320AIC3104 HPLOUT/HPROUT level-control registers: bit3 unmutes and
     * bit0 fully powers the output; bit2 selects high-Z while powered down.
     * This makes SPEAKER mode a real speaker-only route rather than leaving
     * the headphone driver active behind an unplugged jack. */
    if (headphones_enabled != s_codec_headphones_enabled) {
        const uint8_t hp = headphones_enabled ? 0x0DU : 0x04U;
        static const uint8_t regs[] = { 51U, 65U };
        for (size_t i = 0U; i < sizeof(regs); ++i) {
            dev_status_t hp_st = tlv320aic3104_write_reg(&platform->codec, 0U, regs[i], hp);
            if (hp_st != DEV_OK) return hp_st;
        }
        s_codec_headphones_enabled = headphones_enabled;
    }

    if (source == AUDIO_SOURCE_FM && !s_fm_adc_enabled) {
        /* SI4705 FM_L/FM_R are wired to the codec's MIC2L/LINE2L and
         * MIC2R/LINE2R pins. Route each side to its matching ADC at 0 dB,
         * power both ADCs, then unmute their PGAs. */
        static const tlv320_reg_write_t fm_on[] = {
            {0U, 17U, 0x0FU}, /* LINE2L -> left ADC, LINE2R disconnected from left. */
            {0U, 18U, 0xF0U}, /* LINE2R -> right ADC, LINE2L disconnected from right. */
            {0U, 19U, 0x7CU}, /* LINE1L disconnected; left ADC powered. */
            {0U, 22U, 0x7CU}, /* LINE1R disconnected; right ADC powered. */
            {0U, 15U, 0x00U}, /* Left ADC PGA unmuted, 0 dB. */
            {0U, 16U, 0x00U}, /* Right ADC PGA unmuted, 0 dB. */
        };
        dev_status_t st = tlv320aic3104_apply_script(&platform->codec, fm_on,
                                                     sizeof(fm_on) / sizeof(fm_on[0]));
        if (st != DEV_OK) return st;
        s_fm_adc_enabled = true;
    } else if (source != AUDIO_SOURCE_FM && s_fm_adc_enabled) {
        /* TI recommends muting the PGAs before powering an ADC down. */
        static const tlv320_reg_write_t fm_off[] = {
            {0U, 15U, 0x80U}, {0U, 16U, 0x80U},
            {0U, 19U, 0x78U}, {0U, 22U, 0x78U},
            {0U, 17U, 0xFFU}, {0U, 18U, 0xFFU},
        };
        dev_status_t st = tlv320aic3104_apply_script(&platform->codec, fm_off,
                                                     sizeof(fm_off) / sizeof(fm_off[0]));
        if (st != DEV_OK) return st;
        s_fm_adc_enabled = false;
    }
    return DEV_OK;
}

static void transition_gain(void *ctx, uint16_t gain_permille)
{
    (void)ctx;
    audio_stream_set_transition_gain(gain_permille);
}

static dev_status_t platform_soft_power_set(void *ctx, bool on)
{
    platform_devices_t *platform = (platform_devices_t *)ctx;
    if (platform == NULL) return DEV_EINVAL;

    dev_status_t first_error = DEV_OK;
    if (!on) {
        (void)audio_io_set_speaker_enabled(&platform->audio_io, false);

        /* The charger ADC is useful for the live battery/USB UI, but continuous
         * conversions serve no purpose while the product is asleep. Charging
         * itself is left untouched. */
        dev_status_t st = bq25895_set_adc_continuous(&platform->charger, false);
        if (st != DEV_OK && first_error == DEV_OK) first_error = st;

        st = platform_bm83_ensure_powered(platform, false);
        if (st != DEV_OK && first_error == DEV_OK) first_error = st;

        /* Turn the visible hardware off last, after the shutdown animation has
         * already been rendered by the application. */
        st = st7789_sleep(&platform->display);
        platform_set_backlight(0U);
        if (st != DEV_OK && first_error == DEV_OK) first_error = st;

        /* Stop clocks which otherwise continue toggling indefinitely despite a
         * zero-duty backlight and stationary encoder. The encoder push switch is
         * an ordinary GPIO and remains readable for the long-press wake gesture. */
        if (HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1) == HAL_OK) {
            s_soft_power_pwm_off = true;
        } else if (first_error == DEV_OK) {
            first_error = DEV_EIO;
        }
        if (HAL_TIM_Encoder_Stop(&htim2, TIM_CHANNEL_ALL) == HAL_OK) {
            s_soft_power_encoder_off = true;
        } else if (first_error == DEV_OK) {
            first_error = DEV_EIO;
        }

        /* Hold optional analogue/RF devices in hardware reset while asleep.
         * Their register state is not needed: the codec is replayed from the
         * reviewed boot script on wake and the Si4705 is always reset when FM
         * is next selected. This removes residual quiescent current that a
         * software power-down alone cannot eliminate. */
        HAL_GPIO_WritePin(TLV_RST_GPIO_Port, TLV_RST_Pin, GPIO_PIN_RESET);
        platform->codec.current_page = 0xFFU;
        s_soft_power_codec_reset = true;
        HAL_GPIO_WritePin(SI4705_RST_GPIO_Port, SI4705_RST_Pin, GPIO_PIN_RESET);

        /* SPI1/SPI2 audio is already suspended by audio_router_set_source(NONE)
         * before this hook runs. Their dedicated fractional PLL can therefore be
         * shut down for soft-off and restarted before audio routing on wake. */
        if (__HAL_RCC_GET_FLAG(RCC_FLAG_PLL2RDY) != 0U) {
            __HAL_RCC_PLL2_DISABLE();
            uint32_t started = HAL_GetTick();
            while (__HAL_RCC_GET_FLAG(RCC_FLAG_PLL2RDY) != 0U) {
                if ((uint32_t)(HAL_GetTick() - started) >= 10U) {
                    if (first_error == DEV_OK) first_error = DEV_ETIMEOUT;
                    break;
                }
            }
            if (__HAL_RCC_GET_FLAG(RCC_FLAG_PLL2RDY) == 0U) s_soft_power_pll2_off = true;
        }

        /* HSI64 is only selected as the I2C1/I2C3 kernel clock on this board.
         * Both buses are idle once the charger ADC has been stopped and the
         * codec/radio are held reset, so the oscillator itself can also be
         * stopped without touching SYSCLK (PLL1/HSE), FreeRTOS or the IWDG.
         * Restore it before any wake-time I2C access below. */
        if (__HAL_RCC_GET_FLAG(RCC_FLAG_HSIRDY) != 0U) {
            __HAL_RCC_HSI_DISABLE();
            uint32_t started = HAL_GetTick();
            while (__HAL_RCC_GET_FLAG(RCC_FLAG_HSIRDY) != 0U) {
                if ((uint32_t)(HAL_GetTick() - started) >= 10U) {
                    if (first_error == DEV_OK) first_error = DEV_ETIMEOUT;
                    break;
                }
            }
            if (__HAL_RCC_GET_FLAG(RCC_FLAG_HSIRDY) == 0U) s_soft_power_hsi_off = true;
        }
        /* This is intentionally last: every shutdown transaction above runs
         * with the same clocks it always had. The LPEN bits only take effect on
         * the next WFI and therefore cannot truncate an in-flight command. */
        platform_set_soft_off_sleep_clocks(false);
        return first_error;
    }

    /* Restore Sleep permissions before any peripheral is brought back. LPEN
     * does not affect the currently-awake core, but doing this first guarantees
     * the first post-wake WFI has the same semantics as normal operation. */
    platform_set_soft_off_sleep_clocks(true);

    /* Restore HSI before the codec or charger can touch I2C1. SYSCLK never left
     * PLL1/HSE, so this has no effect on RTOS/HAL timing. */
    if (s_soft_power_hsi_off) {
        __HAL_RCC_HSI_ENABLE();
        uint32_t started = HAL_GetTick();
        while (__HAL_RCC_GET_FLAG(RCC_FLAG_HSIRDY) == 0U) {
            if ((uint32_t)(HAL_GetTick() - started) >= 10U) {
                if (first_error == DEV_OK) first_error = DEV_ETIMEOUT;
                break;
            }
        }
        if (__HAL_RCC_GET_FLAG(RCC_FLAG_HSIRDY) != 0U) s_soft_power_hsi_off = false;
    }

    /* Restore the cheap local clocks first. The audio PLL must be stable before
     * app_begin_power_on() selects the previous source immediately after this
     * callback returns. */
    if (s_soft_power_pll2_off) {
        __HAL_RCC_PLL2_ENABLE();
        uint32_t started = HAL_GetTick();
        while (__HAL_RCC_GET_FLAG(RCC_FLAG_PLL2RDY) == 0U) {
            if ((uint32_t)(HAL_GetTick() - started) >= 10U) {
                if (first_error == DEV_OK) first_error = DEV_ETIMEOUT;
                break;
            }
        }
        if (__HAL_RCC_GET_FLAG(RCC_FLAG_PLL2RDY) != 0U) s_soft_power_pll2_off = false;
    }
    if (s_soft_power_codec_reset) {
        dev_status_t codec_st = platform_codec_boot(platform);
        if (codec_st == DEV_OK) s_soft_power_codec_reset = false;
        else if (first_error == DEV_OK) first_error = codec_st;
    }
    if (s_soft_power_encoder_off) {
        if (HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL) == HAL_OK) {
            s_soft_power_encoder_off = false;
            s_encoder_last = __HAL_TIM_GET_COUNTER(&htim2);
            s_encoder_residual = 0;
        } else if (first_error == DEV_OK) {
            first_error = DEV_EIO;
        }
    }
    if (s_soft_power_pwm_off) {
        if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) == HAL_OK) {
            s_soft_power_pwm_off = false;
        } else if (first_error == DEV_OK) {
            first_error = DEV_EIO;
        }
    }

    /* Wake the panel without also waking Bluetooth. The application renders
     * the power-on animation first; source restoration then lazily powers BM83
     * only if Bluetooth is actually the source being resumed. */
    dev_status_t st = st7789_wake(&platform->display);
    /* The app owns the eased wake fade; start dark to avoid a one-frame flash
     * before the first POWERING_ON frame is rendered. */
    platform_set_backlight(0U);
    if (st != DEV_OK) first_error = st;

    st = bq25895_set_adc_continuous(&platform->charger, true);
    if (st != DEV_OK && first_error == DEV_OK) first_error = st;

    return first_error;
}

static dev_status_t local_track_path(void *ctx, int32_t step, char *path, size_t path_capacity)
{
    platform_devices_t *platform = (platform_devices_t *)ctx;
    if (platform == NULL || path == NULL || path_capacity == 0U) return DEV_EINVAL;
    if (platform->track_count == 0U) return DEV_ENOTREADY;

    if (step != 0) {
        int64_t count = (int64_t)platform->track_count;
        int64_t next = (int64_t)platform->track_index + step;
        next %= count;
        if (next < 0) next += count;
        platform->track_index = (size_t)next;
    }
    int n = snprintf(path, path_capacity, "%s", platform->tracks[platform->track_index].path);
    return (n > 0 && (size_t)n < path_capacity) ? DEV_OK : DEV_EOVERFLOW;
}

static dev_status_t local_random_track_path(void *ctx, char *path, size_t path_capacity)
{
    platform_devices_t *platform = (platform_devices_t *)ctx;
    if (platform == NULL || path == NULL || path_capacity == 0U) return DEV_EINVAL;
    if (platform->track_count == 0U) return DEV_ENOTREADY;

    /* xorshift32 is small, deterministic and sufficient for playback order.
     * Mix in user/runtime timing so each power-up does not repeat one sequence. */
    s_shuffle_rng ^= HAL_GetTick() + 0x9E3779B9UL;
    s_shuffle_rng ^= s_shuffle_rng << 13;
    s_shuffle_rng ^= s_shuffle_rng >> 17;
    s_shuffle_rng ^= s_shuffle_rng << 5;
    if (s_shuffle_rng == 0U) s_shuffle_rng = 0x4B494B55UL;

    size_t next = 0U;
    if (platform->track_count > 1U) {
        /* Select uniformly from every index except the track already playing. */
        next = (size_t)(s_shuffle_rng % (uint32_t)(platform->track_count - 1U));
        if (next >= platform->track_index) ++next;
    }
    platform->track_index = next;
    int n = snprintf(path, path_capacity, "%s", platform->tracks[next].path);
    return (n > 0 && (size_t)n < path_capacity) ? DEV_OK : DEV_EOVERFLOW;
}

dev_status_t platform_devices_init(platform_devices_t *platform)
{
    if (platform == NULL) return DEV_EINVAL;
    memset(platform, 0, sizeof(*platform));
    memset((void *)&g_platform_diag, 0, sizeof(g_platform_diag));

    dev_clock_t clock = { .ctx = NULL, .delay_ms = delay_ms, .time_ms = time_ms };
    dev_i2c_bus_t i2c1 = { .ctx = &hi2c1, .write = i2c_write, .read = i2c_read, .write_read = i2c_write_read };
    dev_i2c_bus_t i2c3 = { .ctx = &hi2c3, .write = i2c_write, .read = i2c_read, .write_read = i2c_write_read };
    dev_spi_bus_t spi4 = { .ctx = &hspi4, .write = spi_write };
    dev_uart_bus_t uart3 = { .ctx = &huart3, .write = uart_write };

    dev_gpio_t tlv_rst = make_gpio(&s_gpio_tlv_rst, true);
    dev_gpio_t si_rst = make_gpio(&s_gpio_si_rst, true);
    dev_gpio_t bm_rst = make_gpio(&s_gpio_bm_rst, true);
    dev_gpio_t bm_mfb = make_gpio(&s_gpio_bm_mfb, true);
    dev_gpio_t bm_tx = make_gpio(&s_gpio_bm_tx_ind, false);
    dev_gpio_t lcd_cs = make_gpio(&s_gpio_lcd_cs, true);
    dev_gpio_t lcd_dc = make_gpio(&s_gpio_lcd_dc, true);
    dev_gpio_t lcd_rst = make_gpio(&s_gpio_lcd_rst, true);
    dev_gpio_t hp = make_gpio(&s_gpio_hp_det, false);
    dev_gpio_t speaker = make_gpio(&s_gpio_speaker, true);

    dev_status_t st;
    if ((st = bq25895_init(&platform->charger, &i2c1, BQ25895_I2C_ADDR_DEFAULT)) != DEV_OK) return st;
    /* Do this before SD scanning, BM83 reset delays, display init, etc. The
     * BQ25895 enables automatic D+/D- high-voltage adapter negotiation at
     * reset, whereas this PCB's PTC/TVS input protection is 5-V class. */
    if ((st = bq25895_configure_5v_only_input(&platform->charger,
                                               APP_USB_INPUT_LIMIT_MA)) != DEV_OK) return st;
    /* Any I2C write enters BQ25895 host mode and starts its default 40 s host
     * watchdog. If that watchdog expires, almost every charger register returns
     * to reset defaults -- including automatic D+/D- and high-voltage adapter
     * negotiation. Rev A's VBUS protection is only 5-V class, so do not permit
     * the safety profile above to silently disappear after 40 seconds. */
    if ((st = bq25895_set_watchdog_seconds(&platform->charger, 0U)) != DEV_OK) return st;
    if ((st = bq25895_set_adc_continuous(&platform->charger, true)) != DEV_OK) return st;
    if ((st = max17048_init(&platform->gauge, &i2c1, MAX17048_I2C_ADDR_DEFAULT)) != DEV_OK) return st;
    if ((st = tlv320aic3104_init(&platform->codec, &i2c1, &tlv_rst, &clock, TLV320AIC3104_I2C_ADDR_DEFAULT)) != DEV_OK) return st;
    if ((st = si4705_init(&platform->radio, &i2c3, &si_rst, &clock, SI4705_I2C_ADDR_DEFAULT)) != DEV_OK) return st;
    if ((st = bm83_init(&platform->bluetooth, &uart3, &bm_rst, &bm_mfb, &bm_tx, &clock)) != DEV_OK) return st;

    /* Potentially slow/removable-media work comes only after the charger has
     * been forced into its 5-V-only input profile. */
    storage_init(&platform->storage);
    if (storage_mount(&platform->storage)) {
        platform->track_count = storage_scan_music(&platform->storage, platform->tracks,
                                                   sizeof(platform->tracks) / sizeof(platform->tracks[0]));
    }
    s_bm83_rx_head = 0U;
    s_bm83_rx_tail = 0U;
    if (HAL_UART_Receive_IT(&huart3, &s_bm83_irq_byte, 1U) != HAL_OK) return DEV_EIO;
    /* Board GPIO starts BM83 in reset. Follow Microchip's Host MCU reset and
     * mandatory power-on button press/release sequence before exposing the
     * module to the application. UART RX is already armed so boot events are
     * acknowledged during the 500 ms reset-settle period. */
    if ((st = bm83_hw_reset(&platform->bluetooth, BM83_RECOVERY_RESET_LOW_MS,
                            BM83_RECOVERY_SETTLE_MS)) != DEV_OK) return st;
    bm83_drain_to_driver(&platform->bluetooth);
    /* Report_BTM_Initial_Status (0x30/0x00) is the documented indication that
     * BM83 firmware initialisation is complete. Wait for it before issuing the
     * power-button MMI sequence; if this particular config masks the event the
     * timeout is deliberately non-fatal and the verified command handshake is
     * still attempted. A failed first handshake gets one full reset/retry. */
    bm83_wait_initial_status(&platform->bluetooth, 1000U);
    if ((st = bm83_power_on_sequence(&platform->bluetooth)) != DEV_OK) return st;

    /* Metadata on the production BM83 uses legacy AVRCP event 0x1A. A masked
     * event still leaves command 0x0B ACKing successfully, which is
     * indistinguishable from the live failure unless the mask is normalised.
     * This is a volatile runtime setting and does not rewrite BM83 EEPROM. */
    st = bm83_unmask_all_events(&platform->bluetooth);
    if (st != DEV_OK) return st;
    if ((st = bm83_wait_command_ack(&platform->bluetooth, 1000U)) != DEV_OK) return st;

    /* kiku is an A2DP sink with AVRCP control/metadata in both directions. The
     * Config Tool image does not expose whether CT/TG roles are actually active,
     * while the Audio UART spec provides a documented volatile runtime mask.
     * Normalise those roles before any later link-back so phones negotiate the
     * controller path required for GetElementAttributes/track metadata. */
    st = bm83_set_supported_classic_profiles(
        &platform->bluetooth,
        (uint8_t)(BM83_PROFILE_A2DP | BM83_PROFILE_AVRCP_CT | BM83_PROFILE_AVRCP_TG));
    if (st != DEV_OK) return st;
    if ((st = bm83_wait_command_ack(&platform->bluetooth, 1000U)) != DEV_OK) return st;

    /* Capture the exact running BM83 package versions. These documented
     * queries are also used as a compatibility gate before any maintenance
     * factory-config update is ever allowed. */
    static const uint8_t version_types[] = { 0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U };
    for (size_t i = 0U; i < sizeof(version_types); ++i) {
        st = bm83_read_version(&platform->bluetooth, version_types[i]);
        if (st == DEV_OK) (void)bm83_wait_command_ack(&platform->bluetooth, 1000U);
        HAL_Delay(20U);
        bm83_drain_to_driver(&platform->bluetooth);
    }
    /* Capture the read-only Classic pairing inventory as well. If a phone later
     * reports itself connected while BM83 remains in standby, this tells us
     * whether link-back has a valid Classic record to target without mutating
     * the user's pairing table. */
    st = bm83_read_paired_device_record(&platform->bluetooth);
    if (st == DEV_OK) {
        (void)bm83_wait_command_ack(&platform->bluetooth, 1000U);
        HAL_Delay(20U);
        bm83_drain_to_driver(&platform->bluetooth);
    }
    if ((st = st7789_init(&platform->display, &spi4, &lcd_cs, &lcd_dc, &lcd_rst, &clock,
                          APP_LCD_WIDTH, APP_LCD_HEIGHT, APP_LCD_X_OFFSET, APP_LCD_Y_OFFSET)) != DEV_OK) return st;
    /* Live-board verification: with headphones inserted the fitted SJ1-3525N
     * detect network reads HIGH at HP_DET. Treat HIGH as headphones present;
     * AUTO mode therefore mutes the internal speaker while a plug is fitted. */
    if ((st = audio_io_init(&platform->audio_io, &hp, &speaker, true, true)) != DEV_OK) return st;

    if ((st = platform_codec_boot(platform)) != DEV_OK) return st;

    if ((st = audio_stream_init()) != DEV_OK) return st;

    platform->deps = (app_deps_t){
        .charger = &platform->charger,
        .gauge = &platform->gauge,
        .codec = &platform->codec,
        .radio = &platform->radio,
        .bluetooth = &platform->bluetooth,
        .display = &platform->display,
        .audio_io = &platform->audio_io,
        .settings_storage = storage_settings_adapter(&platform->storage),
        .local_fs = storage_local_fs_adapter(&platform->storage),
        .pcm_sink = { .ctx = platform, .write = pcm_write },
        .mp3_decoder = mp3_adapter_create(),
        .audio_route_ops = {
            .ctx = platform,
            .switch_source = route_switch,
            .rearm_source = route_rearm,
            .apply_codec_profile = codec_profile,
            .set_transition_gain = transition_gain
        },
        .button1 = make_gpio(&s_gpio_button1, false),
        .button2 = make_gpio(&s_gpio_button2, false),
        .encoder_push = make_gpio(&s_gpio_enc_push, false),
        .clock = clock,
        .storage_probe = storage_self_test_probe,
        .storage_probe_ctx = &platform->storage,
        .bm83_controls = {0}, /* Filled only after the final BM83 Config Tool image freezes command mapping. */
        .si4705_power_on_init = false,
        .si4705_power_func = 0x10U,
        .si4705_power_opmode = 0x05U,
        .si4705_properties = NULL,
        .si4705_property_count = 0U,
        .local_track_path = local_track_path,
        .local_random_track_path = local_random_track_path,
        .local_track_ctx = platform,
        .soft_power_set = platform_soft_power_set,
        .soft_power_ctx = platform,
        .bluetooth_recover = platform_bm83_recover_connection,
        .bluetooth_recover_ctx = platform,
    };
    s_encoder_last = __HAL_TIM_GET_COUNTER(&htim2);
    s_encoder_residual = 0;
    /* Keep the panel dark until the boot animation begins. main.c ramps this
     * to the user's configured brightness using the animation progress. */
    platform_set_backlight(0U);
    return DEV_OK;
}

int16_t platform_encoder_delta(void)
{
    uint32_t now = __HAL_TIM_GET_COUNTER(&htim2);
    int32_t diff = (int32_t)(now - s_encoder_last);
    s_encoder_last = now;
    /* PEC11L detents normally span four quadrature edges. Accumulate partial
     * motion across polls instead of throwing away 1-3 edges every 10 ms. */
    s_encoder_residual += diff;
    int32_t detents = s_encoder_residual / 4;
    s_encoder_residual -= detents * 4;
    if (detents > INT16_MAX) detents = INT16_MAX;
    if (detents < INT16_MIN) detents = INT16_MIN;
    return (int16_t)detents;
}

void platform_set_backlight(uint8_t percent)
{
    if (percent > 100U) percent = 100U;
    platform_set_backlight_level((uint16_t)percent * 100U);
}

void platform_set_backlight_level(uint16_t level_10000)
{
    if (level_10000 > 10000U) level_10000 = 10000U;
    uint32_t period = __HAL_TIM_GET_AUTORELOAD(&htim1) + 1U;
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1,
                          (period * (uint32_t)level_10000 + 5000U) / 10000U);
}

void platform_poll_bm83(kiku_app_t *app)
{
    if (app == NULL) return;
    while (s_bm83_rx_tail != s_bm83_rx_head) {
        uint8_t byte = s_bm83_rx_ring[s_bm83_rx_tail];
        s_bm83_rx_tail = (uint16_t)((s_bm83_rx_tail + 1U) % BM83_UART_RX_RING_SIZE);
        kiku_app_bm83_rx(app, &byte, 1U);
    }
}

static void platform_service_storage(kiku_app_t *app)
{
    if (app == NULL || app->deps.local_track_ctx == NULL) return;
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - s_storage_last_probe_ms) < 1000U) return;
    s_storage_last_probe_ms = now;

    platform_devices_t *platform = (platform_devices_t *)app->deps.local_track_ctx;
    bool was_mounted = storage_is_mounted(&platform->storage);
    if (!was_mounted) {
        if (storage_mount(&platform->storage)) {
            platform->track_index = 0U;
            platform->track_count = storage_scan_music(&platform->storage,
                                                        platform->tracks,
                                                        sizeof(platform->tracks) / sizeof(platform->tracks[0]));
        }
    } else if (platform->track_count == 0U) {
        /* No card-detect pin is wired on this PCB. Retrying an empty library is
         * cheap and lets a card inserted after boot become usable automatically. */
        platform->track_index = 0U;
        platform->track_count = storage_scan_music(&platform->storage,
                                                    platform->tracks,
                                                    sizeof(platform->tracks) / sizeof(platform->tracks[0]));
    }

    app->ui.storage_mounted = storage_is_mounted(&platform->storage);
    g_platform_diag.storage_mounted = platform->storage.mounted ? 1U : 0U;
    g_platform_diag.storage_mount_error = (uint8_t)platform->storage.last_error;
    g_platform_diag.storage_scan_error = (uint8_t)platform->storage.last_scan_error;
    g_platform_diag.storage_entries_seen = platform->storage.scan_entries_seen;
    g_platform_diag.storage_dirs_seen = platform->storage.scan_dirs_seen;
    g_platform_diag.storage_audio_seen = platform->storage.scan_audio_seen;
    g_platform_diag.storage_track_count = (uint32_t)platform->track_count;
}
static void platform_update_diagnostics(const kiku_app_t *app)
{
    uint32_t now = HAL_GetTick();
    g_platform_diag.tick_ms = now;
    g_platform_diag.hp_detect_raw = HAL_GPIO_ReadPin(HP_DET_GPIO_Port, HP_DET_Pin) == GPIO_PIN_SET;
    g_platform_diag.headphones_present = app->audio.headphones_present ? 1U : 0U;
    g_platform_diag.speaker_gpio_high = HAL_GPIO_ReadPin(AMP_SD_MODE_GPIO_Port, AMP_SD_MODE_Pin) == GPIO_PIN_SET;
    g_platform_diag.bm83_tx_ind_raw = HAL_GPIO_ReadPin(BM83_TX_IND_GPIO_Port, BM83_TX_IND_Pin) == GPIO_PIN_SET;

    if ((uint32_t)(now - s_diag_last_ms) >= 500U) {
        s_diag_last_ms = now;
        uint16_t v = 0U;
        uint8_t mask = 0U;
        if (max17048_get_version(app->deps.gauge, &v) == DEV_OK) {
            g_platform_diag.max17048_version = v; mask |= 1U << 0;
        }
        if (max17048_read_reg(app->deps.gauge, 0x02U, &v) == DEV_OK) {
            g_platform_diag.max17048_vcell_raw = v; mask |= 1U << 1;
        }
        if (max17048_read_reg(app->deps.gauge, 0x04U, &v) == DEV_OK) {
            g_platform_diag.max17048_soc_raw = v; mask |= 1U << 2;
        }
        if (max17048_read_reg(app->deps.gauge, 0x16U, &v) == DEV_OK) {
            g_platform_diag.max17048_crate_raw = v; mask |= 1U << 3;
        }
        if (max17048_read_reg(app->deps.gauge, 0x1AU, &v) == DEV_OK) {
            g_platform_diag.max17048_status_raw = v; mask |= 1U << 4;
        }
        uint8_t bq = 0U;
        if (bq25895_read_reg(app->deps.charger, 0x02U, &bq) == DEV_OK) g_platform_diag.bq_reg02 = bq;
        if (bq25895_read_reg(app->deps.charger, 0x06U, &bq) == DEV_OK) g_platform_diag.bq_reg06 = bq;
        if (bq25895_read_reg(app->deps.charger, 0x0EU, &bq) == DEV_OK) g_platform_diag.bq_reg0e = bq;
        g_platform_diag.max17048_read_mask = mask;

        uint8_t tr = 0U;
        uint16_t tm = 0U;
        if (tlv320aic3104_read_reg(app->deps.codec, 0U, 3U, &tr) == DEV_OK) { g_platform_diag.tlv_reg03 = tr; tm |= 1U << 0; }
        if (tlv320aic3104_read_reg(app->deps.codec, 0U, 7U, &tr) == DEV_OK) { g_platform_diag.tlv_reg07 = tr; tm |= 1U << 1; }
        if (tlv320aic3104_read_reg(app->deps.codec, 0U, 8U, &tr) == DEV_OK) { g_platform_diag.tlv_reg08 = tr; tm |= 1U << 2; }
        if (tlv320aic3104_read_reg(app->deps.codec, 0U, 9U, &tr) == DEV_OK) { g_platform_diag.tlv_reg09 = tr; tm |= 1U << 3; }
        if (tlv320aic3104_read_reg(app->deps.codec, 0U, 37U, &tr) == DEV_OK) { g_platform_diag.tlv_reg37 = tr; tm |= 1U << 4; }
        if (tlv320aic3104_read_reg(app->deps.codec, 0U, 43U, &tr) == DEV_OK) { g_platform_diag.tlv_reg43 = tr; tm |= 1U << 5; }
        if (tlv320aic3104_read_reg(app->deps.codec, 0U, 44U, &tr) == DEV_OK) { g_platform_diag.tlv_reg44 = tr; tm |= 1U << 6; }
        if (tlv320aic3104_read_reg(app->deps.codec, 0U, 47U, &tr) == DEV_OK) { g_platform_diag.tlv_reg47 = tr; tm |= 1U << 7; }
        if (tlv320aic3104_read_reg(app->deps.codec, 0U, 51U, &tr) == DEV_OK) { g_platform_diag.tlv_reg51 = tr; tm |= 1U << 8; }
        if (tlv320aic3104_read_reg(app->deps.codec, 0U, 64U, &tr) == DEV_OK) { g_platform_diag.tlv_reg64 = tr; tm |= 1U << 9; }
        if (tlv320aic3104_read_reg(app->deps.codec, 0U, 65U, &tr) == DEV_OK) { g_platform_diag.tlv_reg65 = tr; tm |= 1U << 10; }
        if (tlv320aic3104_read_reg(app->deps.codec, 0U, 94U, &tr) == DEV_OK) { g_platform_diag.tlv_reg94 = tr; tm |= 1U << 11; }
        if (tlv320aic3104_read_reg(app->deps.codec, 0U, 95U, &tr) == DEV_OK) { g_platform_diag.tlv_reg95 = tr; tm |= 1U << 12; }
        if (tlv320aic3104_read_reg(app->deps.codec, 0U, 101U, &tr) == DEV_OK) { g_platform_diag.tlv_reg101 = tr; tm |= 1U << 13; }
        if (tlv320aic3104_read_reg(app->deps.codec, 0U, 102U, &tr) == DEV_OK) { g_platform_diag.tlv_reg102 = tr; tm |= 1U << 14; }
        g_platform_diag.tlv_read_mask = tm;
    }
}

static void platform_service_speaker_test(const kiku_app_t *app)
{
    if (g_platform_diag.speaker_test_request != 0U && g_platform_diag.speaker_test_active == 0U) {
        g_platform_diag.speaker_test_request = 0U;
        g_platform_diag.speaker_test_active = 1U;
        s_speaker_test_start_ms = HAL_GetTick();
        s_speaker_test_phase = 0U;
        HAL_GPIO_WritePin(AMP_SD_MODE_GPIO_Port, AMP_SD_MODE_Pin, GPIO_PIN_SET);
        /* Bench diagnostic: make this deliberately obvious. The previous
         * test used +/-3500 samples at 20% software volume, which reduced the
         * signal to only about -33 dBFS before the MAX98357 and was too quiet
         * to use as an acoustic pass/fail test. Keep some digital headroom,
         * but bypass the normal listening-volume attenuation. */
        audio_stream_set_volume(100U);
        (void)audio_stream_set_source(AUDIO_SOURCE_LOCAL);
    }
    if (g_platform_diag.speaker_test_active == 0U) return;

    int16_t tone[192U * 2U];
    for (size_t i = 0U; i < 192U; ++i) {
        /* 1 kHz square wave at 48 kHz. +/-12000 is about -8.7 dBFS, loud
         * enough to be unmistakable while retaining margin for an unknown
         * small speaker/load. */
        int16_t sample = ((s_speaker_test_phase / 24U) & 1U) != 0U ? 12000 : -12000;
        tone[i * 2U] = sample;
        tone[i * 2U + 1U] = sample;
        ++s_speaker_test_phase;
    }
    /* Keep the FIFO comfortably ahead of the 48 kHz consumer.  The app task
     * runs every 10 ms, so writing only one 192-frame block here would supply
     * 19.2 kframe/s and make most of the diagnostic stream digital silence.
     * 192 frames is exactly four periods of the 1 kHz tone, so the same block
     * can be repeated seamlessly while filling all currently available room. */
    while (audio_stream_free_frames() >= 192U) {
        if (audio_stream_write_stereo(tone, 192U) != DEV_OK) break;
    }

    if ((uint32_t)(HAL_GetTick() - s_speaker_test_start_ms) >= 5000U) {
        g_platform_diag.speaker_test_active = 0U;
        audio_stream_set_volume(app->audio.volume_percent);
        (void)audio_stream_set_source(app->audio.source);
        bool speaker = false;
        switch (app->audio.output) {
        case AUDIO_OUTPUT_SPEAKER:
        case AUDIO_OUTPUT_BOTH: speaker = true; break;
        case AUDIO_OUTPUT_AUTO: speaker = !app->audio.headphones_present; break;
        case AUDIO_OUTPUT_HEADPHONES:
        default: speaker = false; break;
        }
        (void)audio_io_set_speaker_enabled(app->audio.io, speaker);
    }
}

static void platform_service_bm83_bench(const kiku_app_t *app)
{
    if (app == NULL || app->deps.bluetooth == NULL) return;
    bm83_t *bt = app->deps.bluetooth;

    if (g_bench_bm83_toggle_audio_request != 0U) {
        dev_status_t st = bm83_toggle_audio_source(bt);
        g_bench_bm83_toggle_audio_result = st;
        if (st != DEV_EBUSY) g_bench_bm83_toggle_audio_request = 0U;
    }
    if (g_bench_bm83_play_request != 0U) {
        uint32_t requested = g_bench_bm83_play_request;
        bm83_music_action_t action;
        switch (requested) {
        case 1U: action = BM83_MUSIC_PLAY; break;
        case 2U: action = BM83_MUSIC_NEXT; break;
        case 3U: action = BM83_MUSIC_PAUSE; break;
        case 4U: action = BM83_MUSIC_TOGGLE; break;
        case 5U: action = BM83_MUSIC_PREVIOUS; break;
        default:
            g_bench_bm83_play_result = DEV_EINVAL;
            g_bench_bm83_play_request = 0U;
            action = BM83_MUSIC_PLAY;
            return;
        }
        dev_status_t st = kiku_app_bluetooth_music_action((kiku_app_t *)app, action);
        g_bench_bm83_play_result = st;
        if (st != DEV_EBUSY) g_bench_bm83_play_request = 0U;
    }
    if (g_bench_bm83_capabilities_request != 0U) {
        dev_status_t st = bm83_request_event_capabilities(bt, 0U);
        g_bench_bm83_capabilities_result = st;
        if (st != DEV_EBUSY) g_bench_bm83_capabilities_request = 0U;
    }
    if (g_bench_bm83_metadata_v206_request != 0U) {
        dev_status_t st = bm83_request_current_metadata_v206(bt, 0U);
        g_bench_bm83_metadata_v206_result = st;
        if (st != DEV_EBUSY) g_bench_bm83_metadata_v206_request = 0U;
    }
    if (g_bench_bm83_eeprom_read_request != 0U) {
        uint32_t len = g_bench_bm83_eeprom_read_length;
        if (len == 0U || len > 16U || g_bench_bm83_eeprom_read_offset > 0xFFFFU) {
            g_bench_bm83_eeprom_read_result = DEV_EINVAL;
            g_bench_bm83_eeprom_read_request = 0U;
        } else {
            dev_status_t st = bm83_read_eeprom(bt,
                                               (uint16_t)g_bench_bm83_eeprom_read_offset,
                                               (uint8_t)len);
            g_bench_bm83_eeprom_read_result = st;
            if (st != DEV_EBUSY) g_bench_bm83_eeprom_read_request = 0U;
        }
    }
    if (g_bench_bm83_link_status_request != 0U) {
        dev_status_t st = bm83_read_link_status(bt);
        g_bench_bm83_link_status_result = st;
        if (st != DEV_EBUSY) g_bench_bm83_link_status_request = 0U;
    }
    if (g_bench_bm83_link_back_request != 0U) {
        dev_status_t st = (g_bench_bm83_link_back_request == 2U) ?
                          bm83_link_back_last_a2dp(bt) :
                          bm83_link_back_last_device(bt);
        g_bench_bm83_link_back_result = st;
        if (st != DEV_EBUSY) g_bench_bm83_link_back_request = 0U;
    }
    if (g_bench_bm83_tone_request != 0U) {
        uint32_t requested = g_bench_bm83_tone_request;
        if (requested > 0xFFU) {
            g_bench_bm83_tone_result = DEV_EINVAL;
            g_bench_bm83_tone_request = 0U;
        } else {
            dev_status_t st = bm83_generate_tone(bt, (uint8_t)requested);
            g_bench_bm83_tone_result = st;
            if (st != DEV_EBUSY) g_bench_bm83_tone_request = 0U;
        }
    }
}

static void platform_service_app_bench(const kiku_app_t *app_const)
{
    if (app_const == NULL) return;
    kiku_app_t *app = (kiku_app_t *)app_const;

    if (g_bench_speaker_direct_request != 0U) {
        uint32_t requested_ms = g_bench_speaker_direct_request;
        g_bench_speaker_direct_request = 0U;
        if (requested_ms > 10000U) requested_ms = 10000U;
        if (requested_ms < 250U) requested_ms = 250U;

        /* Force the amplifier out of shutdown for this isolated test. The tone
         * routine itself bypasses codec routing and production audio DMA. */
        HAL_GPIO_WritePin(AMP_SD_MODE_GPIO_Port, AMP_SD_MODE_Pin, GPIO_PIN_SET);
        g_bench_speaker_direct_result = audio_stream_bench_direct_speaker_tone(requested_ms);

        /* Restore the user's normal output policy after the bench tone. */
        (void)audio_router_set_output(&app->audio, app->audio.output);
    }

    /* Output request is encoded as output+1 so zero can remain the idle value:
     * 1=AUTO, 2=HEADPHONES, 3=SPEAKER, 4=BOTH. */
    if (g_bench_app_output_request != 0U) {
        uint32_t requested = g_bench_app_output_request - 1U;
        if (requested > (uint32_t)AUDIO_OUTPUT_BOTH) {
            g_bench_app_output_result = DEV_EINVAL;
        } else {
            g_bench_app_output_result = audio_router_set_output(&app->audio,
                                                                 (audio_output_t)requested);
            if (g_bench_app_output_result == DEV_OK) app->ui.output = (audio_output_t)requested;
        }
        g_bench_app_output_request = 0U;
    }

    if (g_bench_local_play_request != 0U) {
        char path[APP_PATH_MAX];
        dev_status_t st = app->deps.local_track_path == NULL ? DEV_ENOTSUP :
                          app->deps.local_track_path(app->deps.local_track_ctx, 0,
                                                     path, sizeof(path));
        if (st == DEV_OK) st = kiku_app_open_local(app, path);
        g_bench_local_play_result = st;
        g_bench_local_play_request = 0U;
    }

    if (g_bench_app_source_request == 0U) return;
    uint32_t requested = g_bench_app_source_request;
    if (requested < (uint32_t)AUDIO_SOURCE_LOCAL ||
        requested > (uint32_t)AUDIO_SOURCE_BLUETOOTH) {
        g_bench_app_source_result = DEV_EINVAL;
        g_bench_app_source_request = 0U;
        return;
    }
    dev_status_t st = kiku_app_select_source(app, (audio_source_t)requested);
    g_bench_app_source_result = st;
    if (st != DEV_EBUSY) g_bench_app_source_request = 0U;
}

void platform_service_stream_audio(const kiku_app_t *app)
{
    if (app == NULL || !app->initialised) return;
    audio_stream_service();
    platform_service_storage((kiku_app_t *)app);
    platform_update_diagnostics(app);
    platform_service_speaker_test(app);
    platform_service_bm83_bench(app);
    platform_service_app_bench(app);
}

void platform_watchdog_refresh(void)
{
    (void)HAL_IWDG_Refresh(&hiwdg);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == NULL || huart->Instance != USART3) return;
    uint16_t head = s_bm83_rx_head;
    uint16_t next = (uint16_t)((head + 1U) % BM83_UART_RX_RING_SIZE);
    if (next == s_bm83_rx_tail) {
        /* Single-producer/single-consumer invariant: the ISR owns head and the
         * foreground owns tail. Never advance tail here; doing so races the
         * foreground read-modify-write. Drop the newest byte on the exceptional
         * full condition and let BM83 framing resynchronise at the next 0xAA. */
        ++g_platform_diag.bm83_uart_rx_overrun_count;
        (void)HAL_UART_Receive_IT(&huart3, &s_bm83_irq_byte, 1U);
        return;
    }
    s_bm83_rx_ring[head] = s_bm83_irq_byte;
    s_bm83_rx_head = next;
    if (HAL_UART_Receive_IT(&huart3, &s_bm83_irq_byte, 1U) != HAL_OK) {
        /* HAL_UART_ErrorCallback will also attempt to re-arm after an error. */
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == NULL || huart->Instance != USART3) return;
    ++g_platform_diag.bm83_uart_error_count;
    g_platform_diag.bm83_uart_last_error = huart->ErrorCode;
    (void)HAL_UART_Receive_IT(&huart3, &s_bm83_irq_byte, 1U);
}
