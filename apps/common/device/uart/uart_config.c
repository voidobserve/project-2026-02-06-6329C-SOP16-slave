#include "system/includes.h"
#include "asm/uart_dev.h"
#include "system/event.h"
#include "uart_config.h"
#include "norflash_config.h"

#if 1
/*
    [[  注意!!!  ]]
    * 如果当系统任务较少时使用本demo，需要将低功耗关闭（#define TCFG_LOWPOWER_LOWPOWER_SEL    0//SLEEP_EN ），否则任务被串口接收函数调用信号量pend时会导致cpu休眠，串口中断和DMA接收将遗漏数据或数据不正确
*/

#define UART_DEV_USAGE_TEST_SEL         2       //uart_dev.c api接口使用方法选择
//  选择1  串口中断回调函数推送事件，由事件响应函数接收串口数据
//  选择2  由task接收串口数据

#define UART_DEV_TEST_MULTI_BYTE        1       //uart_dev.c 读写多个字节api / 读写1个字节api 选择

#define UART_DEV_FLOW_CTRL				0

static u8 uart_cbuf[1024 * 32] __attribute__((aligned(4)));
static u8 uart_rxbuf[1024 * 32] __attribute__((aligned(4)));

static void my_put_u8hex(u8 dat)
{
    u8 tmp;
    tmp = dat / 16;
    if (tmp < 10) {
        putchar(tmp + '0');
    } else {
        putchar(tmp - 10 + 'A');
    }
    tmp = dat % 16;
    if (tmp < 10) {
        putchar(tmp + '0');
    } else {
        putchar(tmp - 10 + 'A');
    }
    putchar(0x20);
}

static uint8_t buffer_get_crc(uint8_t *buffer, uint32_t len)
{
    unsigned char crc = 0;

    for (int index = 0; index < len; index++)
    {
        crc += buffer[index];
    }

    return crc & 0xFF;
}

//设备事件响应demo
#if (UART_DEV_USAGE_TEST_SEL == 1)
static void uart_event_handler(struct sys_event *e)
{
    const uart_bus_t *uart_bus;
    u32 uart_rxcnt = 0;

    if ((u32)e->arg == DEVICE_EVENT_FROM_UART_RX_OVERFLOW) {
        if (e->u.dev.event == DEVICE_EVENT_CHANGE) {
            /* printf("uart event: DEVICE_EVENT_FROM_UART_RX_OVERFLOW\n"); */
            uart_bus = (const uart_bus_t *)e->u.dev.value;
            uart_rxcnt = uart_bus->read(uart_rxbuf, sizeof(uart_rxbuf), 0);
            if (uart_rxcnt) {
                printf("get_buffer:\n");
                for (int i = 0; i < uart_rxcnt; i++) {
                    my_put_u8hex(uart_rxbuf[i]);
                    if (i % 16 == 15) {
                        putchar('\n');
                    }
                }
                if (uart_rxcnt % 16) {
                    putchar('\n');
                }
#if (!UART_DEV_FLOW_CTRL)
                uart_bus->write(uart_rxbuf, uart_rxcnt);
#endif
            }
            printf("uart out\n");
        }
    }
    if ((u32)e->arg == DEVICE_EVENT_FROM_UART_RX_OUTTIME) {
        if (e->u.dev.event == DEVICE_EVENT_CHANGE) {
            /* printf("uart event:DEVICE_EVENT_FROM_UART_RX_OUTTIME\n"); */
            uart_bus = (const uart_bus_t *)e->u.dev.value;
            uart_rxcnt = uart_bus->read(uart_rxbuf, sizeof(uart_rxbuf), 0);
            if (uart_rxcnt) {
                printf("get_buffer:\n");
                for (int i = 0; i < uart_rxcnt; i++) {
                    my_put_u8hex(uart_rxbuf[i]);
                    if (i % 16 == 15) {
                        putchar('\n');
                    }
                }
                if (uart_rxcnt % 16) {
                    putchar('\n');
                }
#if (!UART_DEV_FLOW_CTRL)
                uart_bus->write(uart_rxbuf, uart_rxcnt);
#endif
            }
            printf("uart out\n");
        }
    }
}
SYS_EVENT_HANDLER(SYS_DEVICE_EVENT, uart_event_handler, 0);
#endif

