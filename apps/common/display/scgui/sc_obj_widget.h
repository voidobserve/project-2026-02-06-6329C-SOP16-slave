
#ifndef SC_OBJ_WIDGET_H
#define SC_OBJ_WIDGET_H
#include "sc_obj_list.h"
///画布
typedef struct
{
    Obj_t  base;
    SC_ARER  box;
} Obj_Canvas;

///矩形
typedef struct
{
    Obj_t base;
    uint8_t r;
    uint8_t ir;
    uint16_t bc;            //填充色
    uint16_t ac;            //边界色
    uint8_t  alpha;
} Obj_Frame;

///标签控件
typedef struct
{
    Obj_t base;
    SC_label text;
} Obj_Label;

///文本框控件
typedef struct
{
    Obj_t base;
    SC_ARER box;
    SC_label text;
} Obj_Txtbox;

/// 按钮控件
typedef struct
{
    Obj_t    base;
    SC_label text;
    uint8_t r;
    uint8_t ir;
    uint16_t bc;            //填充色
    uint16_t ac;            //边界色
    uint8_t  alpha;
} Obj_But;

///LED控件
typedef struct
{
    Obj_t base;
    uint16_t bc;            //填充色
    uint16_t ac;            //边界色
    uint8_t  alpha;
} Obj_Led;

///开关控件
typedef struct
{
    Obj_t base;
    uint16_t bc;            //填充色
    uint16_t ac;            //边界色
    uint8_t  alpha;
} Obj_Sw;

/// 进度条控件
typedef struct
{
    Obj_t base;             // 基础控件
    int value;              // 滑块当前值
    uint16_t bc;            //填充色
    uint16_t ac;            //边界色
    uint8_t r;
    uint8_t ir;
    uint8_t  alpha;

} Obj_Slider;


///图片控件
typedef struct
{
    Obj_t base;
    const SC_img_t *src;
} Obj_Image;

///图片zip
typedef struct
{
    Obj_t base;
    const SC_img_zip *src;
    SC_dec_zip        dec;
} Obj_Imagezip;

///圆弧
typedef struct
{
    Obj_t base;
    uint16_t bc;            //填充色
    uint16_t ac;            //边界色
    uint16_t r;
    uint16_t ir;
    int16_t start_deg;    //角度
    int16_t end_deg;      //角度
    uint8_t dot;        //端点
    uint8_t alpha;
} Obj_Arc;

///图片变换
typedef struct
{
    Obj_t base;
    const SC_img_t *src;
    Transform    params;
    int16_t     move_x;
    int16_t     move_y;
} Obj_Rotate;

///示波器控件
typedef struct
{
    Obj_t base;             //基础控件
    SC_chart  buf[2];       //波形数量
    uint16_t bc;            //填充色
    uint16_t ac;            //边界色
    uint8_t xd;
    uint8_t yd;
} Obj_Chart;

///控件触控
void obj_touch_check(void* screen, int x, int y,uint8_t state);

///刷新单个活动控件
void obj_draw_active(void *screen,Obj_t *active);

///遍历刷新控件
void obj_draw_screen(void *screen);

///b完全在a区域内
static inline int arae_is_contained(SC_ARER* a, SC_ARER* b)
{
    return (b->xs>=a->xs && b->ys>=a->ys  && b->xe <= a->xe && b->ye <= a->ye);
}
///判断两个区域是否相交
static inline int arae_is_intersect(SC_ARER* a, SC_ARER* b)
{
    return (!(a->xe < b->xs || a->xs > b->xe || a->ye < b->ys || a->ys > b->ye));
}
///合并控件平移后的脏矩形
static inline  void arae_is_merge(SC_ARER* a,int move_x,int move_y)
{
    if(move_x>0)
    {
        a->xs-=move_x;
    }
    else
    {
        a->xe-=move_x;
    }
    if(move_y>0)
    {
        a->ys-=move_y;
    }
    else
    {
        a->ye-=move_y;
    }
}

