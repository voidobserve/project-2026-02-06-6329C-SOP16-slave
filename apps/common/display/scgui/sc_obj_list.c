
#include "sc_obj_list.h"
#include "stdio.h"
#include "string.h"
///控件加入链表
void obj_list_add(void* vparent, void* vobj)
{
    if (vparent == NULL || vobj == NULL)
    {
        return;        // 如果父节点或新节点为空，直接返回
    }
    Obj_t* parent=(Obj_t*) vparent;
    Obj_t* obj=   (Obj_t*) vobj;
    Obj_t* end = parent;
    for( Obj_t* current = parent->next; current != NULL; current = current->next)
    {
        // 检查是否已存在相同的节点，并同时找到父容器的最后一个子控件
        if (current == obj)   return;
        if(current->level>parent->level)
        {
            end = current;    //记录最后一个子控件
        }
        else
        {
            break;
        }
    }
    obj->parent=parent;
    obj->level = parent->level+1;  //子控件
    //批量加入
    if(obj->next)
    {
        Obj_t* current=obj->next;
        while(current!=NULL)
        {
            current->level=obj->level+1;  //子控件
            if(current->next==NULL)
            {
                current->next=end->next;
                break;
            }
            current=current->next;
        }
    }
    else
    {
        obj->next = end->next; // 连接后续的链表
    }
    end->next = obj;
}

/// 创建新节点并加入
Obj_t* obj_create(void* parent,void* p,uint8_t type)
{
    Obj_t* obj=(Obj_t*)p;
    if (obj)
    {
        if(parent==NULL)
        {
            obj->next = NULL;
        }
        obj->level= 0;
        obj->touch_cb=NULL;
        obj->type = type;
        obj->Flag = 0;
        obj->State = OBJ_STATE_VISIBLE;  //可见
        obj_list_add((Obj_t*)parent,obj);
        return obj;
    }
    return NULL;
}

/// 打印树结构
void print_tree(void* tree)
{
    Obj_t* obj=(Obj_t*) tree;
    if (obj == NULL) return ;
    int obj_cnt=0;
    const char* type_name[32]=
    {
        "ARER",
        "CANVAS",
        "FRAME",
        "BUTTON",
        "LABEL",
        "TEXTBOX",
        "SLIDER",
        "IMAGE",
        "IMAGEZIP",
        "ARC",
        "LED",
        "SWITCH",
        "CHART",
        "ROTATE",
    };
    Obj_t *current = obj;
    char print_buff[128];
    while (current != NULL)
    {
        if(current->level<=obj->level&&current!=obj)
        {
            break;
        }
        // 打印缩进和节点数据
        // for (int i = 0; i < current->level; i++)
        // {
        //     printf("  ");
        // }
        // printf("|__%s\n", type_name[current->type]);
        
        memset(print_buff, 0x00, sizeof(print_buff));
        // 打印缩进和节点数据
        for (int i = 0; i < current->level; i++)
        {
            strcat(print_buff, "  ");
        }
        snprintf(print_buff + strlen(print_buff), sizeof(print_buff) - strlen(print_buff), "|__%s\n", type_name[current->type]);
        printf("%s", print_buff);
        obj_cnt++;
        current = current->next;
    }
    printf("obj_cnt=%d\n", obj_cnt);
}

///下一个同级节点
Obj_t* obj_next_node(Obj_t* obj)
{
    Obj_t* current = obj->next;
    while (current != NULL&&current->level>obj->level)
    {
        current = current->next;
    }
    return current;
}

///删除节点，如果控件是容器删除子成员
void obj_delete_node(void* vobj)
{
    Obj_t* obj=(Obj_t*) vobj;

    Obj_t* end = obj;            //尾节点
    Obj_t* prev = obj->parent;   //父节点
    Obj_t* child = obj->next;    //子节点
    while (child != NULL&&child->level>obj->level)
    {
        end=child;
        child = child->next;
    }
    while (prev != NULL)
    {
        if(prev->next==obj)
        {
            prev->next=child;
            break;
        }
        prev = prev->next;
    }
    end->next=NULL;
}

///释放控件链表
void obj_free_controls(void* vobj)
{
    Obj_t* current = (Obj_t*) vobj;
    while (current != NULL)
    {
        Obj_t* next = current->next;
        current = next;

    }
}

///控件更新
void obj_set_Active(void* vobj)
{
    Obj_t* obj=(Obj_t*) vobj;
    obj->Flag|=OBJ_FLAG_ACTIVE;
}

///控件可见
void obj_set_Visible(void* vobj,uint8_t Visible)
{
    Obj_t* obj=(Obj_t*)vobj;
    Obj_t* current = obj;
    while (current != NULL)
    {
        if(current->level<=obj->level&&current!=obj)
        {
            break;    //没有子控件
        }
        if(Visible)
        {
            current->State|=OBJ_STATE_VISIBLE;
        }
        else
        {
            current->State&=~OBJ_STATE_VISIBLE;
        }
        current=current->next;
    }
    obj->Flag|=OBJ_FLAG_ACTIVE;
}

///移动子控件坐标
void obj_move_geometry(void *vobj,int move_x,int move_y)
{
    Obj_t*obj=(Obj_t*)vobj;
    //移动子控件，边界才移动自身
    Obj_t* current = obj->next;
    while (current != NULL)
    {
        if(current->level<=obj->level)
        {
            break;  //没有子控件
        }
        current->Flag|=OBJ_FLAG_ACTIVE;
        current->abs.xs+=move_x;
        current->abs.xe+=move_x;
        current->abs.ys+=move_y;
        current->abs.ye+=move_y;
        current = current->next;
    }

}

///设置位置
void obj_set_geometry(void* vobj, int x, int y, int w, int h, SC_Align  align)
{
    Obj_t* obj=(Obj_t*) vobj;
    if(obj==NULL) return;
    int xs=obj->abs.xs;
    int ys=obj->abs.ys;
    if(obj->parent)
    {
        SC_set_align(&obj->parent->abs,&x,&y, w, h, align);
    }
    obj->abs.xs = x;      //相对坐标
    obj->abs.ys = y;
    obj->abs.xe = obj->abs.xs+w-1;
    obj->abs.ye = obj->abs.ys+h-1;
    obj->Flag |=  OBJ_FLAG_ACTIVE;      // 设置位置后，控件需要重新绘制
    obj_move_geometry(obj,x-xs,y-ys);     //子控件要跟着移动
}

///设置触摸回调函数
void obj_set_touch_cb(void* vobj,void (*touch_cb)(Obj_t*, int, int,uint8_t),uint8_t State)
{
    Obj_t*obj=(Obj_t*)vobj;
    obj->touch_cb=touch_cb;
    obj->State|=State;
}

