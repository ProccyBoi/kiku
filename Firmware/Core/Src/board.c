#include "main.h"

I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c3;
SPI_HandleTypeDef hspi4;
I2S_HandleTypeDef hi2s1;
I2S_HandleTypeDef hi2s2;
UART_HandleTypeDef huart3;
SD_HandleTypeDef hsd1;
TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
IWDG_HandleTypeDef hiwdg;

DMA_HandleTypeDef hdma_spi1_rx;
DMA_HandleTypeDef hdma_spi1_tx;
DMA_HandleTypeDef hdma_spi2_rx;
DMA_HandleTypeDef hdma_spi4_tx;
DMA_HandleTypeDef hdma_usart3_rx;
DMA_HandleTypeDef hdma_usart3_tx;

/* HSI64 kernel, approximately 100 kHz Standard-mode I2C.  The board has
 * external 4.7 kOhm pull-ups on both I2C buses. */
#define BOARD_I2C_TIMING_64MHZ_100KHZ  0x30424757UL

static void Board_DMAChannelInit(DMA_HandleTypeDef *hdma,
                                 DMA_Channel_TypeDef *instance,
                                 uint32_t request,
                                 uint32_t direction,
                                 uint32_t src_inc,
                                 uint32_t dest_inc,
                                 uint32_t src_width,
                                 uint32_t dest_width,
                                 uint32_t priority)
{
  hdma->Instance = instance;
  hdma->Init.Request = request;
  hdma->Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
  hdma->Init.Direction = direction;
  hdma->Init.SrcInc = src_inc;
  hdma->Init.DestInc = dest_inc;
  hdma->Init.SrcDataWidth = src_width;
  hdma->Init.DestDataWidth = dest_width;
  hdma->Init.Priority = priority;
  hdma->Init.SrcBurstLength = 1U;
  hdma->Init.DestBurstLength = 1U;
  hdma->Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
  hdma->Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
  hdma->Init.Mode = DMA_NORMAL;

  if (HAL_DMA_Init(hdma) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_DMA_ConfigChannelAttributes(hdma, DMA_CHANNEL_NPRIV) != HAL_OK)
  {
    Error_Handler();
  }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};

  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE0) != HAL_OK)
  {
    Error_Handler();
  }

  /* 8 MHz HSE / 8 = 1 MHz, x320 = 320 MHz VCO, /2 = 160 MHz SYSCLK.
   * LSI is enabled here because IWDG starts from LSI. */
  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI |
                       RCC_OSCILLATORTYPE_LSI;
  osc.HSEState = RCC_HSE_ON;
  osc.HSIState = RCC_HSI_ON;
  osc.HSIDiv = RCC_HSI_DIV1;
  osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  osc.LSIState = RCC_LSI_ON;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLM = 8U;
  osc.PLL.PLLN = 320U;
  osc.PLL.PLLP = 2U;
  osc.PLL.PLLQ = 8U; /* PLL1Q = 40 MHz for SDMMC1. */
  osc.PLL.PLLR = 8U;
  osc.PLL.PLLRGE = RCC_PLL1_VCIRANGE_0;
  osc.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
  osc.PLL.PLLFRACN = 0U;

  if (HAL_RCC_OscConfig(&osc) != HAL_OK)
  {
    Error_Handler();
  }

  clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                  RCC_CLOCKTYPE_PCLK3;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV1;
  clk.APB2CLKDivider = RCC_HCLK_DIV1;
  clk.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

