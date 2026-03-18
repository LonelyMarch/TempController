#ifndef __UI_H
#define __UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UI 初始化：清屏并重置内部状态。
 */
void ui_init(void);

/**
 * @brief UI 周期处理：读取输入、更新设定值并按需重绘。
 */
void ui_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __UI_H */
