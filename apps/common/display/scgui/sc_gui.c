
#include "sc_gui.h"

//uint16_t  SC_pfb_buf[SC_PFB_BUF_SIZE] __attribute__((aligned(4)));
ALIGN_ARRAY(uint16_t, SC_pfb_buf, SC_PFB_BUF_SIZE, 4);   //内存池4字节对齐

SC_GUI  *gui;

#if 0
//快速运算alpha算法8位MCU专用
uint16_t alphaBlend( uint16_t fc, uint16_t bc,uint16_t alpha)
{
    if(alpha>251)  return fc;
    if(alpha<4)    return bc;
    // Split out and blend 5 bit red and blue channels
    uint32_t rxb = bc & 0xF81F;
    rxb += ((fc & 0xF81F) - rxb) * (alpha >> 2) >> 6;
    // Split out and blend 6 bit green channel
    uint32_t xgx = bc & 0x07E0;
    xgx += ((fc & 0x07E0) - xgx) * alpha >> 8;
    // Recombine channels
    return (rxb & 0xF81F) | (xgx & 0x07E0);
}
#else
//快速运算alpha算法32位MCU更快
uint16_t alphaBlend(uint16_t fc,uint16_t bc,uint16_t alpha)
{
    if(alpha>251)  return fc;
    if(alpha<4)    return bc;
    uint32_t bc_Alpha = ( bc | ( bc<<16 )) & 0x7E0F81F;
    uint32_t fc_Alpha = ((fc | ( fc<<16 )) & 0x7E0F81F) - bc_Alpha;
    uint32_t result=  (bc_Alpha+(fc_Alpha* (alpha>>3)>>5))& 0x7E0F81F;
    return (result&0xFFFF) | (result>>16);
}
#endif // 0

//初始化系统注册画点函数
void SC_GUI_Init( void (*bsp_pset)(int,int,uint16_t),uint16_t bkc,uint16_t fc,uint16_t bc, lv_font_t* font)
{
    static SC_GUI tft;
    gui = &tft;
    gui->bkc=bkc;
    gui->bc= bc;
    gui->fc= fc;
    gui->alpha=255;
    gui->pfb_buf= (uint16_t*)SC_pfb_buf;
    gui->lcd_area.xs=0;
    gui->lcd_area.ys=0;
    gui->lcd_area.xe=LCD_SCREEN_WIDTH-1;   //
    gui->lcd_area.ye=LCD_SCREEN_HEIGHT-1;
    gui->font=font;                 //lvgl字库
    gui->bsp_pset = bsp_pset;       //层底画点
}

/**fun: 显示图片*/
void SC_pfb_Image(SC_tile *dest,int xs,int ys,const SC_img_t *src,uint16_t alpha)
{
    SC_tile pfb;
    uint8_t *mask=(uint8_t*)src->mask;
    if(dest==NULL)
    {
        pfb.stup=0;
        dest=&pfb;
    }
    do
    {
        if(dest==&pfb)
        {
            SC_pfb_clip(&pfb,xs,ys,xs+src->w-1,ys+src->h-1,gui->bkc);
        }
        //===========计算相交===============
        SC_ARER intersection;
        if(!SC_pfb_intersection(dest,&intersection,xs,ys,xs+src->w-1,ys+src->h-1))
        {
            return;
        }
        int dest_offs=(intersection.ys-dest->ys) * dest->w - dest->xs;
        for (int y = intersection.ys; y <=intersection.ye; y++,dest_offs+=dest->w)
        {
            int src_offs= (y-ys) * src->w - xs;
            uint16_t *src_dat=(uint16_t*)src->map+src_offs+intersection.xs;
            uint16_t *dest_out=dest->buf+dest_offs+intersection.xs;
            for (int x = intersection.xs; x <=intersection.xe; x++)
            {
                if(mask)
                {
                    *dest_out=alphaBlend(*src_dat++,*dest_out,mask[src_offs+x]*alpha/256);
                }
                else
                {
                    *dest_out=*src_dat++;
                }
                dest_out++;
            }
        }
    }
    while((dest==&pfb)&&SC_pfb_Refresh(&pfb,0));
}

///======================图片压缩=====================================
#if DRAW_IMAGE_ZIP_EN
/**fun: pfb解压缩图片，解压次数增加，大图性能差*/
void SC_pfb_Image_zip(SC_tile *dest,  int xs,int ys,const SC_img_zip *zip,SC_dec_zip *dec)
{
    int xe=xs+zip->w-1;
    int ye=ys+zip->h-1;
    uint16_t r,g,b;
    SC_tile pfb;
    if(dest==NULL)
    {
        pfb.stup=0;
        dest=&pfb;
    }
    if(dest->stup==0)
    {
        dec->x=xs;
        dec->y=ys;
        dec->rep_cnt=0;
        dec->unzip=0;
        dec->n=0;
    }
    do
    {
        if(dest==&pfb)
        {
            SC_pfb_clip(&pfb,xs,ys,xe,ye,gui->bkc); //创建局部pfb
        }
        SC_ARER intersection;
        if(!SC_pfb_intersection(dest,&intersection,xs,ys,xe,ye))
        {
            return;
        }
        while(dec->n<zip->len)
        {
            if(dec->rep_cnt==0)
            {
                dec->rep_cnt=1;
                if(zip->map[dec->n]&0x20)
                {
                    uint16_t dat16=zip->map[dec->n]|(zip->map[dec->n+1]<<8);
                    if(dec->unzip==dat16)
                    {
                        dec->n+=2;
                        dec->rep_cnt=(zip->map[dec->n]<<8)|zip->map[dec->n+1];  //重复的长度
                    }
                    else
                    {
                        dec->unzip= dat16;
                        dec->out  = dat16;
                    }
                    dec->n+=2;
                }
                else
                {
                    b=zip->map[dec->n];
                    r=(b<<5)&0x1800;
                    g=(b<<3)&0x00e3;
                    dec->out=dec->unzip^(r+g+(b&0x03));
                    dec->n++;
                }
            }
            while(dec->rep_cnt)
            {
                dec->rep_cnt--;
                if(dec->y>=intersection.ys&&dec->x>=intersection.xs&&dec->x<=intersection.xe&&dec->y<=intersection.ye)
                {
                    int dest_offs=(dec->y-dest->ys)*dest->w+(dec->x-dest->xs);
                    dest->buf[dest_offs]=dec->out;
                }
                if(++dec->x>xe)
                {
                    dec->x=xs;
                    if(++dec->y>intersection.ye)
                    {
                        break;
                    }
                }
            }
        }
    }
    while((dest==&pfb)&&SC_pfb_Refresh(&pfb,0));

}
#endif // DRAW_IMAGE_ZIP_EN