void Board_PeripheralClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef periph = {0};

  periph.PeriphClockSelection = RCC_PERIPHCLK_I2C1 | RCC_PERIPHCLK_I2C3 |
                                RCC_PERIPHCLK_SPI1 | RCC_PERIPHCLK_SPI2 |
                                RCC_PERIPHCLK_SPI4 | RCC_PERIPHCLK_USART3 |
                                RCC_PERIPHCLK_SDMMC1;

  /* Dedicated low-jitter audio kernel.  With the verified 8 MHz HSE:
   * 8 MHz / 8 * (393 + 1769/8192) / 8 = 49.1519928 MHz (~-0.15 ppm).
   * HAL I2S then derives the requested 48 kHz audio clocks from PLL2P. */
  periph.PLL2.PLL2Source = RCC_PLL2_SOURCE_HSE;
  periph.PLL2.PLL2M = 8U;
  periph.PLL2.PLL2N = 393U;
  periph.PLL2.PLL2P = 8U;
  periph.PLL2.PLL2Q = 8U;
  periph.PLL2.PLL2R = 8U;
  periph.PLL2.PLL2RGE = RCC_PLL2_VCIRANGE_0;
  periph.PLL2.PLL2VCOSEL = RCC_PLL2_VCORANGE_WIDE;
  periph.PLL2.PLL2FRACN = 1769U;
  periph.PLL2.PLL2ClockOut = RCC_PLL2_DIVP;

  periph.I2c1ClockSelection = RCC_I2C1CLKSOURCE_HSI;
  periph.I2c3ClockSelection = RCC_I2C3CLKSOURCE_HSI;
  periph.Spi1ClockSelection = RCC_SPI1CLKSOURCE_PLL2P;
  periph.Spi2ClockSelection = RCC_SPI2CLKSOURCE_PLL2P;
  periph.Spi4ClockSelection = RCC_SPI4CLKSOURCE_PCLK2;
  periph.Usart3ClockSelection = RCC_USART3CLKSOURCE_PCLK1;
  periph.Sdmmc1ClockSelection = RCC_SDMMC1CLKSOURCE_PLL1Q;

  if (HAL_RCCEx_PeriphCLKConfig(&periph) != HAL_OK)
  {
    Error_Handler();
  }
}

void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();

  /* Safe power-on states: peripherals held reset/muted; LCD deselected. */
  HAL_GPIO_WritePin(GPIOE, LCD_DC_Pin | LCD_RST_Pin | TLV_RST_Pin | AMP_SD_MODE_Pin,
                    GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOE, LCD_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOD, BM83_RST_Pin | BM83_MFB_Pin | SI4705_RST_Pin, GPIO_PIN_RESET);

  gpio.Pin = LCD_DC_Pin | LCD_CS_Pin | LCD_RST_Pin | TLV_RST_Pin | AMP_SD_MODE_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &gpio);

  gpio.Pin = BM83_RST_Pin | BM83_MFB_Pin | SI4705_RST_Pin;
  HAL_GPIO_Init(GPIOD, &gpio);

  gpio.Pin = PUSH_1_Pin | PUSH_2_Pin;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOC, &gpio);

  gpio.Pin = ENC_PUSH_Pin;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &gpio);

  gpio.Pin = HP_DET_Pin;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOD, &gpio);

  /* BM83 UART_TX_IND is a status output.  Use both edges so the host can
   * observe asserted and deasserted states without assuming a polarity that
   * depends on the BM83 firmware/configuration image. */
  gpio.Pin = BM83_TX_IND_Pin;
  gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOD, &gpio);

  /* BQ25895 INT and MAX17048 ALRT already have external 10 kOhm pull-ups. */
  gpio.Pin = BQ_INT_Pin | MAX_INT_Pin;
  gpio.Mode = GPIO_MODE_IT_FALLING;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOE, &gpio);

  /* GPO2 is also sampled at reset: it must be low to select the 2-wire
   * control interface. A pull-up overrides the tuner's weak internal
   * pull-down and prevents it from acknowledging I2C commands. */
  gpio.Pin = SI4705_INT_Pin;
  gpio.Mode = GPIO_MODE_IT_FALLING;
  gpio.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOD, &gpio);

  HAL_NVIC_SetPriority(EXTI10_IRQn, 6U, 0U);
  HAL_NVIC_EnableIRQ(EXTI10_IRQn);
  HAL_NVIC_SetPriority(EXTI11_IRQn, 6U, 0U);
  HAL_NVIC_EnableIRQ(EXTI11_IRQn);
  HAL_NVIC_SetPriority(EXTI12_IRQn, 6U, 0U);
  HAL_NVIC_EnableIRQ(EXTI12_IRQn);
  HAL_NVIC_SetPriority(EXTI14_IRQn, 6U, 0U);
  HAL_NVIC_EnableIRQ(EXTI14_IRQn);
}

