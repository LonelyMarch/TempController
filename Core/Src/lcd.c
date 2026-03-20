#include "lcd.h"

#include "cmsis_os2.h"
#include "dma2d.h"
#include "lcdfont.h"

#define LCD_FB_PIXELS ((uint32_t)LCD_W * (uint32_t)LCD_H)
#define LCD_SPI_DMA_CHUNK_BYTES 16384U

/* 双缓冲显存：front用于显示，back用于本帧绘制。 */
static uint16_t s_lcd_buf_a[LCD_FB_PIXELS] __attribute__((section(".lcd_buf"), aligned(32)));
static uint16_t s_lcd_buf_b[LCD_FB_PIXELS] __attribute__((section(".lcd_buf"), aligned(32)));
static uint16_t *s_lcd_front_buf = s_lcd_buf_a;
static uint16_t *s_lcd_back_buf = s_lcd_buf_b;

/* 软件绘制窗口状态：用于兼容原有 LCD_WR_DATA 连续写入语义。 */
static uint16_t s_win_x1 = 0U;
static uint16_t s_win_y1 = 0U;
static uint16_t s_win_x2 = 0U;
static uint16_t s_win_y2 = 0U;
static uint16_t s_win_cx = 0U;
static uint16_t s_win_cy = 0U;

/**
************************************************************************
* @brief:        LCD_SPI_SetDataSize: 动态切换SPI数据位宽
* @param:        data_size - 目标位宽（8bit/16bit）
* @retval:       void
* @details:      当配置变化时执行DeInit/Init重建SPI，保证命令(8bit)与像素(16bit)可共存。
************************************************************************
**/
static void LCD_SPI_SetDataSize(uint32_t data_size)
{
	if (hspi1.Init.DataSize == data_size)
	{
		return;
	}

	if (HAL_SPI_DeInit(&hspi1) != HAL_OK)
	{
		Error_Handler();
	}

	hspi1.Init.DataSize = data_size;

	if (HAL_SPI_Init(&hspi1) != HAL_OK)
	{
		Error_Handler();
	}
}

/**
************************************************************************
* @brief:        LCD_SPI_Transmit8: 以8位模式发送SPI数据
* @param:        data - 发送缓冲区
* @param:        size - 字节数
* @retval:       void
* @details:      LCD命令和寄存器参数均按8位发送。
************************************************************************
**/
static void LCD_SPI_Transmit8(const uint8_t *data, uint16_t size)
{
	LCD_SPI_SetDataSize(SPI_DATASIZE_8BIT);
	if (HAL_SPI_Transmit(&hspi1, (uint8_t *)data, size, 0xFFFFU) != HAL_OK)
	{
		Error_Handler();
	}
}

/**
************************************************************************
* @brief:        LCD_ColorToBuf: 将颜色转换为帧缓冲格式
* @param:        color - RGB565颜色
* @retval:       uint16_t
* @details:      当前SPI配置为16bit发送，帧缓冲保持原生RGB565字序。
************************************************************************
**/
static uint16_t LCD_ColorToBuf(uint16_t color)
{
	return color;
}

/**
************************************************************************
* @brief:        LCD_SPI_TransmitDMA_Pixels: 使用SPI DMA发送像素流
* @param:        data - 像素缓冲区（RGB565）
* @param:        pixel_count - 像素数量
* @retval:       void
* @details:      SPI处于16bit模式，按像素分块DMA发送并等待每块完成。
************************************************************************
**/
static void LCD_SPI_TransmitDMA_Pixels(const uint16_t *data, uint32_t pixel_count)
{
	LCD_SPI_SetDataSize(SPI_DATASIZE_16BIT);

	while (pixel_count > 0U)
	{
		/* HAL长度参数为16位，超长帧拆分发送。 */
		uint16_t chunk = (pixel_count > 0xFFFFU) ? 0xFFFFU : (uint16_t)pixel_count;
		if (HAL_SPI_Transmit_DMA(&hspi1, (uint8_t *)data, chunk) != HAL_OK)
		{
			Error_Handler();
		}

		/* 该实现为阻塞式等待，保证缓冲区交换时DMA已结束。 */
		while (HAL_SPI_GetState(&hspi1) != HAL_SPI_STATE_READY)
		{
			if (osKernelGetState() == osKernelRunning)
			{
				osThreadYield();
			}
		}

		data += chunk;
		pixel_count -= chunk;
	}
}

/**
************************************************************************
* @brief:        LCD_DMA2D_FillRect: 使用DMA2D填充矩形
* @param:        dst - 目标帧缓冲
* @param:        x, y - 矩形左上角
* @param:        w, h - 矩形宽高
* @param:        color - 填充颜色
* @retval:       void
* @details:      采用DMA2D R2M模式，将单色快速写入back buffer。
************************************************************************
**/
static void LCD_DMA2D_FillRect(uint16_t *dst, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
	if (w == 0U || h == 0U)
	{
		return;
	}

	hdma2d.Init.Mode = DMA2D_R2M;
	hdma2d.Init.ColorMode = DMA2D_OUTPUT_RGB565;
	hdma2d.Init.OutputOffset = LCD_W - w;
	if (HAL_DMA2D_Init(&hdma2d) != HAL_OK)
	{
		Error_Handler();
	}

	if (HAL_DMA2D_Start(&hdma2d,
	                   (uint32_t)LCD_ColorToBuf(color),
	                   (uint32_t)&dst[(uint32_t)y * LCD_W + x],
	                   w,
	                   h) != HAL_OK)
	{
		Error_Handler();
	}

	if (HAL_DMA2D_PollForTransfer(&hdma2d, 100U) != HAL_OK)
	{
		Error_Handler();
	}
}

