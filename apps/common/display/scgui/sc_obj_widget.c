
#include "sc_obj_widget.h"

///绘制函数
static void obj_type_draw(Obj_t *obj,SC_tile *dest)
{
    if(obj->State<OBJ_STATE_VISIBLE)  return;   //不可见
    SC_ARER *abs=&obj->abs;
    uint8_t Flag=obj->Flag&OBJ_FLAG_CLICK?1:0;
    uint8_t alpha=gui->alpha;
    if(obj->parent)
    {
        gui->parent= &obj->parent->abs; //子控件的pfb不要超过父级
    }
    else
    {
        gui->parent=NULL;
    }
    switch (obj->type)
    {
    case ARER:
    case CANVAS:
    {
        SC_pfb_RoundFrame(dest, abs->xs, abs->ys,abs->xe, abs->ye,1,1,gui->fc,gui->bkc);
    }
    break;
    case FRAME:   //矩形
    {
        Obj_Frame* Frame = (Obj_Frame*)obj;
        gui->alpha=Frame->alpha;
        SC_pfb_RoundFrame(dest, abs->xs, abs->ys,abs->xe, abs->ye,Frame->r,Frame->ir,Frame->ac,Frame->bc);
    }
    break;
    case IMAGE:  //图片
    {
        Obj_Image *Image = (Obj_Image*)obj;
        SC_pfb_Image(dest,abs->xs, abs->ys,Image->src,255-Flag*128);
    }
    break;

    case IMAGEZIP:  //压缩图片
    {
        Obj_Imagezip *zip = (Obj_Imagezip*)obj;
        SC_pfb_Image_zip(dest,abs->xs, abs->ys,zip->src,&zip->dec);
    }
    break;
    case ARC:  //圆弧
    {
        Obj_Arc *arc = (Obj_Arc*)obj;
        SC_arc  temp;
        gui->alpha=arc->alpha;
        temp.cx=abs->xs+arc->r;
        temp.cy=abs->ys+arc->r;
        temp.r=arc->r;
        temp.ir=arc->ir;
        SC_pfb_DrawArc(dest,&temp,arc->start_deg, arc->end_deg,arc->ac,arc->bc,arc->dot);
    }
    break;
    case ROTATE:  //图片
    {
        Obj_Rotate *Rotate = (Obj_Rotate*)obj;
        SC_pfb_transform(dest,Rotate->src,&Rotate->params);
    }
    break;
    case LABEL:   //标签
    {
        Obj_Label* label = (Obj_Label*)obj;
        uint16_t lab_bc= Flag?C_RED:gui->bkc;
        SC_pfb_label(dest,0,0,abs,&label->text,lab_bc);
    }
    break;
    case TEXTBOX://文本
    {
        Obj_Txtbox *Txtbox = (Obj_Txtbox*)obj;
        SC_pfb_text_box(dest,abs,-Txtbox->box.xs,-Txtbox->box.ys,&Txtbox->text);
    }
    break;
    case BUTTON:  //按键
    {
        Obj_But *but = (Obj_But*)obj;
        gui->alpha=but->alpha;
        SC_pfb_button(dest,&but->text,abs->xs, abs->ys,abs->xe, abs->ye,but->ac,but->bc,but->r,but->ir,Flag);
    }
    break;

    case LED://LED
    {
        Obj_Led *led = (Obj_Led*)obj;
        gui->alpha=led->alpha;
        SC_pfb_led(dest,abs->xs, abs->ys,abs->xe,abs->ye, led->ac,Flag?led->bc:led->ac);
    }
    break;

    case SWITCH:
    {
        Obj_Sw *sw = (Obj_Sw*)obj;
        gui->alpha=sw->alpha;
        SC_pfb_switch(dest,abs->xs, abs->ys,abs->xe,abs->ye,sw->ac,sw->bc, Flag);
    }
    break;
    case SLIDER:
    {
        Obj_Slider *slider = (Obj_Slider*)obj;
        SC_pfb_RoundBar(dest,abs->xs,abs->ys,abs->xe,abs->ye,slider->r,slider->ir,slider->ac,slider->bc,slider->value,100);
    }
    break;
    case CHART:
    {
        Obj_Chart *chart = (Obj_Chart *)obj;
        int w=abs->xe-abs->xs+1;
        int h=abs->ye-abs->ys+1;
        SC_pfb_chart(dest,abs->xs,abs->ys,w,h, chart->ac,chart->bc,chart->xd,chart->yd,chart->buf,sizeof(chart->buf)/sizeof(chart->buf[0]));
    }
    break;
    default:
        break;
    }
    gui->alpha=alpha;
}

