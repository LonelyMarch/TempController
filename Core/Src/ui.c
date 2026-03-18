#include "ui.h"

#include "lcd.h"
#include "main.h"
#include "stick.h"
#include "cmsis_os2.h"

#include <stdio.h>

/* 主题色：黑底、白刻度、橙色强调文本。 */
#define UI_COLOR_BG 0x0000U
#define UI_COLOR_TEXT 0xFC60U
#define UI_COLOR_TICK 0xFFFFU
#define UI_COLOR_SUBTICK 0x6B4DU
#define UI_COLOR_DIM 0x4A69U

/* 布局常量。 */
#define UI_TEMP_ROW_Y 8U
#define UI_SCALE_LABEL_Y 84U
#define UI_SCALE_TOP_Y 104U
#define UI_SCALE_BASE_Y 198U
#define UI_SCALE_BOTTOM_Y (LCD_H - 1U)
#define UI_POINTER_APEX_Y (LCD_H - 18U)
#define UI_POINTER_BASE_Y (LCD_H - 4U)

/* 刻度长度：小格/中格/主格（每 10 小格）明显分层。 */
#define UI_TICK_LEN_MINOR 10U
#define UI_TICK_LEN_MID 26U
#define UI_TICK_LEN_MAJOR 52U

/* 交互与温度范围：内部统一使用 x10 定点值。 */
#define UI_CENTER_X (LCD_W / 2)
#define UI_PX_PER_01C 2
#define UI_SETPOINT_MIN_X10 50
#define UI_SETPOINT_MAX_X10 450

/* 由 CubeMX 在 freertos.c 中生成的摇杆消息队列句柄。 */
extern osMessageQueueId_t stickQueueHandle;

/* 上次渲染缓存，用于减少不必要重绘。 */
static int16_t s_last_cur_x10 = -32768;
static int16_t s_last_set_x10 = -32768;

/**
 * @brief 浮点温度转 x10 定点值（四舍五入）。
 */
static int16_t ui_float_to_x10(float value)
{
  const float scaled = value * 10.0f;
  return (int16_t)(scaled >= 0.0f ? (scaled + 0.5f) : (scaled - 0.5f));
}

/**
 * @brief 计算无符号整数显示位数。
 */
static uint16_t ui_u16_digits(uint16_t value)
{
  if (value >= 100U) {
    return 3U;
  }
  if (value >= 10U) {
    return 2U;
  }
  return 1U;
}

/**
 * @brief 绘制单个温度块（标题 + 数值）。
 */
static void ui_draw_temp_block(uint16_t x, const char *title, int16_t value_x10)
{
  char buf[20];

  LCD_ShowString(x, UI_TEMP_ROW_Y, (const uint8_t *)title, UI_COLOR_TEXT, UI_COLOR_BG, 16, 0);

  if (value_x10 < 0) {
    value_x10 = 0;
  }

  (void)snprintf(buf, sizeof(buf), "%u.%uC", (unsigned int)(value_x10 / 10), (unsigned int)(value_x10 % 10));
  LCD_ShowString(x, UI_TEMP_ROW_Y + 22U, (const uint8_t *)buf, UI_COLOR_TICK, UI_COLOR_BG, 24, 0);
}

/**
 * @brief 绘制顶部温度信息区。
 */
static void ui_draw_header(int16_t cur_x10, int16_t set_x10)
{
  LCD_Fill(0, 0, LCD_W, 74, UI_COLOR_BG);

  ui_draw_temp_block(10, "CUR", cur_x10);
  ui_draw_temp_block(160, "SET", set_x10);

  LCD_DrawLine(0, 74, LCD_W - 1U, 74, UI_COLOR_DIM);
}

/**
 * @brief 绘制“尺子刻度线”区域，中心指示当前设定温度。
 */
