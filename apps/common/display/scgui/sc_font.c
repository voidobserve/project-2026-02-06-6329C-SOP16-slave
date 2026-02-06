#include "sc_gui.h"

//---------------外置Flash接口-------------------------------
//#include  <SDL2/SDL.h>
int SPIFLASH_Read(uint8_t *buf,uint32_t offset,uint32_t size)
{
    int err=0;
//    SDL_RWops *fs;
//    fs = SDL_RWFromFile("SCGUI/myFont_bin12.bin", "rb");
//    if( fs  == NULL)
//    {
//        printf("open myFont_11.bin failed\n");
//        return -1;
//    }
//    err=  SDL_RWseek(fs,offset, RW_SEEK_SET);
//    err=  SDL_RWread(fs,buf, size, 1);
    return err;
}

///=============================字体位图================================================
/**fun: get_bpp_value*/
static inline uint16_t get_bpp_value(const uint8_t *buffer,uint16_t offset, const uint8_t bpp)
{
    uint16_t alpha=0;
    switch (bpp)
    {
    case 8:
        alpha= (buffer[offset]);
        break;
    case 4:
        alpha= (buffer[offset / 2] >> (4 * (1 - (offset % 2)))) & 0x0F;
        alpha= alpha*17;
        break;
    case 2:
        alpha= (buffer[offset / 4] >> (2 * (3 - (offset % 4)))) & 0x03;
        alpha= alpha*85;
        break;
    case 1:
        alpha= (buffer[offset / 8] >> (7 - (offset % 8))) & 0x01 ? 255 : 0;
        break;
    default:
        break;
    }
    return  alpha;
}
///====================== LVGL字体=====================================
//将utf-8编码转为unicode编码（函数来自LVGL）
uint32_t lv_txt_utf8_next(const char * txt, uint32_t * i)
{
    uint32_t result = 0;
    uint32_t i_tmp = 0;
    if(i == NULL) i = &i_tmp;
    if((txt[*i] & 0x80) == 0)     //Normal ASCII
    {
        result = txt[*i];
        (*i)++;
    }
    else    //Real UTF-8 decode
    {
        // bytes UTF-8 code
        if((txt[*i] & 0xE0) == 0xC0)
        {
            result = (uint32_t)(txt[*i] & 0x1F) << 6;
            (*i)++;
            if((txt[*i] & 0xC0) != 0x80) return 0; //Invalid UTF-8 code
            result += (txt[*i] & 0x3F);
            (*i)++;
        }
        //3 bytes UTF-8 code
        else if((txt[*i] & 0xF0) == 0xE0)
        {
            result = (uint32_t)(txt[*i] & 0x0F) << 12;
            (*i)++;

            if((txt[*i] & 0xC0) != 0x80) return 0;
            result += (uint32_t)(txt[*i] & 0x3F) << 6;
            (*i)++;

            if((txt[*i] & 0xC0) != 0x80) return 0;
            result += (txt[*i] & 0x3F);
            (*i)++;
        }
        else if((txt[*i] & 0xF8) == 0xF0)
        {
            result = (uint32_t)(txt[*i] & 0x07) << 18;
            (*i)++;
            if((txt[*i] & 0xC0) != 0x80) return 0;
            result += (uint32_t)(txt[*i] & 0x3F) << 12;
            (*i)++;

            if((txt[*i] & 0xC0) != 0x80) return 0;
            result += (uint32_t)(txt[*i] & 0x3F) << 6;
            (*i)++;

            if((txt[*i] & 0xC0) != 0x80) return 0;
            result += txt[*i] & 0x3F;
            (*i)++;
        }
        else
        {
            (*i)++;
        }
    }
    return result;
}

///显示一个C51字符
//int SC_pfb_C51_letter(SC_tile *dest,int xs,int ys,uint16_t w,uint16_t h,const uint8_t *src,uint16_t fc,uint16_t bc)
//{
//    uint16_t alpha;
//    int y1= SC_MAX(ys,dest->ys);            //ys边界限定
//    int y2= SC_MIN(ys+h,dest->ys+dest->h);  //ye边界限定
//    if(y1>=y2) return 0;
//    int x1= SC_MAX(xs,dest->xs);            //xs边界限定
//    int x2= SC_MIN(xs+w,dest->xs+dest->w);  //xe边界限定
//    if(x1>=x2) return 0;
//    for (int x = x1; x <x2; x++)
//    {
//        int dest_offs=(y1-dest->ys) * dest->w+x-dest->xs;
//        int src_x = x - xs;
//        for (int y = y1; y <y2; y++,dest_offs+=dest->w)
//        {
//            int src_y = y- (ys);
//            alpha=get_bpp_value(src,src_y * w+src_x, 1);
//            set_pixel_value(dest,dest_offs,alpha,fc);
//        }
//    }
//    return 1;
//}

