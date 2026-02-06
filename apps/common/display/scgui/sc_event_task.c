
#include "sc_event_task.h"
/**多线程事件驱动框架：2024/10
 * 支持多类型事件，带优先级，线程初始化事件，线程定时事件，指针事件，自字义事件
 * 事件发送可以指定线程mask，或广播key。
 * 优化1：增加单次延时回调，共用ID减少MAX_THREADS长度，提高遍历速度
 * 优化2：增加优先级,实现抢占式
 * 优化3：用while处理事件队列中的所有事件，减少函数进出开销
*/

/**fun: 初始化队列
 *输入: 事件句柄
 *返回:
*/
void EventQueue_Init(EventQueue* q)
{
    q->head = 0;
    q->tail = 0;
    q->size = 0;
}

/**fun: 出队列
 *输入: 事件句柄,事件
 *返回:
*/
uint8_t EventQueue_Pop(EventQueue* q, Event *event)
{
    if (q->size == 0)
    {
        return 0;  // 队列为空
    }

    *event = q->events[q->head];
    q->head = (q->head + 1) % EVENT_QUEUE_SIZE;
    q->size--;
    return 1;
}
/**fun: 入队列
 *输入: 事件句柄,事件,优先级
 *返回:
*/
uint8_t EventQueue_Push(EventQueue* q, const Event *event,uint8_t Priority)
{
    if (q->size>= EVENT_QUEUE_SIZE )
    {
        return 0;  // 队列已满
    }
    if(Priority)
    {
        q->head = (q->head - 1+EVENT_QUEUE_SIZE) % EVENT_QUEUE_SIZE;
        q->events[q->head] = *event;    //入队列头
    }
    else
    {
        q->events[q->tail] = *event;     //入队列尾
        if(++q->tail>=EVENT_QUEUE_SIZE) q->tail= 0;
    }
    q->size++;
    return 1;
}

//======================新加多类型事件队列==========================
uint32_t system_tick = 0;  // 全局tick计数器，由中断更新
uint32_t sc_tick = 0;      // 中间tick变量，在主循环中使用

sc_task_t  sc_task[MAX_THREADS] = {0};
EventQueue eventQueue = {0};

/**fun: 创建事件线程,独立id可以和延时共用id
 *输入: 线程ID,ms
 *注:
*/
uint8_t sc_create_task(uint8_t id, void (*thread_cb)(void*), uint32_t ms)
{
    Event e;
    sc_task[id].ms = ms;               //线程定时
    sc_task[id].time_out = sc_tick + ms;
    if (sc_task[id].thread_cb != thread_cb)
    {
        sc_task[id].thread_cb = thread_cb;

        /**首次执行初始化事件，建议使用*/
        e.type=EVENT_TYPE_INIT;
        sc_task[id].thread_cb(&e);
        return 0;
    }
    return 1;       //已存在
}

/**fun: 删除线程
 *输入: 线程ID
 *返回:
*/
void sc_delete_task(uint8_t id)
{
    sc_task[id].thread_cb = NULL;
    sc_task[id].ms = 0;
}

/**fun: 线程存在否
 *输入: 线程ID
 *返回:
*/
uint8_t sc_task_true(uint8_t id)
{
    return (sc_task[id].thread_cb != NULL);
}

/**fun: 单次延时,独立id可以和定时共用id,可以递归使用
 *输入: 线程ID,回调函数,MS
 *返回:
*/
void sc_add_delay(uint8_t id,void (*delay_cb)(void),uint32_t ms)
{
    sc_task[id].time_dly = sc_tick+ms;
    sc_task[id].delay_cb = delay_cb;
}

/**fun: 停止延时
 *输入: 线程ID
 *返回:
*/
void sc_stop_delay(uint8_t id)
{
    sc_task[id].time_dly = 0;
    sc_task[id].delay_cb= NULL;
}
/**fun: 主循环调用
*/
void sc_task_loop(void)
{
    void (*delay_cb) (void);
    static Event event;
    static uint8_t id;
    system_tick = jiffies_msec();
    if(sc_tick!=system_tick)
    {
        sc_tick=system_tick;                    //防止中断打断
        for (id = 0; id < MAX_THREADS; id++)
        {
            //------------延时回调---------
            if (sc_task[id].delay_cb && (int)sc_task[id].time_dly-(int)sc_tick<=0)
            {
                delay_cb = sc_task[id].delay_cb;  // 中间变量
                sc_task[id].delay_cb = NULL;
                delay_cb();  					  // 内部递归
            }
            //------------定时事件--------
            if (sc_task[id].ms && (int)sc_task[id].time_out-(int)sc_tick<=0)
            {
                event.type = EVENT_TYPE_TIMER;
                sc_task[id].thread_cb(&event);
                sc_task[id].time_out = sc_tick + sc_task[id].ms;
            }
        }
        while (EventQueue_Pop(&eventQueue, &event))  // 处理事件队列中的所有事件
        {

            for (id = 0; id < MAX_THREADS; id++)     // 查找匹配索引
            {
                if (sc_task[id].thread_cb && (event.thread_mask & 0x01))
                {
                    sc_task[id].thread_cb(&event);  // 调用对应的线程函数处理事件
                }
                event.thread_mask>>=1;              // 掩码右移，检查下一个位
            }
        }
    }

}


