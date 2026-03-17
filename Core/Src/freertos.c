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
#include "joystick.h"
#include "lvgl.h"
#include "lcd.h"
#include "ui_temp_scale.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
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
/* Definitions for lvglTask */
osThreadId_t lvglTaskHandle;
const osThreadAttr_t lvglTask_attributes = {
    .name = "lvglTask",
    .stack_size = 128 * 4,
    .priority = (osPriority_t)osPriorityNormal,
};
/* Definitions for joystickTask */
osThreadId_t joystickTaskHandle;
const osThreadAttr_t joystickTask_attributes = {
    .name = "joystickTask",
    .stack_size = 128 * 4,
    .priority = (osPriority_t)osPriorityLow,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void LvglTask(void* argument);
extern void JoystickTask(void* argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationTickHook(void);

/* USER CODE BEGIN 3 */
void vApplicationTickHook(void)
{
    /* This function will be called by each tick interrupt if
   configUSE_TICK_HOOK is set to 1 in FreeRTOSConfig.h. User code can be
   added here, but the tick hook is called from an interrupt context, so
   code must not attempt to block, and only the interrupt safe FreeRTOS API
   functions can be used (those that end in FromISR()). */

    /* LVGL uses HAL_GetTick() via lv_tick_set_cb(), no periodic increment needed here. */
}

/* USER CODE END 3 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void)
{
    /* USER CODE BEGIN Init */


    if (Joystick_Init() != HAL_OK)
    {
        Error_Handler();
    }

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

    /* USER CODE BEGIN RTOS_QUEUES */
    /* add queues, ... */
    /* USER CODE END RTOS_QUEUES */

    /* Create the thread(s) */
    /* creation of lvglTask */
    lvglTaskHandle = osThreadNew(LvglTask, NULL, &lvglTask_attributes);

    /* creation of joystickTask */
    joystickTaskHandle = osThreadNew(JoystickTask, NULL, &joystickTask_attributes);

    /* USER CODE BEGIN RTOS_THREADS */
    /* add threads, ... */
    /* USER CODE END RTOS_THREADS */

    /* USER CODE BEGIN RTOS_EVENTS */
    /* add events, ... */
    /* USER CODE END RTOS_EVENTS */
}

/* USER CODE BEGIN Header_LvglTask */
/**
  * @brief  Function implementing the lvglTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_LvglTask */
void LvglTask(void* argument)
{
    /* USER CODE BEGIN LvglTask */
    lv_init();
    lv_tick_set_cb(HAL_GetTick);
    LCD_Init();
    lcd_Dri_Init();
    // ui_init();

    /*Change the active screen's background color*/
    // lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x003a57), LV_PART_MAIN);

    /*Create a white label, set its text and align it to the center*/
    // lv_obj_t* label = lv_label_create(lv_screen_active());
    // lv_label_set_text(label, "Hello world");
    // lv_obj_set_style_text_color(lv_screen_active(), lv_color_hex(0xffffff), LV_PART_MAIN);
    // lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);


    // UI_TempScale_Create();
    /* Infinite loop */
    for (;;)
    {
        lv_task_handler(); /* Let LVGL process pending tasks. */
        osDelay(1);
    }
    /* USER CODE END LvglTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