///显示一个lvgl字符
int SC_pfb_lv_letter(SC_tile *dest,SC_ARER *align,int xs,int ys,lv_font_glyph_dsc_t *dsc,uint32_t unicode, lv_font_t *font,uint16_t fc,uint16_t bc)
{
    int xe=xs+dsc->adv_w-1;
    int ye=ys+font->line_height-1;
    int offs_y= ys+(font->line_height-dsc->box_h-dsc->ofs_y-font->base_line);  //先计算再对齐
    int offs_x= xs+dsc->ofs_x;
    //-----------对齐窗口-------------------------
    ys= SC_MAX(ys, align->ys);     //ys边界限定
    ye= SC_MIN(ye, align->ye);    //ye边界限定
    if(ys>=ye) return 1;
    xs= SC_MAX(xs, align->xs);    //xs边界限定
    xe= SC_MIN(xe, align->xe);    //xe边界限定
    if(xs>=xe) return 1;
    SC_tile pfb;
    if(dest==NULL)
    {
        pfb.stup=0;
        dest=&pfb;
    }
    do
    {
        if(dest==&pfb)
        {
            SC_pfb_clip(&pfb,xs,ys,xe,ye,gui->bkc);//创建局部pfb
        }
        //-----------计算相交-------------------------
        SC_ARER intersection;
        if(!SC_pfb_intersection(dest,&intersection,xs,ys,xe,ye))
        {
            return 0;
        }
        int x,y,src_x,src_y;
        const uint8_t *src =font->get_glyph_bitmap(font,unicode);
        int dest_offs=(intersection.ys-dest->ys) * dest->w -dest->xs;
        for ( y = intersection.ys; y <=intersection.ye; y++, dest_offs+= dest->w)
        {
            src_y = y- offs_y;
            for ( x = intersection.xs; x <=intersection.xe; x++)
            {
                uint16_t *out=dest->buf+dest_offs+x;
                src_x = x - offs_x;
                if(bc!=gui->bkc)
                {
                    *out=alphaBlend(bc,*out,gui->alpha);
                }
                if (src_x >= 0 && src_x< dsc->box_w&&src_y >= 0 && src_y < dsc->box_h)
                {
                    uint16_t alpha=get_bpp_value(src,src_y * dsc->box_w+src_x, dsc->bpp);
                    *out=alphaBlend(fc,*out,alpha);
                }
            }

        }
    }
    while((dest==&pfb)&&SC_pfb_Refresh(&pfb,0));
    return 0;
}


///长文本显示自动换行，支持\n换行
void SC_pfb_printf(SC_tile *dest,SC_ARER *box, int x,int y,const char* txt,uint16_t fc,uint16_t bc, lv_font_t* font)
{
    lv_font_glyph_dsc_t g;
    uint32_t unicode,i=0;
    if(box==NULL)
    {
        box=&gui->lcd_area;
    }
    x=box->xs+x;
    y=box->ys+y;
    for(;; x+=g.adv_w)
    {
        unicode = lv_txt_utf8_next(txt,&i);            //txt转unicode
        if(font->get_glyph_dsc(font,&g,unicode,0)==0)
        {
            if(unicode==0) return;
            if(unicode=='\n'||unicode=='\r')
            {
                x=box->xe;
                continue;
            }
            else
            {
                unicode=' ';
                font->get_glyph_dsc(font,&g,unicode,0);
            }
        }
        if(x+g.adv_w>=box->xe)        //换行
        {
            x=box->xs;
            y+=font->line_height;
            if(y>box->ye)  break;
        }
        SC_pfb_lv_letter(dest,box,x,y,&g,unicode,font,fc,bc);
    }
}

///设置坐标对齐到parent
void SC_set_align(SC_ARER *parent,int *xs,int *ys,int w,int h,uint8_t align)
{
    if(parent!=NULL)
    {
        if(align&ALIGN_RIGHT)      //右对齐
        {
            int width =  parent->xe - parent->xs+1;
            if(align&ALIGN_LEFT)  //中
            {
                *xs+= (width - w) / 2;
            }
            else
            {
                *xs+= width - w ;
            }
        }
        if(align&ALIGN_BOTTOM)   //底对齐
        {
            int height = parent->ye - parent->ys+1;
            if(align&ALIGN_TOP)
            {
                *ys+= (height - h ) / 2;
            }
            else
            {
                *ys+= height -  h ;
            }
        }
        *xs += parent->xs;
        *ys += parent->ys;
    }
}

