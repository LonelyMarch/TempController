#ifndef __JOYSTICK_H__
#define __JOYSTICK_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef enum
{
    JOYSTICK_DIR_NONE = 0,
    JOYSTICK_DIR_LEFT,
    JOYSTICK_DIR_RIGHT,
    JOYSTICK_DIR_UP,
    JOYSTICK_DIR_DOWN,
    JOYSTICK_DIR_PRESS
} joystick_dir_t;

HAL_StatusTypeDef Joystick_Init(void);
joystick_dir_t Joystick_GetDirection(void);
int16_t Joystick_GetEncoderStep(void);
uint16_t Joystick_GetAdcRaw(void);

#ifdef __cplusplus
}
#endif

#endif