#ifndef _SPI_LCD_CONFIG_H_
#define _SPI_LCD_CONFIG_H_

#include "asm/spi.h"

#define lcd_d(fmt, ...)	printf("[LCD DEBUG]: "fmt, ##__VA_ARGS__)
#define lcd_w(fmt, ...)	printf("[LCD WARNING]: "fmt, ##__VA_ARGS__)
#define lcd_e(fmt, ...)	printf("[LCD ERROR]: "fmt, ##__VA_ARGS__)

struct lcd_spi_platform_data {
    u32  pin_reset;
    u32  pin_cs;
    u32  pin_rs;
    u32  pin_bl;
    spi_dev spi_cfg;
    const struct spi_platform_data *spi_pdata;
};

typedef struct {
    u8 cmd;		// 地址
    u8 cnt;		// 数据个数
    u8 dat[64];	// 数据
} InitCode;
extern const InitCode LcdInit_code[];
extern const uint8_t LcdInit_code_len;

#define REGFLAG_DELAY		0xEE

#ifdef CONFIG_CPU_BD19
#define LCD_RGB_ARR 1 // 0为RGB, 1为BGR
#define LCD_GRAM    3
#endif
#ifdef CONFIG_CPU_BR23
#define LCD_RGB_ARR 1 // 0为RGB, 1为BGR
#define LCD_GRAM    3
#endif
#ifdef CONFIG_CPU_BR25
#define LCD_RGB_ARR 0 // 0为RGB, 1为BGR

// 3和5互为旋转180度
// 3和7互为左右镜像
// 5和7互为上下镜像
#define LCD_GRAM    5

#endif

#define LCD_TYPE_DEFAULT_TEST           0       // 
#define LCD_TYPE_CHENGYI_YUAN240240     1       // 诚艺圆屏240x240
#define LCD_TYPE_CHENGYI_FANG320240     2       // 诚艺方屏320x240
#define LCD_TYPE_SEL                    LCD_TYPE_CHENGYI_FANG320240

#if LCD_TYPE_SEL == LCD_TYPE_CHENGYI_YUAN240240
#undef LCD_RGB_ARR
#define LCD_RGB_ARR                     1
#undef LCD_GRAM
#define LCD_GRAM                        3
#elif LCD_TYPE_SEL == LCD_TYPE_CHENGYI_FANG320240
#undef LCD_RGB_ARR
#define LCD_RGB_ARR                     0
#undef LCD_GRAM
#define LCD_GRAM                        1
#endif


void lcd_spi_dev_init(void);
void spi_dma_wait_finish(void);
void spi_dma_wait_isr_finish(void);
void lcd_spi_draw_point(int x, int y, uint16_t rgb565);
void lcd_spi_draw_area(int xsta, int ysta, int xend, int yend, uint16_t *rgb565);

#endif