static void uart_u_task(void *arg)
{
    const uart_bus_t *uart_bus = arg;
    int ret;
    u32 uart_rxcnt = 0;

    printf("uart_u_task start\n");
    while (1) {
#if !UART_DEV_TEST_MULTI_BYTE
        //uart_bus->getbyte()在尚未收到串口数据时会pend信号量，挂起task，直到UART_RX_PND或UART_RX_OT_PND中断发生，post信号量，唤醒task
        ret = uart_bus->getbyte(&uart_rxbuf[0], 0);
        if (ret) {
            uart_rxcnt = 1;
            printf("get_byte: %02x\n", uart_rxbuf[0]);
            uart_bus->putbyte(uart_rxbuf[0]);
        }
#else
        //uart_bus->read()在尚未收到串口数据时会pend信号量，挂起task，直到UART_RX_PND或UART_RX_OT_PND中断发生，post信号量，唤醒task
        uart_rxcnt = uart_bus->read(uart_rxbuf, sizeof(uart_rxbuf), 0);
        if (uart_rxcnt) {
            printf("get_buffer[%d]:\n", uart_rxcnt);
#if 0
            for (int i = 0; i < uart_rxcnt; i++) {
                my_put_u8hex(uart_rxbuf[i]);
                if (i % 16 == 15) {
                    putchar('\n');
                }
            }
            if (uart_rxcnt % 16) {
                putchar('\n');
            }
#endif
            uint8_t crc = buffer_get_crc(uart_rxbuf, uart_rxcnt - 1);
            if (crc != uart_rxbuf[uart_rxcnt - 1] || uart_rxcnt < 5 || uart_rxbuf[0] != 0x5A)
                continue;
            uint32_t data_len = uart_rxbuf[2]; data_len <<= 8; data_len |= uart_rxbuf[3];
            uint8_t command = uart_rxbuf[1];
            switch (command)
            {
            case UT_CMD_CHECK_ONLINE:
                {
                    uint8_t tx_temp[] = { 0x5A, UT_CMD_CHECK_ONLINE, 0, 1, 0x02, 0 };
                    tx_temp[sizeof(tx_temp) - 1] = buffer_get_crc(tx_temp, sizeof(tx_temp) - 1);
                    uart_bus->write(tx_temp, 6);
                }
                break;

            case UT_CMD_ERASE_CHIP:
                if (flash_spi_erase_chip() == 0)
                {
                    uint8_t tx_temp[] = { 0x5A, UT_CMD_ERASE_CHIP, 0, 1, 0x02, 0 };
                    tx_temp[sizeof(tx_temp) - 1] = buffer_get_crc(tx_temp, sizeof(tx_temp) - 1);
                    uart_bus->write(tx_temp, 6);
                }
                break;

            case UT_CMD_WRITE_DATA:
                {
                    if (flash_spi_write(&uart_rxbuf[4], data_len) == 0)
                        break;
                    
                    uint8_t tx_temp[] = { 0x5A, UT_CMD_WRITE_DATA, 0, 1, 0x02, 0 };
                    tx_temp[sizeof(tx_temp) - 1] = buffer_get_crc(tx_temp, sizeof(tx_temp) - 1);
                    uart_bus->write(tx_temp, 6);
                }
                break;

            case UT_CMD_WRITE_DATA_FINISHED:
                {
                    uint8_t tx_temp[] = { 0x5A, UT_CMD_WRITE_DATA_FINISHED, 0, 1, 0x02, 0 };
                    tx_temp[sizeof(tx_temp) - 1] = buffer_get_crc(tx_temp, sizeof(tx_temp) - 1);
                    uart_bus->write(tx_temp, 6);
                }
                break;



            case UT_CMD_CHECK_READ_ONLINE:
                {
                    flash_spi_read(NULL, 0, 0, 1);
                    uint8_t tx_temp[] = { 0x5A, UT_CMD_CHECK_READ_ONLINE, 0, 1, 0x02, 0 };
                    tx_temp[sizeof(tx_temp) - 1] = buffer_get_crc(tx_temp, sizeof(tx_temp) - 1);
                    uart_bus->write(tx_temp, 6);
                }
                break;

            case UT_CMD_READ_DATA:
                {
                    uint32_t packet_offset;
                    packet_offset  = uart_rxbuf[4]; packet_offset <<= 8;
                    packet_offset |= uart_rxbuf[5]; packet_offset <<= 8;
                    packet_offset |= uart_rxbuf[6]; packet_offset <<= 8;
                    packet_offset |= uart_rxbuf[7];
                    uint32_t packet_size;
                    packet_size  = uart_rxbuf[8];  packet_size <<= 8;
                    packet_size |= uart_rxbuf[9];  packet_size <<= 8;
                    packet_size |= uart_rxbuf[10]; packet_size <<= 8;
                    packet_size |= uart_rxbuf[11];

                    flash_spi_read(&uart_rxbuf[4], packet_size, packet_offset, 0);
                    uart_rxbuf[0] = 0x5A;
                    uart_rxbuf[1] = UT_CMD_READ_DATA;
                    uart_rxbuf[2] = (packet_size >> 8) & 0xFF;
                    uart_rxbuf[3] = packet_size & 0xFF;

                    uart_rxbuf[4 + packet_size] = buffer_get_crc(uart_rxbuf, 4 + packet_size);
                    uart_bus->write(uart_rxbuf, 4 + packet_size + 1);
                }
                break;

            case UT_CMD_READ_DATA_FINISHED:
                {
                    uint8_t tx_temp[] = { 0x5A, UT_CMD_READ_DATA_FINISHED, 0, 1, 0x02, 0 };
                    tx_temp[sizeof(tx_temp) - 1] = buffer_get_crc(tx_temp, sizeof(tx_temp) - 1);
                    uart_bus->write(tx_temp, 6);
                }
                break;


            case UT_CMD_CHECK_DATA_START:
                {
                    flash_spi_check_data(NULL, 0, 1);
                    uint8_t tx_temp[] = { 0x5A, UT_CMD_CHECK_DATA_START, 0, 1, 0x02, 0 };
                    tx_temp[sizeof(tx_temp) - 1] = buffer_get_crc(tx_temp, sizeof(tx_temp) - 1);
                    uart_bus->write(tx_temp, 6);
                }
                break;

            case UT_CMD_CHECK_DATA:
                {
                    uint8_t tx_temp[] = { 0x5A, UT_CMD_CHECK_DATA, 0, 1, 0x02, 0 };

                    if (flash_spi_check_data(&uart_rxbuf[4], data_len, 0) != 0)
                    {
                        tx_temp[4] = 0x03;
                    }
                    
                    tx_temp[sizeof(tx_temp) - 1] = buffer_get_crc(tx_temp, sizeof(tx_temp) - 1);
                    uart_bus->write(tx_temp, 6);
                }
                break;

            case UT_CMD_CHECK_DATA_FINISHED:
                {
                    uint8_t tx_temp[] = { 0x5A, UT_CMD_CHECK_DATA_FINISHED, 0, 1, 0x02, 0 };
                    tx_temp[sizeof(tx_temp) - 1] = buffer_get_crc(tx_temp, sizeof(tx_temp) - 1);
                    uart_bus->write(tx_temp, 6);
                }
                break;
            }
#if (!UART_DEV_FLOW_CTRL)
            // uart_bus->write(uart_rxbuf, uart_rxcnt);
#endif
        }
#endif
    }
}

