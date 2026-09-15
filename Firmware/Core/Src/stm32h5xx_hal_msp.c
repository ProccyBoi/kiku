#include "main.h"

void HAL_MspInit(void)
{
  __HAL_RCC_SBS_CLK_ENABLE();

  HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
}

void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
  GPIO_InitTypeDef gpio = {0};

  if (hi2c->Instance == I2C1)
  {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    gpio.Pin = SYS_I2C_SCL_Pin | SYS_I2C_SDA_Pin;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gpio);
  }
  else if (hi2c->Instance == I2C3)
  {
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_I2C3_CLK_ENABLE();

    gpio.Pin = FM_SCL_Pin | FM_SDA_Pin;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF4_I2C3;
    HAL_GPIO_Init(GPIOD, &gpio);
  }
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef *hi2c)
{
  if (hi2c->Instance == I2C1)
  {
    __HAL_RCC_I2C1_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOB, SYS_I2C_SCL_Pin | SYS_I2C_SDA_Pin);
  }
  else if (hi2c->Instance == I2C3)
  {
    __HAL_RCC_I2C3_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOD, FM_SCL_Pin | FM_SDA_Pin);
  }
}

void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
  GPIO_InitTypeDef gpio = {0};

  if (hspi->Instance != SPI4)
  {
    return;
  }

  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_SPI4_CLK_ENABLE();

  /* PE4 LCD_CS stays a software-controlled GPIO because SPI4 uses NSS_SOFT. */
  gpio.Pin = LCD_SCK_Pin | LCD_MOSI_Pin;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF5_SPI4;
  HAL_GPIO_Init(GPIOE, &gpio);

  __HAL_LINKDMA(hspi, hdmatx, hdma_spi4_tx);
}

void HAL_SPI_MspDeInit(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance != SPI4)
  {
    return;
  }

  __HAL_RCC_SPI4_CLK_DISABLE();
  HAL_GPIO_DeInit(GPIOE, LCD_SCK_Pin | LCD_MOSI_Pin);
  HAL_DMA_DeInit(hspi->hdmatx);
}

void HAL_I2S_MspInit(I2S_HandleTypeDef *hi2s)
{
  GPIO_InitTypeDef gpio = {0};

  if (hi2s->Instance == SPI1)
  {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();

    gpio.Pin = AUDIO_WCLK_Pin | AUDIO_BCLK_Pin | AUDIO_DOUT_Pin | AUDIO_DIN_Pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = AUDIO_MCLK_Pin;
    HAL_GPIO_Init(GPIOC, &gpio);

    __HAL_LINKDMA(hi2s, hdmarx, hdma_spi1_rx);
    __HAL_LINKDMA(hi2s, hdmatx, hdma_spi1_tx);
  }
  else if (hi2s->Instance == SPI2)
  {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();

    gpio.Pin = BM83_RFS_Pin | BM83_SCLK_Pin | BM83_DT_Pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOB, &gpio);

    __HAL_LINKDMA(hi2s, hdmarx, hdma_spi2_rx);
  }
}

void HAL_I2S_MspDeInit(I2S_HandleTypeDef *hi2s)
{
  if (hi2s->Instance == SPI1)
  {
    __HAL_RCC_SPI1_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, AUDIO_WCLK_Pin | AUDIO_BCLK_Pin | AUDIO_DOUT_Pin | AUDIO_DIN_Pin);
    HAL_GPIO_DeInit(GPIOC, AUDIO_MCLK_Pin);
    HAL_DMA_DeInit(hi2s->hdmarx);
    HAL_DMA_DeInit(hi2s->hdmatx);
  }
  else if (hi2s->Instance == SPI2)
  {
    __HAL_RCC_SPI2_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOB, BM83_RFS_Pin | BM83_SCLK_Pin | BM83_DT_Pin);
    HAL_DMA_DeInit(hi2s->hdmarx);
  }
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
  GPIO_InitTypeDef gpio = {0};

  if (huart->Instance != USART3)
  {
    return;
  }

  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_USART3_CLK_ENABLE();

  gpio.Pin = BM83_UART_TX_Pin | BM83_UART_RX_Pin;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Alternate = GPIO_AF7_USART3;
  HAL_GPIO_Init(GPIOD, &gpio);

  __HAL_LINKDMA(huart, hdmarx, hdma_usart3_rx);
  __HAL_LINKDMA(huart, hdmatx, hdma_usart3_tx);

  /* RX callbacks only write the byte ring and use no FreeRTOS APIs. Keep
   * them above audio DMA and the kernel's BASEPRI critical sections. */
  HAL_NVIC_SetPriority(USART3_IRQn, 4U, 0U);
  HAL_NVIC_EnableIRQ(USART3_IRQn);
}

