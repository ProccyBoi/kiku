#include "main.h"

/* Keep the STM32 HAL millisecond timebase separate from the FreeRTOS SysTick.
 * This is essential on this project because HAL timeouts are used both before
 * the scheduler starts and from application tasks after FreeRTOS owns SysTick. */
static TIM_HandleTypeDef htim6_timebase;

HAL_StatusTypeDef HAL_InitTick(uint32_t tick_priority)
{
  RCC_ClkInitTypeDef clk = {0};
  uint32_t latency = 0U;
  uint32_t pclk1;
  uint32_t timer_clock;

  HAL_RCC_GetClockConfig(&clk, &latency);
  pclk1 = HAL_RCC_GetPCLK1Freq();
  timer_clock = pclk1;
  if (clk.APB1CLKDivider != RCC_HCLK_DIV1)
  {
    timer_clock = pclk1 * 2U;
  }
  if (timer_clock < 1000000U || tick_priority >= (1UL << __NVIC_PRIO_BITS))
  {
    return HAL_ERROR;
  }

  htim6_timebase.Instance = TIM6;
  htim6_timebase.Init.Prescaler = (timer_clock / 1000000U) - 1U;
  htim6_timebase.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6_timebase.Init.Period = 1000U - 1U;
  htim6_timebase.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim6_timebase.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

  if (HAL_TIM_Base_Init(&htim6_timebase) != HAL_OK)
  {
    return HAL_ERROR;
  }

  HAL_NVIC_SetPriority(TIM6_IRQn, tick_priority, 0U);
  HAL_NVIC_EnableIRQ(TIM6_IRQn);
  /* RCC reinitialises the timebase after changing the HSI divider or HCLK.
   * Preserve the priority just as the default HAL timebase does; otherwise
   * those calls receive HAL's initial invalid priority and abort startup. */
  uwTickPrio = tick_priority;
  return HAL_TIM_Base_Start_IT(&htim6_timebase);
}

void HAL_SuspendTick(void)
{
  __HAL_TIM_DISABLE_IT(&htim6_timebase, TIM_IT_UPDATE);
}

void HAL_ResumeTick(void)
{
  __HAL_TIM_ENABLE_IT(&htim6_timebase, TIM_IT_UPDATE);
}

void TIM6_IRQHandler(void)
{
  if ((__HAL_TIM_GET_FLAG(&htim6_timebase, TIM_FLAG_UPDATE) != RESET) &&
      (__HAL_TIM_GET_IT_SOURCE(&htim6_timebase, TIM_IT_UPDATE) != RESET))
  {
    __HAL_TIM_CLEAR_IT(&htim6_timebase, TIM_IT_UPDATE);
    HAL_IncTick();
  }
}
