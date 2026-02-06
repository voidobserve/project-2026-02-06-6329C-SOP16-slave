#include "app_config.h"
#include "includes.h"
#include "gpio.h"
#include "lcd_spi_config.h"
#include "sc_lcd.h"
#include "debug.h"

#define SPI_RS_CMD          0
#define SPI_RS_DAT          1
#define SPI_CS_EN           0
#define SPI_CS_DIS          1

#define LCD_WIDTH           LCD_SCREEN_WIDTH  // (320)
#define LCD_HIGHT           LCD_SCREEN_HEIGHT // (240)
// #define LCD_SPI_INDEX_SEL   1
// #define LCD_SPI_PIN_RST     IO_PORTC_02
// #define LCD_SPI_PIN_CS      IO_PORTC_03
// #define LCD_SPI_PIN_RS      IO_PORTC_01
// #define LCD_SPI_PIN_BL      IO_PORTA_00

#define SPI_INTERRUPT_ENABLE    1
#define SPI_FULL_TEST_AFT_INIT  0

#if	TCFG_HW_SPI1_ENABLE
const struct spi_platform_data spi1_p_data = {
    .port = TCFG_HW_SPI1_PORT,
    .mode = TCFG_HW_SPI1_MODE,
    .clk  = TCFG_HW_SPI1_BAUD,//24000000,//
    .role = TCFG_HW_SPI1_ROLE,
};
#endif

const struct lcd_spi_platform_data lcd_spi_data = {
    .pin_reset	= LCD_SPI_PIN_RST,
    .pin_cs		= LCD_SPI_PIN_CS,
    .pin_rs		= LCD_SPI_PIN_RS,
    .pin_bl     = LCD_SPI_PIN_BL,
#if LCD_SPI_INDEX_SEL == 1
    .spi_cfg	= SPI1,
    .spi_pdata  = &spi1_p_data,
#elif LCD_SPI_INDEX_SEL == 2
    .spi_cfg	= SPI2,
    .spi_pdata  = &spi2_p_data,
#endif
};

struct lcd_spi_platform_data *spi_dat = (struct lcd_spi_platform_data *)(&lcd_spi_data);
static bool spi_pnd = false;
OS_SEM display_sem;