///======================波形图=====================================
#if DRAW_CHART_EN

//波形线性插值，采样点数放大到全屏
//static void SC_bilinear_bmp(int16_t *src,int16_t *dst,uint16_t src_width, uint16_t dst_width)
//{
//    if(src==NULL) return;
//    int32_t  x_diff, x_diff_comp;           //权重
//    int32_t  x,x_ratio=0;
//    uint32_t ratio=((src_width-1)<<8)/(dst_width-1);//缩放系数,防止边界问题指针超出
//    for (int j = 0; j < dst_width; j++)
//    {
//        x =  x_ratio >> 8;                                //坐标取整
//        x_diff = x_ratio%256;                             //坐标取余
//        x_diff_comp = 255 - x_diff;                       //1-余
//        x_ratio += ratio;             //累加
//        //-----------邻值取权重--------------
//        int x1=(x)%src_width;
//        int x2=(x+1)%src_width;
//        int32_t result = (x_diff_comp*src[x1])+(x_diff*src[x2]);   //x,y;
//        dst[j]=result/256;
//    }
//}

void SC_chart_put(SC_chart *p, int16_t vol,int scaleX)
{

    int num=(scaleX>256)? scaleX/256:1;         //入列数量
    p->wp = (p->indx/256);                      //当前下标取整
    p->indx+=scaleX;                            //累加坐标
    for(int i=1; i<=num; i++)
    {
        p->src_buf[p->wp] = p->last + (256*i*(vol-p->last)/scaleX); //插值
        if(++p->wp>= sizeof(p->src_buf)/2)
        {
            p->wp=0;
            p->indx= p->indx&0xff;                            //下标保留小数
        }
    }
    p->last = vol;
}

void SC_pfb_chart(SC_tile *dest,int xs,int ys,int w,int h,uint16_t ac,uint16_t gc,int xd,int yd,SC_chart *p,int ch)
{
    uint16_t C_ch[]= {C_RED,C_BLUE};
    int xe=xs+w-1;
    int ye=ys+h-1;
    SC_tile pfb;
    if(dest==NULL)
    {
        pfb.stup=0;
        dest=&pfb;
    }
    xd=w/xd;
    yd=h/yd;
    do
    {
        if(dest==&pfb)
        {
            SC_pfb_clip(&pfb,xs,ys,xe,ye,gui->bkc); //创建局部pfb
        }
        //===========计算相交===============
        SC_ARER intersection;
        if(!SC_pfb_intersection(dest,&intersection,xs,ys,xe,ye))
        {
            return;
        }
        int  base_line_x=w/2+xs; //x基准线
        int  base_line_y=h/2+ys; //y基准线
        int x,y,min,max,wp,wp1;
        uint16_t *dest_out;
        for( y=intersection.ys; y<=intersection.ye; y++)
        {
            if((y-base_line_y)%yd==0||(y==ys||y==ye))
            {
                uint8_t Solid=(y==base_line_y||y==ys||y==ye)? 1:0;   //实线标志
                dest_out=dest->buf+(y-dest->ys)*dest->w+(intersection.xs-dest->xs);
                for(x=intersection.xs; x<=intersection.xe; x++)
                {
                    *dest_out=(Solid||(x&0x03)==0)?gc:*dest_out; //水平线
                    dest_out++;
                }
            }
        }
        // xtime++;    //时间轴
        for( x=intersection.xs; x<=intersection.xe; x++)
        {
            if((x-base_line_x)%xd==0||x==xs||x==xe)
            {
                uint8_t Solid=(x==base_line_x||x==xs||x==xe)? 1:0;   //实线标志
                dest_out=dest->buf+(intersection.ys-dest->ys) * dest->w+x-dest->xs;
                for(y=intersection.ys; y<=intersection.ye; y++)
                {
                    *dest_out=(Solid||(y&0x03)==0)?gc:*dest_out; ////垂直线
                    dest_out+=dest->w;
                }
            }
            if(x>xs&&x<xe)   //边界内
            {
                for(int i=0; i<ch; i++)
                {
                    wp= (p[i].wp+x-xs);
                    wp=wp>=w? wp-w:wp;
                    wp1=(wp+1)>=w? (wp-w+1):(wp+1);
                    min=base_line_y-p[i].src_buf[wp];
                    max=base_line_y-p[i].src_buf[wp1];
                    if(min>max)
                    {
                        min=max+1;
                        max=base_line_y-p[i].src_buf[wp];
                    }
                    else  if(min<max)
                    {
                        max-=1;
                    }
                    min=SC_MAX(min,intersection.ys);
                    max=SC_MIN(max,intersection.ye);
                    dest_out=dest->buf+(min-dest->ys) * dest->w+x-dest->xs;
                    for(y=min; y<=max; y++)
                    {
                        *dest_out=C_ch[i];			//波形
                        dest_out+=dest->w;
                    }
                }
            }
        }
    }
    while((dest==&pfb)&&SC_pfb_Refresh(&pfb,0));
}

