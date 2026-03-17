#include "lcd.h"
#include "cmsis_os2.h"
#include "display/lv_display.h"

#include "tim.h"

#include "lvgl.h"
#include "src/drivers/display/st7789/lv_st7789.h"


/** 每次局部刷新使用的缓冲行数。 */
#define LCD_BUF_LINES 40U

#define BUS_SPI1_POLL_TIMEOUT 0x1000U

/* Put large LVGL draw buffers into RAM_D1 via linker section .lvgl_buf. */
#define LVGL_DRAW_BUF_ATTR __attribute__((section(".lvgl_buf"), aligned(32)))

lv_display_t* lcd_disp;
volatile int lcd_bus_busy = 0;

lv_color_t * buf1 = NULL;
lv_color_t * buf2 = NULL;

void lcd_color_transfer_ready_cb(SPI_HandleTypeDef* hspi)
{
	LV_UNUSED(hspi);
	LCD_CS_Set();
	lcd_bus_busy = 0;
	lv_display_flush_ready(lcd_disp);
}

/* Initialize LCD I/O bus, reset LCD */
HAL_StatusTypeDef lcd_io_init(void)
{
	/* Register SPI Tx Complete Callback */
	HAL_SPI_RegisterCallback(&hspi1, HAL_SPI_TX_COMPLETE_CB_ID, lcd_color_transfer_ready_cb);

	/* reset LCD */
	LCD_RES_Clr();
	osDelay(100);
	LCD_RES_Set();
	osDelay(100);

	LCD_CS_Set();
	LCD_DC_Set();

	LCD_Init();
	return HAL_OK;
}

/* Platform-specific implementation of the LCD send command function. In general this should use polling transfer. */
void lcd_send_cmd(lv_display_t* disp, const uint8_t* cmd, size_t cmd_size, const uint8_t* param, size_t param_size)
{
	LV_UNUSED(disp);
	while (lcd_bus_busy); /* wait until previous transfer is finished */
	/* Set the SPI in 8-bit mode */
	hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
	HAL_SPI_Init(&hspi1);
	/* DCX low (command) */
	LCD_DC_Clr();
	/* CS low */
	LCD_CS_Clr();
	/* send command */
	if (HAL_SPI_Transmit(&hspi1, cmd, cmd_size, BUS_SPI1_POLL_TIMEOUT) == HAL_OK)
	{
		/* DCX high (data) */
		LCD_DC_Set();
		/* for short data blocks we use polling transfer */
		HAL_SPI_Transmit(&hspi1, (uint8_t*)param, (uint16_t)param_size, BUS_SPI1_POLL_TIMEOUT);
		/* CS high */
		LCD_CS_Set();
	}
}

/* Platform-specific implementation of the LCD send color function. For better performance this should use DMA transfer.
 * In case of a DMA transfer a callback must be installed to notify LVGL about the end of the transfer.
 */