static SC_ARER temp_i= {0};     //移动累加
static int16_t temp_x,temp_y=0; //计算移动脏矩阵
static Obj_Canvas *Canvas=NULL;
static Obj_t* click=NULL;
///控件触控可以中断调用
void obj_touch_check(void* screen, int x, int y,uint8_t state)
{

    Obj_t* obj=(Obj_t*) screen;
    if(obj==NULL)return;
    static int16_t last_x,last_y;
    int move_x=0,move_y=0;
    switch(state)
    {
    case OBJ_STATE_CLICK:  //按下
    {
        if(Canvas==NULL)
        {
            click = obj;  //默认为屏,从obj->next开始遍历
            for (Obj_t* current = obj->next; current != NULL; current = current->next)
            {
                if(current->State>OBJ_STATE_VISIBLE&&arae_is_touch(&current->abs,x,y))//找到控件
                {
                    if(arae_is_touch(&current->parent->abs,x,y))  //限制父级
                    {
                        click=current;
                    }
                }
            }
            if(click->State&OBJ_STATE_CLICK)   //单击事件回调
            {
                click->Flag|=OBJ_FLAG_ACTIVE|OBJ_FLAG_CLICK;
                click->touch_cb(click,x,y,OBJ_STATE_CLICK);
            }
            else  if(click->State&OBJ_STATE_BOOL)  //开关事件回调
            {
                click->Flag|=OBJ_FLAG_ACTIVE;
                click->Flag^=OBJ_FLAG_CLICK;
                click->touch_cb(click,x,y,OBJ_STATE_BOOL);
            }
            temp_i.xs=0;   //清0
            temp_i.ys=0;
            temp_i.xe=0;
            temp_i.ye=0;
        }
    }
    break;
    case OBJ_STATE_MOVXY://移动中
    {
        if(click)
        {
            if(click->type!=CANVAS&&click->type!=TEXTBOX) break;   //不可以移动
            Canvas= ( Obj_Canvas *)click;
            if(click->State&OBJ_STATE_MOVX)
            {
                move_x=x-last_x;
                if(move_x<0&&Canvas->box.xs< -Canvas->box.xe)    //左移
                {
                    move_x/=8;
                }
                if(move_x>0&&Canvas->box.xs>0)    //右移
                {
                    move_x/=8;
                }
            }
            if(click->State&OBJ_STATE_MOVY)
            {
                move_y=y-last_y;
                if(move_y<0&&Canvas->box.ys<-Canvas->box.ye)   //上移
                {
                    move_y/=8;
                }
                if(move_y>0&&Canvas->box.ys>0)       //下移
                {
                    move_y/=8;
                }
            }
            if(move_x!=0||move_y!=0)
            {
                temp_i.xs+=move_x;
                temp_i.ys+=move_y;
                Canvas->box.xs+=move_x;
                Canvas->box.ys+=move_y;
                obj_move_geometry(click,move_x,move_y);
                click->Flag|=OBJ_FLAG_ACTIVE;
                click->touch_cb(click,x,y,click->State&OBJ_STATE_MOVXY);
            }
            //累加坐标
        }
    }
    break;
    case OBJ_STATE_REL://释放
    {
        if(click)
        {
            if(click->State&OBJ_STATE_CLICK) //释放事件回调
            {
                click->Flag=OBJ_FLAG_ACTIVE;
                click->touch_cb(click,x,y,OBJ_STATE_REL);
            }
            if(temp_i.xs||temp_i.ys)   //有移动开启平移动画
            {
                int w=(click->abs.xe-click->abs.xs)+1;
                int h=(click->abs.ye-click->abs.ys)+1;
                if(SC_ABS(temp_i.xs)>w/4)       //平移超过1/4
                {
                    temp_i.xe =temp_i.xs>0? w:-w;  //前一页或后一页坐标
                }
                if(SC_ABS(temp_i.ys)>h/4)
                {
                    temp_i.ye =temp_i.ys>0? h:-h;  //上一页或下一页坐标
                }
            }
            click=NULL;
        }
    }
    break;
    }
    if(click)
    {
        if(click->type==SLIDER)
        {
            Obj_Slider  *slider=(Obj_Slider*) click;
            int w=(click->abs.xe-click->abs.xs);
            int h=(click->abs.ye-click->abs.ys);
            if(w>h)
                slider->value= (x-click->abs.xs)*100/w;
            else
                slider->value= (click->abs.ye-y)*100/h;
            click->Flag=OBJ_FLAG_ACTIVE;
        }
    }
    last_x=x;
    last_y=y;
}