#endif // DRAW_CHART_EN

static const int16_t sin0_90_table[] =
{
    0,     572,   1144,  1715,  2286,  2856,  3425,  3993,  4560,  5126,  5690,  6252,  6813,  7371,  7927,  8481,
    9032,  9580,  10126, 10668, 11207, 11743, 12275, 12803, 13328, 13848, 14364, 14876, 15383, 15886, 16383, 16876,
    17364, 17846, 18323, 18794, 19260, 19720, 20173, 20621, 21062, 21497, 21925, 22347, 22762, 23170, 23571, 23964,
    24351, 24730, 25101, 25465, 25821, 26169, 26509, 26841, 27165, 27481, 27788, 28087, 28377, 28659, 28932, 29196,
    29451, 29697, 29934, 30162, 30381, 30591, 30791, 30982, 31163, 31335, 31498, 31650, 31794, 31927, 32051, 32165,
    32269, 32364, 32448, 32523, 32587, 32642, 32687, 32722, 32747, 32762, 32767
};
/*******************************************
 * Return with sinus of an angle
 * @param angle
 * @return sinus of 'angle'. sin(-90) = -32767, sin(90) = 32767
 */
int32_t sc_sin(int16_t angle)
{
    int32_t ret = 0;

    if(angle>=360) angle = angle-360 ;
    if(angle < 0)  angle = angle+360 ;
    if(angle < 90)
    {
        ret = sin0_90_table[angle];
    }
    else if(angle >= 90 && angle < 180)
    {
        angle = 180 - angle;
        ret   = sin0_90_table[angle];
    }
    else if(angle >= 180 && angle < 270)
    {
        angle = angle - 180;
        ret   = -sin0_90_table[angle];
    }
    else     /*angle >=270*/
    {
        angle = 360 - angle;
        ret   = -sin0_90_table[angle];
    }
    return ret;
}

int32_t sc_cos(int16_t angle)
{
    return -sc_sin(angle-90);
}

#if 0
///------------旧版本废弃代码-------------------
// Compute the fixed point square root of an integer and
// return the 8 MS bits of fractional part.
// Quicker than sqrt() for processors that do not have an FPU (e.g. RP2040)
uint8_t sc_sqrt(uint32_t num)
{
    if (num > (0x40000000)) return 0;
    uint32_t bsh = 0x00004000;
    uint32_t fpr = 0;
    uint32_t osh = 0;
    uint32_t bod;
    // Auto adjust from U8:8 up to U15:16
    while (num>bsh)
    {
        bsh <<= 2;
        osh++;
    }
    do
    {
        bod = bsh + fpr;
        if(num >= bod)
        {
            num -= bod;
            fpr = bsh + bod;
        }
        num <<= 1;
    }
    while(bsh >>= 1);
    return fpr>>osh;
}
uint16_t SC_atan2(int x, int y)
{
    unsigned char negflag;
    unsigned char tempdegree;
    unsigned char comp;
    unsigned int degree;     // this will hold the result
    unsigned int ux;
    unsigned int uy;
    // Save the sign flags then remove signs and get XY as unsigned ints
    negflag = 0;
    if(x < 0)
    {
        negflag |= 0x01;    // x flag bit
        x = (0 - x);        // is now +
    }
    ux = x!=0?x:1;                // copy to unsigned var before multiply
    if(y < 0)
    {
        negflag |= 0x02;    // y flag bit
        y = (0 - y);        // is now +
    }
    uy = y!=0?y:1;                // copy to unsigned var before multiply

    // 1. Calc the scaled "degrees"
    if(ux > uy)
    {
        degree = (uy * 45) / ux;   // degree result will be 0-45 range
        negflag += 0x10;    // octant flag bit
    }
    else
    {
        degree = (ux * 45) / uy;   // degree result will be 0-45 range
    }

    // 2. Compensate for the 4 degree error curve
    comp = 0;
    tempdegree = degree;    // use an unsigned char for speed!
    if(tempdegree > 22)      // if top half of range
    {
        if(tempdegree <= 44) comp++;
        if(tempdegree <= 41) comp++;
        if(tempdegree <= 37) comp++;
        if(tempdegree <= 32) comp++;  // max is 4 degrees compensated
    }
    else     // else is lower half of range
    {
        if(tempdegree >= 2) comp++;
        if(tempdegree >= 6) comp++;
        if(tempdegree >= 10) comp++;
        if(tempdegree >= 15) comp++;  // max is 4 degrees compensated
    }
    degree += comp;   // degree is now accurate to +/- 1 degree!
    // Invert degree if it was X>Y octant, makes 0-45 into 90-45
    if(negflag & 0x10) degree = (90 - degree);
    // 3. Degree is now 0-90 range for this quadrant,
    // need to invert it for whichever quadrant it was in
    if(negflag & 0x02)   // if -Y
    {
        if(negflag & 0x01)   // if -Y -X
            degree = (180 + degree);
        else        // else is -Y +X
            degree = (180 - degree);
    }
    else     // else is +Y
    {
        if(negflag & 0x01)   // if +Y -X
            degree = (360 - degree);
    }
    return degree;
}
//四份之一圆形mask
int SC_arc_set_mask(SC_arc *p,uint8_t *arc_mask,uint16_t mask_size)
{
    uint16_t r=(p->r+1);
    uint16_t rmax = (p->r+1)*(p->r+1);
    uint16_t r2 =    p->r*p->r;
    uint16_t inv_outer = (rmax == r2) ? 0 : (0xff00) / (rmax - r2);
    uint16_t temp;
    uint8_t k,alpha,max=0;
    int x,y;
    if(mask_size<rmax) return 0;            //内存不够
    for ( y = 0 ; y<r; y++)                 //1/4扇区计算
    {
        int dy2 = y*y;
        for (x=0; x <r; x++)
        {
            temp =x*x + dy2;
            if (temp>=rmax)
            {
                alpha=0;
            }
            else if (temp> r2)
            {
                alpha =(rmax - temp)*inv_outer>>8 ;//外边缘抗锯齿
            }
            else
            {
                alpha=255;
            }
            if(arc_mask)
            {
                arc_mask[y*r+x]=alpha;
            }
        }
    }
    return 1;
}

