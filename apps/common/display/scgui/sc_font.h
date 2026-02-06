
#ifndef _SC_FONT_H
#define _SC_FONT_H

#include "sc_gui.h"
#include "sc_lcd.h"

typedef struct
{
    lv_font_t*    font;
    const char    *str;
    uint16_t      tc;  //字体色
    uint16_t      align;
} SC_label;

typedef struct
{
    SC_ARER abs;   // 物理像素坐标
    char str[2];
} key_rect_t;


// lv_font_t* font;
// const char* str;
// uint16_t    tc;        //字体色


int  SPIFLASH_Read(uint8_t *buf,uint32_t offset,uint32_t size);         //bin×Öżâ˝ÓżÚ

///设置坐标对齐到parent
void SC_set_align(SC_ARER *parent,int *xs,int *ys,int w,int h,uint8_t align);

///长文本显示自动换行，支持\n换行
void SC_pfb_printf(SC_tile *dest,SC_ARER *box, int x,int y,const char* txt,uint16_t fc,uint16_t bc, lv_font_t* font);

///显示文本
void SC_pfb_str(SC_tile *dest,int x,int y,const char *text,uint16_t fc,uint16_t bc,lv_font_t* font,SC_ARER *parent,uint8_t align);
///设置标签
void SC_set_label_text(SC_label* Label,const char *str,uint16_t fc,lv_font_t* font,uint8_t align);

///显示标签
void SC_pfb_label(SC_tile *dest,int x,int y,SC_ARER *parent,SC_label *label,uint16_t lab_bc);

///文本框
void SC_pfb_text_box(SC_tile *dest,SC_ARER *parent,int xs,int ys,SC_label* label);

///-按键-
void SC_pfb_button(SC_tile *dest,SC_label *label,int xs,int ys,int xe,int ye,uint16_t ac,uint16_t bc,uint8_t r,uint8_t ir,uint8_t state);


///-LED-
void SC_pfb_led(SC_tile *dest,int xs,int ys,int xe,int ye, uint16_t ac,uint16_t bc);


///-开关-
void SC_pfb_switch(SC_tile *dest,int xs,int ys,int xe,int ye, uint16_t ac,uint16_t bc,uint8_t state);

#endif