/**
************************************************************************
* @brief:        LCD_DMA2D_CopyFullFrame: 使用DMA2D复制整帧
* @param:        dst - 目标帧缓冲
* @param:        src - 源帧缓冲
* @retval:       void
* @details:      每帧开始时将front复制到back，实现保留上一帧内容后再增量绘制。
************************************************************************
**/
static void LCD_DMA2D_CopyFullFrame(uint16_t *dst, const uint16_t *src)
{
	hdma2d.Init.Mode = DMA2D_M2M;
	hdma2d.Init.ColorMode = DMA2D_OUTPUT_RGB565;
	hdma2d.Init.OutputOffset = 0;
	hdma2d.LayerCfg[1].InputOffset = 0;
	hdma2d.LayerCfg[1].InputColorMode = DMA2D_INPUT_RGB565;
	hdma2d.LayerCfg[1].AlphaMode = DMA2D_NO_MODIF_ALPHA;
	hdma2d.LayerCfg[1].InputAlpha = 0;
	hdma2d.LayerCfg[1].AlphaInverted = DMA2D_REGULAR_ALPHA;
	hdma2d.LayerCfg[1].RedBlueSwap = DMA2D_RB_REGULAR;
	hdma2d.LayerCfg[1].ChromaSubSampling = DMA2D_NO_CSS;

	if (HAL_DMA2D_Init(&hdma2d) != HAL_OK)
	{
		Error_Handler();
	}
	if (HAL_DMA2D_ConfigLayer(&hdma2d, 1) != HAL_OK)
	{
		Error_Handler();
	}

	if (HAL_DMA2D_Start(&hdma2d,
	                   (uint32_t)src,
	                   (uint32_t)dst,
	                   LCD_W,
	                   LCD_H) != HAL_OK)
	{
		Error_Handler();
	}

	if (HAL_DMA2D_PollForTransfer(&hdma2d, 100U) != HAL_OK)
	{
		Error_Handler();
	}
}

/**
************************************************************************
* @brief:        LCD_MapToPanelWindow: 将逻辑坐标映射到面板物理窗口
* @param:        x1, y1, x2, y2 - 逻辑窗口坐标
* @param:        tx1, ty1, tx2, ty2 - 映射后的面板坐标输出
* @retval:       void
* @details:      与原驱动保持一致：竖屏Y偏移+20，横屏X偏移+20。
************************************************************************
**/
static void LCD_MapToPanelWindow(uint16_t x1,
	                             uint16_t y1,
	                             uint16_t x2,
	                             uint16_t y2,
	                             uint16_t *tx1,
	                             uint16_t *ty1,
	                             uint16_t *tx2,
	                             uint16_t *ty2)
{
#if USE_HORIZONTAL==0 || USE_HORIZONTAL==1
	*tx1 = x1;
	*tx2 = x2;
	*ty1 = y1 + 20U;
	*ty2 = y2 + 20U;
#else
	*tx1 = x1 + 20U;
	*tx2 = x2 + 20U;
	*ty1 = y1;
	*ty2 = y2;
#endif
}

/**
************************************************************************
* @brief:        LCD_Panel_BeginMemoryWrite: 配置LCD显存写窗口
* @param:        x1, y1, x2, y2 - 逻辑窗口坐标
* @retval:       void
* @details:      发送ST7789的2A/2B/2C命令，后续可连续写入像素数据。
************************************************************************
**/
static void LCD_Panel_BeginMemoryWrite(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
	uint8_t cmd;
	uint8_t data[4];
	uint16_t tx1, ty1, tx2, ty2;

	LCD_MapToPanelWindow(x1, y1, x2, y2, &tx1, &ty1, &tx2, &ty2);

	LCD_CS_Clr();

	cmd = 0x2A;
	LCD_DC_Clr();
	LCD_SPI_Transmit8(&cmd, 1);
	LCD_DC_Set();
	data[0] = (uint8_t)(tx1 >> 8);
	data[1] = (uint8_t)tx1;
	data[2] = (uint8_t)(tx2 >> 8);
	data[3] = (uint8_t)tx2;
	LCD_SPI_Transmit8(data, 4);

	cmd = 0x2B;
	LCD_DC_Clr();
	LCD_SPI_Transmit8(&cmd, 1);
	LCD_DC_Set();
	data[0] = (uint8_t)(ty1 >> 8);
	data[1] = (uint8_t)ty1;
	data[2] = (uint8_t)(ty2 >> 8);
	data[3] = (uint8_t)ty2;
	LCD_SPI_Transmit8(data, 4);

	cmd = 0x2C;
	LCD_DC_Clr();
	LCD_SPI_Transmit8(&cmd, 1);
	LCD_DC_Set();
}

/**
************************************************************************
* @brief:        LCD_BeginFrame: 开始一帧绘制
* @param:        void
* @retval:       void
* @details:      使用DMA2D将前台缓冲复制到后台缓冲，供本帧继续绘制。
************************************************************************
**/
void LCD_BeginFrame(void)
{
	LCD_DMA2D_CopyFullFrame(s_lcd_back_buf, s_lcd_front_buf);
}

