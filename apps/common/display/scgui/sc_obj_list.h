
#ifndef SC_OBJ_H
#define SC_OBJ_H
#include "sc_gui.h"

typedef enum
{
    ARER,     //占位区
    CANVAS,   //画布
    FRAME,    //圆角矩形
    BUTTON,   //按键
    LABEL,    //标签
    TEXTBOX,  //文本框
    SLIDER,   //进度条
    IMAGE,    //图片
    IMAGEZIP, //图片压缩
    ARC,      //图片压缩
    LED,      //LED
    SWITCH,   //开关
    CHART,    //波形图
    ROTATE,   //旋转表针
} ObjType;

typedef enum
{
    OBJ_STATE_REL =          0x00,   //释放
    OBJ_STATE_CLICK =        0x01,   //可以点击
    OBJ_STATE_BOOL =         0x02,   //开关
    OBJ_STATE_MOVX =         0x04,   //可以X移动
    OBJ_STATE_MOVY =         0x08,   //可以Y移动
    OBJ_STATE_MOVXY =        (OBJ_STATE_MOVX|OBJ_STATE_MOVY),   //可以XY移动
    OBJ_STATE_VISIBLE =       0x10,   //可见
} ObjState ;

typedef enum
{
    OBJ_FLAG_ACTIVE   =       0x01,   //活动标志
    OBJ_FLAG_CLICK    =       0x02,   //点击标志
    OBJ_FLAG_FOCUSED   =      0x04,   //聚焦标志
} ObjFlag;

typedef struct Obj_t
{
    struct Obj_t* parent;                  // 父控件
    struct Obj_t* next;                    // 指向下一个控件的指针
    void (*touch_cb)(struct Obj_t* obj, int x, int y,uint8_t state);   //触摸回调
    uint8_t type:5;                        //控件类型
    uint8_t level:3;                       //层次最大7级
    uint8_t State:5;                       //状态
    uint8_t Flag:3;                        //标志
    SC_ARER abs;                           //绝对位置
} Obj_t;


/// 创建新节点并加入
Obj_t* obj_create(void* parent,void* p,uint8_t type);

/// 打印树结构
void print_tree(void* tree);


void obj_list_add(void* vparent, void* vobj);

///下一个同级节点
Obj_t* obj_next_node(Obj_t* obj);

///删除节点，如果控件是容器删除子成员
void obj_delete_node(void* vobj);

///释放控件链表
void obj_free_controls(void* vobj);

///控件更新
void obj_set_Active(void* vobj);

///控件可见
void obj_set_Visible(void* vobj,uint8_t Visible);


///移动子控件坐标
void obj_move_geometry(void* vobj,int move_x,int move_y);

///设置位置
void obj_set_geometry(void* vobj, int x, int y, int w, int h, SC_Align  align);


///设置触摸回调函数
void obj_set_touch_cb(void* vobj,void (*touch_cb)(Obj_t*, int, int,uint8_t),uint8_t State);


#endif