void MX_GPDMA1_Init(void)
{
  __HAL_RCC_GPDMA1_CLK_ENABLE();

  /* Direct-mode GPDMA channels are deliberately normal mode. Continuous
   * audio ring buffers on STM32H5 require GPDMA linked-list/circular setup;
   * the higher audio layer may replace/reconfigure these channels accordingly. */
  Board_DMAChannelInit(&hdma_spi1_rx, GPDMA1_Channel0, GPDMA1_REQUEST_SPI1_RX,
                       DMA_PERIPH_TO_MEMORY, DMA_SINC_FIXED, DMA_DINC_INCREMENTED,
                       DMA_SRC_DATAWIDTH_HALFWORD, DMA_DEST_DATAWIDTH_HALFWORD,
                       DMA_HIGH_PRIORITY);
  Board_DMAChannelInit(&hdma_spi1_tx, GPDMA1_Channel1, GPDMA1_REQUEST_SPI1_TX,
                       DMA_MEMORY_TO_PERIPH, DMA_SINC_INCREMENTED, DMA_DINC_FIXED,
                       DMA_SRC_DATAWIDTH_HALFWORD, DMA_DEST_DATAWIDTH_HALFWORD,
                       DMA_HIGH_PRIORITY);
  Board_DMAChannelInit(&hdma_spi2_rx, GPDMA1_Channel2, GPDMA1_REQUEST_SPI2_RX,
                       DMA_PERIPH_TO_MEMORY, DMA_SINC_FIXED, DMA_DINC_INCREMENTED,
                       DMA_SRC_DATAWIDTH_HALFWORD, DMA_DEST_DATAWIDTH_HALFWORD,
                       DMA_HIGH_PRIORITY);
  Board_DMAChannelInit(&hdma_spi4_tx, GPDMA1_Channel3, GPDMA1_REQUEST_SPI4_TX,
                       DMA_MEMORY_TO_PERIPH, DMA_SINC_INCREMENTED, DMA_DINC_FIXED,
                       DMA_SRC_DATAWIDTH_BYTE, DMA_DEST_DATAWIDTH_BYTE,
                       DMA_LOW_PRIORITY_HIGH_WEIGHT);
  Board_DMAChannelInit(&hdma_usart3_rx, GPDMA1_Channel4, GPDMA1_REQUEST_USART3_RX,
                       DMA_PERIPH_TO_MEMORY, DMA_SINC_FIXED, DMA_DINC_INCREMENTED,
                       DMA_SRC_DATAWIDTH_BYTE, DMA_DEST_DATAWIDTH_BYTE,
                       DMA_LOW_PRIORITY_HIGH_WEIGHT);
  Board_DMAChannelInit(&hdma_usart3_tx, GPDMA1_Channel5, GPDMA1_REQUEST_USART3_TX,
                       DMA_MEMORY_TO_PERIPH, DMA_SINC_INCREMENTED, DMA_DINC_FIXED,
                       DMA_SRC_DATAWIDTH_BYTE, DMA_DEST_DATAWIDTH_BYTE,
                       DMA_LOW_PRIORITY_HIGH_WEIGHT);

  HAL_NVIC_SetPriority(GPDMA1_Channel0_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(GPDMA1_Channel0_IRQn);
  HAL_NVIC_SetPriority(GPDMA1_Channel1_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(GPDMA1_Channel1_IRQn);
  HAL_NVIC_SetPriority(GPDMA1_Channel2_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(GPDMA1_Channel2_IRQn);
  HAL_NVIC_SetPriority(GPDMA1_Channel3_IRQn, 7U, 0U);
  HAL_NVIC_EnableIRQ(GPDMA1_Channel3_IRQn);
  HAL_NVIC_SetPriority(GPDMA1_Channel4_IRQn, 6U, 0U);
  HAL_NVIC_EnableIRQ(GPDMA1_Channel4_IRQn);
  HAL_NVIC_SetPriority(GPDMA1_Channel5_IRQn, 6U, 0U);
  HAL_NVIC_EnableIRQ(GPDMA1_Channel5_IRQn);
}

void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = BOARD_I2C_TIMING_64MHZ_100KHZ;
  hi2c1.Init.OwnAddress1 = 0U;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0U;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0U) != HAL_OK)
  {
    Error_Handler();
  }
}