#endif

#if DRAW_ARC_EN
//-------圆头端点------------------------
typedef struct
{
    int16_t  cx;
    int16_t  cy;
    int16_t  r;
    uint16_t r2 ;
    uint16_t rmax ;
    uint16_t outer;
} SC_arc_dot;

//圆头端点坐标运算无浮点会有一定误差
void SC_dot_sin_cos(SC_arc *p,SC_arc_dot *dot,int sin, int cos)
{
    int len  = (p->r+p->ir)/2;
    int r    = (p->r-p->ir)/2;
    if(0==r) return;
    if(sin<0)
    {
        dot->cx =(sin*len-16348)/32768;  //圆心坐标4舍5入+0.5
    }
    else
    {
        dot->cx =(sin*len+16348)/32768;
    }
    if(cos<0)
    {
        dot->cy = (cos*len-16348)/32768;
    }
    else
    {
        dot->cy = (cos*len+16348)/32768;
    }
    dot->cx=p->cx-dot->cx;
    dot->cy=p->cy-dot->cy;
    dot-> r= r+1;
    dot-> r2 =  r*r;
    dot-> rmax = (r+1)*(r+1);
    dot-> outer=0;

}
//圆头端点计算
static inline uint8_t SC_get_dot(SC_arc_dot *dot,int ax,int ay)
{
    uint16_t temp;
    uint8_t k,alpha,max=0;
    SC_arc_dot *p=dot;
    for(k=0; k<2; k++,p++)
    {
        int x=ax>p->cx?ax-p->cx:p->cx-ax;
        int y=ay>p->cy?ay-p->cy:p->cy-ay;
        if(x<p->r&&y<p->r)
        {
            temp =x*x + y*y;
            if (temp>=p->rmax)
            {
                alpha=0;
            }
            else  if (temp>p->r2)
            {
                if(dot->outer==0)
                {
                    dot->outer = (0xff00) / (dot->rmax - dot->r2);  //除法优化
                }
                alpha  =(p->rmax - temp) *dot->outer>>8 ;//外边缘抗锯齿
            }
            else
            {
                alpha=255;
            }
            max=max>alpha?max:alpha;  //重合取最大的值
        }
    }
    return max;
}

