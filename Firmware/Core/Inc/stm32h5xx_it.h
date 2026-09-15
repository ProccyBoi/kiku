#ifndef STM32H5XX_IT_H
#define STM32H5XX_IT_H

#ifdef __cplusplus
extern "C" {
#endif

void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SecureFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

void EXTI10_IRQHandler(void);
void EXTI11_IRQHandler(void);
void EXTI12_IRQHandler(void);
void EXTI14_IRQHandler(void);

void GPDMA1_Channel0_IRQHandler(void);
void GPDMA1_Channel1_IRQHandler(void);
void GPDMA1_Channel2_IRQHandler(void);
void GPDMA1_Channel3_IRQHandler(void);
void GPDMA1_Channel4_IRQHandler(void);
void GPDMA1_Channel5_IRQHandler(void);

void USART3_IRQHandler(void);
void SDMMC1_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* STM32H5XX_IT_H */
