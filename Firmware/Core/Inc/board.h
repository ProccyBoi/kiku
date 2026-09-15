#ifndef KIKU_BOARD_H
#define KIKU_BOARD_H

#include "stm32h5xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Verified MCU clock source. */
#define BOARD_HSE_HZ                    8000000UL
#define BOARD_SYSCLK_HZ               160000000UL
#define BOARD_AUDIO_SAMPLE_RATE_HZ        48000UL

/* LCD: HS20HS072RX/ST7789 through FPC1. */
#define LCD_SCK_Pin                    GPIO_PIN_2
#define LCD_SCK_GPIO_Port              GPIOE
#define LCD_DC_Pin                     GPIO_PIN_3
#define LCD_DC_GPIO_Port               GPIOE
#define LCD_CS_Pin                     GPIO_PIN_4
#define LCD_CS_GPIO_Port               GPIOE
#define LCD_RST_Pin                    GPIO_PIN_5
#define LCD_RST_GPIO_Port              GPIOE
#define LCD_MOSI_Pin                   GPIO_PIN_6
#define LCD_MOSI_GPIO_Port             GPIOE

/* User controls. PUSH_1 and HP_DET both occupy EXTI line 0, so these
 * application-facing inputs are intentionally polled rather than EXTI-driven. */
#define PUSH_1_Pin                     GPIO_PIN_0
#define PUSH_1_GPIO_Port               GPIOC
#define PUSH_2_Pin                     GPIO_PIN_1
#define PUSH_2_GPIO_Port               GPIOC
#define ENC_A_Pin                      GPIO_PIN_0
#define ENC_A_GPIO_Port                GPIOA
#define ENC_B_Pin                      GPIO_PIN_1
#define ENC_B_GPIO_Port                GPIOA
#define ENC_PUSH_Pin                   GPIO_PIN_2
#define ENC_PUSH_GPIO_Port             GPIOA
#define HP_DET_Pin                     GPIO_PIN_0
#define HP_DET_GPIO_Port               GPIOD

/* I2S1: MCU master to TLV320AIC3104 and MAX98357A, with codec return data. */
#define AUDIO_WCLK_Pin                 GPIO_PIN_4
#define AUDIO_WCLK_GPIO_Port           GPIOA
#define AUDIO_BCLK_Pin                 GPIO_PIN_5
#define AUDIO_BCLK_GPIO_Port           GPIOA
#define AUDIO_DOUT_Pin                 GPIO_PIN_6
#define AUDIO_DOUT_GPIO_Port           GPIOA
#define AUDIO_DIN_Pin                  GPIO_PIN_7
#define AUDIO_DIN_GPIO_Port            GPIOA
#define AUDIO_MCLK_Pin                 GPIO_PIN_4
#define AUDIO_MCLK_GPIO_Port           GPIOC

/* Backlight and audio control. */
#define LCD_BL_PWM_Pin                 GPIO_PIN_9
#define LCD_BL_PWM_GPIO_Port           GPIOE
#define BQ_INT_Pin                     GPIO_PIN_10
#define BQ_INT_GPIO_Port               GPIOE
#define MAX_INT_Pin                    GPIO_PIN_11
#define MAX_INT_GPIO_Port              GPIOE
#define TLV_RST_Pin                    GPIO_PIN_13
#define TLV_RST_GPIO_Port              GPIOE
#define AMP_SD_MODE_Pin                GPIO_PIN_14
#define AMP_SD_MODE_GPIO_Port          GPIOE

/* BM83 digital audio and host interface. KiCad net /CT1 is BM83 DT1. */
#define BM83_RFS_Pin                   GPIO_PIN_12
#define BM83_RFS_GPIO_Port             GPIOB
#define BM83_SCLK_Pin                  GPIO_PIN_13
#define BM83_SCLK_GPIO_Port            GPIOB
#define BM83_DT_Pin                    GPIO_PIN_14
#define BM83_DT_GPIO_Port              GPIOB
#define BM83_UART_TX_Pin               GPIO_PIN_8
#define BM83_UART_TX_GPIO_Port         GPIOD
#define BM83_UART_RX_Pin               GPIO_PIN_9
#define BM83_UART_RX_GPIO_Port         GPIOD
#define BM83_RST_Pin                   GPIO_PIN_10
#define BM83_RST_GPIO_Port             GPIOD
#define BM83_MFB_Pin                   GPIO_PIN_11
#define BM83_MFB_GPIO_Port             GPIOD
#define BM83_TX_IND_Pin                GPIO_PIN_12
#define BM83_TX_IND_GPIO_Port          GPIOD

/* SI4705 FM tuner. */
#define SI4705_RST_Pin                 GPIO_PIN_13
#define SI4705_RST_GPIO_Port           GPIOD
#define SI4705_INT_Pin                 GPIO_PIN_14
#define SI4705_INT_GPIO_Port           GPIOD
#define FM_SCL_Pin                     GPIO_PIN_6
#define FM_SCL_GPIO_Port               GPIOD
#define FM_SDA_Pin                     GPIO_PIN_7
#define FM_SDA_GPIO_Port               GPIOD

/* microSD, native 4-bit SDMMC1. */
#define SD_D0_Pin                      GPIO_PIN_8
#define SD_D0_GPIO_Port                GPIOC
#define SD_D1_Pin                      GPIO_PIN_9
#define SD_D1_GPIO_Port                GPIOC
#define SD_D2_Pin                      GPIO_PIN_10
#define SD_D2_GPIO_Port                GPIOC
#define SD_D3_Pin                      GPIO_PIN_11
#define SD_D3_GPIO_Port                GPIOC
#define SD_CLK_Pin                     GPIO_PIN_12
#define SD_CLK_GPIO_Port               GPIOC
#define SD_CMD_Pin                     GPIO_PIN_2
#define SD_CMD_GPIO_Port               GPIOD

/* Shared I2C1 bus: BQ25895 + TLV320AIC3104 + MAX17048. */
#define SYS_I2C_SCL_Pin                GPIO_PIN_6
#define SYS_I2C_SCL_GPIO_Port          GPIOB
#define SYS_I2C_SDA_Pin                GPIO_PIN_7
#define SYS_I2C_SDA_GPIO_Port          GPIOB

extern I2C_HandleTypeDef hi2c1;
extern I2C_HandleTypeDef hi2c3;
extern SPI_HandleTypeDef hspi4;
extern I2S_HandleTypeDef hi2s1;
extern I2S_HandleTypeDef hi2s2;
extern UART_HandleTypeDef huart3;
extern SD_HandleTypeDef hsd1;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;
extern IWDG_HandleTypeDef hiwdg;

extern DMA_HandleTypeDef hdma_spi1_rx;
extern DMA_HandleTypeDef hdma_spi1_tx;
extern DMA_HandleTypeDef hdma_spi2_rx;
extern DMA_HandleTypeDef hdma_spi4_tx;
extern DMA_HandleTypeDef hdma_usart3_rx;
extern DMA_HandleTypeDef hdma_usart3_tx;

void SystemClock_Config(void);
void Board_PeripheralClock_Config(void);
void MX_GPIO_Init(void);
void MX_GPDMA1_Init(void);
void MX_I2C1_Init(void);
void MX_I2C3_Init(void);
void MX_SPI4_Init(void);
void MX_I2S1_Init(void);
void MX_I2S2_Init(void);
void MX_USART3_UART_Init(void);
void MX_SDMMC1_SD_Init(void);
void MX_TIM1_Init(void);
void MX_TIM2_Init(void);
void MX_IWDG_Init(void);

void Board_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* KIKU_BOARD_H */
