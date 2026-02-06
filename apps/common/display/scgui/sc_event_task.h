#ifndef _SC_ENEVT_TASK_H_
#define	_SC_ENEVT_TASK_H_

// #include "typedef.h"
#include  <stdint.h>
#include  "jiffies.h"

#ifndef NULL
#define NULL  0
#endif

//=======================新增多类型队列===================================
#define MAX_THREADS 		6     	//任务数量
#define EVENT_QUEUE_SIZE    10		//队列数量
#define MAX_EVENT_DATA_SIZE 32		//队列数据长度，用于传字符串

//队列数据类型
enum {
    EVENT_TYPE_TIMER,  // 定时器事件类型
    EVENT_TYPE_INIT,   // 初始化事件类型
    EVENT_TYPE_MSG,    // 消息事件类型
    EVENT_TYPE_KEY,    // 按键事件类型
    //----------以上顺序不要修改---------------
    EVENT_TYPE_CUSTOM,  //自定义事件
    EVENT_TYPE_STR,    // 字符事件类型
    EVENT_TYPE_FLOAT,  // 浮点事件类型
    EVENT_TYPE_GUI,    // GUI事件类型
};
//队列数据结构
typedef struct {
#if (MAX_THREADS>16)
	uint32_t thread_mask;
#else
	uint16_t thread_mask;   //线程掩码
#endif
    uint8_t type;          //事件类型
    union {
        int msg;
        int key;
        void *arg;
        float f;
        char str[MAX_EVENT_DATA_SIZE];
    } dat;
    uint8_t uid;
} Event;

//环形队列
typedef struct {

    uint8_t size;
    uint8_t head;
    uint8_t tail;
    Event events[EVENT_QUEUE_SIZE];
} EventQueue;

//事件线程
typedef struct sc_task_t {
    uint32_t time_dly;
    uint32_t time_out;
    uint32_t ms;
    void (*thread_cb) (void*);
    void (*delay_cb) (void);
} sc_task_t;

extern uint32_t system_tick;

// 创建事件线程
uint8_t sc_create_task(uint8_t id, void (*thread_cb)(void*), uint32_t ms);
// 线程存在否
uint8_t sc_task_true(uint8_t id);
// 删除线程
void sc_delete_task(uint8_t id);
// 遍历线程
void sc_task_loop(void);

//单次延时，可以递归使用
void sc_add_delay(uint8_t id,void (*delay_cb)(void),uint32_t ms);

//停止延时
void sc_stop_delay(uint8_t id);

//发送线程消息
void sc_send_event_mask(uint32_t mask,uint16_t type,long event,uint8_t Priority);

void sc_send_event(int key);

void sc_send_user_arg(uint16_t id,const char* str,int vol);
//------------------demo--------------------------

//按键事件定义
enum
{
    SCKEY_EVENT_UP    = (0x01<<0),
    SCKEY_EVENT_DP    = (0x01<<1),
    SCKEY_EVENT_L     = (0x01<<2),
    SCKEY_EVENT_R     = (0x01<<3),
    SCKEY_EVENT_OK    = (0x01<<4),
    SCKEY_EVENT_BLACK = (0x01<<5),
    SCKEY_EVENT_VOLN  = (0x01<<6),
    SCKEY_EVENT_ON    = (0x01<<7),
    SCKEY_EVENT_VOLP  = (0x01<<8),
};

#define KEY_REL        (1<<12)             //单击释放
#define KEY_MULIT(n)   (n<<12)             //多击释放
#define KEY_LONG1      (0x8000)            //长按
#define KEY_LONG2      (KEY_LONG1|KEY_REL) //长按++

void sc_key_scan(int key);

#endif