#if LCD_TYPE_SEL == LCD_TYPE_DEFAULT_TEST
const InitCode LcdInit_code[] = {
#ifdef CONFIG_CPU_BD19
    {0x01, 0},				// soft reset
    {REGFLAG_DELAY, 120},	// delay 120ms
    {0x11, 0},				// sleep out
    {REGFLAG_DELAY, 120},

    /*  Power control B (CFh)  */
    {0xCF, 3, {0x00, 0xC1, 0x30}},
    /*  Power on sequence control (EDh) */
    {0xED, 4, {0x64, 0x03, 0x12, 0x81}},
    /*  Driver timing control A (E8h) */
    {0xE8, 3, {0x85, 0x10, 0x78}},
    /*  Power control A (CBh) */
    {0xCB, 5, {0x39, 0x2C, 0x00, 0x34, 0x02}},
    /* Pump ratio control (F7h) */
    {0xF7, 1, {0x20}},
    /* Driver timing control B */
    {0xEA, 2, {0x00, 0x00}},
    /* Power Control 1 (C0h) */
    {0xC0, 1, {0x21}},
    /* Power Control 2 (C1h) */
    {0xC1, 1, {0x11}},
    /* VCOM Control 1 (C5h) */
    {0xC5, 2, {0x2D, 0x33}},
    /*  VCOM Control 2 (C7h)  */
    // {0xC7, 1, {0XC0}},
    /* memory access control set */
    {0x36, 1, {0x00}},
    {0x3A, 1, {0x55}},
    /* Frame Rate Control (In Normal Mode/Full Colors) (B1h) */
    {0xB1, 2, {0x00, 0x17}},
    /*  Display Function Control (B6h) */
    {0xB6, 2, {0x0A, 0xA2}},
    {0xF6, 2, {0x01, 0x30}},
    /* Enable 3G (F2h) */
    {0xF2, 1, {0x00}},
    /* Gamma Set (26h) */
    {0x26, 1, {0x01}},
    /* Positive Gamma Correction */
    {0xe0, 14, {0xd0, 0x00, 0x02, 0x07, 0x0b, 0x1a, 0x31, 0x54, 0x40, 0x29, 0x12, 0x12, 0x12, 0x17}},
    /* Negative Gamma Correction (E1h) */
    {0xe1, 14, {0xd0, 0x00, 0x02, 0x07, 0x05, 0x25, 0x2d, 0x44, 0x45, 0x1c, 0x18, 0x16, 0x1c, 0x1d}},
    /* Sleep Out (11h)  */
    {0x11, 0}, // Exit Sleep
    {REGFLAG_DELAY, 120}, // ILI9341_Delay(0xAFFf<<2);
    /* Display ON (29h) */
    {0x29, 0}, // Display on
    {0x2c, 0},
#endif
#ifdef CONFIG_CPU_BR23
    {0x01, 0},				// soft reset
    {REGFLAG_DELAY, 120},	// delay 120ms
    {0x11, 0},				// sleep out
    {REGFLAG_DELAY, 120},

    /*  Power control B (CFh)  */
    {0xCF, 3, {0x00, 0xC1, 0x30}},
    /*  Power on sequence control (EDh) */
    {0xED, 4, {0x64, 0x03, 0x12, 0x81}},
    /*  Driver timing control A (E8h) */
    {0xE8, 3, {0x85, 0x10, 0x78}},
    /*  Power control A (CBh) */
    {0xCB, 5, {0x39, 0x2C, 0x00, 0x34, 0x02}},
    /* Pump ratio control (F7h) */
    {0xF7, 1, {0x20}},
    /* Driver timing control B */
    {0xEA, 2, {0x00, 0x00}},
    /* Power Control 1 (C0h) */
    {0xC0, 1, {0x21}},
    /* Power Control 2 (C1h) */
    {0xC1, 1, {0x11}},
    /* VCOM Control 1 (C5h) */
    {0xC5, 2, {0x2D, 0x33}},
    /*  VCOM Control 2 (C7h)  */
    // {0xC7, 1, {0XC0}},
    /* memory access control set */
    {0x36, 1, {0x00}},
    {0x3A, 1, {0x55}},
    /* Frame Rate Control (In Normal Mode/Full Colors) (B1h) */
    {0xB1, 2, {0x00, 0x17}},
    /*  Display Function Control (B6h) */
    {0xB6, 2, {0x0A, 0xA2}},
    {0xF6, 2, {0x01, 0x30}},
    /* Enable 3G (F2h) */
    {0xF2, 1, {0x00}},
    /* Gamma Set (26h) */
    {0x26, 1, {0x01}},
    /* Positive Gamma Correction */
    {0xe0, 14, {0xd0, 0x00, 0x02, 0x07, 0x0b, 0x1a, 0x31, 0x54, 0x40, 0x29, 0x12, 0x12, 0x12, 0x17}},
    /* Negative Gamma Correction (E1h) */
    {0xe1, 14, {0xd0, 0x00, 0x02, 0x07, 0x05, 0x25, 0x2d, 0x44, 0x45, 0x1c, 0x18, 0x16, 0x1c, 0x1d}},
    /* Sleep Out (11h)  */
    {0x11, 0}, // Exit Sleep
    {REGFLAG_DELAY, 120}, // ILI9341_Delay(0xAFFf<<2);
    /* Display ON (29h) */
    {0x29, 0}, // Display on
    {0x2c, 0},
#endif
#ifdef CONFIG_CPU_BR25
    { REGFLAG_DELAY,  200 },

    { 0xFD, 2, { 0x06, 0x08 } },
    { 0x61, 2, { 0x07, 0x07 } },
    { 0x73, 1, { 0x70 } },
    { 0x73, 1, { 0x00 } },
    //bias
    { 0x62, 3, { 0x00, 0x44, 0x40 } },
    { 0x63, 4, { 0x41, 0x07, 0x12, 0x12 } },
    //VSP
    { 0x65, 3, { 0x09, 0x17, 0x21 } },
    //VSN
    { 0x66, 3, { 0x09, 0x17, 0x21 } },
    //add source_neg_time
    { 0x67, 2, { 0x20, 0x40 } },
    //gamma vap/van
    { 0x68, 4, { 0x90, 0x30, 0x1C, 0x27 } },
    { 0xb1, 3, { 0x0F, 0x02, 0x01 } },
    { 0xB4, 1, { 0x01 } },
    ////porch
    { 0xB5, 4, { 0x02, 0x02, 0x0a, 0x14 } },
    { 0xB6, 5, { 0x44, 0x01, 0x9f, 0x00, 0x02 } },
    ////gamme sel
    { 0xdf, 1, { 0x11 } },
    { 0xE2, 6, { 0x0d, 0x0F, 0x11, 0x33, 0x36, 0x3f } },
    { 0xE5, 6, { 0x3f, 0x37, 0x33, 0x12, 0x10, 0x04 } },
    { 0xE1, 2, { 0x2C, 0x74 } },
    { 0xE4, 2, { 0x74, 0x2B } },
    { 0xE0, 8, { 0x08, 0x07, 0x0D, 0x13, 0x11, 0x13, 0x0E, 0x14 } },
    { 0xE3, 8, { 0x17, 0x0F, 0x14, 0x11, 0x13, 0x0C, 0x06, 0x04 } },
    //GAMMA---------------------
    { 0xE6, 2, { 0x00, 0xff } },
    { 0xE7, 6, { 0x01, 0x04, 0x03, 0x03, 0x00, 0x12 } },
    { 0xE8, 3, { 0x00, 0x70, 0x00 } },
    ////gate
    { 0xEc, 1, { 0x52 } },
    { 0xF1, 3, { 0x01, 0x01, 0x02 } },
    { 0xF6, 4, { 0x01, 0x30, 0x00, 0x00 } },
    { 0xfd, 2, { 0xfa, 0xfc } },
    { 0x3a, 1, { 0x55 } },
    { 0x35, 1, { 0x00 } },
    { 0x36, 1, { 0x00 } },
    { 0x11, 0 },
    { REGFLAG_DELAY,  200 },
    { 0x29, 0 },
    { REGFLAG_DELAY,  20 },
#endif
};
const uint8_t LcdInit_code_len = ARRAY_SIZE(LcdInit_code);
#endif

