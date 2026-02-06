
#ifndef _SC_GUI_H
#define _SC_GUI_H

#include "sc_lcd.h"
#include "sc_font.h"


/* 公共对齐宏  ALIGN_ARRAY(type, name, size, boundary) */
#if   defined(__ICCARM__)
#  define ALIGN_ARRAY(type, name, size, boundary) \
        _Pragma("data_alignment=" #boundary) \
        type name[size]
#elif defined(__CC_ARM)
#  define ALIGN_ARRAY(type, name, size, boundary) \
        __align(boundary) type name[size]
#elif defined(__GNUC__) || defined(__clang__)
#  define ALIGN_ARRAY(type, name, size, boundary) \
        type name[size] __attribute__((aligned(boundary)))
#else
#  define ALIGN_ARRAY(type, name, size, boundary) \
        type name[size]
#endif

#define  DRAW_IMAGE_ZIP_EN    1   //图片压缩

#define  DRAW_CHART_EN        1   //波形图

#define  DRAW_ARC_EN          1   //圆弧库

#define  DRAW_LINE_EN         1   //画斜线打点

#define  DRAW_LINE_SDF_EN     0   //圆头粗线浮点

#define  DRAW_TRANSFORM_EN    1   //图片旋转

#define  SC_DEBUG             0

#if SC_DEBUG
#define  sc_printf  printf
#else
#define  sc_printf(...)
#endif

#define Q15       (1<<15)
#define SC_ABS(a) (((a)<0)?(-(a)):(a))
#define SC_MAX(a,b)(a>b?a:b)
#define SC_MIN(a,b)(a<b?a:b)
#define set_pixel_value(dest, offs, src, c)   dest->buf[offs]=alphaBlend(c,dest->buf[offs],src)

typedef struct
{
    const uint8_t *map;
    const uint8_t *mask;
    uint16_t w;
    uint16_t h;
} SC_img_t;

typedef struct
{
    const uint8_t *map;
    uint32_t   len;
    uint16_t w;
    uint16_t h;
} SC_img_zip;

typedef struct
{
    uint32_t n;
    int16_t  x;
    int16_t  y;
    uint16_t rep_cnt;
    uint16_t out;
    uint16_t unzip;
} SC_dec_zip;


typedef struct
{
    int16_t   src_buf[200];
    int16_t   last;
    uint32_t  indx;
    uint16_t  wp;   //队列
} SC_chart;


//-------圆弧-----
typedef struct SC_arc
{
    int16_t  cx;
    int16_t  cy;
    int16_t  r;
    int16_t  ir;
} SC_arc;



typedef struct
{
    int16_t center_x ;  //源图中心点
    int16_t center_y ;  //源图中心点
    int16_t move_x ;    //平移x
    int16_t move_y ;    //平移y
    int16_t scaleX;     //X缩放
    int16_t scaleY;     //Y缩放
    int16_t sinA;       //角度sin
    int16_t cosA;       //角度cos
    SC_ARER  abs;
    SC_ARER *last;
} Transform;

typedef enum
{
    ALIGN_NONE     = 0x00,
    ALIGN_LEFT     = (1 << 0),
    ALIGN_RIGHT    = (1 << 1),
    ALIGN_TOP      = (1 << 2),
    ALIGN_BOTTOM   = (1 << 3),
    ALIGN_HOR      = ALIGN_LEFT | ALIGN_RIGHT,
    ALIGN_VER      = ALIGN_TOP | ALIGN_BOTTOM,
    ALIGN_CENTER   = ALIGN_HOR | ALIGN_VER,
} SC_Align;

typedef struct
{
    uint16_t    alpha;       //全局透明度
    uint16_t    bkc;         //主题底色
    uint16_t    bc;          //背影色
    uint16_t    fc;          //前影色
    uint16_t    *pfb_buf;
    lv_font_t   *font;           //字体
    SC_ARER     lcd_area;
    SC_ARER     *parent;
    void       (*bsp_pset)(int,int,uint16_t);
    //void       (*Refresh)(int xs, int ys,uint16_t w, uint16_t h,uint16_t *buf);

} SC_GUI;

extern SC_GUI *gui;

uint16_t alphaBlend( uint16_t fc, uint16_t bc,uint16_t alpha);

void SC_GUI_Init( void (*bsp_pset)(int,int,uint16_t),uint16_t bkc,uint16_t bc,uint16_t fc, lv_font_t* font);

void SC_pfb_Image(SC_tile *dest,int xs,int ys,const SC_img_t *src,uint16_t alpha);

void SC_pfb_Image_zip(SC_tile *dest,  int xs,int ys,const SC_img_zip *zip,SC_dec_zip *dec);

void SC_chart_put(SC_chart *p, int16_t vol,int scaleX);

void SC_pfb_chart(SC_tile *dest,int xs,int ys,int w,int h,uint16_t gc,uint16_t ac,int xd,int yd,SC_chart *p,int ch);

int SC_pfb_RoundFrame(SC_tile *dest,int xs,int ys,int xe,int ye, int r,int ir, uint16_t ac,uint16_t bc);

void SC_pfb_DrawArc(SC_tile *dest,SC_arc *p,int startAngle, int endAngle,uint16_t ac,uint16_t bc,uint8_t dot);

void SC_pfb_RoundBar(SC_tile *dest,int xs,int ys,int w,int h,int r,int ir,uint16_t ac,uint16_t bc,int vol,int max);

void SC_pfb_transform(SC_tile *dest,const SC_img_t *Src,Transform *p);

void SC_set_transform(Transform *p,const SC_img_t* Src, int16_t Angle);

extern void SC_pfb_DrawLine_AA(SC_tile *dest,int x1, int y1, int x2, int y2,uint16_t colour);

void SC_DrawLine_SDF(float ax, float ay, float bx, float by, float r, uint16_t color);

#endif
