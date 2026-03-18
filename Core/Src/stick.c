#include "stick.h"

#include "adc.h"
#include "cmsis_os.h"

/* 由 CubeMX 在 freertos.c 中生成的摇杆消息队列句柄。 */
extern osMessageQueueId_t stickQueueHandle;

/* DMA 环形缓冲长度。取 16 可兼顾平滑度与响应速度。 */
#define STICK_DMA_SAMPLE_COUNT 16U

/* ADC DMA 缓冲区（16bit 采样）。 */
static uint16_t s_adc_dma_buf[STICK_DMA_SAMPLE_COUNT];
/* 对外可读的最新平均 ADC 值。 */
static volatile uint16_t s_adc_latest = 0U;
/* 对外可读的稳定键值。 */
static volatile stick_key_t s_stick_key = STICK_KEY_NONE;

/**
 * @brief 将按键事件写入队列；若队列满则丢弃最旧一条后再写入。
 */
static void stick_queue_push(stick_key_t key)
{
  uint16_t msg = (uint16_t)key;

  if (stickQueueHandle == NULL) {
    return;
  }

  if (osMessageQueuePut(stickQueueHandle, &msg, 0U, 0U) != osOK) {
    uint16_t dropped;
    (void)osMessageQueueGet(stickQueueHandle, &dropped, NULL, 0U);
    (void)osMessageQueuePut(stickQueueHandle, &msg, 0U, 0U);
  }
}

/**
 * @brief 计算 DMA 缓冲区均值，用于抑制采样抖动。
 */
static uint16_t stick_average_adc(void)
{
  uint32_t sum = 0U;

  for (uint32_t i = 0; i < STICK_DMA_SAMPLE_COUNT; ++i) {
    sum += s_adc_dma_buf[i];
  }

  return (uint16_t)(sum / STICK_DMA_SAMPLE_COUNT);
}

/**
 * @brief 将 ADC 值映射为摇杆方向/按压状态。
 * @note 阈值按 16bit ADC 与 3.3V 线性换算得到。
 */
static stick_key_t stick_decode_key(uint16_t adc)
{
  if (adc <= 6553U) {
    return STICK_KEY_PRESS;
  }
  if (adc <= 19660U) {
    return STICK_KEY_LEFT;
  }
  if (adc <= 32767U) {
    return STICK_KEY_RIGHT;
  }
  if (adc <= 45874U) {
    return STICK_KEY_UP;
  }
  if (adc <= 58981U) {
    return STICK_KEY_DOWN;
  }

  return STICK_KEY_NONE;
}

/** @brief 读取当前稳定键值。 */
stick_key_t stick_get_key(void)
{
  return s_stick_key;
}

/** @brief 读取当前平均 ADC 值。 */
uint16_t stick_get_raw_adc(void)
{
  return s_adc_latest;
}

/**
 * @brief 摇杆任务：启动 ADC DMA，并周期性执行均值+去抖。
 */
void StickTask(void *argument)
{
  (void)argument;

  /* 启动 ADC1 + DMA 循环采样，失败时保持 NONE 并退化等待。 */
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_adc_dma_buf, STICK_DMA_SAMPLE_COUNT) != HAL_OK) {
    for (;;) {
      s_stick_key = STICK_KEY_NONE;
      osDelay(100U);
    }
  }

  stick_key_t candidate = STICK_KEY_NONE;
  uint8_t stable_count = 0U;
  uint32_t next_repeat_tick = 0U;

  for (;;) {
    const uint16_t adc = stick_average_adc();
    const stick_key_t decoded = stick_decode_key(adc);
    const uint32_t now = osKernelGetTickCount();

    s_adc_latest = adc;

    /* 简单去抖：同一候选状态连续出现后才更新稳定状态。 */
    if (decoded == candidate) {
      if (stable_count < 3U) {
        stable_count++;
      }
    } else {
      candidate = decoded;
      stable_count = 0U;
    }

    if (stable_count >= 2U) {
      if (candidate != s_stick_key) {
        /* 边沿事件：状态变化时立即发送一次。 */
        s_stick_key = candidate;
        if (s_stick_key != STICK_KEY_NONE) {
          stick_queue_push(s_stick_key);
          next_repeat_tick = now + 280U;
        }
      } else if (s_stick_key != STICK_KEY_NONE && now >= next_repeat_tick) {
        /* 长按连发：保持按下时周期发送。 */
        stick_queue_push(s_stick_key);
        next_repeat_tick = now + 90U;
      }
    }

    /* 100Hz 处理频率。 */
    osDelay(10U);
  }
}