void spi_dma_wait_finish(void)
{
    if (spi_pnd)
    {
        while (!spi_get_pending(spi_dat->spi_cfg)) {
            wdt_clear();
        }
        spi_clear_pending(spi_dat->spi_cfg);
        spi_pnd = false;
    }
}

void spi_dma_send_byte(u8 dat)
{
    int err = 0;
    u32 _dat __attribute__((aligned(4))) = 0;

    ((u8 *)(&_dat))[0] = dat;

    if (spi_dat) {
        spi_dma_wait_finish();
        err = spi_dma_send(spi_dat->spi_cfg, &_dat, 1);
        spi_pnd = false;
    }

    if (err < 0) {
        lcd_e("spi dma send byte timeout\n");
    }
}

#if SPI_INTERRUPT_ENABLE
__attribute__((interrupt("")))
AT_VOLATILE_RAM_CODE
static void spi_isr()
{
    static int i = 0;
    if (spi_get_pending(spi_dat->spi_cfg)) {
        // spi_clear_pending(spi_dat->spi_cfg);
        spi_set_ie(spi_dat->spi_cfg, 0);
        spi_pnd = false;
        gpio_direction_output((u32)spi_dat->pin_cs, SPI_CS_DIS);
        os_sem_post(&display_sem);
    }
}

void spi_dma_wait_isr_finish(void)
{
    // if (spi_pnd) {
    //     os_sem_pend(&display_sem, 0);
    //     while (spi_pnd)
    //     {
    //         wdt_clear();
    //     }
    // }

    while (spi_pnd)
    {
        os_sem_pend(&display_sem, 0);
    }
}

void spi_dma_send_bytes_for_isr(u8 *dat, u32 len)
{
    if (spi_dat) {
        spi_dma_wait_isr_finish();
        os_sem_set(&display_sem, 0);
        spi_pnd = true;
        spi_dma_set_addr_for_isr(spi_dat->spi_cfg, dat, len, 0);
        spi_set_ie(spi_dat->spi_cfg, 1);
    }
}