static void ui_draw_scale(int16_t set_x10)
{
  LCD_Fill(0, UI_SCALE_LABEL_Y, LCD_W, LCD_H, UI_COLOR_BG);

  /*
   * 按绝对温度逐 0.1C 扫描，再筛选 0.2C 小格。
   * 这样即使 set_x10 是奇数（如 24.3C），也能正确命中 1.0C/2.0C 大刻度与数字。
   */
  for (int16_t tick_x10 = (int16_t)(set_x10 - 260); tick_x10 <= (int16_t)(set_x10 + 260); ++tick_x10) {
    const int16_t delta_x10 = (int16_t)(tick_x10 - set_x10);
    const int16_t x = (int16_t)(UI_CENTER_X + delta_x10 * UI_PX_PER_01C);
    uint16_t color = UI_COLOR_SUBTICK;
    uint16_t len = UI_TICK_LEN_MINOR;

    /* 每小格 = 0.2C，仅绘制偶数 0.1C 点位。 */
    if ((tick_x10 % 2) != 0) {
      continue;
    }

    if (x < 0 || x >= LCD_W) {
      continue;
    }

    /* 每 5 小格（1.0C）加长一次刻度。 */
    if ((tick_x10 % 10) == 0) {
      color = UI_COLOR_TICK;
      len = UI_TICK_LEN_MID;
    }

    /* 每 10 小格（2.0C）在上方标注数字。 */
    if ((tick_x10 % 20) == 0) {
      color = UI_COLOR_TICK;
      len = UI_TICK_LEN_MAJOR;

      if (tick_x10 >= 0) {
        char num_buf[8];
        const int16_t whole = (int16_t)(tick_x10 / 10);
        const uint16_t abs_whole = (uint16_t)(whole >= 0 ? whole : -whole);
        const uint16_t digits = ui_u16_digits(abs_whole);
        const uint16_t text_x = (uint16_t)((x > (int16_t)(digits * 4U)) ? (x - (int16_t)(digits * 4U)) : 0);

        (void)snprintf(num_buf, sizeof(num_buf), "%d", (int)whole);
        LCD_ShowString(text_x, UI_SCALE_LABEL_Y, (const uint8_t *)num_buf, UI_COLOR_TEXT, UI_COLOR_BG, 16, 0);
      }
    }

    LCD_DrawLine((uint16_t)x, UI_SCALE_BASE_Y - len, (uint16_t)x, UI_SCALE_BASE_Y, color);
  }

  LCD_DrawLine(0, UI_SCALE_BASE_Y, LCD_W - 1U, UI_SCALE_BASE_Y, UI_COLOR_DIM);

  /* 底部中间固定小正三角形，尖端朝上指向当前刻度。 */
  for (uint16_t i = 0; i <= (UI_POINTER_BASE_Y - UI_POINTER_APEX_Y); ++i) {
    uint16_t half_w = (uint16_t)(((uint32_t)i * 7U) / (UI_POINTER_BASE_Y - UI_POINTER_APEX_Y));
    if (half_w == 0U) {
      half_w = 1U;
    }

    LCD_DrawLine(UI_CENTER_X - half_w,
                 UI_POINTER_APEX_Y + i,
                 UI_CENTER_X + half_w,
                 UI_POINTER_APEX_Y + i,
                 UI_COLOR_TEXT);
  }
}

/**
 * @brief 按步进修改设定温度，并进行上下限钳位。
 */
static void ui_apply_setpoint_delta(int16_t delta_x10)
{
  int16_t set_x10 = ui_float_to_x10(g_temp_setpoint_c);

  set_x10 = (int16_t)(set_x10 + delta_x10);

  if (set_x10 < UI_SETPOINT_MIN_X10) {
    set_x10 = UI_SETPOINT_MIN_X10;
  }
  if (set_x10 > UI_SETPOINT_MAX_X10) {
    set_x10 = UI_SETPOINT_MAX_X10;
  }

  g_temp_setpoint_c = (float)set_x10 / 10.0f;
}

/**
 * @brief 消费摇杆事件队列并执行温度设定动作。
 */
static void ui_process_input_queue(void)
{
  uint16_t msg = 0U;

  if (stickQueueHandle == NULL) {
    return;
  }

  while (osMessageQueueGet(stickQueueHandle, &msg, NULL, 0U) == osOK) {
    const stick_key_t key = (stick_key_t)msg;

    /* 左右细调 0.1C，上下粗调 1.0C，按下对齐当前温度。 */
    if (key == STICK_KEY_LEFT) {
      ui_apply_setpoint_delta(-1);
    } else if (key == STICK_KEY_RIGHT) {
      ui_apply_setpoint_delta(1);
    } else if (key == STICK_KEY_UP) {
      ui_apply_setpoint_delta(10);
    } else if (key == STICK_KEY_DOWN) {
      ui_apply_setpoint_delta(-10);
    } else if (key == STICK_KEY_PRESS) {
      g_temp_setpoint_c = g_temp_current_c;
    }
  }
}

/**
 * @brief 初始化 UI 状态并清屏。
 */
void ui_init(void)
{
  s_last_cur_x10 = -32768;
  s_last_set_x10 = -32768;

  LCD_Fill(0, 0, LCD_W, LCD_H, UI_COLOR_BG);
}

/**
 * @brief UI 周期处理入口。
 */
void ui_handler(void)
{
  const int16_t cur_x10 = ui_float_to_x10(g_temp_current_c);

  ui_process_input_queue();

  {
    const int16_t set_x10 = ui_float_to_x10(g_temp_setpoint_c);

    /* 顶部区域：当前值或设定值变化时刷新。 */
    if (cur_x10 != s_last_cur_x10 || set_x10 != s_last_set_x10) {
      ui_draw_header(cur_x10, set_x10);
      s_last_cur_x10 = cur_x10;
    }

    /* 刻度区域：仅在设定值变化时重绘。 */
    if (set_x10 != s_last_set_x10) {
      ui_draw_scale(set_x10);
      s_last_set_x10 = set_x10;
    }
  }
}

/**
 * @brief LCD 任务实现（覆盖 freertos.c 中的 __weak 默认实现）。
 * @param argument RTOS 任务参数（未使用）。
 */
void LcdTask(void *argument)
{
  (void)argument;

  LCD_Init();
  ui_init();

  for (;;) {
    ui_handler();
    osDelay(20U);
  }
}