/**抗锯齿画圆*/
//判断正负符号 ax==x  ay==y  为真
//#define CHECK_XY(x, y,ax, ay)   ((ax*x) < 0 || (ay*y) < 0)
static inline int CHECK_XY( int x, int y,int ax, int ay)
{
    return ((x != 0 && ((ax ^ x) < 0)) ||
            (y != 0 && ((ay ^ y) < 0)));
}
int _SC_DrawRound(SC_tile *dest,SC_arc *p,int start_deg, int end_deg,uint16_t ac,uint16_t bc,SC_arc_dot *arc_dot)
{
    SC_tile pfb;
    SC_ARER intersection;
    if(dest==NULL)
    {
        pfb.stup=0;
        dest=&pfb;
    }
    else
    {
        if(!SC_pfb_intersection(dest,&intersection,p->cx-p->r,p->cy-p->r,p->cx+p->r,p->cy+p->r))
        {
            return 0;
        }
    }
    //半径大于255要用int32_t位变量
    uint16_t r1 = p->ir * p->ir;
    uint16_t r2 = p->r * p->r;
    uint16_t rmax = (p->r + 1) * (p->r + 1);
    uint16_t rmin = p->ir > 0 ? (p->ir - 1) * (p->ir - 1) : 0;
    uint16_t inv_inner = 0;
    uint16_t inv_outer = 0;
    uint16_t temp,tbc,*dest_out;
    //--------------角度圆mask----------------
    uint8_t alpha,in_range,big=0xff;
    int16_t ds,de,sd,ed;     //半径大于255要用int32_t
    int16_t sx,sy,ex,ey;
    int16_t x, y,dx,dy,dy2;

    uint16_t fc=ac;
    if(start_deg!=0||end_deg!=360)
    {
        /* ---------- 角度转Q16查表定点向量 ---------- */
        big = (end_deg - start_deg > 180)?1:0;
        sx = sc_sin(start_deg);
        sy =  -sc_cos(start_deg);
        ex = sc_sin(end_deg);
        ey =  -sc_cos(end_deg);
        if(arc_dot)
        {
            SC_dot_sin_cos(p,&arc_dot[0],sx,sy);
            SC_dot_sin_cos(p,&arc_dot[1],ex,ey);
        }
        sx=sx>>7;
        sy=sy>>7;
        ex=ex>>7;
        ey=ey>>7;
    }
    do
    {
        if(dest==&pfb)
        {
            SC_pfb_clip(&pfb,p->cx-p->r,p->cy-p->r,p->cx+p->r,p->cy+p->r,gui->bkc);   //创建局部pfb
            SC_pfb_intersection(dest,&intersection,p->cx-p->r,p->cy-p->r,p->cx+p->r,p->cy+p->r);
        }
        int dest_offs=(intersection.ys-dest->ys) * dest->w-dest->xs;
        for (y = intersection.ys; y <= intersection.ye; y++,dest_offs+=dest->w)
        {
            dy = y - p->cy;
            dy2=(y - p->cy)*(y - p->cy);
            for (x = intersection.xs; x <= intersection.xe; x++)
            {
                dx = x - p->cx;
                temp = (dx) * (dx) +dy2;
                if (temp >= rmax)
                {
                    if (x > p->cx) break;  // 大于 rmax 外圆跳过
                    continue;
                }
                if (temp < rmin)
                {
                    if (x < p->cx) x = p->cx * 2 - x; // 小于于 rmin 内圆跳过
                    continue;
                }
                if (temp < r1)
                {
                    if(inv_inner==0)
                    {
                        inv_inner = (0xff00) / (r1 - rmin) ;  //除法优化
                    }
                    alpha  =(temp - rmin) *inv_inner>>8 ;//内边缘抗锯齿
                }
                else if (temp > r2)
                {
                    if(inv_outer==0)
                    {
                        inv_outer = (0xff00) / (rmax - r2);  //除法优化
                    }
                    alpha  =(rmax - temp) *inv_outer>>8 ;//外边缘抗锯齿
                }
                else
                {
                    alpha = 255;
                }
                dest_out=dest->buf+dest_offs+x;
                temp=fc;
                if(big!=255)
                {
                    ds = (dx *  sy - dy *  sx); // 到起始点的方向
                    de = (dy *  ex - dx *  ey); // 到结束点的方向
                    in_range =  big>0 ? (ds > 0 || de >0) : (ds >= 0 && de >= 0);
                    if (!in_range)                  //弧外
                    {
                        tbc= (bc==gui->bkc)?  *dest_out:bc;  //背景色是不是透明
                        if(arc_dot==NULL)                    //无内存
                        {
                            /* 弧外1px插值符号相同否则为反向线*/
                            sd=   CHECK_XY(dx,dy, sx, sy) ?SC_ABS(ds):256;
                            ed=   CHECK_XY(dx,dy, ex, ey) ?SC_ABS(de):256;
                            dx =  SC_MIN(sd, ed);
                            temp=(dx<255) ? alphaBlend(fc,tbc,SC_MIN(255-dx, alpha)):tbc;
                        }
                        else
                        {
                            dx= SC_get_dot(arc_dot,x,y);    //圆头读点
                            temp=(dx<255)?alphaBlend(fc,tbc,dx):fc;
                        }
                    }
                }
                *dest_out=alphaBlend(temp,*dest_out,alpha*gui->alpha/255);
            }
        }
    }
    while((dest==&pfb)&&SC_pfb_Refresh(&pfb,0));
    return 1;
}

/**fun: pfb画圆弧进度条*/
void SC_pfb_DrawArc(SC_tile *dest,SC_arc *p,int start_deg, int end_deg,uint16_t ac,uint16_t bc,uint8_t dot)
{
    SC_arc_dot  arc_dot[2];    //端点
    if(dot)
    {
        _SC_DrawRound(dest,p,start_deg,end_deg, ac,bc,arc_dot);
    }
    else
    {
        _SC_DrawRound(dest,p,start_deg,end_deg, ac,bc,NULL);
    }
}

