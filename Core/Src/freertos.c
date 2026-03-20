/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */


/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
typedef StaticTask_t osStaticThreadDef_t;
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for lcdTask */
osThreadId_t lcdTaskHandle;
uint32_t lcdTaskBuffer[ 8192 ];
osStaticThreadDef_t lcdTaskControlBlock;
const osThreadAttr_t lcdTask_attributes = {
  .name = "lcdTask",
  .cb_mem = &lcdTaskControlBlock,
  .cb_size = sizeof(lcdTaskControlBlock),
  .stack_mem = &lcdTaskBuffer[0],
  .stack_size = sizeof(lcdTaskBuffer),
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for stickTask */
osThreadId_t stickTaskHandle;
uint32_t stickTaskBuffer[ 2048 ];
osStaticThreadDef_t stickTaskControlBlock;
const osThreadAttr_t stickTask_attributes = {
  .name = "stickTask",
  .cb_mem = &stickTaskControlBlock,
  .cb_size = sizeof(stickTaskControlBlock),
  .stack_mem = &stickTaskBuffer[0],
  .stack_size = sizeof(stickTaskBuffer),
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for stickQueue */
osMessageQueueId_t stickQueueHandle;
const osMessageQueueAttr_t stickQueue_attributes = {
  .name = "stickQueue"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void LcdTask(void *argument);
void StickTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */




  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
    /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
    /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
    /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of stickQueue */
  stickQueueHandle = osMessageQueueNew (16, sizeof(uint16_t), &stickQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
    /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of lcdTask */
  lcdTaskHandle = osThreadNew(LcdTask, NULL, &lcdTask_attributes);

  /* creation of stickTask */
  stickTaskHandle = osThreadNew(StickTask, NULL, &stickTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
    /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
    /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_LcdTask */
/**
  * @brief  Function implementing the lcdTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_LcdTask */
__weak void LcdTask(void *argument)
{
  /* USER CODE BEGIN LcdTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END LcdTask */
}

/* USER CODE BEGIN Header_StickTask */
/**
* @brief Function implementing the stickTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StickTask */
__weak void StickTask(void *argument)
{
  /* USER CODE BEGIN StickTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StickTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