void spi_dma_send_byte_for_isr(u8 dat)
{
    u32 _dat __attribute__((aligned(4))) = 0;

    ((u8 *)(&_dat))[0] = dat;

    if (spi_dat) {
        spi_dma_send_bytes_for_isr(&_dat, 1);
        spi_dma_wait_isr_finish();
    }
}

#endif

void lcd_spi_send_byte(u8 rs, u8 byte)
{
    gpio_direction_output((u32)spi_dat->pin_cs, SPI_CS_EN);
    gpio_direction_output((u32)spi_dat->pin_rs, rs);
    // spi_dma_send_byte(byte);
    spi_dma_send_byte_for_isr(byte);
    gpio_direction_output((u32)spi_dat->pin_cs, SPI_CS_DIS);
}

void lcd_spi_set_gramscan(uint8_t ucOption)
{	
    uint32_t LCD_X_LENGTH;
    uint32_t LCD_Y_LENGTH;

	//参数检查，只可输入0-7
	if(ucOption > 7)
		return;
	
	//根据模式更新XY方向的像素宽度
	if((ucOption % 2) == 0)	
	{
		//0 2 4 6模式下X方向像素宽度为240，Y方向为320
		LCD_X_LENGTH = LCD_HIGHT;
		LCD_Y_LENGTH = LCD_WIDTH;
	}
	else				
	{
		//1 3 5 7模式下X方向像素宽度为320，Y方向为240
		LCD_X_LENGTH = LCD_WIDTH;
		LCD_Y_LENGTH = LCD_HIGHT; 
	}

	//0x36命令参数的高3位可用于设置GRAM扫描方向	
	lcd_spi_send_byte(SPI_RS_CMD, 0x36);
    lcd_spi_send_byte(SPI_RS_DAT, 0x00 | (ucOption<<5) | (LCD_RGB_ARR << 3));//根据ucOption的值设置LCD参数，共0-7种模式

	lcd_spi_send_byte(SPI_RS_CMD, 0x2A); 
	lcd_spi_send_byte(SPI_RS_DAT, 0x00);		/* x 起始坐标高8位 */
	lcd_spi_send_byte(SPI_RS_DAT, 0x00);		/* x 起始坐标低8位 */
	lcd_spi_send_byte(SPI_RS_DAT, ((LCD_X_LENGTH - 1) >> 8) & 0xFF); /* x 结束坐标高8位 */	
	lcd_spi_send_byte(SPI_RS_DAT, (LCD_X_LENGTH - 1) & 0xFF);				/* x 结束坐标低8位 */

	lcd_spi_send_byte(SPI_RS_CMD, 0x2B); 
	lcd_spi_send_byte(SPI_RS_DAT, 0x00);		/* y 起始坐标高8位 */
	lcd_spi_send_byte(SPI_RS_DAT, 0x00);		/* y 起始坐标低8位 */
	lcd_spi_send_byte(SPI_RS_DAT, ((LCD_Y_LENGTH - 1) >> 8) & 0xFF);	/* y 结束坐标高8位 */	 
	lcd_spi_send_byte(SPI_RS_DAT, (LCD_Y_LENGTH - 1) & 0xFF);				/* y 结束坐标低8位 */

	/* write gram start */
	lcd_spi_send_byte(SPI_RS_CMD, 0x2C);
}

static void spi_init_code(const InitCode *code, u8 cnt)
{
    u8 i, j;

    for (i = 0; i < cnt; i++) {
        if (code[i].cmd == REGFLAG_DELAY) {
            extern void wdt_clear(void);
            wdt_clear();
            delay_2ms(code[i].cnt / 2);
            // os_time_dly(code[i].cnt / 10 + 1);
        } else {
            lcd_spi_send_byte(SPI_RS_CMD, code[i].cmd);
            for (j = 0; j < code[i].cnt; j++) {
                lcd_spi_send_byte(SPI_RS_DAT, code[i].dat[j]);
            }
        }
    }
}