///判断xy是否在区域内
static inline int arae_is_touch(SC_ARER *a,int x, int y)
{
    return (x >= a->xs && x <= a->xe &&y >= a->ys && y <= a->ye);
}
///创建屏幕
static inline void obj_add_Screen(Obj_t* parent, Obj_t* obj,int x, int y, int w, int h, SC_Align  align)
{
    if(obj_create(parent,obj,ARER))
    {
        obj_set_geometry(obj,  x,  y,  w,  h, align);
    }
}
///创建画布
static inline void obj_add_Canvas(void* parent, Obj_Canvas* obj,int x, int y, int w, int h, int row,int column,SC_Align  align)
{
    if (obj_create(parent,obj,CANVAS))
    {
        obj->box.xs=0;
        obj->box.ys=0;
        obj->box.xe=row-w;
        obj->box.ye=column-h;
        obj_set_geometry(obj, x,  y, w, h, align);
    }
}
///创建矩形控件
static inline void obj_add_Frame(void* parent, Obj_Frame* Frame,int x, int y, int w, int h,uint16_t ac,uint16_t bc,uint8_t r,uint8_t ir, SC_Align  align)
{
    if(obj_create(parent,Frame,FRAME))
    {
        Frame->r=r;
        Frame->ir=ir;   //倒角
        Frame->bc=bc;   //填充色
        Frame->ac=ac;   //边界色
        Frame->alpha=255;
        obj_set_geometry(Frame,  x,  y,  w,  h, align);
    }
}
static inline void obj_add_Frame_Sample(void* parent, Obj_Frame* Frame)
{
    if(obj_create(parent,Frame,FRAME))
    {
        Frame->r=1;
        Frame->ir=1;   //倒角
        Frame->bc=gui->bc;   //填充色
        Frame->ac=gui->fc;   //边界色
        Frame->alpha=255;
    }
}
static inline void obj_set_Frame_r(Obj_Frame* Frame, uint8_t r)
{
    uint8_t width = Frame->r + 1 - Frame->ir;
    Frame->r = r;
    if (r >= width)
        Frame->ir = r + 1 - width;   //倒角
    else
        Frame->ir = 1;   //倒角
}
static inline void obj_set_Frame_width(Obj_Frame* Frame, uint8_t width)
{   // r=外角弧度(>0,1=90度);ir=内角弧度(>0,<=r时边框宽度=r+1-ir,>r时无边框)
    uint8_t r = Frame->r;
    if (r >= width)
        Frame->ir = r + 1 - width;   //倒角
    else
        Frame->ir = 1;   //倒角
}

///创建Label控件
static inline void obj_add_Label(void* parent, Obj_Label* Label,int x, int y, int w, int h, SC_Align  align)
{
    if(obj_create(parent,Label,LABEL))
    {
        obj_set_geometry(Label,  x,  y,  w,  h, align);
    }
}

///设置Label字体
static inline void obj_set_Label_text(Obj_Label* Label,const char *str,uint16_t tc, lv_font_t* font,uint8_t align)
{
    SC_set_label_text(&Label->text,str,tc,font,align);
}

///创建Txtbox控件
static inline void obj_add_Txtbox(void* parent, Obj_Txtbox* obj,int x, int y, int w, int h,int row,int column,SC_Align  align)
{
    if(obj_create(parent,obj,TEXTBOX))
    {
        obj->box.xs=0;
        obj->box.ys=0;
        obj->box.xe=row-w;
        obj->box.ye=column-h;
        obj_set_geometry(obj, x,  y, w, h, align);
    }
}
///设置Txtbox字体
static inline void obj_set_Txtbox_text(Obj_Txtbox* txtbox,const char *str,uint16_t tc, lv_font_t* font,uint8_t align)
{
    SC_set_label_text(&txtbox->text,str,tc,font,align);
}

///创建按键控件
static inline void obj_add_Button(void* parent, Obj_But* button,int x, int y, int w, int h,uint16_t bc,uint16_t ac,uint8_t r,uint8_t ir, SC_Align  align)
{
    if(obj_create(parent,button,BUTTON))
    {
        button->r=r;
        button->ir=ir;   //倒角
        button->bc=bc;   //填充色
        button->ac=ac;   //边界色
        button->alpha=255;
        obj_set_geometry(button,  x,  y,  w,  h, align);
    }
}
///设置按键字体
static inline void obj_set_button_text(Obj_But* button, const char *str,uint16_t tc, lv_font_t* font,uint8_t align)
{
    SC_set_label_text(&button->text,str,tc,font,align);
}

///创建开关
static inline void obj_add_Switch(void* parent, Obj_Sw* obj,int x, int y, int w, int h,uint16_t bc,uint16_t ac, SC_Align  align)
{
    if(obj_create(parent,obj,SWITCH))
    {
        obj->bc=bc;   //填充色
        obj->ac=ac;   //边界色
        obj->alpha=255;
        obj_set_geometry(obj,  x,  y, w, h, align);
    }
}
///创建LED
static inline void obj_add_Led(void* parent, Obj_Led* led,int x, int y, int w, int h,uint16_t bc,uint16_t ac, SC_Align  align)
{
    if(obj_create(parent,led,LED))
    {
        led->bc=bc;   //填充色
        led->ac=ac;   //边界色
        led->alpha=255;
        obj_set_geometry(led,  x,  y, w, h, align);
    }
}

