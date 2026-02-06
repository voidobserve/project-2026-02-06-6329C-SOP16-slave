
#include "sc_gui.h"
//#include "lcd.h"
///DMA中断完成标志
#define dma_wait_ok()  //do{}while(JL_SPI1->CON& (1<<13))

extern void LCD_DMA_Fill_COLOR(int xsta, int ysta, int xend, int yend, u16 *color);
//底层接口，与lvgl接口一样
void LCD_DMA_color(int xs, int ys, int xe, int ye, u16 *color)
{
#if 1
    dma_wait_ok();
    LCD_DMA_Fill_COLOR(xs, ys, xe, ye, color);
#else
    //-----------------无DMA------------------------
    // LCD_SetWindows(xs, ys,xe, ye);
    for(int y = ys; y <=ye; y++)
    {
        for(int x = xs; x <=xe; x++)
        {
            gui->bsp_pset (x,y,*color);
            color++;
        }
    }
#endif
}

///SC_Refresh刷屏
void SC_Refresh(int xs, int ys,uint16_t w, uint16_t h,uint16_t *buf)
{
#if LCD_DMA_WAP
    uint32_t len=w*h;
    for(uint32_t i=0; i<len; i++)
    {
        if(buf[i])
        {
            buf[i]=buf[i]>>8|buf[i]<<8;    //高低位WAP
        }
    }
#endif
    dma_wait_ok();
    LCD_DMA_color(xs, ys,xs+w-1, ys+h-1,buf);
}

/**fun: pfb分割*/
int SC_pfb_clip(SC_tile *clip, int xs,int ys,int xe,int ye,uint16_t colour)
{
    ys= SC_MAX(ys,gui->lcd_area.ys);
    xs= SC_MAX(xs,gui->lcd_area.xs);
    xe= SC_MIN(xe,gui->lcd_area.xe);
    ye= SC_MIN(ye,gui->lcd_area.ye);
    if(xs>xe||ys>ye) return 1;
    int16_t width= xe-xs+1;
    int16_t height=ye-ys+1;
#if LCD_DMA_ISR_EN
    clip->buf = (clip->buf != gui->pfb_buf) ? gui->pfb_buf : gui->pfb_buf + SC_PFB_BUF_SIZE / 2;
    int max_height = SC_PFB_BUF_SIZE / (width * 2); // 根据帧缓冲区大小计算最大高度
#else
    clip->buf= gui->pfb_buf;
    int max_height = SC_PFB_BUF_SIZE / (width); // 根据帧缓冲区大小计算最大高度
#endif
    clip->num = (height + max_height - 1) / max_height; // 向上取整
    clip->xs = xs;
    clip->ys = ys + clip->stup* max_height;
    clip->w  = width;
    clip->h =(clip->stup == clip->num-1)? height - clip->stup * max_height:max_height; // 最后一个分段可能高度不同
    //-------------初始化pfb内存--------------------
    uint32_t ctemp=(colour<<16)|colour;
    uint32_t len=width*clip->h;
    uint32_t *p=(uint32_t *)clip->buf;
    for(uint32_t i=0; i<len/2; i++)
    {
        *p++=ctemp;    //4字节对齐
    }
    clip->buf[len-1]=colour;
    return 0;
}

///计算相交区
int SC_pfb_intersection(SC_tile *dest,SC_ARER *p,int xs,int ys,int xe,int ye)
{
    if(dest==NULL)  return 0;
    if(gui->parent)
    {
        ys= SC_MAX(ys,gui->parent->ys);
        ye= SC_MIN(ye,gui->parent->ye);
        xs= SC_MAX(xs,gui->parent->xs);
        xe= SC_MIN(xe,gui->parent->xe);
    }
    p->ys= SC_MAX(ys,dest->ys);             //ys边界限定
    p->ye= SC_MIN(ye,dest->ys+dest->h-1);   //ye边界限定
    if(p->ys>p->ye) return 0;
    p->xs= SC_MAX(xs,dest->xs);            //xs边界限定
    p->xe= SC_MIN(xe,dest->xs+dest->w-1);  //xe边界限定
    if(p->xs>p->xe) return 0;
    return 1;     //相交
}
/**fun: pfb输出到屏，skip跳过背景像素*/
int SC_pfb_Refresh(SC_tile *dest, uint8_t skip)
{
    int width =  dest->w;
    int height = dest->h;
    uint16_t *buf=dest->buf;
    if(++dest->stup>=dest->num)
    {
        dest->stup=0;   //记录刷新位置
    }
    if(skip==0)
    {
        SC_Refresh(dest->xs,dest->ys,width, height,buf);
        return dest->stup;
    }
    ///用于只刷边框或圆环，跳过中间象素拆行刷
    for(int y=dest->ys; y<dest->ys+height; y++)
    {
        int len=0;
        int st=-1;
        buf=&dest->buf[(y-dest->ys)*width];
        for(int x=0; x<width; x++)
        {
            if(buf[x]==gui->bkc)              //skip
            {
                while(buf[x+1]==gui->bkc) x++;
                if(len)
                {
                    SC_Refresh(dest->xs+st,y,len,1,&buf[st]);
                }
                len=0;
                st=-1;
            }
            else
            {
                if(st==-1) st=x;
                len++;
            }
        }
        if(len)
        {
            SC_Refresh(dest->xs+st,y,len, 1, &buf[st]);
        }
    }
    return dest->stup;
}