void MX_I2C3_Init(void)
{
  hi2c3.Instance = I2C3;
  hi2c3.Init.Timing = BOARD_I2C_TIMING_64MHZ_100KHZ;
  hi2c3.Init.OwnAddress1 = 0U;
  hi2c3.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c3.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c3.Init.OwnAddress2 = 0U;
  hi2c3.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c3.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c3.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c3, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c3, 0U) != HAL_OK)
  {
    Error_Handler();
  }
}

void MX_SPI4_Init(void)
{
  hspi4.Instance = SPI4;
  hspi4.Init.Mode = SPI_MODE_MASTER;
  hspi4.Init.Direction = SPI_DIRECTION_2LINES_TXONLY;
  hspi4.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi4.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi4.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi4.Init.NSS = SPI_NSS_SOFT;
  hspi4.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8; /* 20 MHz from PCLK2. */
  hspi4.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi4.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi4.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi4.Init.CRCPolynomial = 0x7U;
  hspi4.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi4.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  hspi4.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  hspi4.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  hspi4.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi4.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi4.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi4.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi4.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  hspi4.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;
  hspi4.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  hspi4.Init.ReadyMasterManagement = SPI_RDY_MASTER_MANAGEMENT_INTERNALLY;
  hspi4.Init.ReadyPolarity = SPI_RDY_POLARITY_HIGH;
  if (HAL_SPI_Init(&hspi4) != HAL_OK)
  {
    Error_Handler();
  }
}

void MX_I2S1_Init(void)
{
  hi2s1.Instance = SPI1;
  hi2s1.Init.Mode = I2S_MODE_MASTER_FULLDUPLEX;
  hi2s1.Init.Standard = I2S_STANDARD_PHILIPS;
  hi2s1.Init.DataFormat = I2S_DATAFORMAT_16B;
  hi2s1.Init.MCLKOutput = I2S_MCLKOUTPUT_ENABLE;
  hi2s1.Init.AudioFreq = I2S_AUDIOFREQ_48K;
  hi2s1.Init.CPOL = I2S_CPOL_LOW;
  hi2s1.Init.FirstBit = I2S_FIRSTBIT_MSB;
  hi2s1.Init.WSInversion = I2S_WS_INVERSION_DISABLE;
  hi2s1.Init.Data24BitAlignment = I2S_DATA_24BIT_ALIGNMENT_RIGHT;
  hi2s1.Init.MasterKeepIOState = I2S_MASTER_KEEP_IO_STATE_ENABLE;
  if (HAL_I2S_Init(&hi2s1) != HAL_OK)
  {
    Error_Handler();
  }

  /* Keep the native SPI1 data directions. PCB net names are codec-relative:
   * PA7/SPI1_MOSI -> /DIN -> TLV320 DIN and MAX98357 DIN (MCU transmit),
   * PA6/SPI1_MISO <- /DOUT <- TLV320 DOUT (codec ADC return). */
}

void MX_I2S2_Init(void)
{
  hi2s2.Instance = SPI2;
  hi2s2.Init.Mode = I2S_MODE_SLAVE_RX;
  hi2s2.Init.Standard = I2S_STANDARD_PHILIPS;
  /* BM83 EVB/Config Tool external-I2S profile uses 24-bit samples by
   * default. Receive each channel in a 32-bit SPI/I2S slot and reduce to
   * 16-bit only after capture for the TLV/MAX98357 output domain. */
  hi2s2.Init.DataFormat = I2S_DATAFORMAT_24B;
  hi2s2.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
  hi2s2.Init.AudioFreq = I2S_AUDIOFREQ_48K;
  hi2s2.Init.CPOL = I2S_CPOL_LOW;
  hi2s2.Init.FirstBit = I2S_FIRSTBIT_MSB;
  hi2s2.Init.WSInversion = I2S_WS_INVERSION_DISABLE;
  hi2s2.Init.Data24BitAlignment = I2S_DATA_24BIT_ALIGNMENT_RIGHT;
  hi2s2.Init.MasterKeepIOState = I2S_MASTER_KEEP_IO_STATE_DISABLE;
  if (HAL_I2S_Init(&hi2s2) != HAL_OK)
  {
    Error_Handler();
  }

  /* BM83 DT1 is wired to PB14 / SPI2_MISO.  On STM32H523 the dedicated I2S
   * receive data path uses the opposite SPI data input unless IOSWP is set.
   * Without this swap RFS1/SCLK1 clock correctly and RX DMA runs, but RXDR is
   * filled with zeroes even though valid audio is present on PB14. */
  if (HAL_I2S_EnableIOSwap(&hi2s2) != HAL_OK)
  {
    Error_Handler();
  }
}