///控件触控释放,防止中断重入
void obj_touch_rel_cb(int x, int y)
{
    int move_x=0,move_y=0;
    if(Canvas&&click==NULL)   ///释放后启动
    {
        if(temp_i.xe!=temp_i.xs)
        {
            int errx=temp_i.xe-temp_i.xs;
            int erry=temp_i.ye-temp_i.ys;
            if(SC_ABS(errx)<x)
            {
                move_x=errx%x;
            }
            else
            {
                move_x=errx<0 ?-x:x;
            }
            if(SC_ABS(erry)<y)
            {
                move_y=erry%y;
            }
            else
            {
                move_y=erry<0 ?-y:y;
            }
            temp_i.xs+=move_x;
            temp_i.ys+=move_y;
            Canvas->box.xs+=move_x;
            Canvas->box.ys+=move_y;
            obj_move_geometry(Canvas,move_x,move_y);
            Canvas->base.Flag|=OBJ_FLAG_ACTIVE;
        }
        else
        {
            Canvas=NULL;
        }
    }
}

///刷新单个活动控件
void obj_draw_active(void *screen,Obj_t *active)
{
    int intersect_cnt=0;                //相交控件
    Obj_t* intersect_buf[20];
    SC_ARER arer =active->abs;                               //刷新区
    arae_is_merge(&arer,temp_i.xs-temp_x,temp_i.ys-temp_y);  //如果移动合并刷新区
    SC_tile pfb;
    pfb.stup= 0;
    pfb.xs= 0;
    pfb.ys= 0;
    pfb.w= LCD_SCREEN_WIDTH;
    pfb.h= LCD_SCREEN_HEIGHT;
    gui->parent= active->parent? &active->parent->abs:NULL;   //pfb不要超过父级
    if(SC_pfb_intersection(&pfb,&arer,arer.xs, arer.ys,arer.xe, arer.ye))
    {
        //----------------找出相交控件---------------------------
        Obj_t* current=(Obj_t *)screen;
        while (current != NULL)
        {
            if(current->type<=CANVAS)        //占位区不用更新
            {
                current = current->next;
                continue;
            }
            if(arae_is_intersect(&arer,&current->abs))
            {
                //完全在脏矩形内或是子控件
                if(current->parent==active||arae_is_contained(&active->abs,&current->abs))
                {
                    current->Flag&=~OBJ_FLAG_ACTIVE;  //清标志
                    Obj_t *child=current->next;
                    while(child!=NULL&&child->level>current->level)
                    {
                        child->Flag&=~OBJ_FLAG_ACTIVE;  //清标志
                        child=child->next;
                    }
                }
                intersect_buf[intersect_cnt++]=current;
                current = current->next;
            }
            else
            {
                current=obj_next_node(current);
            }
        }
        sc_printf("pfb_arer (%d, %d,%d, %d) %d/%d \n",arer.xs, arer.ys,arer.xe, arer.ye,intersect_cnt,20);
        //-----------------刷新控件--------------------------------
        do
        {
            SC_pfb_clip(&pfb,arer.xs,arer.ys,arer.xe,arer.ye,gui->bkc);
            for(int i=0; i<intersect_cnt; i++)
            {
                obj_type_draw(intersect_buf[i],&pfb);
            }
        }
        while(SC_pfb_Refresh(&pfb,0));            //分帧刷新
    }
}