/**fun: 矩形填充*/
void SC_pfb_DrawFill(SC_tile *dest,int xs,int ys,int xe,int ye,uint16_t fc)
{
    if(xs>xe||ys>ye)
    {
        return;
    }
    if(dest)
    {
        //===========计算相交===============
        SC_ARER intersection;
        if(!SC_pfb_intersection(dest,&intersection,xs,ys,xe,ye))
        {
            return;
        }
        int dest_offs=(intersection.ys-dest->ys) * dest->w -dest->xs;
        for (int y = intersection.ys; y <=intersection.ye; y++,dest_offs+=dest->w)
        {
            uint16_t *dest_out=dest->buf+dest_offs+intersection.xs;
            for (int x = intersection.xs; x <=intersection.xe; x++)
            {
                *dest_out=alphaBlend(fc,*dest_out,gui->alpha);
                dest_out++;
            }
        }
        return;
    }
    SC_tile pfb;
    pfb.stup=0;
    uint16_t colour= (fc==gui->bkc)?gui->bkc:alphaBlend(fc,0,gui->alpha);
    do
    {
        SC_pfb_clip(&pfb,xs,ys,xe,ye,colour);//创建局部pfb
    }
    while(SC_pfb_Refresh(&pfb,0));
}

/**fun: 空芯矩形*/
void SC_pfb_DrawFrame(SC_tile *dest, int xs, int ys, int xe, int ye,uint16_t fc)
{
    SC_pfb_DrawFill(dest, xs, ys, xe, ys,fc);      //上
    SC_pfb_DrawFill(dest, xs, ye, xe, ye,fc);      //下
    SC_pfb_DrawFill(dest, xs, ys+1, xs, ye-1,fc);  //左
    SC_pfb_DrawFill(dest, xe, ys+1, xe, ye-1,fc);  //右
}

///**fun: 清屏*/
void SC_Clear(int xs,int ys,int xe,int ye)
{
    SC_pfb_DrawFill(NULL, xs, ys, xe, ye,gui->bkc);
}
///**fun:打点画线*/
void SC_DrawLine_AA(int x1, int y1, int x2, int y2,uint16_t colour)
{
    int dx = x2 - x1;
    int dy = y2 - y1;
    int sgndx = (dx < 0) ? -1 : 1;  // 确保x方向正确
    int sgndy = (dy < 0) ? -1 : 1;  // 确保y方向正确
    int dxabs = SC_ABS(dx);  // 宽度
    int dyabs = SC_ABS(dy);  // 高度
    uint8_t isHorizontal = dxabs > dyabs;  // 判断主方向
    int majorStep = isHorizontal ? dxabs : dyabs;  // 主方向步长
    int minorStep = isHorizontal ? dyabs : dxabs;  // 次方向步长
    int error =0;// majorStep/2;  // 初始化误差项
    uint16_t abuf[3];
    for (int i = 0; i <= majorStep; i++)
    {
        int a = (error << 8) / majorStep;  // 插值计算
        abuf[0]=alphaBlend(colour,gui->bkc, 255-a);
        abuf[1]=colour;
        abuf[2]=alphaBlend(colour,gui->bkc, a);
        if (isHorizontal)                   // 横向线条的插值
        {
            if(sgndy<0)
            {
                uint16_t temp= abuf[2];
                abuf[2]= abuf[0];
                abuf[0]=temp;
            }
            SC_Refresh(x1,y1-1,1,3,abuf);
            x1 += sgndx;                    // 更新x坐标
        }
        else                                // 纵向线条的插值
        {
            if(sgndx<0)
            {
                uint16_t temp= abuf[2];
                abuf[2]= abuf[0];
                abuf[0]=temp;
            }
            SC_Refresh(x1-1,y1,3,1,abuf);
            y1 += sgndy;                    // 更新y坐标
        }
        // 更新误差值
        error += minorStep;
        if (error >= majorStep)
        {
            error -= majorStep;
            // 更新次方向坐标
            if (isHorizontal)
            {
                y1 += sgndy;  // 横向时调整y
            }
            else
            {
                x1 += sgndx;  // 纵向时调整x
            }
        }
    }
}

