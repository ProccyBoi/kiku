#pragma once

#include <stdint.h>
#include "stm32h523xx.h"

extern uint32_t SystemCoreClock;

#define configUSE_PREEMPTION                         1
#define configUSE_TIME_SLICING                       1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION      0
#define configCPU_CLOCK_HZ                           ((uint32_t)SystemCoreClock)
#define configTICK_RATE_HZ                           ((TickType_t)1000)
#define configMAX_PRIORITIES                         8
#define configMINIMAL_STACK_SIZE                     ((uint16_t)256)
#define configMAX_TASK_NAME_LEN                      20
#define configUSE_16_BIT_TICKS                       0
#define configIDLE_SHOULD_YIELD                      1
#define configUSE_TASK_NOTIFICATIONS                 1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES        2
#define configUSE_MUTEXES                            1
#define configUSE_RECURSIVE_MUTEXES                  1
#define configUSE_COUNTING_SEMAPHORES                1
#define configQUEUE_REGISTRY_SIZE                    8
#define configUSE_QUEUE_SETS                         0
#define configUSE_APPLICATION_TASK_TAG               0
#define configUSE_NEWLIB_REENTRANT                   0
#define configENABLE_BACKWARD_COMPATIBILITY          0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS      0

#define configSUPPORT_STATIC_ALLOCATION              0
#define configSUPPORT_DYNAMIC_ALLOCATION             1
#define configTOTAL_HEAP_SIZE                        (96U * 1024U)
#define configAPPLICATION_ALLOCATED_HEAP             0

#define configUSE_TIMERS                             1
#define configTIMER_TASK_PRIORITY                    2
#define configTIMER_QUEUE_LENGTH                     8
#define configTIMER_TASK_STACK_DEPTH                 384

#define configUSE_IDLE_HOOK                          1
#define configUSE_TICK_HOOK                          0
#define configCHECK_FOR_STACK_OVERFLOW               2
#define configUSE_MALLOC_FAILED_HOOK                 1
#define configASSERT(x) do { if ((x) == 0) { taskDISABLE_INTERRUPTS(); for (;;) {} } } while (0)

#define configPRIO_BITS                              __NVIC_PRIO_BITS
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY              (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY         (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define configENABLE_FPU                             1
#define configENABLE_MPU                             0
#define configENABLE_TRUSTZONE                       0
#define configRUN_FREERTOS_SECURE_ONLY               0
#define configUSE_MPU_WRAPPERS_V1                    0

#define INCLUDE_vTaskPrioritySet                     1
#define INCLUDE_uxTaskPriorityGet                    1
#define INCLUDE_vTaskDelete                          1
#define INCLUDE_vTaskSuspend                         1
#define INCLUDE_vTaskDelayUntil                      1
#define INCLUDE_vTaskDelay                           1
#define INCLUDE_xTaskGetSchedulerState               1
#define INCLUDE_xTaskGetCurrentTaskHandle            1
#define INCLUDE_uxTaskGetStackHighWaterMark          1

#define configUSE_TRACE_FACILITY                     0
#define configGENERATE_RUN_TIME_STATS                0
#define configUSE_STATS_FORMATTING_FUNCTIONS         0