void lcd_spi_dev_init(void)
{
    clk_set("sys", BT_NORMAL_HZ);

    ASSERT(spi_dat, "Error! spi io not config");
    printf("spi pin rest:%d, cs:%d, rs:%d, spi:%d\n", spi_dat->pin_reset, spi_dat->pin_cs, spi_dat->pin_rs, spi_dat->spi_cfg);

#if LCD_SPI_PIN_RST != NO_CONFIG_PORT
    gpio_direction_output((u32)spi_dat->pin_reset, 1);
    gpio_set_direction((u32)spi_dat->pin_reset, 0);
#endif
    gpio_direction_output((u32)spi_dat->pin_cs, 1);
    gpio_set_direction((u32)spi_dat->pin_cs, 0);
    gpio_direction_output((u32)spi_dat->pin_rs, 1);
    gpio_set_direction((u32)spi_dat->pin_rs, 0);

    spi_open(spi_dat->spi_cfg);
#if SPI_INTERRUPT_ENABLE
    // 配置中断优先级，中断函数
    /* spi_set_ie(spi_cfg, 1); */
    u8 irq_spi_index = IRQ_SPI1_IDX;
    if (spi_dat->spi_cfg == SPI1)
        irq_spi_index = IRQ_SPI1_IDX;
    else if (spi_dat->spi_cfg == SPI2)
        irq_spi_index = IRQ_SPI2_IDX;

    printf("create sem\n");
    os_sem_create(&display_sem, 0);
    spi_set_ie(spi_dat->spi_cfg, 0);
    request_irq(irq_spi_index, 3, spi_isr, 0);
#endif

#if LCD_SPI_PIN_BL != NO_CONFIG_PORT
    gpio_direction_output((u32)spi_dat->pin_bl, 1);
#endif

    printf("lcd reset start\n");

#if LCD_SPI_PIN_RST != NO_CONFIG_PORT
    // 如果有硬件复位
    // gpio_direction_output((u32)spi_dat->pin_reset, 0);
    // os_time_dly(100);
    // gpio_direction_output((u32)spi_dat->pin_reset, 1);
#endif

    printf("lcd init start\n");

    spi_init_code(LcdInit_code, LcdInit_code_len);  // 初始化屏幕

    printf("lcd init end\n");

    lcd_spi_set_gramscan(LCD_GRAM);

#if SPI_FULL_TEST_AFT_INIT
#if !SPI_INTERRUPT_ENABLE
    u16 temp_color = 0b0111111111111111;
    // u16 temp_color = 0b1000000000000000;
    gpio_direction_output((u32)spi_dat->pin_rs, SPI_RS_DAT);
    for (u16 x = 0; x < 240; x++)
    {
        wdt_clear();
        gpio_direction_output((u32)spi_dat->pin_cs, SPI_CS_EN);
        for (u16 y = 0; y < 240; y++)
        {
            spi_dma_send_byte(temp_color >> 8);
            spi_dma_send_byte(temp_color);
        }
        gpio_direction_output((u32)spi_dat->pin_cs, SPI_CS_DIS);
        temp_color >>= 1;
        if (temp_color == 0) {
            temp_color = 0b0111111111111111;
        }
    }
#else
    extern u8 test_buffer[];
    extern void spi_dma_send_area_bitmap(int xsta, int xend, int ysta, int yend, u8 *buff, u32 len);
    for (u16 y = 0; y < 240; y += 80)
    {
        spi_dma_wait_isr_finish();
        memset(test_buffer, 0xFF, 38400);
        spi_dma_send_area_bitmap(1 - 1, 240 - 1, y, y + 80 - 1, test_buffer, 38400);
    }
    for (u16 y = 0; y < 120; y += 120)
    {
        spi_dma_wait_isr_finish();
        memset(test_buffer, 0x00, 38400);
        spi_dma_send_area_bitmap(61 - 1, 180 - 1, y, y + 160 - 1, test_buffer, 38400);
    }
#endif
#endif
}

#if SPI_FULL_TEST_AFT_INIT
#if SPI_INTERRUPT_ENABLE
u8 test_buffer[240 * 80 * 2] __attribute((aligned(4)));