#if (UART_DEV_USAGE_TEST_SEL == 1)
static void uart_isr_hook(void *arg, u32 status)
{
    const uart_bus_t *ubus = arg;
    struct sys_event e;

    //当CONFIG_UARTx_ENABLE_TX_DMA（x = 0, 1）为1时，不要在中断里面调用ubus->write()，因为中断不能pend信号量
    if (status == UT_RX) {
        printf("uart_rx_frame_full_isr\n");
#if (UART_DEV_USAGE_TEST_SEL == 1)
        e.type = SYS_DEVICE_EVENT;
        e.arg = (void *)DEVICE_EVENT_FROM_UART_RX_OVERFLOW;
        e.u.dev.event = DEVICE_EVENT_CHANGE;
        e.u.dev.value = (int)ubus;
        sys_event_notify(&e);
#endif
    }
    if (status == UT_RX_OT) {
        printf("uart_rx_overtime_isr\n");
#if (UART_DEV_USAGE_TEST_SEL == 1)
        e.type = SYS_DEVICE_EVENT;
        e.arg = (void *)DEVICE_EVENT_FROM_UART_RX_OUTTIME;
        e.u.dev.event = DEVICE_EVENT_CHANGE;
        e.u.dev.value = (int)ubus;
        sys_event_notify(&e);
#endif
    }
}
#endif

