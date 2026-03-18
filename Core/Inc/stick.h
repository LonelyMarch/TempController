#ifndef __STICK_H
#define __STICK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  /* 未触发任何方向/按压。 */
  STICK_KEY_NONE = 0,
  /* 摇杆向左。 */
  STICK_KEY_LEFT,
  /* 摇杆向右。 */
  STICK_KEY_RIGHT,
  /* 摇杆向上。 */
  STICK_KEY_UP,
  /* 摇杆向下。 */
  STICK_KEY_DOWN,
  /* 摇杆按下。 */
  STICK_KEY_PRESS
} stick_key_t;

/**
 * @brief 摇杆任务入口。
 * @param argument RTOS 任务参数（未使用）。
 */
void StickTask(void *argument);

/**
 * @brief 获取去抖后的按键状态。
 * @return 当前稳定键值。
 */
stick_key_t stick_get_key(void);

/**
 * @brief 获取当前 ADC 平均原始值。
 * @return 16 位 ADC 值。
 */
uint16_t stick_get_raw_adc(void);

#ifdef __cplusplus
}
#endif

#endif /* __STICK_H */
