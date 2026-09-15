#ifndef BOARD_CONTRACT_H
#define BOARD_CONTRACT_H

#include <stdint.h>

/*
 * Hardware contract extracted from Walkman - Blobject.kicad_pcb.
 * This file records the exact MCU/net mapping but deliberately contains no
 * STM32 HAL objects. Core is expected to bind these signals to generated HAL
 * handles without changing the device/application APIs.
 */

typedef enum {
    BOARD_PORT_A = 0,
    BOARD_PORT_B,
    BOARD_PORT_C,
    BOARD_PORT_D,
    BOARD_PORT_E,
    BOARD_PORT_H
} board_port_t;

typedef struct {
    board_port_t port;
    uint8_t pin;
} board_pin_t;

#define BOARD_PIN(p, n) ((board_pin_t){ (p), (uint8_t)(n) })

/* LCD / HS20HS072RX FPC, ST7789-compatible serial interface. */
#define BOARD_NET_LCD_SCK       BOARD_PIN(BOARD_PORT_E, 2)
#define BOARD_NET_LCD_DC        BOARD_PIN(BOARD_PORT_E, 3)
#define BOARD_NET_LCD_CS        BOARD_PIN(BOARD_PORT_E, 4)
#define BOARD_NET_LCD_RST       BOARD_PIN(BOARD_PORT_E, 5)
#define BOARD_NET_LCD_MOSI      BOARD_PIN(BOARD_PORT_E, 6)
#define BOARD_NET_LCD_BL_PWM    BOARD_PIN(BOARD_PORT_E, 9)  /* /TIM1_CH1_PWM -> Q1 -> FPC K */

/* Front-panel controls. */
#define BOARD_NET_PUSH_1        BOARD_PIN(BOARD_PORT_C, 0)
#define BOARD_NET_PUSH_2        BOARD_PIN(BOARD_PORT_C, 1)
#define BOARD_NET_ENC_A         BOARD_PIN(BOARD_PORT_A, 0)  /* /TIM2_CH1 */
#define BOARD_NET_ENC_B         BOARD_PIN(BOARD_PORT_A, 1)  /* /TIM2_CH2 */
#define BOARD_NET_ENC_PUSH      BOARD_PIN(BOARD_PORT_A, 2)

/* I2S1 shared by TLV320AIC3104 and MAX98357A. */
#define BOARD_NET_I2S1_WS       BOARD_PIN(BOARD_PORT_A, 4)  /* /WCLK */
#define BOARD_NET_I2S1_BCLK     BOARD_PIN(BOARD_PORT_A, 5)  /* /BCLK */
#define BOARD_NET_I2S1_DOUT     BOARD_PIN(BOARD_PORT_A, 6)  /* /DOUT, codec -> MCU */
#define BOARD_NET_I2S1_DIN      BOARD_PIN(BOARD_PORT_A, 7)  /* /DIN, MCU -> codec + amp */
#define BOARD_NET_CODEC_MCLK    BOARD_PIN(BOARD_PORT_C, 4)  /* /MCLK */

/* Power/audio control and interrupts. */
#define BOARD_NET_BQ_INT        BOARD_PIN(BOARD_PORT_E, 10)
#define BOARD_NET_MAX17048_INT  BOARD_PIN(BOARD_PORT_E, 11)
#define BOARD_NET_TLV_RST       BOARD_PIN(BOARD_PORT_E, 13)
#define BOARD_NET_MAX_SD_MODE   BOARD_PIN(BOARD_PORT_E, 14)
#define BOARD_NET_HP_DET        BOARD_PIN(BOARD_PORT_D, 0)

/* BM83 digital audio + host control. */
#define BOARD_NET_BM83_RFS1     BOARD_PIN(BOARD_PORT_B, 12)
#define BOARD_NET_BM83_SCLK1    BOARD_PIN(BOARD_PORT_B, 13)
#define BOARD_NET_BM83_DT1      BOARD_PIN(BOARD_PORT_B, 14) /* /CT1 */
#define BOARD_NET_BM83_UART_TX  BOARD_PIN(BOARD_PORT_D, 8)  /* MCU /UART_TXD */
#define BOARD_NET_BM83_UART_RX  BOARD_PIN(BOARD_PORT_D, 9)  /* MCU /UART_RXD */
#define BOARD_NET_BM83_RST      BOARD_PIN(BOARD_PORT_D, 10)
#define BOARD_NET_BM83_MFB      BOARD_PIN(BOARD_PORT_D, 11) /* /BN_MFB */
#define BOARD_NET_BM83_TX_IND   BOARD_PIN(BOARD_PORT_D, 12)

/* SI4705 dedicated control bus. */
#define BOARD_NET_SI4705_RST    BOARD_PIN(BOARD_PORT_D, 13)
#define BOARD_NET_SI4705_INT    BOARD_PIN(BOARD_PORT_D, 14)
#define BOARD_NET_FM_SCL        BOARD_PIN(BOARD_PORT_D, 6)
#define BOARD_NET_FM_SDA        BOARD_PIN(BOARD_PORT_D, 7)

/* Shared I2C1: BQ25895 + MAX17048 + TLV320AIC3104. */
#define BOARD_NET_I2C1_SCL      BOARD_PIN(BOARD_PORT_B, 6)
#define BOARD_NET_I2C1_SDA      BOARD_PIN(BOARD_PORT_B, 7)

/* SDMMC1 / microSD. */
#define BOARD_NET_SD_D0         BOARD_PIN(BOARD_PORT_C, 8)
#define BOARD_NET_SD_D1         BOARD_PIN(BOARD_PORT_C, 9)
#define BOARD_NET_SD_D2         BOARD_PIN(BOARD_PORT_C, 10)
#define BOARD_NET_SD_D3         BOARD_PIN(BOARD_PORT_C, 11)
#define BOARD_NET_SD_CLK        BOARD_PIN(BOARD_PORT_C, 12)
#define BOARD_NET_SD_CMD        BOARD_PIN(BOARD_PORT_D, 2)

/* Product USB-C D+/D- are routed to BQ25895 through USBLC6, not PA11/PA12. */
#define BOARD_STM32_USB_DEVICE_ROUTED 0

/* Intended peripheral ownership from the exact pin map. */
#define BOARD_PERIPH_LCD_SPI        "SPI4"
#define BOARD_PERIPH_AUDIO_I2S      "SPI1/I2S1"
#define BOARD_PERIPH_BM83_I2S       "SPI2/I2S2"
#define BOARD_PERIPH_BM83_UART      "USART3"
#define BOARD_PERIPH_SHARED_I2C     "I2C1"
#define BOARD_PERIPH_FM_I2C         "I2C3"
#define BOARD_PERIPH_STORAGE        "SDMMC1"
#define BOARD_PERIPH_ENCODER        "TIM2"
#define BOARD_PERIPH_BACKLIGHT      "TIM1_CH1"

#endif