/**fun: pfb圆角矩形*/
int SC_pfb_RoundFrame(SC_tile *dest,int xs,int ys,int xe,int ye, int r,int ir, uint16_t ac,uint16_t bc)
{
    SC_tile pfb;
    SC_ARER intersection;
    if(dest==NULL)
    {
        pfb.stup=0;
        dest=&pfb;
    }
    else
    {
        if(!SC_pfb_intersection(dest,&intersection,xs,ys,xe,ye))
        {
            return 0;
        }
    }
    uint16_t r1 =   ir*ir;
    uint16_t r2 =   r*r;
    uint16_t rmax = (r+1)*(r+1);
    uint16_t rmin=  ir>0? (ir-1)*(ir-1):0;
    uint16_t inv_inner = 0;
    uint16_t inv_outer = 0;
    int16_t ax=xs+r,ay=ys+r;
    int16_t bx=xe-r,by=ye-r;
    int16_t x,y,dy2,dx2,temp;
    uint16_t alpha,*dest_out;
    do
    {
        if(dest==&pfb)
        {
            SC_pfb_clip(&pfb,xs,ys,xe,ye,gui->bkc);   //创建局部pfb
            SC_pfb_intersection(dest,&intersection,xs,ys,xe,ye);
        }
        int dest_offs=(intersection.ys-dest->ys) * dest->w-dest->xs;
        for ( y = intersection.ys; y<=intersection.ye; y++,dest_offs+=dest->w)
        {
            if(y>ay&&y<by)       //中间填充
            {
                for ( x = intersection.xs; x <=intersection.xe; x++)
                {
                    dest_out=dest->buf+dest_offs+x;
                    if(x<=ax-ir||x>=bx+ir)     //左右垂直线，线宽判断
                    {
                        *dest_out=alphaBlend(ac,*dest_out,gui->alpha);
                    }
                    else
                    {
                        if(bc!=gui->bkc&&x<=gui->lcd_area.xe&& y>=gui->lcd_area.ys)
                        {
                            *dest_out=alphaBlend(bc,*dest_out,gui->alpha);
                        }
                    }
                }
                continue;
            }
            dy2 =y>ay?(y-by)*(y-by):(y-ay)*(y-ay);
            for ( x = intersection.xs; x <=intersection.xe; x++,dest_out++)
            {
                dest_out=dest->buf+dest_offs+x;
                if(x>ax&&x<bx)                    //(x>ax&&x<bx)
                {
                    if(dy2>=r1)             //上下水平直线，线宽判断
                    {
                        *dest_out=alphaBlend(ac,*dest_out,gui->alpha);
                    }
                    else
                    {
                        if(bc!=gui->bkc&&x<=gui->lcd_area.xe&& y>=gui->lcd_area.ys)
                        {
                            *dest_out=alphaBlend(bc,*dest_out,gui->alpha);
                        }
                    }
                    continue;
                }
                dx2 =x>ax?(x-bx)*(x-bx):(x-ax)*(x-ax);
                temp =dx2 + dy2;
                if (temp>=rmax)
                {
                    if(x>bx) break;
                    continue;          //大于rmax外圆跳过
                }
                if (temp<r1)
                {
                    if(inv_inner==0)
                    {
                        inv_inner = (0xff00) / (r1 - rmin);  //除法优化
                    }
                    if(bc!=gui->bkc&&x<=gui->lcd_area.xe&& y>=gui->lcd_area.ys)
                    {
                        uint16_t c= bc;
                        if (temp>rmin)
                        {

                            alpha = (temp - rmin) *inv_inner>>8; //内边缘抗锯齿
                            c=alphaBlend(ac,bc,alpha);
                        }
                        *dest_out=alphaBlend(c,*dest_out,gui->alpha);
                    }
                    else
                    {
                        if (temp>rmin)
                        {
                            alpha = (temp - rmin) *inv_inner>>8; //内边缘抗锯齿
                            *dest_out=alphaBlend(ac,*dest_out,alpha*gui->alpha/256);
                        }
                    }
                }
                else if (temp> r2)
                {
                    if(inv_outer==0)
                    {
                        inv_outer = (0xff00) / (rmax - r2);  //除法优化
                    }
                    alpha  =(rmax - temp) *inv_outer>>8 ;//外边缘抗锯齿
                    *dest_out=alphaBlend(ac,*dest_out,alpha*gui->alpha/256);
                }
                else
                {
                    *dest_out=alphaBlend(ac,*dest_out,gui->alpha);
                }
            }
        }
    }
    while((dest==&pfb)&&SC_pfb_Refresh(&pfb,0));
    return 1;
}

/***************************************************************************************
** Function name:        SC_pfb_RoundBar
** Description:          画一个圆角进度条
***************************************************************************************/
void SC_pfb_RoundBar(SC_tile *dest,int xs,int ys,int xe,int ye,int r,int ir,uint16_t ac,uint16_t bc,int vol,int max)
{
    int w=xe-xs+1;
    int h=ye-ys+1;
    if(r>w/2) r=w/2;
    if(r>h/2) r=h/2;
    if(w>h)
    {
        gui->lcd_area.xe= (xs)+vol*w/max-1;
        SC_pfb_RoundFrame(dest,xs,ys,xe,ye, r, ir,  ac, bc);
        gui->lcd_area.xe=LCD_SCREEN_WIDTH-1;
    }
    else
    {
        gui->lcd_area.ys= (ys)+ h-vol*h/max-1;
        SC_pfb_RoundFrame(dest,xs,ys,xe,ye, r, ir,  ac, bc);
        gui->lcd_area.ys=0;
    }
}

#endif // DRAW_ARC_EN

///======================图片旋转缩放=====================================
#if DRAW_TRANSFORM_EN
static inline void Bilinear_Interpolate(const SC_img_t *Src, int32_t srcx, int32_t srcy,uint16_t *dst)
{
    uint16_t p00,p01,p10,p11;
    int16_t xe= Src->w-2;
    int16_t ye= Src->h-2;
    int16_t x = (srcx) >> 15;  // 计算整数部分，定位到像素位置
    int16_t y = (srcy) >> 15;
    uint8_t fx = (srcx )>> 7;// 取余alpha
    uint8_t fy = (srcy )>> 7;
    int32_t offs= y*Src->w+x;
    uint8_t *mask=(uint8_t *)Src->mask;
    uint16_t *src=(uint16_t*)Src->map;
    p00 = src[offs];
    p10 = src[offs+1];
    p01 = src[offs+Src->w];
    p11 = src[offs+Src->w+1];
    if(mask)
    {
        p00 = (x<0 ||y<0) ? *dst:alphaBlend(p00,*dst,mask[offs]);                 // 左上角像素
        p10 = (x>xe||y<0) ? *dst:alphaBlend(p10,*dst,mask[offs+1]);               // 右上角像素
        p01 = (y>ye||x<0) ? *dst:alphaBlend(p01,*dst,mask[offs+Src->w]);          // 左下角像素
        p11 = (y>ye||x>xe)? *dst:alphaBlend(p11,*dst,mask[offs+Src->w+1]);// 右下角像素
    }
    else
    {
        if(x<0 ||y<0)         p00 =*dst;// 左上角像素
        if(x>xe||y<0)         p10 =*dst;// 右上角像素
        if(y>ye||x<0)         p01 =*dst;// 左下角像素
        if(y>ye||x>xe)        p11 =*dst;// 右下角像素
    }
    // 在X方向进行权重插值
    p00 = alphaBlend(p00, p10, 255 - fx);  // 左上和右上的插值
    p01 = alphaBlend(p01, p11, 255 - fx);  // 左下和右下的插值
    *dst= alphaBlend(p00, p01, 255-  fy);   // 在Y方向进行权重插值，返回最终结果
}

