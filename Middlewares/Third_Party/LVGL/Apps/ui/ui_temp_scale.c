#include "ui_temp_scale.h"

#include <stdio.h>

#include "joystick.h"

#define UI_TEMP_MIN_C           40.0f
#define UI_TEMP_MAX_C           70.0f
#define UI_TEMP_FINE_STEP_C      0.5f
#define UI_TEMP_COARSE_STEP_C    1.0f

#define SCALE_MINOR_PER_C        2
#define SCALE_PIXEL_PER_MINOR    10
#define SCALE_VISIBLE_MINOR     14

typedef struct
{
    lv_obj_t* label_curr_value;
    lv_obj_t* label_set_value;
    lv_obj_t* scale;
    lv_obj_t* marker;
    lv_timer_t* timer;
} ui_ctx_t;

static ui_ctx_t s_ui;

/**
 * @brief 将温度值限制在可配置区间内。
 * @param value 待限制的温度值（摄氏度）。
 * @return 限制后的温度值。
 */
static float clamp_temp(float value)
{
    if (value < UI_TEMP_MIN_C)
    {
        return UI_TEMP_MIN_C;
    }
    if (value > UI_TEMP_MAX_C)
    {
        return UI_TEMP_MAX_C;
    }
    return value;
}

/**
 * @brief 刷新界面上的“当前温度/设定温度”文本。
 */
static void update_labels(void)
{
    if (s_ui.label_curr_value != NULL)
    {
        lv_label_set_text_fmt(s_ui.label_curr_value, "%.1f C", g_temp_current_c);
    }

    if (s_ui.label_set_value != NULL)
    {
        lv_label_set_text_fmt(s_ui.label_set_value, "%.1f C", g_temp_setpoint_c);
    }
}

/**
 * @brief 绘制横向刻度尺（指南针刻度风格）。
 * @param e LVGL 绘制事件参数。
 */
static void scale_draw_event_cb(lv_event_t* e)
{
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_draw_line_dsc_t line_dsc;
    lv_draw_label_dsc_t text_dsc;
    int32_t center_x;
    int32_t y_bottom;
    int32_t y_minor_top;
    int32_t y_major_top;
    int32_t i;
    int32_t set_minor_index;

    lv_obj_get_coords(obj, &coords);
    center_x = (coords.x1 + coords.x2) / 2;
    y_bottom = coords.y2 - 8;
    y_minor_top = y_bottom - 12;
    y_major_top = y_bottom - 26;

    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(0xD8E6FF);
    line_dsc.opa = LV_OPA_80;
    line_dsc.width = 2;

    lv_draw_label_dsc_init(&text_dsc);
    text_dsc.color = lv_color_hex(0xF67A4D);
    text_dsc.opa = LV_OPA_COVER;
    text_dsc.font = LV_FONT_DEFAULT;

    set_minor_index = (int32_t)(g_temp_setpoint_c * (float)SCALE_MINOR_PER_C);

    for (i = -SCALE_VISIBLE_MINOR; i <= SCALE_VISIBLE_MINOR; i++)
    {
        int32_t x = center_x + i * SCALE_PIXEL_PER_MINOR;
        int32_t minor_index = set_minor_index + i;
        /* 每 2 摄氏度画一根主刻度，其余为次刻度。 */
        bool major = ((minor_index % (SCALE_MINOR_PER_C * 2)) == 0);
        int32_t dist = (i < 0) ? -i : i;
        uint8_t opa = (dist > 10) ? 50U : (uint8_t)(255U - (uint8_t)(dist * 16U));

        if (x < coords.x1 + 2 || x > coords.x2 - 2)
        {
            continue;
        }

        line_dsc.p1.x = x;
        line_dsc.p2.x = x;
        line_dsc.p1.y = major ? y_major_top : y_minor_top;
        line_dsc.p2.y = y_bottom;
        line_dsc.color = major ? lv_color_hex(0xF67A4D) : lv_color_hex(0xD8E6FF);
        line_dsc.opa = opa;
        line_dsc.width = major ? 3 : 2;
        lv_draw_line(layer, &line_dsc);

        if (major)
        {
            char txt[8];
            lv_area_t txt_area;
            int32_t temp_i = minor_index / SCALE_MINOR_PER_C;

            (void)snprintf(txt, sizeof(txt), "%d", temp_i);
            /* LVGL v9: 文本内容通过 draw descriptor 传入。 */
            text_dsc.text = txt;
            text_dsc.text_local = 1;
            txt_area.x1 = x - 14;
            txt_area.x2 = x + 14;
            txt_area.y1 = coords.y1 + 2;
            txt_area.y2 = coords.y1 + 18;
            lv_draw_label(layer, &text_dsc, &txt_area);
        }
    }
}

/**
 * @brief UI 定时轮询：读取摇杆并更新设定温度。
 * @param timer LVGL 定时器参数（未使用）。
 */