#if UART_DEV_FLOW_CTRL
static void uart_flow_ctrl_task(void *arg)
{
    const uart_bus_t *uart_bus = arg;
	while (1) {
		uart_bus->write("flow control test ", sizeof("flow control test "));
		os_time_dly(100);	
	}
}
#endif

void uart_dev_init(void)
{
    const uart_bus_t *uart_bus;
    struct uart_platform_data_t u_arg = {0};
    u_arg.tx_pin = IO_PORTA_03;//IO_PORT_DP;//IO_PORTB_00;//
    u_arg.rx_pin = IO_PORTA_04;//IO_PORT_DM;//IO_PORTB_01;//
    u_arg.rx_cbuf = uart_cbuf;
    u_arg.rx_cbuf_size = sizeof(uart_cbuf);
    u_arg.frame_length = sizeof(uart_cbuf) + 1;
    u_arg.rx_timeout = 10;
#if (UART_DEV_USAGE_TEST_SEL == 2)
    u_arg.isr_cbfun = NULL;  // 方式2采用线程查询方式，不需要中断回调推送消息
#else
    u_arg.isr_cbfun = uart_isr_hook;
#endif
    u_arg.baud = 1500000;
    u_arg.is_9bit = 0;
#if UART_DEV_FLOW_CTRL
    u_arg.tx_pin = IO_PORTA_00;
    u_arg.rx_pin = IO_PORTA_01;
    u_arg.baud = 1000000;
	extern void flow_ctl_hw_init(void);
	flow_ctl_hw_init();
#endif
    uart_bus = uart_dev_open(&u_arg);
    if (uart_bus != NULL) {
        printf("uart_dev_open() success\n");
#if (UART_DEV_USAGE_TEST_SEL == 2)
        os_task_create(uart_u_task, (void *)uart_bus, 31, 512, 0, "uart_u_task");
#endif
#if UART_DEV_FLOW_CTRL
		os_task_create(uart_flow_ctrl_task, (void *)uart_bus, 31, 128, 0, "flow_ctrl");
#endif
    }
}

#if UART_DEV_FLOW_CTRL
void uart_change_rts_state(void)
{
	static u8 rts_state = 1;
	extern void change_rts_state(u8 state);
	change_rts_state(rts_state);
	rts_state = !rts_state;
}
#endif

static u16 my_task_post_timer_id = 0;
static void my_task_post_isr(void *priv)
{
    OS_SEM *sem = (OS_SEM *)priv;
    
    /**
     * @brief 查询sem信号量是否有效
     * @param OS_SEM* 信号量句柄
     * @return 1:有效，0:已被删除但是仍然能对计数进行赋值和查询
     */
    int is_sem_valid = os_sem_valid(sem);
    printf("os_sem_valid: %d\n", is_sem_valid);
    // if (is_sem_valid == 0)
    //     return;

    /**
     * @brief 使sem信号量计数+1
     * @param OS_SEM* 信号量句柄
     */
    os_sem_post(sem);
    printf("%s: post\n", __func__);

    
    /**
     * @brief 查询sem信号量当前计数值
     * @param OS_SEM* 信号量句柄
     * @return 信号量当前计数值
     */
    printf("os_sem_query: %d\n", os_sem_query(sem));

    static uint8_t count = 0;
    count++;
    if (count > 2) {
        count = 0;
        
        /**
         * @brief sem信号量计数赋值
         * @param OS_SEM* 信号量句柄
         * @param cnt 信号量计数值赋值
         */
        os_sem_set(sem, 0);
        printf("%s: set 0\n", __func__);
    }
}