//设置旋转角度，新增函数用于触摸
void SC_set_transform(Transform *p,const SC_img_t* Src, int16_t Angle)
{
    p->sinA = -sc_sin(Angle);   //符号控制正反转
    p->cosA =  sc_cos(Angle);
    int32_t corners[4][2] =     // 定义源图像的四个角点
    {
        {0, -1},                  // 左上角
        {Src->w, 0},             // 右上角
        {0, Src->h},             // 左下角
        {Src->w, Src->h}         // 右下角
    };
    // 正映射缩放系数乘法
    int32_t Ax_16 = (p->cosA * p->scaleX )>>8;  //cosAX
    int32_t Ay_16 = (p->sinA * p->scaleY )>>8;  //sinAX
    int32_t Bx_16 = (p->sinA * p->scaleX )>>8;  //sinAY
    int32_t By_16 = (p->cosA * p->scaleY )>>8;  //cosAY
    //-----------旋转后的窗口------------------
    if(p->last)
    {
        *(p->last)= p->abs;
    }
    p->abs.xe =0;
    p->abs.ye =0;
    p->abs.xs = LCD_SCREEN_WIDTH;
    p->abs.ys = LCD_SCREEN_HEIGHT;
    for (int i = 0; i < 4; ++i)
    {
        // 将每个角点平移到新的旋转中心坐标系
        int32_t translatedX = corners[i][0] - p->center_x;
        int32_t translatedY = corners[i][1] - p->center_y;
        // 正映射旋转并缩放返回坐标
        int32_t rotatedX  = ( translatedX * Ax_16 + translatedY * Bx_16)/Q15 + p->move_x;
        int32_t rotatedY  = (-translatedX * Ay_16 + translatedY * By_16)/Q15 + p->move_y;
        // 更新边界框
        p->abs.xs = SC_MIN(p->abs.xs, rotatedX);
        p->abs.ys = SC_MIN(p->abs.ys, rotatedY);
        p->abs.xe = SC_MAX(p->abs.xe, rotatedX);
        p->abs.ye = SC_MAX(p->abs.ye, rotatedY);
    }
    if(p->last)
    {
        p->last->xs = SC_MIN(p->abs.xs, p->last->xs);
        p->last->ys = SC_MIN(p->abs.ys, p->last->ys);
        p->last->xe = SC_MAX(p->abs.xe, p->last->xe);
        p->last->ye = SC_MAX(p->abs.ye, p->last->ye);
    }
}

void SC_pfb_transform(SC_tile *dest,const SC_img_t *Src,Transform *p)
{
    SC_ARER intersection;
#if SC_DEBUG
    SC_pfb_DrawFrame(dest, p->abs.xs,  p->abs.ys,  p->abs.xe,  p->abs.ye,C_RED);
#endif
    if(SC_pfb_intersection(dest,&intersection,p->abs.xs,  p->abs.ys,  p->abs.xe,  p->abs.ye))
    {
        // 反映射缩放系数是除
        int32_t  Ax_16 = (p->cosA * 256/p->scaleX);  //cosAX
        int32_t  Ay_16 = (p->sinA * 256/p->scaleX);  //sinAX
        int32_t  Bx_16 = (p->sinA * 256/p->scaleY);  //sinAY
        int32_t  By_16 = (p->cosA * 256/p->scaleY);  //cosAY
        //--------------------------------------------------
        int32_t dest_offs=(intersection.ys-dest->ys) * dest->w -dest->xs;
        for (int y = intersection.ys; y <= intersection.ye; y++,dest_offs+=dest->w)
        {
            int32_t Cx_16 =  Bx_16 * (y-p->move_y)- p->center_x*Q15;
            int32_t Cy_16 =  By_16 * (y-p->move_y)+ p->center_y*Q15;
            uint8_t _end=0;
            for (int x = intersection.xs; x <= intersection.xe; x++)
            {
                int32_t rotatedX =  Ax_16*(x-p->move_x) - Cx_16;
                int32_t rotatedY =  Ay_16*(x-p->move_x) + Cy_16;
                // 转换到源图像坐标
                int32_t srcIntX = rotatedX/Q15;
                int32_t srcIntY = rotatedY/Q15;
                // 确保源坐标在有效范围内
                if (srcIntX >=0 && srcIntX < Src->w && srcIntY>=0 && srcIntY < Src->h)
                {
                    uint16_t *dest_out=dest->buf+dest_offs+x;
                    //*dest_out=Src->map[srcIntY*Src->w+srcIntX];
                    Bilinear_Interpolate(Src,rotatedX,rotatedY,dest_out); //插值
                    _end=1;
                }
                else if(_end)
                {
                    break;
                }
            }

        }
    }
}

