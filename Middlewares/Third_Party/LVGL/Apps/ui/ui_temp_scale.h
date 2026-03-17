#ifndef __UI_TEMP_SCALE_H__
#define __UI_TEMP_SCALE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief 创建温控 UI 页面并初始化内部状态。
 */
void UI_TempScale_Create(void);

/**
 * @brief 设置当前温度显示值。
 * @param value_c 当前温度（摄氏度）。
 */
void UI_TempScale_SetCurrentTemp(float value_c);

/**
 * @brief 设置目标温度（设定温度）。
 * @param value_c 目标温度（摄氏度）。
 */
void UI_TempScale_SetSetpointTemp(float value_c);

#ifdef __cplusplus
}
#endif

#endif