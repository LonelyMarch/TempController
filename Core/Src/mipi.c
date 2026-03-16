#include "mipi.h"

#include "lcd.h"
#include "lvgl.h"
#include "src/drivers/display/st7789/lv_st7789.h"

/** 每次局部刷新使用的缓冲行数。 */
#define LCD_BUF_LINES 40U

/* Put large LVGL draw buffers into RAM_D1 via linker section .lvgl_buf. */
#define LVGL_DRAW_BUF_ATTR __attribute__((section(".lvgl_buf"), aligned(32)))

/** LVGL 显示对象句柄（单例）。 */
static lv_display_t * s_disp;
/** LVGL 主绘制缓冲区。 */
static lv_color_t s_buf1[LCD_W * LCD_BUF_LINES] LVGL_DRAW_BUF_ATTR;
/** LVGL 次绘制缓冲区（双缓冲）。 */
static lv_color_t s_buf2[LCD_W * LCD_BUF_LINES] LVGL_DRAW_BUF_ATTR;

/**
 * @brief 通过 lcd.c 的总线接口连续发送原始字节流。
 * @param data 待发送数据首地址。
 * @param len  待发送字节数。
 */
static void lcd_send_data_bytes(const uint8_t * data, size_t len)
{
    while(len--) {
        LCD_Writ_Bus(*data++);
    }
}

/**
 * @brief ST7789 命令发送回调（供 LVGL Generic MIPI/ST7789 驱动调用）。
 * @param disp LVGL 显示对象。
 * @param cmd 命令缓冲区。
 * @param cmd_size 命令字节数。
 * @param param 参数缓冲区。
 * @param param_size 参数字节数。
 *
 * @note 该回调为阻塞式发送，满足 LVGL 对 send_cmd 的要求。
 */
static void st7789_send_cmd(lv_display_t * disp, const uint8_t * cmd, size_t cmd_size,
                            const uint8_t * param, size_t param_size)
{
    (void)disp;

    if(cmd == NULL || cmd_size == 0U) {
        return;
    }

    if(cmd_size == 1U) {
        LCD_WR_REG(cmd[0]);
    }
    else {
        LCD_DC_Clr();
        lcd_send_data_bytes(cmd, cmd_size);
        LCD_DC_Set();
    }

    if(param != NULL && param_size > 0U) {
        lcd_send_data_bytes(param, param_size);
    }
}

/**
 * @brief ST7789 像素数据发送回调（供 LVGL 刷新流程调用）。
 * @param disp LVGL 显示对象。
 * @param cmd 写显存命令缓冲区（通常为 0x2C）。
 * @param cmd_size 命令字节数。
 * @param param 像素数据缓冲区。
 * @param param_size 像素数据字节数。
 *
 * @note 当前实现为阻塞式发送，发送完成后立即调用 lv_display_flush_ready()。
 */
static void st7789_send_color(lv_display_t * disp, const uint8_t * cmd, size_t cmd_size,
                              uint8_t * param, size_t param_size)
{
    if(cmd == NULL || cmd_size == 0U || param == NULL || param_size == 0U) {
        lv_display_flush_ready(disp);
        return;
    }

    if(cmd_size == 1U) {
        LCD_WR_REG(cmd[0]);
    }
    else {
        LCD_DC_Clr();
        lcd_send_data_bytes(cmd, cmd_size);
        LCD_DC_Set();
    }

    lcd_send_data_bytes(param, param_size);

    lv_display_flush_ready(disp);
}

/**
 * @brief 初始化 ST7789 与 LVGL 显示驱动绑定。
 *
 * 初始化流程：
 * - 调用既有 LCD_Init() 完成 ST7789 上电与寄存器初始化；
 * - 通过 lv_st7789_create() 创建显示对象；
 * - 根据 USE_HORIZONTAL 设置 LVGL 旋转与 ST7789 显存偏移；
 * - 绑定双缓冲，采用部分刷新模式。
 */
int8_t MIPI_LVGL_Init(void)
{
    uint32_t buf_size;

    if(s_disp != NULL) {
        return HAL_ERROR;
    }

    LCD_Init();

    s_disp = lv_st7789_create(LCD_W, LCD_H, LV_LCD_FLAG_NONE, st7789_send_cmd, st7789_send_color);

#if USE_HORIZONTAL==0
    lv_display_set_rotation(s_disp, LV_DISPLAY_ROTATION_0);
    lv_st7789_set_gap(s_disp, 0, 20);
#elif USE_HORIZONTAL==1
    lv_display_set_rotation(s_disp, LV_DISPLAY_ROTATION_180);
    lv_st7789_set_gap(s_disp, 0, 20);
#elif USE_HORIZONTAL==2
    lv_display_set_rotation(s_disp, LV_DISPLAY_ROTATION_90);
    lv_st7789_set_gap(s_disp, 20, 0);
#else
    lv_display_set_rotation(s_disp, LV_DISPLAY_ROTATION_270);
    lv_st7789_set_gap(s_disp, 20, 0);
#endif

    buf_size = sizeof(s_buf1);
    lv_display_set_buffers(s_disp, s_buf1, s_buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    
    return HAL_OK;
}