/**fun: 发送事件指定线程接收
 *输入: 线程mask,优先级Priority,event可以是指针用long类型
 *返回:
*/
void sc_send_event_mask(uint32_t mask,uint16_t type,long event,uint8_t Priority)
{
    Event e;
    e.thread_mask=mask;             //线程mask
    e.type=type;
    int i=0;
    const char* str= (const char*)0+event;
    switch (type)
    {
    case EVENT_TYPE_KEY:
        e.dat.key=event;
        break;
    case EVENT_TYPE_MSG:
        e.dat.msg=event;
        break;
    case EVENT_TYPE_CUSTOM:
        e.dat.arg= (void*)0+event;
        break;
    case EVENT_TYPE_STR:
        while(*str&&i<MAX_EVENT_DATA_SIZE)
        {
            e.dat.str[i++]= *str;
            str++;
        }
        e.dat.str[i]='\0';
        break;
    default:
        break;
    }
    EventQueue_Push(&eventQueue, &e,Priority);
}
/**fun:发送广播事件key
 *输入:键值
 *返回:
*/
void sc_send_event(int key)
{
    sc_send_event_mask(0xffff,EVENT_TYPE_KEY,key,0);
}

//用于发送参数给指定控件
void sc_send_user_arg(uint16_t id,const char* str,int vol)
{
    Event e;
    e.thread_mask=0xffff;             //线程mask
    e.type=EVENT_TYPE_GUI;
    int i=0;
    if(str)
    {
        while(*str&&i<MAX_EVENT_DATA_SIZE)
        {
            e.dat.str[i++]= *str;
            str++;
        }
        e.dat.str[i]='\0';
    }
    else
    {
        e.dat.key=vol;
    }
    e.uid=id;
    EventQueue_Push(&eventQueue, &e,1);
}

/***************按键事件驱动demo*************************/
#define BSP_KEY_FILTER   3           /*消抖计数*/
#define BSP_KEY_LONG     100         /*长按定义*/
#define BSP_KEY_MULIT    20			 /*多击间隔*/
#define BSP_KEY_REPEAT   50			 /*长按连发速度*/
void sc_key_scan(int key)
{
    static  uint8_t   cnt;         //双击
    static  uint8_t   key_dly;     //多击间隔
    static	uint16_t  key_count;
    static	int       old_key=-1;   //
    if(key)
    {
        key_count++;
        if(key_count==BSP_KEY_FILTER)
        {
            cnt++;
            if(key_dly==0)
            {
                sc_send_event(key);                //按下
                key_dly=1;                        //单按
            }
//            if(key==KEY_EVENT_1)                //双击
//            {
//                key_dly=BSP_KEY_MULIT;
//            }

        }
        if(key_count==BSP_KEY_LONG)
        {
            sc_send_event(key|KEY_LONG1);        //长按事件
            key_dly=0;                           //无释放事件
        }
        if(key_count>=BSP_KEY_LONG+BSP_KEY_REPEAT)
        {
            key_count-=BSP_KEY_REPEAT;
            sc_send_event(key|KEY_LONG2);         //长按++
            key_dly=0;                            //无释放事件

        }
        old_key=key;
    }
    else
    {
        if(key_dly)
        {
            if(--key_dly==0||cnt==2)                    //2连
            {
                sc_send_event(old_key|KEY_MULIT(cnt));  //释放事件
                key_dly=0;
            }
        }
        else
        {
            cnt=0;
            old_key=0;
        }
        key_count=0;
    }
}

/***************demo*************************/
#if 0

void task_main(void *arg)
{
    Event *e= (Event*)arg;
    //接收所有事件
    switch (e->type)
    {
    case EVENT_TYPE_INIT:
        //这里初始化，比如开个延时回调倒计时关机
        //sc_add_delay(uint8_t id,void (*delay_cb)(void),uint32_t ms);
        printf("EVENT_TYPE_INIT.... \n");
        break;

    case EVENT_TYPE_KEY:
        //这里处理按键事件
        printf("EVENT_TYPE_KEY: %d\n", e->dat.key);

        break;
    case EVENT_TYPE_MSG:
        //这里处理消息事件，比如低电报警消息
        printf("EVENT_TYPE_MSG: %d\n", e->dat.msg);
        break;

    case EVENT_TYPE_STR:
        //这里字符串消息事件
        printf("EVENT_TYPE_STR: %s\n", e->dat.str);
        break;
    case EVENT_TYPE_CUSTOM:
        //这里处理自定义事件
        printf("EVENT_TYPE_CUSTOM: %p\n", e->dat.arg);
        break;
    case EVENT_TYPE_TIMER:
        //当前线程设置的定时事件
       // printf("is 500ms:   \n");
        break;
    default:

        break;
    }
}


#endif // 0