#endif // DRAW_TRANSFORM_EN

///=====================快速反锯齿画线=====================
#if DRAW_LINE_EN
void SC_pfb_DrawLine_AA(SC_tile *dest,int x1, int y1, int x2, int y2,uint16_t colour)
{
    int dx = x2 - x1;
    int dy = y2 - y1;
    int dxabs = SC_ABS(dx);  // 宽度
    int dyabs = SC_ABS(dy);  // 高度
    int xs=SC_MIN(x1,x2);
    int ys=SC_MIN(y1,y2);
    SC_ARER intersection;
    if(!SC_pfb_intersection(dest,&intersection,xs,  ys,  xs+dxabs,  ys+dyabs))  //加一个像素
    {
        return ;
    }
    int sgndx = (dx < 0) ? -1 : 1;                  // 确保x方向正确
    int sgndy = (dy < 0) ? -1 : 1;                  // 确保y方向正确
    int majorStep = dxabs > dyabs ? dxabs : dyabs;  // 主方向步长
    int minorStep = dxabs > dyabs ? dyabs : dxabs;  // 次方向步长
    int error = majorStep/2;                        // 初始化误差项
    if (dxabs > dyabs)                              // 横向线条的插值
    {
        error=minorStep*(intersection.xs-xs)+majorStep/4;  //0.25
        for (x1 = intersection.xs; x1 <= intersection.xe; x1++)
        {
            uint16_t a = (error << 8)/majorStep&0xff;  // 插值计算;
            if((sgndy*sgndx)>0)
            {
                y1=  error/majorStep+ys;
            }
            else
            {
                y1= - error/majorStep+ys+dyabs;
                a = 255-a;  // 插值计算
            }
            int dest_offs=(y1-dest->ys) * dest->w -dest->xs+x1;
            if(y1 >= intersection.ys && y1 <= intersection.ye)
            {
                set_pixel_value(dest, dest_offs, 255, colour);
            }
            if(y1+1 >= intersection.ys && y1+1 <= intersection.ye)
            {
                set_pixel_value(dest, dest_offs+dest->w, a, colour);
            }
            if(y1-1 >= intersection.ys && y1-1 <= intersection.ye)
            {
                set_pixel_value(dest, dest_offs-dest->w, 255-a, colour);
            }
            error += minorStep;
        }
    }
    else
    {
        error+=minorStep*(intersection.ys-ys)+majorStep/4; //0.25
        for (y1 = intersection.ys; y1 <= intersection.ye; y1++)
        {
            uint16_t a = (error << 8)/majorStep&0xff;  // 插值计算;
            if((sgndy*sgndx)>0)
            {
                x1=  error/majorStep+xs;
            }
            else
            {
                x1=  -error/majorStep+xs+dxabs;
                a=255-a;
            }
            int dest_offs=(y1-dest->ys) * dest->w -dest->xs+x1;
            if(x1 >= intersection.xs && x1 <= intersection.xe)
            {
                set_pixel_value(dest, dest_offs, 255, colour);
            }
            if(x1+1 >= intersection.xs && x1+1 <= intersection.xe)
            {
                set_pixel_value(dest, dest_offs+1, a, colour);
            }
            if(x1-1 >= intersection.xs && x1-1 <= intersection.xe)
            {
                set_pixel_value(dest, dest_offs-1, 255-a, colour);
            }
            error+=minorStep;
        }
    }
}
#endif

///=====================SDF快速反锯齿画线=====================
#if DRAW_LINE_SDF_EN
#include <stdint.h>
#include <math.h>
// SDF 绘制带有胶囊体的线段
void SC_DrawLine_SDF(float ax, float ay, float bx, float by, float r, uint16_t color)
{

    float dx =  bx - ax;
    float dy =  by - ay;
    float dxy2= 1/(dx * dx + dy * dy);
    float rmin = (r) * (r)-r;  // 半径的平方
     float rmax = (r) * (r)+r;  // 半径的平方
    // 计算绘制范围的边界
    int x0 = (int)floorf(fminf(ax, bx) - r);
    int x1 = (int) ceilf(fmaxf(ax, bx) + r);
    int y0 = (int)floorf(fminf(ay, by) - r);
    int y1 = (int) ceilf(fmaxf(ay, by) + r);

    // 遍历矩形区域内的每个像素
    for (int y = y0; y <= y1; y++)
    {
        uint8_t _end=0;
        float pay = (y - ay);
        for (int x = x0; x <=x1; x++)   //遍历胶囊体的左右范围
        {
            float  pax = (x - ax);
            // 计算点在单位方向上的投影值
            float A = (pax * dx + pay * dy) * dxy2;
            if (A < 0) A = 0;
            if (A > 1) A = 1;
            float distX = (pax - (dx * A));
            float distY = (pay - (dy * A));
            // 计算当前点到线段的平方距离
            float distSq = (distX * distX + distY * distY);
            if(distSq <= rmin)
            {
                gui->bsp_pset(x, y, color);
                _end=1;
            }
            else if(distSq < rmax)
            {
                float alpha=0.5 - (sqrtf(distSq)-r);
                uint16_t pcol =alphaBlend(color, gui->bkc,alpha*255);
                gui->bsp_pset(x, y, pcol);
                _end=1;
            }
            else if(_end)
            {
                break;
            }
        }
    }
}


#endif

