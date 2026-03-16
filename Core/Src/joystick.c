#include "joystick.h"

#include <stdbool.h>

#include "adc.h"
#include "cmsis_os.h"

#define JOY_PRESS_MAX_RAW      3500U
#define JOY_LEFT_MAX_RAW       10000U
#define JOY_RIGHT_MAX_RAW      23000U
#define JOY_UP_MAX_RAW         36000U
#define JOY_DOWN_MAX_RAW       50000U

#define JOY_REPEAT_DELAY_MS    260U
#define JOY_REPEAT_RATE_MS     110U
#define JOY_SAMPLE_PERIOD_MS    20U

/* 上一次用于左右连发判断的方向（仅 LEFT/RIGHT 有意义）。 */
static joystick_dir_t s_repeat_dir = JOYSTICK_DIR_NONE;
/* 任务解析后的最新方向缓存，供 UI/业务线程直接读取。 */
static joystick_dir_t s_last_dir = JOYSTICK_DIR_NONE;
/* 下一次允许触发“连发步进”的系统 tick 时间点。 */
static uint32_t s_next_repeat_tick = 0U;
/* 最近一次有效 ADC 原始值缓存。 */
static uint16_t s_last_adc_raw = 0U;
/* DMA 写入的单样本原始值缓冲区（ADC1 oneshot）。 */
static volatile uint16_t s_dma_adc_raw = 0U;
/* DMA 转换完成标志：在 ADC 完成回调中置位。 */
static volatile bool s_dma_done = false;
/* DMA 错误标志：在 ADC 错误回调中置位。 */
static volatile bool s_dma_error = false;

/**
 * @brief 将 ADC 原始采样值转换为摇杆方向。
 * @param raw 最新 ADC 原始值。
 * @return 解析后的摇杆方向。
 */
static joystick_dir_t joystick_decode_raw(uint16_t raw)
{
    if(raw <= JOY_PRESS_MAX_RAW) {
        return JOYSTICK_DIR_PRESS;
    }
    if(raw <= JOY_LEFT_MAX_RAW) {
        return JOYSTICK_DIR_LEFT;
    }
    if(raw <= JOY_RIGHT_MAX_RAW) {
        return JOYSTICK_DIR_RIGHT;
    }
    if(raw <= JOY_UP_MAX_RAW) {
        return JOYSTICK_DIR_UP;
    }
    if(raw <= JOY_DOWN_MAX_RAW) {
        return JOYSTICK_DIR_DOWN;
    }

    return JOYSTICK_DIR_NONE;
}

/**
 * @brief 通过 DMA 触发一次 ADC 采样并等待完成。
 * @return 最新 ADC 值；若 DMA/超时失败则返回上次缓存值。
 */
static uint16_t joystick_read_adc_once_dma(void)
{
    uint32_t start_tick;

    /* DMA 采样仅在调度器运行后执行，避免在初始化阶段阻塞等待。 */
    if(osKernelGetState() != osKernelRunning) {
        return s_last_adc_raw;
    }

    s_dma_done = false;
    s_dma_error = false;

    if(HAL_ADC_Start_DMA(&hadc1, (uint32_t *)&s_dma_adc_raw, 1U) != HAL_OK) {
        return s_last_adc_raw;
    }

    start_tick = HAL_GetTick();
    while((!s_dma_done) && (!s_dma_error)) {
        if((HAL_GetTick() - start_tick) > 3U) {
            break;
        }
        osDelay(1U);
    }

    (void)HAL_ADC_Stop_DMA(&hadc1);

    if(s_dma_done && (!s_dma_error)) {
        s_last_adc_raw = (uint16_t)s_dma_adc_raw;
    }

    return s_last_adc_raw;
}

/**
 * @brief 初始化摇杆驱动状态并完成 ADC 校准。
 * @return 成功返回 HAL_OK，校准失败返回 HAL_ERROR。
 */
HAL_StatusTypeDef Joystick_Init(void)
{
    if(HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK) {
        return HAL_ERROR;
    }

    s_repeat_dir = JOYSTICK_DIR_NONE;
    s_next_repeat_tick = HAL_GetTick();
    s_last_adc_raw = 0xFFFFU;
    s_last_dir = JOYSTICK_DIR_NONE;
    return HAL_OK;
}

/**
 * @brief 获取任务缓存中的最新摇杆方向。
 * @return 最近一次解析出的方向。
 */
joystick_dir_t Joystick_GetDirection(void)
{
    return s_last_dir;
}

/**
 * @brief 获取左右方向的“编码器式”步进值（支持按住连发）。
 * @return 左为 -1，右为 +1，无步进为 0。
 */
int16_t Joystick_GetEncoderStep(void)
{
    joystick_dir_t dir = Joystick_GetDirection();
    uint32_t now = HAL_GetTick();

    /* 非左右方向时，复位连发状态。 */
    if(dir != JOYSTICK_DIR_LEFT && dir != JOYSTICK_DIR_RIGHT) {
        s_repeat_dir = JOYSTICK_DIR_NONE;
        s_next_repeat_tick = now + JOY_REPEAT_DELAY_MS;
        return 0;
    }

    /* 方向首次变化时立即触发一步，并设置首个连发延时。 */
    if(dir != s_repeat_dir) {
        s_repeat_dir = dir;
        s_next_repeat_tick = now + JOY_REPEAT_DELAY_MS;
        return (dir == JOYSTICK_DIR_LEFT) ? -1 : 1;
    }

    /* 持续按住同一方向时按固定周期连发。 */
    if((int32_t)(now - s_next_repeat_tick) >= 0) {
        s_next_repeat_tick = now + JOY_REPEAT_RATE_MS;
        return (dir == JOYSTICK_DIR_LEFT) ? -1 : 1;
    }

    return 0;
}

/**
 * @brief 获取最新缓存的 ADC 原始采样值。
 * @return 最近一次 ADC 原始值。
 */
uint16_t Joystick_GetAdcRaw(void)
{
    return s_last_adc_raw;
}

/**
 * @brief FreeRTOS 任务：周期采样摇杆 ADC 并更新缓存。
 * @param argument 未使用的任务参数。
 */
void StartJoystickTask(void *argument)
{
    (void)argument;

    for(;;) {
        /* 摇杆状态只在该任务中写入，其他上下文仅读取缓存。 */
        s_last_adc_raw = joystick_read_adc_once_dma();
        s_last_dir = joystick_decode_raw(s_last_adc_raw);
        osDelay(JOY_SAMPLE_PERIOD_MS);
    }
}

/**
 * @brief ADC 转换完成回调：用于 DMA 采样完成通知。
 * @param hadc ADC 句柄。
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if(hadc->Instance == ADC1) {
        s_dma_done = true;
    }
}

/**
 * @brief ADC 错误回调：用于 DMA 采样异常通知。
 * @param hadc ADC 句柄。
 */
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    if(hadc->Instance == ADC1) {
        s_dma_error = true;
    }
}