///**fun: 直接解压缩图片，速度快*/
//void SC_Show_Image_zip(int xs,int ys,const SC_img_zip *zip)
//{
//    uint16_t r,g,b;
//    uint16_t dat16,unzip,out;
//    const u8 *buf=zip->map;
//    uint32_t n=0;
//    uint16_t rep_cnt;
//    for(uint32_t n=0; n<zip->len; n++)
//    {
//        if(buf[n]&0x20)
//        {
//            dat16=(buf[n+1]<<8)|buf[n];
//            if(unzip==dat16)
//            {
//                n+=2;
//                rep_cnt=(buf[n]<<8)|buf[n+1];  //重复的长度
//                while(rep_cnt--)
//                {
//                    gui->dma_prt[gui->dma_i++]= out;
//                    if(gui->dma_i>=zip->w)
//                    {
//                        gui->Refresh(xs, ys,gui->dma_i, 1,gui->dma_prt);  //gui->dma_i=0
//                        ys++;
//                    }
//                }
//            }
//            else
//            {
//                unzip= dat16;
//                out  = dat16;
//                gui->dma_prt[gui->dma_i++]= out;
//            }
//            n++;
//        }
//        else
//        {
//            b=zip->map[n];
//            r=(b<<5)&0x1800;
//            g=(b<<3)&0x00e3;
//            out=unzip^(r+g+(b&0x03));
//            gui->dma_prt[gui->dma_i++]= out;
//        }
//        if(gui->dma_i>=zip->w)
//        {
//            gui->Refresh(xs, ys,gui->dma_i, 1,gui->dma_prt);  //gui->dma_i=0
//            ys++;
//        }
//    }
//}
////RGB565图片数据压缩,返回压缩后的尺寸，上位机算法
//u32 RGB565_zip_in(u8 *buf,const u8 *map,u32 w,u32 h)
//{
//    u8 r,g,b;
//    u32 n=0;
//    u32 len=w*h;
//    uint16_t xor,mask=(~0x18e3);
//    uint16_t *dat=(uint16_t*)map;
//    //压缩：两个像素高位相同异或后高位为0
//    // R      G      B
//    //00011 000 11x 00011   x=1表示unzip    ff 1f
//    //000rr 000ggg 000bb   压缩为u8格式rrxgggbb
//    //解压：判断压缩标志进行异或还原
//    //rrxgggbb 解压回uint16_t格式000rr 000ggg 000bb
//    uint16_t unzip=dat[0]|0x0020;     //第一字节不压缩
//    buf[n++]=(unzip);
//    buf[n++]=unzip>>8;
//    u32 rep_cnt=0;
//    for(u32 i=1; i<len; i++)
//    {
//        xor= unzip^dat[i];
//        if(!(xor&mask))             //异或判断数据相似
//        {
//            r=(xor&0x1800)>>5;
//            g=(xor&0x00e0)>>3;
//            b=xor&0x0003;
//            buf[n++]=r|g|b;        //压缩为u8格式
//            //---------完全相等压缩---------------
//            rep_cnt=0;
//            while((dat[i])==(dat[rep_cnt+i+1]))
//            {
//                if(++rep_cnt>=0x1fff) break; //长度限制高位用作0x0020标志
//                if(rep_cnt+i>=len)    break;
//            }
//            if(rep_cnt>=4)                     //4个重复才压缩
//            {
//                buf[n++]= unzip;
//                buf[n++]= unzip>>8;
//                // printf("\r\nrep_cnt=%d n=%d",rep_cnt,n);
//                buf[n++]=(rep_cnt>>8);         //高低位互换0xff1f
//                buf[n++]=(rep_cnt);
//                i+=rep_cnt;
//            }
//        }
//        else
//        {
//            unzip=dat[i]|0x0020;
//            buf[n++]=unzip;
//            buf[n++]=unzip>>8;
//        }
//    }
//    len<<=1;
//    // printf("\r\n map_len=%d,buf_len=%d zip=%f/100  \r\n",len,n,(n*100/(float)len));
//    return n;
//}