static void ui_tick_cb(lv_timer_t* timer)
{
    int16_t step;
    joystick_dir_t dir;

    (void)timer;

    /* 左右细调：一次 0.5 度。 */
    step = Joystick_GetEncoderStep();
    if (step != 0)
    {
        g_temp_setpoint_c = clamp_temp(g_temp_setpoint_c + (float)step * UI_TEMP_FINE_STEP_C);
        update_labels();
        lv_obj_invalidate(s_ui.scale);
    }

    /* 上下粗调：一次 1.0 度；按压回到当前温度。 */
    dir = Joystick_GetDirection();
    if (dir == JOYSTICK_DIR_UP)
    {
        g_temp_setpoint_c = clamp_temp(g_temp_setpoint_c + UI_TEMP_COARSE_STEP_C);
        update_labels();
        lv_obj_invalidate(s_ui.scale);
    }
    else if (dir == JOYSTICK_DIR_DOWN)
    {
        g_temp_setpoint_c = clamp_temp(g_temp_setpoint_c - UI_TEMP_COARSE_STEP_C);
        update_labels();
        lv_obj_invalidate(s_ui.scale);
    }
    else if (dir == JOYSTICK_DIR_PRESS)
    {
        g_temp_setpoint_c = clamp_temp(g_temp_current_c);
        update_labels();
        lv_obj_invalidate(s_ui.scale);
    }
}

/**
 * @brief 创建温控主界面对象并启动 UI 轮询定时器。
 */
void UI_TempScale_Create(void)
{
    lv_obj_t* scr = lv_screen_active();
    lv_obj_t* title_curr;
    lv_obj_t* title_set;

    /* 暗色线性风格背景。 */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x060B1A), 0);
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x0F1A34), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    title_curr = lv_label_create(scr);
    lv_label_set_text(title_curr, "CURRENT TEMP");
    lv_obj_set_style_text_color(title_curr, lv_color_hex(0x8B9AB8), 0);
    lv_obj_align(title_curr, LV_ALIGN_TOP_LEFT, 12, 12);

    s_ui.label_curr_value = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.label_curr_value, lv_color_hex(0xF0F5FF), 0);
    lv_obj_set_style_text_letter_space(s_ui.label_curr_value, 1, 0);
    lv_obj_align_to(s_ui.label_curr_value, title_curr, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    title_set = lv_label_create(scr);
    lv_label_set_text(title_set, "SET TEMP");
    lv_obj_set_style_text_color(title_set, lv_color_hex(0xF67A4D), 0);
    lv_obj_align(title_set, LV_ALIGN_TOP_RIGHT, -12, 12);

    s_ui.label_set_value = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ui.label_set_value, lv_color_hex(0xF8B18E), 0);
    lv_obj_set_style_text_letter_space(s_ui.label_set_value, 1, 0);
    lv_obj_align_to(s_ui.label_set_value, title_set, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 2);

    /* 刻度绘制容器。 */
    s_ui.scale = lv_obj_create(scr);
    lv_obj_set_size(s_ui.scale, lv_pct(100), 82);
    lv_obj_align(s_ui.scale, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_style_bg_opa(s_ui.scale, LV_OPA_0, 0);
    lv_obj_set_style_border_width(s_ui.scale, 0, 0);
    lv_obj_set_style_pad_all(s_ui.scale, 0, 0);
    lv_obj_clear_flag(s_ui.scale, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ui.scale, scale_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);

    /* 中央固定基准指示器。 */
    s_ui.marker = lv_obj_create(scr);
    lv_obj_set_size(s_ui.marker, 4, 18);
    lv_obj_align_to(s_ui.marker, s_ui.scale, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_radius(s_ui.marker, 2, 0);
    lv_obj_set_style_bg_color(s_ui.marker, lv_color_hex(0xF67A4D), 0);
    lv_obj_set_style_border_width(s_ui.marker, 0, 0);

    g_temp_current_c = clamp_temp(g_temp_current_c);
    g_temp_setpoint_c = clamp_temp(g_temp_setpoint_c);
    update_labels();

    /* 避免重复创建定时器导致多重轮询。 */
    if (s_ui.timer != NULL)
    {
        lv_timer_delete(s_ui.timer);
    }
    s_ui.timer = lv_timer_create(ui_tick_cb, 60, NULL);
}

/**
 * @brief 更新“当前温度”并刷新文本显示。
 * @param value_c 当前温度（摄氏度）。
 */
void UI_TempScale_SetCurrentTemp(float value_c)
{
    g_temp_current_c = clamp_temp(value_c);
    update_labels();
}

/**
 * @brief 更新“设定温度”，并触发刻度区重绘。
 * @param value_c 设定温度（摄氏度）。
 */
void UI_TempScale_SetSetpointTemp(float value_c)
{
    g_temp_setpoint_c = clamp_temp(value_c);
    update_labels();
    if (s_ui.scale != NULL)
    {
        lv_obj_invalidate(s_ui.scale);
    }
}