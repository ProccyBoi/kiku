#ifndef STM32H5XX_HAL_CONF_H
#define STM32H5XX_HAL_CONF_H

#ifndef STM32H523xx
#define STM32H523xx
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#define HAL_I2S_MODULE_ENABLED
#define HAL_IWDG_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_SD_MODULE_ENABLED
#define HAL_SPI_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

#ifndef HSE_VALUE
#define HSE_VALUE                 8000000UL
#endif
#ifndef HSE_STARTUP_TIMEOUT
#define HSE_STARTUP_TIMEOUT       100UL
#endif
#ifndef CSI_VALUE
#define CSI_VALUE                 4000000UL
#endif
#ifndef HSI_VALUE
#define HSI_VALUE                 64000000UL
#endif
#ifndef HSI48_VALUE
#define HSI48_VALUE               48000000UL
#endif
#ifndef LSI_VALUE
#define LSI_VALUE                 32000UL
#endif
#ifndef LSI_STARTUP_TIME
#define LSI_STARTUP_TIME          130UL
#endif
#ifndef LSE_VALUE
#define LSE_VALUE                 32768UL
#endif
#ifndef LSE_STARTUP_TIMEOUT
#define LSE_STARTUP_TIMEOUT       5000UL
#endif
#ifndef EXTERNAL_CLOCK_VALUE
#define EXTERNAL_CLOCK_VALUE      12288000UL
#endif

#define VDD_VALUE                 3300UL
#define TICK_INT_PRIORITY         ((1UL << __NVIC_PRIO_BITS) - 1UL)
#define USE_RTOS                  0U
#define PREFETCH_ENABLE           0U

#define USE_HAL_I2C_REGISTER_CALLBACKS  0U
#define USE_HAL_I2S_REGISTER_CALLBACKS  0U
#define USE_HAL_SD_REGISTER_CALLBACKS   0U
#define USE_HAL_SPI_REGISTER_CALLBACKS  0U
#define USE_HAL_TIM_REGISTER_CALLBACKS  0U
#define USE_HAL_UART_REGISTER_CALLBACKS 0U
#define USE_HAL_IWDG_REGISTER_CALLBACKS 0U

#define USE_SD_TRANSCEIVER        0U
#define USE_SDIO_TRANSCEIVER      0U
#define SDIO_MAX_IO_NUMBER        7U
#define USE_SPI_CRC               0U

#ifdef HAL_RCC_MODULE_ENABLED
#include "stm32h5xx_hal_rcc.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
#include "stm32h5xx_hal_gpio.h"
#endif
#ifdef HAL_EXTI_MODULE_ENABLED
#include "stm32h5xx_hal_exti.h"
#endif
#ifdef HAL_DMA_MODULE_ENABLED
#include "stm32h5xx_hal_dma.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
#include "stm32h5xx_hal_cortex.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
#include "stm32h5xx_hal_flash.h"
#endif
#ifdef HAL_PWR_MODULE_ENABLED
#include "stm32h5xx_hal_pwr.h"
#endif
#ifdef HAL_I2C_MODULE_ENABLED
#include "stm32h5xx_hal_i2c.h"
#endif
#ifdef HAL_I2S_MODULE_ENABLED
#include "stm32h5xx_hal_i2s.h"
#endif
#ifdef HAL_IWDG_MODULE_ENABLED
#include "stm32h5xx_hal_iwdg.h"
#endif
#ifdef HAL_SD_MODULE_ENABLED
#include "stm32h5xx_hal_sd.h"
#endif
#ifdef HAL_SPI_MODULE_ENABLED
#include "stm32h5xx_hal_spi.h"
#endif
#ifdef HAL_TIM_MODULE_ENABLED
#include "stm32h5xx_hal_tim.h"
#endif
#ifdef HAL_UART_MODULE_ENABLED
#include "stm32h5xx_hal_uart.h"
#endif

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line);
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
#else
#define assert_param(expr) ((void)0U)
#endif

#ifdef __cplusplus
}
#endif

#endif /* STM32H5XX_HAL_CONF_H */
