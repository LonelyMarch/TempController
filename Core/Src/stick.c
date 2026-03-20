#include "stick.h"

#include "adc.h"
#include "cmsis_os.h"

/* 由 CubeMX 在 freertos.c 中生成的摇杆消息队列句柄。 */
extern osMessageQueueId_t stickQueueHandle;

/* DMA 环形缓冲长度。取 16 可兼顾平滑度与响应速度。 */
#define STICK_DMA_SAMPLE_COUNT 16U

/* 摇杆扫描周期与状态判定时间参数（单位：ms）。 */
#define STICK_SCAN_PERIOD_MS 10U
#define STICK_ACTIVATE_MS 40U
#define STICK_RELEASE_MS 30U
#define STICK_LONGPRESS_MS 600U
#define STICK_REPEAT_MS 120U

/*
 * ADC DMA 缓冲区（16bit 采样）。
 * 注意：工程默认 .bss 在 DTCMRAM，DMA1 无法访问 DTCM。
 * 因此这里显式放入可 DMA 访问的 RAM_D1 段（.lcd_buf），并按 32B 对齐。
 */
static uint16_t s_adc_dma_buf[STICK_DMA_SAMPLE_COUNT] __attribute__((section(".lcd_buf"), aligned(32)));
/* 对外可读的最新滤波 ADC 值（当前为最小值滤波结果）。 */
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
 * @brief 软件滤波：读取 DMA 窗口中的最小值。
 * @note 该策略会优先响应低电平方向，可能更敏感；配合后级去抖使用。
 */
static uint16_t stick_min_adc(void)
{
  uint16_t min_val = 0xFFFFU;

  for (uint32_t i = 0; i < STICK_DMA_SAMPLE_COUNT; ++i) {
    if (s_adc_dma_buf[i] < min_val) {
      min_val = s_adc_dma_buf[i];
    }
  }

  return min_val;
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

/** @brief 读取当前滤波后的 ADC 值（当前为窗口最小值）。 */
uint16_t stick_get_raw_adc(void)
{
  return s_adc_latest;
}

/**
 * @brief 摇杆任务：最小值滤波 + 时间状态机 + 队列事件输出。
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

  stick_key_t raw_candidate = STICK_KEY_NONE;
  uint32_t raw_candidate_tick = 0U;

  stick_key_t active_key = STICK_KEY_NONE;
  uint32_t active_tick = 0U;
  uint32_t next_repeat_tick = 0U;
  uint32_t release_tick = 0U;
  uint8_t longpress_entered = 0U;

  for (;;) {
#if (__DCACHE_PRESENT == 1U)
    /* H7 开启 D-Cache 时，先失效缓存再读取 DMA 写入的数据。 */
    SCB_InvalidateDCache_by_Addr((uint32_t *)s_adc_dma_buf, sizeof(s_adc_dma_buf));
#endif

    const uint16_t adc = stick_min_adc();
    const stick_key_t decoded = stick_decode_key(adc);
    const uint32_t now = osKernelGetTickCount();

    s_adc_latest = adc;

    if (active_key == STICK_KEY_NONE) {
      /*
       * 空闲态：检测“掰动”动作。
       * 只有某个非 NONE 状态持续 STICK_ACTIVATE_MS 才算一次有效动作。
       */
      if (decoded != raw_candidate) {
        raw_candidate = decoded;
        raw_candidate_tick = now;
      } else if (raw_candidate != STICK_KEY_NONE &&
                 (now - raw_candidate_tick) >= STICK_ACTIVATE_MS) {
        active_key = raw_candidate;
        active_tick = now;
        next_repeat_tick = now + STICK_LONGPRESS_MS;
        release_tick = 0U;
        longpress_entered = 0U;

        s_stick_key = active_key;
        stick_queue_push(active_key);
      } else {
        s_stick_key = STICK_KEY_NONE;
      }
    } else {
      /*
       * 激活态：强制要求先回到 NONE 才允许下一次状态变化。
       * 即使中途抖到其他方向，也不会切换 active_key。
       */
      s_stick_key = active_key;

      if (decoded == STICK_KEY_NONE) {
        if (release_tick == 0U) {
          release_tick = now;
        }

        if ((now - release_tick) >= STICK_RELEASE_MS) {
          active_key = STICK_KEY_NONE;
          raw_candidate = STICK_KEY_NONE;
          raw_candidate_tick = now;
          release_tick = 0U;
          longpress_entered = 0U;
          s_stick_key = STICK_KEY_NONE;
        }
      } else {
        release_tick = 0U;

        /*
         * 长按判定：持续 STICK_LONGPRESS_MS 后进入长按连发。
         * 连发仅对方向键生效，按下键不连发。
         */
        if (active_key == STICK_KEY_LEFT || active_key == STICK_KEY_RIGHT ||
            active_key == STICK_KEY_UP || active_key == STICK_KEY_DOWN) {
          if (longpress_entered == 0U) {
            if ((now - active_tick) >= STICK_LONGPRESS_MS) {
              longpress_entered = 1U;
              next_repeat_tick = now + STICK_REPEAT_MS;
              stick_queue_push(active_key);
            }
          } else if (now >= next_repeat_tick) {
            stick_queue_push(active_key);
            next_repeat_tick = now + STICK_REPEAT_MS;
          }
        }
      }
    }

    osDelay(STICK_SCAN_PERIOD_MS);
  }
}