///显示文本
void SC_pfb_str(SC_tile *dest,int x,int y,const char *text,uint16_t fc,uint16_t bc,lv_font_t* font,SC_ARER *parent,uint8_t align)
{
    if(parent==NULL)    parent=&gui->lcd_area;
    lv_font_glyph_dsc_t g[64];
    int width=parent->xe-parent->xs+1;
    int height=font->line_height;
    uint32_t unicode[64];
    uint32_t  i=0,len=0,j=0;
    if(dest)
    {
        SC_ARER intersection;
        int xs=x;
        int ys=y;
        SC_set_align(parent,&xs,&ys,width,height,align);     //预对齐
        if(!SC_pfb_intersection(dest,&intersection,xs,ys,xs+width-1,ys+width-1))
        {
            return ;
        }
    }
    //--------实际长度------
    for(len=0; len<width; len+=g[j++].adv_w)
    {
        unicode[j] = lv_txt_utf8_next(text,&i);                 //txt转unicode
        if(font->get_glyph_dsc(font,&g[j],unicode[j],0)==0)
        {
            if(unicode[j]==0) break;
            unicode[j]=' ';
            font->get_glyph_dsc(font,&g[j],unicode[j],0);
        }
    }
    //--------对齐输出------
    SC_set_align(parent,&x,&y,len,height,align);
    for(i=0; i<j; i++)
    {
        SC_pfb_lv_letter(dest,parent,x,y,&g[i],unicode[i],font,fc,bc);
        x+=g[i].adv_w;
    }
}
///设置标签
void SC_set_label_text(SC_label* Label,const char *str,uint16_t fc,lv_font_t* font,uint8_t align)
{
    Label->str=str;
    Label->tc=fc;
    Label->font=font;
    Label->align=align;
}

///显示标签
void SC_pfb_label(SC_tile *dest,int x,int y,SC_ARER *parent,SC_label *label,uint16_t lab_bc)
{
    if(label->font)
    {
        SC_pfb_str(dest, x, y,label->str,label->tc,lab_bc,label-> font,parent,label->align);
    }
}
///文本框
void SC_pfb_text_box(SC_tile *dest,SC_ARER *parent,int xs,int ys,SC_label* label)
{
    SC_ARER text_box;
    int w=5;
    int h=32;
    if( SC_pfb_RoundFrame(dest, parent->xs, parent->ys, parent->xe, parent->ye, 1,1, C_WHEAT,gui->bkc))
    {
        if(label->font)
        {
            text_box.xs=parent->xs+1;
            text_box.ys=parent->ys+1;
            text_box.xe=parent->xe-w;
            text_box.ye=parent->ye-1;
            SC_pfb_printf(dest,&text_box,-xs,-ys,label->str,label->tc,gui->bkc, label->font);
        }
        SC_set_align(parent,&xs,&ys,w,h,ALIGN_RIGHT);    //右边进度条
        int ye=ys+h-1;
        if(ye> parent->ye)
        {
            h= (parent->ye-parent->ys)*h/ (ye-parent->ys);  //进度条压缩
            ye= parent->ye;
            ys= ye-h+1;
        }
        else if(ys< parent->ys)
        {
            h= (parent->ye-parent->ys)*h/(parent->ye-ys)+1;//进度条压缩
            ys= parent->ys;
            ye= ys+h-1;
        }
        //------------进度条显示------------
        SC_pfb_RoundFrame(dest, parent->xe-w, parent->ys, parent->xe, parent->ye,1,1,C_WHEAT,C_WHEAT);
        SC_pfb_RoundFrame(dest, parent->xe-w, ys, parent->xe, ye,1,1,gui->fc,gui->fc);
    }
}

///-按键-
void SC_pfb_button(SC_tile *dest,SC_label *label,int xs,int ys,int xe,int ye,uint16_t ac,uint16_t bc,uint8_t r,uint8_t ir,uint8_t state)
{
    SC_ARER box;
    if(state==1)
    {
        xs+=1;
        ys+=1;
        xe-=1;
        ye-=1;
    }
    if(SC_pfb_RoundFrame(dest, xs, ys, xe, ye, r,ir,ac,bc))
    {
        if(label->font)
        {
            box.xs=xs;
            box.ys=ys;
            box.xe=xe;
            box.ye=ye;
            SC_pfb_str(dest, 0, 0,label->str,label->tc,gui->bkc,label-> font,&box,label->align);
        }
    }
}

