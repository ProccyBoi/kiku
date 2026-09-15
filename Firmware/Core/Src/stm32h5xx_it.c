#include "main.h"
#include "stm32h5xx_it.h"

void NMI_Handler(void)
{
  Error_Handler();
}

void HardFault_Handler(void)
{
  Error_Handler();
}

void MemManage_Handler(void)
{
  Error_Handler();
}

void BusFault_Handler(void)
{
  Error_Handler();
}

void UsageFault_Handler(void)
{
  Error_Handler();
}

void SecureFault_Handler(void)
{
  Error_Handler();
}

void DebugMon_Handler(void)
{
}

/* SVC_Handler, PendSV_Handler and SysTick_Handler are supplied by the
 * Cortex-M33 FreeRTOS port. The STM32 HAL uses TIM6 as its own timebase. */

void EXTI10_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(BQ_INT_Pin);
}

void EXTI11_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(MAX_INT_Pin);
}

void EXTI12_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(BM83_TX_IND_Pin);
}

void EXTI14_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(SI4705_INT_Pin);
}

void GPDMA1_Channel0_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_spi1_rx);
}

void GPDMA1_Channel1_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_spi1_tx);
}

void GPDMA1_Channel2_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_spi2_rx);
}

void GPDMA1_Channel3_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_spi4_tx);
}

void GPDMA1_Channel4_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_usart3_rx);
}

void GPDMA1_Channel5_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_usart3_tx);
}

void USART3_IRQHandler(void)
{
  HAL_UART_IRQHandler(&huart3);
}

void SDMMC1_IRQHandler(void)
{
  HAL_SD_IRQHandler(&hsd1);
}
