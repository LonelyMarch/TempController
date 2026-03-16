/**
 * @file mipi.h
 * @brief LVGL 与 ST7789(MIPI DCS over SPI) 适配接口。
 */

#ifndef __MIPI_H
#define __MIPI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 LVGL 显示适配层并创建 ST7789 显示对象。
 *
 * 该函数会完成以下步骤：
 * - 初始化底层 LCD 控制器；
 * - 创建 LVGL ST7789 驱动实例；
 * - 设置旋转与显示偏移（gap）；
 * - 绑定 LVGL 绘制缓冲区。
 *
 * 该函数具备幂等性，重复调用不会重复创建显示对象。
 */
int8_t MIPI_LVGL_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __MIPI_H */