void MX_USART3_UART_Init(void)
{
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200U;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart3, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart3, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  /* Buffer incoming metadata while audio interrupts are being serviced. */
  if (HAL_UARTEx_EnableFifoMode(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
}

void MX_SDMMC1_SD_Init(void)
{
  hsd1.Instance = SDMMC1;
  hsd1.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
  hsd1.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
  hsd1.Init.BusWide = SDMMC_BUS_WIDE_4B;
  hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
  /* HAL_SD_InitCard temporarily divides to <=400 kHz for enumeration and
   * subsequently caps a legacy card at <=25 MHz. */
  hsd1.Init.ClockDiv = 0U;
  /* Do not call HAL_SD_Init here. A card is optional, and treating an empty
   * socket as a fatal boot failure would disable Bluetooth/FM operation.
   * FatFs disk_initialize() performs the first HAL_SD_Init on demand. */
}

void MX_TIM1_Init(void)
{
  TIM_OC_InitTypeDef pwm = {0};

  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0U;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 7999U; /* 160 MHz / 8000 = 20 kHz. */
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0U;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }

  pwm.OCMode = TIM_OCMODE_PWM1;
  pwm.Pulse = 0U;
  pwm.OCPolarity = TIM_OCPOLARITY_HIGH;
  pwm.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  pwm.OCFastMode = TIM_OCFAST_DISABLE;
  pwm.OCIdleState = TIM_OCIDLESTATE_RESET;
  pwm.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &pwm, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
}

void MX_TIM2_Init(void)
{
  TIM_Encoder_InitTypeDef encoder = {0};
  TIM_MasterConfigTypeDef master = {0};

  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0U;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 0xFFFFFFFFUL;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

  encoder.EncoderMode = TIM_ENCODERMODE_TI12;
  encoder.IC1Polarity = TIM_ICPOLARITY_RISING;
  encoder.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  encoder.IC1Prescaler = TIM_ICPSC_DIV1;
  encoder.IC1Filter = 8U;
  encoder.IC2Polarity = TIM_ICPOLARITY_RISING;
  encoder.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  encoder.IC2Prescaler = TIM_ICPSC_DIV1;
  encoder.IC2Filter = 8U;
  if (HAL_TIM_Encoder_Init(&htim2, &encoder) != HAL_OK)
  {
    Error_Handler();
  }

  master.MasterOutputTrigger = TIM_TRGO_RESET;
  master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &master) != HAL_OK)
  {
    Error_Handler();
  }
}

void MX_IWDG_Init(void)
{
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_64;
  hiwdg.Init.Reload = 2499U; /* nominal 5 s at 32 kHz LSI */
  hiwdg.Init.Window = IWDG_WINDOW_DISABLE;
  hiwdg.Init.EWI = IWDG_EWI_DISABLE;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    Error_Handler();
  }
}

void Board_Init(void)
{
  HAL_Init();
  SystemClock_Config();
  Board_PeripheralClock_Config();

  MX_GPDMA1_Init();
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_I2C3_Init();
  MX_SPI4_Init();
  MX_I2S1_Init();
  MX_I2S2_Init();
  MX_USART3_UART_Init();
  MX_SDMMC1_SD_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();

  if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL) != HAL_OK)
  {
    Error_Handler();
  }

  /* Start watchdog last so lengthy peripheral initialisation cannot trip it. */
  MX_IWDG_Init();
}

__weak void Error_Handler(void)
{
  __disable_irq();
  for (;;)
  {
    /* A strong application Error_Handler may override this fail-stop. */
  }
}