void HAL_UART_MspDeInit(UART_HandleTypeDef *huart)
{
  if (huart->Instance != USART3)
  {
    return;
  }

  HAL_NVIC_DisableIRQ(USART3_IRQn);
  __HAL_RCC_USART3_CLK_DISABLE();
  HAL_GPIO_DeInit(GPIOD, BM83_UART_TX_Pin | BM83_UART_RX_Pin);
  HAL_DMA_DeInit(huart->hdmarx);
  HAL_DMA_DeInit(huart->hdmatx);
}

void HAL_SD_MspInit(SD_HandleTypeDef *hsd)
{
  GPIO_InitTypeDef gpio = {0};

  if (hsd->Instance != SDMMC1)
  {
    return;
  }

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_SDMMC1_CLK_ENABLE();

  gpio.Pin = SD_D0_Pin | SD_D1_Pin | SD_D2_Pin | SD_D3_Pin | SD_CLK_Pin;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF12_SDMMC1;
  HAL_GPIO_Init(GPIOC, &gpio);

  gpio.Pin = SD_CMD_Pin;
  HAL_GPIO_Init(GPIOD, &gpio);

  HAL_NVIC_SetPriority(SDMMC1_IRQn, 6U, 0U);
  HAL_NVIC_EnableIRQ(SDMMC1_IRQn);
}

void HAL_SD_MspDeInit(SD_HandleTypeDef *hsd)
{
  if (hsd->Instance != SDMMC1)
  {
    return;
  }

  HAL_NVIC_DisableIRQ(SDMMC1_IRQn);
  __HAL_RCC_SDMMC1_CLK_DISABLE();
  HAL_GPIO_DeInit(GPIOC, SD_D0_Pin | SD_D1_Pin | SD_D2_Pin | SD_D3_Pin | SD_CLK_Pin);
  HAL_GPIO_DeInit(GPIOD, SD_CMD_Pin);
}

void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *htim)
{
  GPIO_InitTypeDef gpio = {0};

  if (htim->Instance != TIM1)
  {
    return;
  }

  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_TIM1_CLK_ENABLE();

  gpio.Pin = LCD_BL_PWM_Pin;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Alternate = GPIO_AF1_TIM1;
  HAL_GPIO_Init(GPIOE, &gpio);
}

void HAL_TIM_PWM_MspDeInit(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM1)
  {
    __HAL_RCC_TIM1_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOE, LCD_BL_PWM_Pin);
  }
}

void HAL_TIM_Encoder_MspInit(TIM_HandleTypeDef *htim)
{
  GPIO_InitTypeDef gpio = {0};

  if (htim->Instance != TIM2)
  {
    return;
  }

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_TIM2_CLK_ENABLE();

  gpio.Pin = ENC_A_Pin | ENC_B_Pin;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = GPIO_AF1_TIM2;
  HAL_GPIO_Init(GPIOA, &gpio);
}

void HAL_TIM_Encoder_MspDeInit(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM2)
  {
    __HAL_RCC_TIM2_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, ENC_A_Pin | ENC_B_Pin);
  }
}

void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM6)
  {
    __HAL_RCC_TIM6_CLK_ENABLE();
  }
}

void HAL_TIM_Base_MspDeInit(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM6)
  {
    __HAL_RCC_TIM6_CLK_DISABLE();
  }
}
