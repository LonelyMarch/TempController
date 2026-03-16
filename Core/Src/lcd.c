#include "lcd.h"
#include "cmsis_os.h"

/**
************************************************************************
* @brief:      	LCD_Writ_Bus: LCD串行数据写入函数
* @param:      	dat - 要写入的串行数据
* @retval:     	void
* @details:    	将串行数据写入LCD，根据使用的通信方式选择使用软件模拟SPI（USE_ANALOG_SPI宏定义为真）或硬件SPI。
*               - 当USE_ANALOG_SPI宏定义为真时，通过GPIO控制SCLK、MOSI和CS信号，循环将8位数据写入。
*               - 当USE_ANALOG_SPI宏定义为假时，通过HAL_SPI_Transmit函数使用硬件SPI传输1字节数据。
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
	HAL_SPI_Transmit(&hspi1, &dat, 1, 0xffff);
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
	LCD_Writ_Bus(dat>>8);
	LCD_Writ_Bus(dat);
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
	if(USE_HORIZONTAL==0)
	{
		LCD_WR_REG(0x2a);//列地址设置
		LCD_WR_DATA(x1);
		LCD_WR_DATA(x2);
		LCD_WR_REG(0x2b);//行地址设置
		LCD_WR_DATA(y1+20);
		LCD_WR_DATA(y2+20);
		LCD_WR_REG(0x2c);//储存器写
	}
	else if(USE_HORIZONTAL==1)
	{
		LCD_WR_REG(0x2a);//列地址设置
		LCD_WR_DATA(x1);
		LCD_WR_DATA(x2);
		LCD_WR_REG(0x2b);//行地址设置
		LCD_WR_DATA(y1+20);
		LCD_WR_DATA(y2+20);
		LCD_WR_REG(0x2c);//储存器写
	}
	else if(USE_HORIZONTAL==2)
	{
		LCD_WR_REG(0x2a);//列地址设置
		LCD_WR_DATA(x1+20);
		LCD_WR_DATA(x2+20);
		LCD_WR_REG(0x2b);//行地址设置
		LCD_WR_DATA(y1);
		LCD_WR_DATA(y2);
		LCD_WR_REG(0x2c);//储存器写
	}
	else
	{
		LCD_WR_REG(0x2a);//列地址设置
		LCD_WR_DATA(x1+20);
		LCD_WR_DATA(x2+20);
		LCD_WR_REG(0x2b);//行地址设置
		LCD_WR_DATA(y1);
		LCD_WR_DATA(y2);
		LCD_WR_REG(0x2c);//储存器写
	}
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
	uint16_t i,j; 
	LCD_Address_Set(xsta,ysta,xend-1,yend-1);//设置显示范围
	for(i=ysta;i<yend;i++)
	{													   	 	
		for(j=xsta;j<xend;j++)
		{
			LCD_WR_DATA(color);
		}
	} 					  	    
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
	LCD_Address_Set(x,y,x,y);//设置光标位置 
	LCD_WR_DATA(color);
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
* @brief:      	LCD_Init: LCD初始化函数
* @param:      	void
* @details:    	执行LCD的初始化过程，包括复位、背光控制、寄存器配置等
* @retval:     	void
************************************************************************
**/
void LCD_Init(void)
{
	
	LCD_RES_Clr();//复位
	osDelay(100);
	LCD_RES_Set();
	osDelay(100);

	LCD_BLK_Set();//打开背光
	osDelay(100);

	//************* Start Initial Sequence **********//
	LCD_WR_REG(0x11); //Sleep out 
	osDelay(120);              //Delay 120ms
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
}