///创建滑条
static inline void obj_add_Slider(void* parent, Obj_Slider* slider,int x, int y, int w, int h,uint16_t bc,uint16_t ac,uint8_t r,uint8_t ir,int value, SC_Align  align)
{
    if(obj_create(parent,slider,SLIDER))
    {
        slider->r=r;
        slider->ir=ir;   //倒角
        slider->bc=bc;   //填充色
        slider->ac=ac;   //边界色
        slider->value=value;
        obj_set_geometry(slider,  x,  y, w, h, align);
    }
}

///创建图片
static inline void obj_add_Image(void* parent, Obj_Image* image,const SC_img_t *src,int x, int y, SC_Align  align)
{
    if(obj_create(parent,image,IMAGE))
    {
        image->src=src;
        obj_set_geometry(image,  x,  y, src->w,  src->h, align);
    }
}

///创建ZIP图片
static inline void obj_add_Imagezip(void* parent, Obj_Imagezip* zip,const SC_img_zip *src,int x, int y, SC_Align  align)
{
    if(obj_create(parent,zip,IMAGEZIP))
    {
        zip->src=src;
        obj_set_geometry(zip,  x,  y, src->w,  src->h, align);
    }
}
///创建圆弧
static inline void obj_add_arc(void* parent,Obj_Arc *obj, int cx,int cy,int r, int ir, uint16_t ac,uint16_t bc, SC_Align  align)
{
    if(obj_create(parent,obj,ARC))
    {
        obj_set_geometry(obj, cx,  cy, 2*r+1, 2*r+1, align);
        obj->r=r;
        obj->ir=ir;
        obj->ac=ac;
        obj->bc=bc;
        obj->start_deg=0;
        obj->end_deg=360;
        obj->alpha=255;
    }
}

///设置圆弧角度
static inline void obj_set_arcdeg(Obj_Arc* obj,int start_deg,int end_deg,uint8_t dot)
{
    obj->start_deg=start_deg;
    obj->end_deg=end_deg;
    obj->dot = dot;
    obj->base.Flag|=  OBJ_FLAG_ACTIVE;
}

///图片变换,设置底图和指针src的中心坐标
static inline void obj_add_Rotate(void* parent, Obj_Rotate* obj,const SC_img_t *src,int dx, int dy,int src_x,int src_y, SC_Align  align)
{
    Obj_t *p=(Obj_t *)parent;
    if(obj_create(parent,obj,ROTATE))
    {
        obj->src=src;
        obj->params.scaleX   = 256;                 //缩放1.0
        obj->params.scaleY   = 256;
        obj->params.center_x = obj->src->w/2+src_x;  //源图中心
        obj->params.center_y = obj->src->h/2+src_y;
        SC_set_align(&p->abs,&dx,&dy, 0, 0, align);
        obj->move_x=dx-p->abs.xs;      //转为偏移量
        obj->move_y=dy-p->abs.ys;      //转为偏移量
        obj->base.abs=p->abs;        //首次坐标
        obj->params.last= &obj->base.abs; //下次计算脏矩阵
    }
}

///设置图片旋转
static inline void obj_set_Rotate(Obj_Rotate* obj,int angle)
{
    Obj_t *parent=obj->base.parent;
    if(arae_is_intersect(&parent->abs,&gui->lcd_area))  //相交于屏幕
    {
        obj->params.move_x=parent->abs.xs+obj->move_x;   //跟随parent
        obj->params.move_y=parent->abs.ys+obj->move_y;
        SC_set_transform(&obj->params,obj->src,angle);
        obj->base.Flag |=  OBJ_FLAG_ACTIVE;
    }
}

///创建波形图
static inline void obj_add_Chart(void* parent, Obj_Chart* chart,int x, int y, int w, int h,uint16_t ac,uint16_t bc,uint8_t xd,uint8_t yd, SC_Align  align)
{
    if(obj_create(parent,chart,CHART))
    {
        int src_width=sizeof(chart->buf[0].src_buf)/2;   //SC_chart 数组默认为200
        chart->ac=ac;
        chart->bc=bc;
        chart->xd=xd;
        chart->yd=yd;
        if(w>src_width) w=src_width;
        obj_set_geometry(chart,  x,  y, w, h, align);
    }
}

///示波器通道压入数据vol,scaleX缩放Q8格式
static inline void obj_set_Chart_vol(int ch,Obj_Chart *chart,uint16_t vol,int scaleX)
{
    if(sizeof(chart->buf)/sizeof(chart->buf[0])>ch)  //通道选择
    {
        SC_chart_put(&chart->buf[ch], vol,scaleX);
        chart->base.Flag|=OBJ_FLAG_ACTIVE;
    }
}

#endif