/**
************************************************************************
* @brief:        LCD_EndFrame: 结束一帧绘制并刷新到屏幕
* @param:        void
* @retval:       void
* @details:      将back buffer通过SPI DMA整帧发送，再交换front/back指针。
************************************************************************
**/
void LCD_EndFrame(void)
{
	uint16_t *tmp;

	/* 配置全屏写窗口并发送整帧像素。 */
	LCD_Panel_BeginMemoryWrite(0U, 0U, LCD_W - 1U, LCD_H - 1U);
	LCD_SPI_TransmitDMA_Pixels(s_lcd_back_buf, LCD_FB_PIXELS);
	LCD_CS_Set();

	/* 交换双缓冲角色：刚发送完成的缓冲成为新的front。 */
	tmp = s_lcd_front_buf;
	s_lcd_front_buf = s_lcd_back_buf;
	s_lcd_back_buf = tmp;
}

/**
************************************************************************
* @brief:      	LCD_Writ_Bus: LCD串行数据写入函数
* @param:      	dat - 要写入的串行数据
* @retval:     	void
* @details:    	将串行数据写入LCD，根据使用的通信方式选择使用软件模拟SPI（USE_ANALOG_SPI宏定义为真）或硬件SPI。
*               - 当USE_ANALOG_SPI宏定义为真时，通过GPIO控制SCLK、MOSI和CS信号，循环将8位数据写入。
*              - 当USE_ANALOG_SPI宏定义为假时，通过HAL_SPI_Transmit函数使用硬件SPI传输1字节数据。
************************************************************************
**/
void LCD_Writ_Bus(uint8_t dat) 
{	
	LCD_CS_Clr();
#if USE_ANALOG_SPI
	for(uint8_t i=0;i<8;i++) {			  
		LCD_SCLK_Clr();
		if(dat&0x80) {
		   LCD_MOSI_Set();
		} else {
		   LCD_MOSI_Clr();
		}
		LCD_SCLK_Set();
		dat<<=1;
	}
#else
	LCD_SPI_Transmit8(&dat, 1);
#endif	
  LCD_CS_Set();	
}
/**
************************************************************************
* @brief:      	LCD_WR_DATA8: 向LCD写入8位数据
* @param:      	dat - 要写入的8位数据
* @retval:     	void
* @details:    	调用LCD_Writ_Bus函数将8位数据写入LCD。
************************************************************************
**/
void LCD_WR_DATA8(uint8_t dat)
{
	LCD_Writ_Bus(dat);
}
/**
************************************************************************
* @brief:      	LCD_WR_DATA: 向LCD写入16位数据
* @param:      	dat - 要写入的16位数据
* @retval:     	void
* @details:    	调用LCD_Writ_Bus函数将16位数据的高8位和低8位分别写入LCD。
************************************************************************
**/
void LCD_WR_DATA(uint16_t dat)
{
	if (s_win_cx < LCD_W && s_win_cy < LCD_H)
	{
		s_lcd_back_buf[(uint32_t)s_win_cy * LCD_W + s_win_cx] = LCD_ColorToBuf(dat);
	}

	if (s_win_cx < s_win_x2)
	{
		s_win_cx++;
	}
	else
	{
		s_win_cx = s_win_x1;
		if (s_win_cy < s_win_y2)
		{
			s_win_cy++;
		}
	}
}
/**
************************************************************************
* @brief:      	LCD_WR_REG: 向LCD写入寄存器地址
* @param:      	dat - 要写入的寄存器地址
* @retval:     	void
* @details:    	通过调用LCD_Writ_Bus函数向LCD写入寄存器地址。
************************************************************************
**/
void LCD_WR_REG(uint8_t dat)
{
	LCD_DC_Clr();//写命令
	LCD_Writ_Bus(dat);
	LCD_DC_Set();//写数据
}
/**
************************************************************************
* @brief:      	LCD_Address_Set: 设置LCD显示区域的地址范围
* @param:      	x1, y1, x2, y2 - 显示区域的左上角和右下角坐标
* @retval:     	void
* @details:    	根据屏幕方向通过调用LCD_WR_REG和LCD_WR_DATA函数，设置LCD的列地址和行地址，然后写入储存器进行显示。
************************************************************************
**/
void LCD_Address_Set(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
{
	if (x1 >= LCD_W)
	{
		x1 = LCD_W - 1U;
	}
	if (x2 >= LCD_W)
	{
		x2 = LCD_W - 1U;
	}
	if (y1 >= LCD_H)
	{
		y1 = LCD_H - 1U;
	}
	if (y2 >= LCD_H)
	{
		y2 = LCD_H - 1U;
	}

	s_win_x1 = x1;
	s_win_y1 = y1;
	s_win_x2 = x2;
	s_win_y2 = y2;
	s_win_cx = x1;
	s_win_cy = y1;
}
/**
************************************************************************
* @brief:      	LCD_Fill: 在LCD指定区域填充指定颜色
* @param:      	xsta, ysta, xend, yend - 填充区域的左上角和右下角坐标
*              	color - 填充颜色
* @retval:     	void
* @details:    	通过调用LCD_Address_Set函数设置LCD显示区域的地址范围，然后在该范围内填充指定颜色。
************************************************************************
**/
void LCD_Fill(uint16_t xsta,uint16_t ysta,uint16_t xend,uint16_t yend,uint16_t color)
{          
	uint16_t x2;
	uint16_t y2;

	if (xsta >= LCD_W || ysta >= LCD_H || xend <= xsta || yend <= ysta)
	{
		return;
	}

	x2 = (xend > LCD_W) ? LCD_W : xend;
	y2 = (yend > LCD_H) ? LCD_H : yend;
	LCD_DMA2D_FillRect(s_lcd_back_buf, xsta, ysta, (uint16_t)(x2 - xsta), (uint16_t)(y2 - ysta), color);
}
/**
************************************************************************
* @brief:      	LCD_DrawPoint: 在LCD指定位置画点
* @param:      	x, y - 点的坐标
*              	color - 点的颜色
* @retval:     	void
* @details:    	通过调用LCD_Address_Set函数设置LCD显示区域的地址范围，然后在指定位置画点，颜色由color参数指定。
************************************************************************
**/
void LCD_DrawPoint(uint16_t x,uint16_t y,uint16_t color)
{
	if (x >= LCD_W || y >= LCD_H)
	{
		return;
	}

	s_lcd_back_buf[(uint32_t)y * LCD_W + x] = LCD_ColorToBuf(color);
} 
/**
************************************************************************
* @brief:      	LCD_DrawLine: 在LCD上画线
* @param:      	x1, y1 - 线的起始坐标
*              	x2, y2 - 线的结束坐标
*              	color - 线的颜色
* @retval:     	void
* @details:    	通过计算坐标增量，选择基本增量坐标轴，从起始坐标到结束坐标逐点画线，颜色由color参数指定。
************************************************************************
**/
void LCD_DrawLine(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2,uint16_t color)
{
	uint16_t t; 
	int xerr=0,yerr=0,delta_x,delta_y,distance;
	int incx,incy,uRow,uCol;
	delta_x=x2-x1; //计算坐标增量 
	delta_y=y2-y1;
	uRow=x1;//画线起点坐标
	uCol=y1;
	if(delta_x>0)incx=1; //设置单步方向 
	else if (delta_x==0)incx=0;//垂直线 
	else {incx=-1;delta_x=-delta_x;}
	if(delta_y>0)incy=1;
	else if (delta_y==0)incy=0;//水平线 
	else {incy=-1;delta_y=-delta_y;}
	if(delta_x>delta_y)distance=delta_x; //选取基本增量坐标轴 
	else distance=delta_y;
	for(t=0;t<distance+1;t++)
	{
		LCD_DrawPoint(uRow,uCol,color);//画点
		xerr+=delta_x;
		yerr+=delta_y;
		if(xerr>distance)
		{
			xerr-=distance;
			uRow+=incx;
		}
		if(yerr>distance)
		{
			yerr-=distance;
			uCol+=incy;
		}
	}
}
/**
************************************************************************
* @brief:      	LCD_DrawRectangle: 在LCD上画矩形
* @param:      	x1, y1 - 矩形的左上角坐标
*              	x2, y2 - 矩形的右下角坐标
*              	color - 矩形的颜色
* @retval:     	void
* @details:    	通过调用LCD_DrawLine函数画出矩形的四条边，颜色由color参数指定。
************************************************************************
**/
void LCD_DrawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,uint16_t color)
{
	LCD_DrawLine(x1,y1,x2,y1,color);
	LCD_DrawLine(x1,y1,x1,y2,color);
	LCD_DrawLine(x1,y2,x2,y2,color);
	LCD_DrawLine(x2,y1,x2,y2,color);
}
/**
************************************************************************
* @brief:      	Draw_Circle: 在LCD上画圆
* @param:      	x0, y0 - 圆心坐标
*              	r - 圆的半径
*              	color - 圆的颜色
* @retval:     	void
* @details:    	使用中点画圆法，以圆心(x0, y0)为中心，半径为r，在LCD上画出圆，
*              	颜色由color参数指定。
************************************************************************
**/
void Draw_Circle(uint16_t x0,uint16_t y0,uint8_t r,uint16_t color)
{
	int a,b;
	a=0;b=r;	  
	while(a<=b)
	{
		LCD_DrawPoint(x0-b,y0-a,color);             //3           
		LCD_DrawPoint(x0+b,y0-a,color);             //0           
		LCD_DrawPoint(x0-a,y0+b,color);             //1                
		LCD_DrawPoint(x0-a,y0-b,color);             //2             
		LCD_DrawPoint(x0+b,y0+a,color);             //4               
		LCD_DrawPoint(x0+a,y0-b,color);             //5
		LCD_DrawPoint(x0+a,y0+b,color);             //6 
		LCD_DrawPoint(x0-b,y0+a,color);             //7
		a++;
		if((a*a+b*b)>(r*r))//判断要画的点是否过远
		{
			b--;
		}
	}
}
/**
************************************************************************
* @brief:      	LCD_ShowChinese: 在LCD上显示汉字字符串
* @param:      	x, y - 起始坐标，显示汉字字符串的左上角坐标
*              	s - 要显示的汉字字符串，每个汉字占两个字节
*              	fc - 字体颜色
*              	bc - 背景颜色
*              	sizey - 汉字字体大小，支持12x12、16x16、24x24、32x32
*              	mode - 显示模式，1表示反色，0表示正常显示
* @retval:     	void
* @details:    	根据sizey选择字体大小，在LCD上显示汉字字符串，可以选择显示模式。
************************************************************************
**/
void LCD_ShowChinese(uint16_t x,uint16_t y,uint8_t *s,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode)
{
	while(*s!=0)
	{
		if(sizey==12) LCD_ShowChinese12x12(x,y,s,fc,bc,sizey,mode);
		else if(sizey==16) LCD_ShowChinese16x16(x,y,s,fc,bc,sizey,mode);
		else if(sizey==24) LCD_ShowChinese24x24(x,y,s,fc,bc,sizey,mode);
		else if(sizey==32) LCD_ShowChinese32x32(x,y,s,fc,bc,sizey,mode);
		else return;
		s+=2;
		x+=sizey;
	}
}
/**
************************************************************************
* @brief:      	LCD_ShowChinese12x12: 在LCD上显示12x12汉字
* @param:      	x, y - 起始坐标，显示汉字的左上角坐标
*              	s - 要显示的汉字，每个汉字占两个字节
*              	fc - 字体颜色
*              	bc - 背景颜色
*              	sizey - 汉字字体大小，支持12x12
*              	mode - 显示模式，1表示反色，0表示正常显示
* @retval:     	void
* @details:    	在LCD上显示12x12汉字，可以选择显示模式。
************************************************************************
**/
void LCD_ShowChinese12x12(uint16_t x,uint16_t y,uint8_t *s,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode)
{
	uint8_t i,j,m=0;
	uint16_t k;
	uint16_t HZnum;//汉字数目
	uint16_t TypefaceNum;//一个字符所占字节大小
	uint16_t x0=x;
	TypefaceNum=(sizey/8+((sizey%8)?1:0))*sizey;
	                         
	HZnum=sizeof(tfont12)/sizeof(typFNT_GB12);	//统计汉字数目
	for(k=0;k<HZnum;k++) 
	{
		if((tfont12[k].Index[0]==*(s))&&(tfont12[k].Index[1]==*(s+1)))
		{ 	
			LCD_Address_Set(x,y,x+sizey-1,y+sizey-1);
			for(i=0;i<TypefaceNum;i++)
			{
				for(j=0;j<8;j++)
				{	
					if(!mode)//非叠加方式
					{
						if(tfont12[k].Msk[i]&(0x01<<j))LCD_WR_DATA(fc);
						else LCD_WR_DATA(bc);
						m++;
						if(m%sizey==0)
						{
							m=0;
							break;
						}
					}
					else//叠加方式
					{
						if(tfont12[k].Msk[i]&(0x01<<j))	LCD_DrawPoint(x,y,fc);//画一个点
						x++;
						if((x-x0)==sizey)
						{
							x=x0;
							y++;
							break;
						}
					}
				}
			}
		}				  	
		continue;  //查找到对应点阵字库立即退出，防止多个汉字重复取模带来影响
	}
} 
/**
************************************************************************
* @brief:      	LCD_ShowChinese16x16: 在LCD上显示16x16汉字
* @param:      	x, y - 起始坐标，显示汉字的左上角坐标
*              	s - 要显示的汉字，每个汉字占两个字节
*              	fc - 字体颜色
*              	bc - 背景颜色
*              	sizey - 汉字字体大小，支持16x16
*              	mode - 显示模式，1表示反色，0表示正常显示
* @retval:     	void
* @details:    	在LCD上显示16x16汉字，可以选择显示模式。
************************************************************************
**/
void LCD_ShowChinese16x16(uint16_t x,uint16_t y,uint8_t *s,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode)
{
	uint8_t i,j,m=0;
	uint16_t k;
	uint16_t HZnum;//汉字数目
	uint16_t TypefaceNum;//一个字符所占字节大小
	uint16_t x0=x;
  TypefaceNum=(sizey/8+((sizey%8)?1:0))*sizey;
	HZnum=sizeof(tfont16)/sizeof(typFNT_GB16);	//统计汉字数目
	for(k=0;k<HZnum;k++) 
	{
		if ((tfont16[k].Index[0]==*(s))&&(tfont16[k].Index[1]==*(s+1)))
		{ 	
			LCD_Address_Set(x,y,x+sizey-1,y+sizey-1);
			for(i=0;i<TypefaceNum;i++)
			{
				for(j=0;j<8;j++)
				{	
					if(!mode)//非叠加方式
					{
						if(tfont16[k].Msk[i]&(0x01<<j))LCD_WR_DATA(fc);
						else LCD_WR_DATA(bc);
						m++;
						if(m%sizey==0)
						{
							m=0;
							break;
						}
					}
					else//叠加方式
					{
						if(tfont16[k].Msk[i]&(0x01<<j))	LCD_DrawPoint(x,y,fc);//画一个点
						x++;
						if((x-x0)==sizey)
						{
							x=x0;
							y++;
							break;
						}
					}
				}
			}
		}				  	
		continue;  //查找到对应点阵字库立即退出，防止多个汉字重复取模带来影响
	}
} 
/**
************************************************************************
* @brief:      	LCD_ShowChinese24x24: 在LCD上显示24x24汉字
* @param:      	x, y - 起始坐标，显示汉字的左上角坐标
*              	s - 要显示的汉字，每个汉字占两个字节
*              	fc - 字体颜色
*              	bc - 背景颜色
*              	sizey - 汉字字体大小，支持24x24
*              	mode - 显示模式，1表示反色，0表示正常显示
* @retval:     	void
* @details:    	在LCD上显示24x24汉字，可以选择显示模式。
************************************************************************
**/
void LCD_ShowChinese24x24(uint16_t x,uint16_t y,uint8_t *s,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode)
{
	uint8_t i,j,m=0;
	uint16_t k;
	uint16_t HZnum;//汉字数目
	uint16_t TypefaceNum;//一个字符所占字节大小
	uint16_t x0=x;
	TypefaceNum=(sizey/8+((sizey%8)?1:0))*sizey;
	HZnum=sizeof(tfont24)/sizeof(typFNT_GB24);	//统计汉字数目
	for(k=0;k<HZnum;k++) 
	{
		if ((tfont24[k].Index[0]==*(s))&&(tfont24[k].Index[1]==*(s+1)))
		{ 	
			LCD_Address_Set(x,y,x+sizey-1,y+sizey-1);
			for(i=0;i<TypefaceNum;i++)
			{
				for(j=0;j<8;j++)
				{	
					if(!mode)//非叠加方式
					{
						if(tfont24[k].Msk[i]&(0x01<<j))LCD_WR_DATA(fc);
						else LCD_WR_DATA(bc);
						m++;
						if(m%sizey==0)
						{
							m=0;
							break;
						}
					}
					else//叠加方式
					{
						if(tfont24[k].Msk[i]&(0x01<<j))	LCD_DrawPoint(x,y,fc);//画一个点
						x++;
						if((x-x0)==sizey)
						{
							x=x0;
							y++;
							break;
						}
					}
				}
			}
		}				  	
		continue;  //查找到对应点阵字库立即退出，防止多个汉字重复取模带来影响
	}
} 
/**
************************************************************************
* @brief:      	LCD_ShowChinese32x32: 在LCD上显示32x32汉字
* @param:      	x, y - 起始坐标，显示汉字的左上角坐标
*              	s - 要显示的汉字，每个汉字占两个字节
*              	fc - 字体颜色
*              	bc - 背景颜色
*              	sizey - 汉字字体大小，支持32x32
*              	mode - 显示模式，1表示反色，0表示正常显示
* @retval:     	void
* @details:    	在LCD上显示32x32汉字，可以选择显示模式。
************************************************************************
**/
void LCD_ShowChinese32x32(uint16_t x,uint16_t y,uint8_t *s,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode)
{
	uint8_t i,j,m=0;
	uint16_t k;
	uint16_t HZnum;//汉字数目
	uint16_t TypefaceNum;//一个字符所占字节大小
	uint16_t x0=x;
	TypefaceNum=(sizey/8+((sizey%8)?1:0))*sizey;
	HZnum=sizeof(tfont32)/sizeof(typFNT_GB32);	//统计汉字数目
	for(k=0;k<HZnum;k++) 
	{
		if ((tfont32[k].Index[0]==*(s))&&(tfont32[k].Index[1]==*(s+1)))
		{ 	
			LCD_Address_Set(x,y,x+sizey-1,y+sizey-1);
			for(i=0;i<TypefaceNum;i++)
			{
				for(j=0;j<8;j++)
				{	
					if(!mode)//非叠加方式
					{
						if(tfont32[k].Msk[i]&(0x01<<j))LCD_WR_DATA(fc);
						else LCD_WR_DATA(bc);
						m++;
						if(m%sizey==0)
						{
							m=0;
							break;
						}
					}
					else//叠加方式
					{
						if(tfont32[k].Msk[i]&(0x01<<j))	LCD_DrawPoint(x,y,fc);//画一个点
						x++;
						if((x-x0)==sizey)
						{
							x=x0;
							y++;
							break;
						}
					}
				}
			}
		}				  	
		continue;  //查找到对应点阵字库立即退出，防止多个汉字重复取模带来影响
	}
}
/**
************************************************************************
* @brief:      	LCD_ShowChar: 在LCD上显示单个字符
* @param:      	x, y - 起始坐标，显示字符的左上角坐标
*              	num - 要显示的字符的ASCII码值
*              	fc - 字体颜色
*              	bc - 背景颜色
*              	sizey - 字体大小，支持12x6, 16x8, 24x12, 32x16
*              	mode - 显示模式，1表示反色，0表示正常显示
* @retval:     	void
* @details:    	在LCD上显示单个字符，可以选择显示模式和不同的字体大小。
************************************************************************
**/
void LCD_ShowChar(uint16_t x,uint16_t y,uint8_t num,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode)
{
	uint8_t temp,sizex,t,m=0;
	uint16_t i,TypefaceNum;//一个字符所占字节大小
	uint16_t x0=x;
	sizex=sizey/2;
	TypefaceNum=(sizex/8+((sizex%8)?1:0))*sizey;
	num=num-' ';    //得到偏移后的值
	LCD_Address_Set(x,y,x+sizex-1,y+sizey-1);  //设置光标位置 
	for(i=0;i<TypefaceNum;i++)
	{ 
		if(sizey==12)temp=ascii_1206[num][i];		       //调用6x12字体
		else if(sizey==16)temp=ascii_1608[num][i];		 //调用8x16字体
		else if(sizey==24)temp=ascii_2412[num][i];		 //调用12x24字体
		else if(sizey==32)temp=ascii_3216[num][i];		 //调用16x32字体
		else return;
		for(t=0;t<8;t++)
		{
			if(!mode)//非叠加模式
			{
				if(temp&(0x01<<t))LCD_WR_DATA(fc);
				else LCD_WR_DATA(bc);
				m++;
				if(m%sizex==0)
				{
					m=0;
					break;
				}
			}
			else//叠加模式
			{
				if(temp&(0x01<<t))LCD_DrawPoint(x,y,fc);//画一个点
				x++;
				if((x-x0)==sizex)
				{
					x=x0;
					y++;
					break;
				}
			}
		}
	}   	 	  
}
/**
************************************************************************
* @brief:      	LCD_ShowString: 在LCD上显示字符串
* @param:      	x, y - 起始坐标，显示字符串的左上角坐标
*              	p - 要显示的字符串，以'\0'结尾
*              	fc - 字体颜色
*              	bc - 背景颜色
*              	sizey - 字体大小，支持12x6, 16x8, 24x12, 32x16
*              	mode - 显示模式，1表示反色，0表示正常显示
* @retval:     	void
* @details:    	在LCD上显示字符串，可以选择显示模式和不同的字体大小。
************************************************************************
**/
void LCD_ShowString(uint16_t x,uint16_t y,const uint8_t *p,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode)
{         
	while(*p!='\0')
	{       
		LCD_ShowChar(x,y,*p,fc,bc,sizey,mode);
		x+=sizey/2;
		p++;
	}  
}
/**
************************************************************************
* @brief:      	mypow: 计算m的n次方
* @param:      	m - 底数
*              	n - 指数
* @retval:     	uint32_t - m的n次方
* @details:    	计算m的n次方，返回结果。
************************************************************************
**/
uint32_t mypow(uint8_t m,uint8_t n)
{
	uint32_t result=1;	 
	while(n--)result*=m;
	return result;
}
/**
************************************************************************
* @brief:      	LCD_ShowIntNum: 在LCD上显示整数数字
* @param:      	x - x坐标
*              	y - y坐标
*              	num - 要显示的整数
*              	len - 数字显示的位数
*              	fc - 字体颜色
*              	bc - 背景颜色
*              	sizey - 字体大小
* @retval:     	void
* @details:    	在LCD上显示整数数字，支持设置显示的位数、字体颜色、背景颜色和字体大小。
************************************************************************
**/
void LCD_ShowIntNum(uint16_t x,uint16_t y,uint16_t num,uint8_t len,uint16_t fc,uint16_t bc,uint8_t sizey)
{         	
	uint8_t t,temp;
	uint8_t enshow=0;
	uint8_t sizex=sizey/2;
	for(t=0;t<len;t++)
	{
		temp=(num/mypow(10,len-t-1))%10;
		if(enshow==0&&t<(len-1))
		{
			if(temp==0)
			{
				LCD_ShowChar(x+t*sizex,y,' ',fc,bc,sizey,0);
				continue;
			}else enshow=1; 
		 	 
		}
	 	LCD_ShowChar(x+t*sizex,y,temp+48,fc,bc,sizey,0);
	}
} 
/**
************************************************************************
* @brief:      	LCD_ShowFloatNum: 在LCD上显示格式化的浮点数，支持负数
* @param:      	x - x坐标
*              	y - y坐标
*              	num - 要显示的浮点数
*              	len - 整数位数
*              	decimal - 小数位数
*              	fc - 字的颜色
*              	bc - 背景颜色
*              	sizey - 字体大小
* @retval:     	void
* @details:    	在LCD上显示格式化的浮点数，支持设置整数位数、小数位数、字体颜色、背景颜色和字体大小。
************************************************************************
**/
void LCD_ShowFloatNum(uint16_t x, uint16_t y, float num, uint8_t len, uint8_t decimal, uint16_t fc, uint16_t bc, uint8_t sizey)
{
		int16_t num_int;
		uint8_t t, temp, sizex;
    sizex = sizey / 2;
    num_int = num * mypow(10, decimal);

    if (num < 0)
    {
        LCD_ShowChar(x, y, '-', fc, bc, sizey, 0);
        num_int = -num_int;
        x += sizex;
        len++;
    }
    else
    {
        LCD_ShowChar(x, y, ' ', fc, bc, sizey, 0);
        num_int = num_int;
        x += sizex;
        len++;
    }

    // 在更新数字时刷新显示的位置
    LCD_Fill(x, y, x + len * sizex + decimal + 1, y + sizey + 1, bc);

    for (t = 0; t < len; t++)
    {
        if (t == (len - decimal))
        {
            LCD_ShowChar(x + (len - decimal) * sizex, y, '.', fc, bc, sizey, 0);
            t++;
            len += 1;
        }
        temp = ((num_int / mypow(10, len - t - 1)) % 10) + '0';
        LCD_ShowChar(x + t * sizex, y, temp, fc, bc, sizey, 0);
    }
}
/**
************************************************************************
* @brief:      	LCD_ShowFloatNum1: 在LCD上显示格式化的浮点数，不支持负数
* @param:      	x - x坐标
*              	y - y坐标
*              	num - 要显示的浮点数
*              	len - 整数位数
*              	decimal - 小数位数
*              	fc - 字的颜色
*              	bc - 背景颜色
*              	sizey - 字体大小
* @retval:     	void
* @details:    	在LCD上显示格式化的浮点数，支持设置整数位数、小数位数、字体颜色、背景颜色和字体大小。
************************************************************************
**/
void LCD_ShowFloatNum1(uint16_t x, uint16_t y, float num, uint8_t len, uint8_t decimal, uint16_t fc, uint16_t bc, uint8_t sizey)
{
		int16_t num_int;
		uint8_t t, temp, sizex;
    sizex = sizey / 2;
    num_int = num * mypow(10, decimal);


		num_int = num_int;
		x += sizex;
		len++;

    // 在更新数字时刷新显示的位置
    LCD_Fill(x, y, x + len * sizex + decimal + 1, y + sizey + 1, bc);

    for (t = 0; t < len; t++)
    {
        if (t == (len - decimal))
        {
            LCD_ShowChar(x + (len - decimal) * sizex, y, '.', fc, bc, sizey, 0);
            t++;
            len += 1;
        }
        temp = ((num_int / mypow(10, len - t - 1)) % 10) + '0';
        LCD_ShowChar(x + t * sizex, y, temp, fc, bc, sizey, 0);
    }
}
/**
************************************************************************
* @brief:      	LCD_ShowFloatNum1: 在LCD上显示格式化的浮点数，不支持负数
* @param:      	x - x坐标
*              	y - y坐标
*              	num - 要显示的浮点数
*              	len - 整数位数
*              	decimal - 小数位数
*              	fc - 字的颜色
*              	bc - 背景颜色
*              	sizey - 字体大小
* @retval:     	void
* @details:    	在LCD上显示格式化的浮点数，支持设置整数位数、小数位数、字体颜色、背景颜色和字体大小。
************************************************************************
**/
void LCD_ShowPicture(uint16_t x,uint16_t y,uint16_t length,uint16_t width,const uint8_t pic[])
{
	uint16_t i,j;
	uint32_t k=0;
	if (x >= LCD_W || y >= LCD_H)
	{
		return;
	}

	if ((uint32_t)x + length > LCD_W)
	{
		length = (uint16_t)(LCD_W - x);
	}
	if ((uint32_t)y + width > LCD_H)
	{
		width = (uint16_t)(LCD_H - y);
	}

	for(i=0;i<length;i++)
	{
		for(j=0;j<width;j++)
		{
			s_lcd_back_buf[(uint32_t)(y + j) * LCD_W + (x + i)] =
				(uint16_t)(((uint16_t)pic[k * 2U + 1U] << 8) | pic[k * 2U]);
			k++;
		}
	}			
}
/**
************************************************************************
* @brief:      	LCD_Init: LCD初始化函数
* @param:      	void
* @details:    	执行LCD的初始化过程，包括复位、背光控制、寄存器配置等
* @retval:     	void
************************************************************************
**/
void LCD_Init(void)
{
	uint32_t i;
	
	LCD_RES_Clr();//复位
	osDelay(100);
	LCD_RES_Set();
	osDelay(100);
	
	LCD_BLK_Set();//打开背光
	osDelay(100);
	
	//************* Start Initial Sequence **********//
	LCD_WR_REG(0x11); //Sleep out 
	osDelay(120);         //Delay 120ms
	//************* Start Initial Sequence **********// 
	LCD_WR_REG(0x36);
	if(USE_HORIZONTAL==0)LCD_WR_DATA8(0x00);
	else if(USE_HORIZONTAL==1)LCD_WR_DATA8(0xC0);
	else if(USE_HORIZONTAL==2)LCD_WR_DATA8(0x70);
	else LCD_WR_DATA8(0xA0);

	LCD_WR_REG(0x3A);			
	LCD_WR_DATA8(0x05);

	LCD_WR_REG(0xB2);			
	LCD_WR_DATA8(0x0C);
	LCD_WR_DATA8(0x0C); 
	LCD_WR_DATA8(0x00); 
	LCD_WR_DATA8(0x33); 
	LCD_WR_DATA8(0x33); 			

	LCD_WR_REG(0xB7);			
	LCD_WR_DATA8(0x35);

	LCD_WR_REG(0xBB);			
	LCD_WR_DATA8(0x32); //Vcom=1.35V
					
	LCD_WR_REG(0xC2);
	LCD_WR_DATA8(0x01);

	LCD_WR_REG(0xC3);			
	LCD_WR_DATA8(0x15); //GVDD=4.8V  颜色深度
				
	LCD_WR_REG(0xC4);			
	LCD_WR_DATA8(0x20); //VDV, 0x20:0v

	LCD_WR_REG(0xC6);			
	LCD_WR_DATA8(0x0F); //0x0F:60Hz        	

	LCD_WR_REG(0xD0);			
	LCD_WR_DATA8(0xA4);
	LCD_WR_DATA8(0xA1); 

	LCD_WR_REG(0xE0);
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

	LCD_WR_REG(0xE1);     
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
	LCD_WR_REG(0x21); 

	LCD_WR_REG(0x29);

	for (i = 0U; i < LCD_FB_PIXELS; i++)
	{
		s_lcd_front_buf[i] = LCD_ColorToBuf(BLACK);
		s_lcd_back_buf[i] = LCD_ColorToBuf(BLACK);
	}

	LCD_Address_Set(0U, 0U, LCD_W - 1U, LCD_H - 1U);
}