void spi_dma_send_area_bitmap(int xsta, int xend, int ysta, int yend, u8 *buff, u32 len)
{
    spi_dma_wait_isr_finish();

	lcd_spi_send_byte(SPI_RS_CMD, 0x2A);
	lcd_spi_send_byte(SPI_RS_DAT, xsta >> 8);   /* x 起始坐标高8位 */
	lcd_spi_send_byte(SPI_RS_DAT, xsta);        /* x 起始坐标低8位 */
	lcd_spi_send_byte(SPI_RS_DAT, xend >> 8);   /* x 结束坐标高8位 */	
	lcd_spi_send_byte(SPI_RS_DAT, xend);        /* x 结束坐标低8位 */

	lcd_spi_send_byte(SPI_RS_CMD, 0x2B);
	lcd_spi_send_byte(SPI_RS_DAT, ysta >> 8);   /* y 起始坐标高8位 */
	lcd_spi_send_byte(SPI_RS_DAT, ysta);        /* y 起始坐标低8位 */
	lcd_spi_send_byte(SPI_RS_DAT, yend >> 8);   /* y 结束坐标高8位 */	 
	lcd_spi_send_byte(SPI_RS_DAT, yend);        /* y 结束坐标低8位 */

	/* write gram start */
	lcd_spi_send_byte(SPI_RS_CMD, 0x2C);

    gpio_direction_output((u32)spi_dat->pin_cs, SPI_CS_EN);
    gpio_direction_output((u32)spi_dat->pin_rs, SPI_RS_DAT);
    spi_dma_send_bytes_for_isr(buff, len);
}
#endif
#endif


void lcd_spi_draw_point(int x, int y, uint16_t rgb565)
{
    spi_dma_wait_isr_finish();
    lcd_spi_send_byte(SPI_RS_CMD, 0x2A);
	lcd_spi_send_byte(SPI_RS_DAT, x >> 8);   /* x 起始坐标高8位 */
	lcd_spi_send_byte(SPI_RS_DAT, x);        /* x 起始坐标低8位 */
	lcd_spi_send_byte(SPI_RS_DAT, x >> 8);   /* x 结束坐标高8位 */
	lcd_spi_send_byte(SPI_RS_DAT, x);        /* x 结束坐标低8位 */

	lcd_spi_send_byte(SPI_RS_CMD, 0x2B);
	lcd_spi_send_byte(SPI_RS_DAT, y >> 8);   /* y 起始坐标高8位 */
	lcd_spi_send_byte(SPI_RS_DAT, y);        /* y 起始坐标低8位 */
	lcd_spi_send_byte(SPI_RS_DAT, y >> 8);   /* y 结束坐标高8位 */
	lcd_spi_send_byte(SPI_RS_DAT, y);        /* y 结束坐标低8位 */
    
	lcd_spi_send_byte(SPI_RS_CMD, 0x2C);
    lcd_spi_send_byte(SPI_RS_DAT, rgb565 >> 8);
    lcd_spi_send_byte(SPI_RS_DAT, rgb565);
}

void lcd_spi_draw_area(int xsta, int ysta, int xend, int yend, uint16_t *rgb565)
{
    // spi_dma_wait_isr_finish();
    lcd_spi_send_byte(SPI_RS_CMD, 0x2A);
	lcd_spi_send_byte(SPI_RS_DAT, xsta >> 8);   /* x 起始坐标高8位 */
	lcd_spi_send_byte(SPI_RS_DAT, xsta);        /* x 起始坐标低8位 */
	lcd_spi_send_byte(SPI_RS_DAT, xend >> 8);   /* x 结束坐标高8位 */	
	lcd_spi_send_byte(SPI_RS_DAT, xend);        /* x 结束坐标低8位 */

	lcd_spi_send_byte(SPI_RS_CMD, 0x2B);
	lcd_spi_send_byte(SPI_RS_DAT, ysta >> 8);   /* y 起始坐标高8位 */
	lcd_spi_send_byte(SPI_RS_DAT, ysta);        /* y 起始坐标低8位 */
	lcd_spi_send_byte(SPI_RS_DAT, yend >> 8);   /* y 结束坐标高8位 */	 
	lcd_spi_send_byte(SPI_RS_DAT, yend);        /* y 结束坐标低8位 */
    
	lcd_spi_send_byte(SPI_RS_CMD, 0x2C);
    gpio_direction_output((u32)spi_dat->pin_rs, SPI_RS_DAT);
    gpio_direction_output((u32)spi_dat->pin_cs, SPI_CS_EN);
    spi_dma_send_bytes_for_isr((uint8_t *)rgb565, (xend - xsta + 1) * (yend - ysta + 1) * 2);
    spi_dma_wait_isr_finish();
}