void lcd_send_color(lv_display_t* disp, const uint8_t* cmd, size_t cmd_size, uint8_t* param, size_t param_size)
{
	LV_UNUSED(disp);
	while (lcd_bus_busy); /* wait until previous transfer is finished */
	/* Set the SPI in 8-bit mode */
	hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
	HAL_SPI_Init(&hspi1);
	/* DCX low (command) */
	LCD_DC_Clr();
	/* CS low */
	LCD_CS_Clr();
	/* send command */
	if (HAL_SPI_Transmit(&hspi1, cmd, cmd_size, BUS_SPI1_POLL_TIMEOUT) == HAL_OK)
	{
		/* DCX high (data) */
		LCD_DC_Set();
		/* for color data use DMA transfer */
		/* Set the SPI in 16-bit mode to match endianess */
		hspi1.Init.DataSize = SPI_DATASIZE_16BIT;
		HAL_SPI_Init(&hspi1);
		lcd_bus_busy = 1;
		HAL_SPI_Transmit_DMA(&hspi1, param, (uint16_t)param_size / 2);
		/* NOTE: CS will be reset in the transfer ready callback */
	}
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
HAL_StatusTypeDef lcd_Dri_Init(void)
{
	// if (lcd_io_init()!= HAL_OK) return HAL_ERROR;

	// LCD_Init();

	// __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, 1000);
	// HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_2);

	lcd_disp = lv_st7789_create(LCD_W_RES, LCD_H_RES, LV_LCD_FLAG_NONE, lcd_send_cmd, lcd_send_color);

#if USE_HORIZONTAL==0
	lv_display_set_rotation(s_disp, LV_DISPLAY_ROTATION_0);
	lv_st7789_set_gap(s_disp, 0, 20);
#elif USE_HORIZONTAL==1
	lv_display_set_rotation(s_disp, LV_DISPLAY_ROTATION_180);
	lv_st7789_set_gap(s_disp, 0, 20);
#elif USE_HORIZONTAL==2
	lv_display_set_rotation(lcd_disp, LV_DISPLAY_ROTATION_90);
	lv_st7789_set_gap(lcd_disp, 20, 0);
#else
	lv_display_set_rotation(s_disp, LV_DISPLAY_ROTATION_270);
	lv_st7789_set_gap(s_disp, 20, 0);
#endif

	uint32_t buf_size = LCD_H_RES * LCD_W_RES / 10 * lv_color_format_get_size(lv_display_get_color_format(lcd_disp));
	buf1 = lv_malloc(buf_size);
	if(buf1 == NULL) {
		LV_LOG_ERROR("display draw buffer malloc failed");
		return HAL_ERROR;
	}
	buf2 = lv_malloc(buf_size);
	if(buf2 == NULL) {
		LV_LOG_ERROR("display buffer malloc failed");
		lv_free(buf1);
		return HAL_ERROR;
	}
	lv_display_set_buffers(lcd_disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

	return HAL_OK;
}

/**
************************************************************************
* @brief:      	LCD_Init: LCD初始化函数
* @param:      	void
* @details:    	执行LCD的初始化过程，包括复位、背光控制、寄存器配置等
* @retval:     	void
************************************************************************
**/
void LCD_WR_REG8(uint8_t data)
{
	lcd_send_cmd(lcd_disp, &data, 1, NULL, 0);
}
void LCD_WR_DATA8(uint8_t data)
{
	lcd_send_color(lcd_disp, NULL, 0, &data, 1);
}
void LCD_Init(void)
{
	LCD_RES_Clr(); //复位
	osDelay(100);
	LCD_RES_Set();
	osDelay(100);

	LCD_BLK_Set(); //打开背光
	osDelay(100);

	//************* Start Initial Sequence **********//
	LCD_WR_REG8(0x11); //Sleep out
	osDelay(120); //Delay 120ms
	//************* Start Initial Sequence **********//
	LCD_WR_REG8(0x36);
	if (USE_HORIZONTAL == 0)LCD_WR_DATA8(0x00);
	else if (USE_HORIZONTAL == 1)LCD_WR_DATA8(0xC0);
	else if (USE_HORIZONTAL == 2)LCD_WR_DATA8(0x70);
	else LCD_WR_DATA8(0xA0);

	LCD_WR_REG8(0x3A);
	LCD_WR_DATA8(0x05);

	LCD_WR_REG8(0xB2);
	LCD_WR_DATA8(0x0C);
	LCD_WR_DATA8(0x0C);
	LCD_WR_DATA8(0x00);
	LCD_WR_DATA8(0x33);
	LCD_WR_DATA8(0x33);

	LCD_WR_REG8(0xB7);
	LCD_WR_DATA8(0x35);

	LCD_WR_REG8(0xBB);
	LCD_WR_DATA8(0x32); //Vcom=1.35V

	LCD_WR_REG8(0xC2);
	LCD_WR_DATA8(0x01);

	LCD_WR_REG8(0xC3);
	LCD_WR_DATA8(0x15); //GVDD=4.8V  颜色深度

	LCD_WR_REG8(0xC4);
	LCD_WR_DATA8(0x20); //VDV, 0x20:0v

	LCD_WR_REG8(0xC6);
	LCD_WR_DATA8(0x0F); //0x0F:60Hz

	LCD_WR_REG8(0xD0);
	LCD_WR_DATA8(0xA4);
	LCD_WR_DATA8(0xA1);

	LCD_WR_REG8(0xE0);
	LCD_WR_DATA8(0xD0);
	LCD_WR_DATA8(0x08);
	LCD_WR_DATA8(0x0E);
	LCD_WR_DATA8(0x09);
	LCD_WR_DATA8(0x09);
	LCD_WR_DATA8(0x05);
	LCD_WR_DATA8(0x31);
	LCD_WR_DATA8(0x33);
	LCD_WR_DATA8(0x48);
	LCD_WR_DATA8(0x17);
	LCD_WR_DATA8(0x14);
	LCD_WR_DATA8(0x15);
	LCD_WR_DATA8(0x31);
	LCD_WR_DATA8(0x34);

	LCD_WR_REG8(0xE1);
	LCD_WR_DATA8(0xD0);
	LCD_WR_DATA8(0x08);
	LCD_WR_DATA8(0x0E);
	LCD_WR_DATA8(0x09);
	LCD_WR_DATA8(0x09);
	LCD_WR_DATA8(0x15);
	LCD_WR_DATA8(0x31);
	LCD_WR_DATA8(0x33);
	LCD_WR_DATA8(0x48);
	LCD_WR_DATA8(0x17);
	LCD_WR_DATA8(0x14);
	LCD_WR_DATA8(0x15);
	LCD_WR_DATA8(0x31);
	LCD_WR_DATA8(0x34);
	LCD_WR_REG8(0x21);

	LCD_WR_REG8(0x29);
}