static void my_task(void *priv)
{
    OS_SEM *sem = (OS_SEM *)priv;

    while (1)
    {
        /**
         * @brief 线程阻塞，在未达到条件时一直处于阻塞状态(即系统在进行切换线程时会判断跳过当前线程，直到判断到非阻塞状态才会跑后面的代码)
         * @param OS_SEM* 信号量句柄
         * @param timeout 阻塞时间，单位10ms，为0时则一直阻塞，此时只判断sem信号量计数
         * @note 切换到非阻塞的条件：
         * @note 1、在系统轮询时检测到sem信号量的计数>0时会切换到非阻塞，并会将sem计数值-1；
         * @note 2、在系统轮询时检测到阻塞时间超时后，会切换到非阻塞；
         * @note 在切换非阻塞并执行完一次循环又回到pend()时，sem信号量又会变为阻塞状态，直到下一次达成切换条件
         */
        os_sem_pend(sem, 0);
        printf("%s\n", __func__);
        
        /**
         * @brief 删除sem信号量
         * @param OS_SEM* 信号量句柄
         * @param block 系统预留的无用变量，默认传入0即可
         * @note  已被删除的信号量仍然能对计数进行赋值和查询，只能通过os_sem_valid()来判断是否已被删除，和对pend()或者post()进行调用限制
         */
        os_sem_del(sem, 0);
        printf("sem del\n");

        if (my_task_post_timer_id)
        {    
            sys_s_hi_timer_del(my_task_post_timer_id);
            my_task_post_timer_id = 0;
            printf("timer del\n");
        }

        /**
         * @brief 删除线程
         * @param name 需要删除的线程的线程名，对应创建线程时的线程名
         * @note 在调用这个api后会即时对这个线程进行删除，在后面的代码也不会执行
         */
        os_task_del("my_task");
        printf("os task del\n");
    }
}

void my_task_init(void)
{
    static OS_SEM my_task_sem;

    /**
     * @brief 创建sem信号量
     * @param OS_SEM* 信号量句柄
     * @param cnt 信号量初始计数值
     */
    os_sem_create(&my_task_sem, 0);
    my_task_post_timer_id = sys_s_hi_timer_add(&my_task_sem, my_task_post_isr, 2000);
    os_task_create(my_task, &my_task_sem, 3, 512, 0, "my_task");
}

///DOTO:对mutex互斥量进行学习

static void my_mutex_task(void *priv)
{
    OS_MUTEX *mutex = (OS_MUTEX *)priv;

    while (1)
    {
        printf("os_mutex_pend\n");
        os_mutex_pend(mutex, 0);
        os_time_dly(20);
        os_mutex_post(mutex);
        printf("os_mutex_post\n");
    }
}

static void my_mutex2_task(void *priv)
{
    OS_MUTEX *mutex = (OS_MUTEX *)priv;

    while (1)
    {
        printf("os_mutex2_pend\n");
        os_mutex_pend(mutex, 0);
        os_time_dly(200);
        os_mutex_post(mutex);
        printf("os_mutex2_post\n");
    }
}

void my_mutex_task_init(void)
{
    static OS_MUTEX my_task_mutex;

    /**
     * @brief 创建sem信号量
     * @param OS_SEM* 信号量句柄
     * @param cnt 信号量初始计数值
     */
    os_mutex_create(&my_task_mutex);
    os_task_create(my_mutex_task, &my_task_mutex, 3, 512, 0, "my_mx_task");
    os_task_create(my_mutex2_task, &my_task_mutex, 3, 512, 0, "my_mx2_task");
}



#endif