///-LED-
void SC_pfb_led(SC_tile *dest,int xs,int ys,int xe,int ye, uint16_t ac,uint16_t bc)
{
    int w=xe-xs;
    int h=ye-ys;
    int r=  w<h?w/2:h/2;
    int ir= r-1;   //倒角
    SC_pfb_RoundFrame(dest,xs,ys,xe,ye, r,ir,ac,bc);
}

///-开关-
void SC_pfb_switch(SC_tile *dest,int xs,int ys,int xe,int ye, uint16_t ac,uint16_t bc,uint8_t state)
{
    int w=xe-xs;
    int h=ye-ys;
    int r= w<h?w/2:h/2;
    uint16_t bkc=alphaBlend(ac,gui->bkc,128);
    SC_tile pfb;
    if(dest==NULL)
    {
        pfb.stup=0;
        dest=&pfb;
    }
    do
    {
        if(dest==&pfb)
        {
            SC_pfb_clip(&pfb,xs,ys,xe,ye,gui->bkc); //创建局部pfb
        }
        if(SC_pfb_RoundFrame(dest,xs,ys,xe,ye, r,r-1,ac,state?bkc:bc))
        {
            int x1=xs+4;
            int y1=ys+4;
            int x2=xe-4;
            int y2=ye-4;
            int r2=y2-y1;
            if(state==0)
            {
                SC_pfb_led(dest, x1, y1, x1+r2, y1+r2,  ac, ac);
            }
            else
            {
                SC_pfb_led(dest, x2-r2, y2-r2, x2, y2,  ac, ac);
            }
        }
    }
    while((dest==&pfb)&&SC_pfb_Refresh(&pfb,0));
}

#define KEY_ROWS   4
static const uint8_t keys_per_row[KEY_ROWS] = {10, 9, 9,10};
static const char *kb_26_str = "qwertyuiopasdfghjkl#zxcvbnm,0123456789";

void SC_key_layout(key_rect_t *tbl, int xs, int ys,int xe,int ye)
{
    uint16_t lcd_w=xe-xs;
    uint16_t lcd_h=ye-ys;
    uint16_t key_gap  = lcd_w/80;
    uint16_t top_gap  = lcd_h/40;
    uint16_t key_w    = (lcd_w-key_gap)*0.9/10;   // 固定 9 % 屏宽
    uint16_t row_h    = (lcd_h-top_gap)/(KEY_ROWS);
    uint16_t row_y[KEY_ROWS] =
    {
        top_gap,
        top_gap + row_h,
        top_gap + row_h * 2,
        top_gap + row_h * 3
    };
    uint8_t k = 0;
    SC_pfb_RoundFrame(NULL,xs,ys, xe, ye, 4,4,C_WHEAT,C_WHEAT);
    gui->bkc=C_WHEAT;
    for (uint8_t r = 0; r < KEY_ROWS; ++r)
    {
        uint8_t n = keys_per_row[r];
        /* 居中留白 */
        uint16_t total = n * key_w + (n - 1) * key_gap;
        uint16_t left  = (lcd_w - total) / 2;
        for (uint8_t c = 0; c < n; ++c)
        {
            tbl[k].abs.xs = left + c * (key_w + key_gap)+xs;
            tbl[k].abs.ys = ys+row_y[r];
            tbl[k].abs.xe = tbl[k].abs.xs + key_w;
            tbl[k].abs.ye = tbl[k].abs.ys + row_h * 0.9f;
            tbl[k].str[0] = kb_26_str[k];
            tbl[k].str[1] = '\0';
            if(kb_26_str[k]=='#')
            {
                tbl[k].abs.xs=tbl[0].abs.xs;
            }
            else  if(kb_26_str[k]==',')
            {
                tbl[k].abs.xe=tbl[9].abs.xe;
            }
            SC_ARER *abs= &tbl[k].abs;
            SC_pfb_RoundFrame(NULL, abs->xs, abs->ys, abs->xe, abs->ye, 4,0,C_WHITE,C_WHITE);
            SC_pfb_str(NULL, 0, 0,tbl[k].str,C_BLACK,C_WHITE,gui-> font,abs,0xff);
            ++k;
        }
    }
}
//组合控件
void SC_pfb_demo(SC_tile *dest,int xs,int ys,int xe,int ye)
{
    SC_tile pfb= {0};
__PFB_demo:
    if(dest==NULL||pfb.buf)
    {
        //创建局部pfb
        SC_pfb_clip(&pfb,xs,ys,xe,ye,gui->bkc);
        dest=&pfb;
    }
    //-----------------------
    //在这里组合控件
    //---------------------
    if(pfb.buf)
    {
        if(SC_pfb_Refresh(&pfb,0))
        {
            goto __PFB_demo;
        }
    }
}