///遍历刷新
void obj_draw_screen(void *screen)
{
    obj_touch_rel_cb(15,15);      //平移动画
    Obj_t* active=(Obj_t*)screen;
    while (active != NULL)
    {
        if(active->Flag&OBJ_FLAG_ACTIVE)     //控件活动事件
        {
            active->Flag&=~OBJ_FLAG_ACTIVE;  //清标志
            if(active->type>CANVAS)        //占位区不用更新
            {
                obj_draw_active(screen,active);
            }
        }
        active = active->next;
    }
    temp_x=temp_i.xs;
    temp_y=temp_i.ys;
}

#if 0
void touchpad_loop(Obj_t* obj)
{
    static uint8_t state=0;
    if(gui->touch_pressed())
    {
        int tx = gui->touch_read_x();
        int ty = gui->touch_read_y();
        if(state==0)
        {
            state=1;
            obj_touch_check(obj, tx, ty,OBJ_STATE_CLICK);
        }
        else
        {
            obj_touch_check(obj, tx, ty,OBJ_STATE_MOVXY);
        }
    }
    else
    {
        if(state)
        {
            state=0;
            int tx = gui->touch_read_x();
            int ty = gui->touch_read_y();
            obj_touch_check(obj, tx, ty,OBJ_STATE_REL);
        }
    }
}

// 根据方向键导航到下一个控件
void navigate_focus(Obj_t* current, KeyDirection key)
{
    if (current == NULL) return;
    SC_ARER abs,child;
    Obj_t* next = NULL;
    Obj_t* candidate = current->parent->next;  // 从父控件的第一个子控件开始
    while (candidate != NULL)
    {
        if (candidate != current)
        {
            switch (key)
            {
            case KEY_UP:
                if (child.ys < abs.ys || (child.ys == abs.ys && child.xs < abs.xs))
                {
                    if (next == NULL || child.ys > next->offset_y || (child.ys == next->offset_y && child.xs > next->offset_x))
                    {
                        next = candidate;
                    }
                }
                break;
            case KEY_DOWN:
                if (child.ys > abs.ys || (child.ys == abs.ys && child.xs > abs.xs))
                {
                    if (next == NULL || child.ys < next->offset_y || (child.ys == next->offset_y && child.xs < next->offset_x))
                    {
                        next = candidate;
                    }
                }
                break;
            case KEY_LEFT:
                if (child.xs < abs.xs || (child.xs == abs.xs && child.ys < abs.ys))
                {
                    if (next == NULL || child.xs > next->offset_x || (child.xs == next->offset_x && child.ys > next->offset_y))
                    {
                        next = candidate;
                    }
                }
                break;
            case KEY_RIGHT:
                if (child.xs > abs.xs || (child.xs == abs.xs && child.ys > abs.ys))
                {
                    if (next == NULL || child.xs < next->offset_x || (child.xs == next->offset_x && child.ys < next->offset_y))
                    {
                        next = candidate;
                    }
                }
                break;
            default:
                break;
            }
        }
        candidate = candidate->next;
    }
    // 如果没有找到符合条件的兄弟控件，返回父控件的第一个子控件
    if (next == NULL)
    {
        next = current->parent->next;
    }
    // 清除当前控件的聚焦状态
    if(current->state & STATE_focused)
    {
        current->state &=~ STATE_focused;   //失焦恢复
        current->event |= SCREEN_updat;
    }
    // 设置下一个控件为聚焦状态
    next->Flag |= STATE_focused;            //聚焦变色
    next->event |= SCREEN_focused;
}
#endif
