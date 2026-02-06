#include "system/app_core.h"
#include "system/includes.h"

#include "app_config.h"
#include "app_action.h"

#include "btstack/btstack_task.h"
#include "btstack/bluetooth.h"
#include "user_cfg.h"
#include "vm.h"
#include "btcontroller_modules.h"
#include "bt_common.h"
#include "3th_profile_api.h"
#include "le_common.h"
#include "rcsp_bluetooth.h"
#include "JL_rcsp_api.h"
#include "custom_cfg.h"
#include "btstack/btstack_event.h"
#include "gatt_common/le_gatt_common.h"
#include "ble_trans.h"
#include "ble_trans_profile.h"


/* rx update ******************************************************************************************/
#define UPDATE_FILE_SIZE (4 * 1024)

static cbuffer_t rx_update_cbuf;
static u8 rx_update_buffer[UPDATE_FILE_SIZE] __attribute__((aligned(4)));

void rx_update_file_init(void)
{
    cbuf_init(&rx_update_cbuf, rx_update_buffer, UPDATE_FILE_SIZE);
    cbuf_clear(&rx_update_cbuf);
}

void rx_update_file_clear(void)
{
    cbuf_clear(&rx_update_cbuf);
}

void rx_update_file_write(u8 *buf, u32 len)
{
    cbuf_write(&rx_update_cbuf, (void *)buf, len);
}

u32 rx_update_file_get_size(void)
{
    return cbuf_get_data_size(&rx_update_cbuf);
}

u32 rx_update_file_read(u8 *buf, u32 len, u8 full)
{
    if (!full)
    {
        u32 rlen = cbuf_get_data_size(&rx_update_cbuf);
        if (rlen < len)
            len = rlen;
    }
    return cbuf_read(&rx_update_cbuf, buf, len);
}

/* rx command ******************************************************************************************/
#define COMMAND_SIZE (256)

static cbuffer_t rx_command_cbuf;
static u8 rx_command_buffer[COMMAND_SIZE] __attribute__((aligned(4)));

void rx_command_init(void)
{
    cbuf_init(&rx_command_cbuf, rx_command_buffer, COMMAND_SIZE);
    cbuf_clear(&rx_command_cbuf);
}

void rx_command_clear(void)
{
    cbuf_clear(&rx_command_cbuf);
}

void rx_command_write(u8 *buf, u32 len)
{
    cbuf_write(&rx_command_cbuf, (void *)buf, len);
}

u32 rx_command_get_size(void)
{
    return cbuf_get_data_size(&rx_command_cbuf);
}

u32 rx_command_read(u8 *buf, u32 len, u8 full)
{
    if (!full)
    {
        u32 rlen = cbuf_get_data_size(&rx_command_cbuf);
        if (rlen < len)
            len = rlen;
    }
    return cbuf_read(&rx_command_cbuf, buf, len);
}

/******************************************************************************************************
 * uart task
 ******************************************************************************************************/

#define UART_DEV_USAGE_TEST_SEL     2
#define UART_DEV_FLOW_CTRL          0
#define UART_DEV_TEST_MULTI_BYTE    1

#define RX_COMMAND_READ_FULL    1
#define RX_COMMAND_READ_LESS    0

#define UART_PACKET_MAX_SIZE        COMMAND_SIZE // (1024 * 4)

static u8 uart_cbuf[UART_PACKET_MAX_SIZE] __attribute__((aligned(4)));
static u8 uart_rxbuf[UART_PACKET_MAX_SIZE] __attribute__((aligned(4)));
static OS_SEM uart_command_sem;

struct _uart_command_name_t {
    u8 id;
    const char *name;
};
static struct _uart_command_name_t uart_command_name_table[] = {
    { 0x0A, "开/关" },
    { 0x0B, "增加亮度" },
    { 0x0C, "减少亮度" },
    { 0x0D, "读参数" },
    { 0x0E, "读状态" },
    { 0x0F, "感应到人体" },
    { 0x10, "感应到人体离开" },
    { 0x20, "下载数据" },
};

extern void trans_uart_rx_to_ble(u8 *packet, u32 size);

static void uart_u_task(void *arg)
{
    const uart_bus_t *uart_bus = arg;
    int ret;
    u32 uart_rxcnt = 0;

    printf("uart_u_task start\n");
    while (1)
    {
#if !UART_DEV_TEST_MULTI_BYTE
        //uart_bus->getbyte()在尚未收到串口数据时会pend信号量，挂起task，直到UART_RX_PND或UART_RX_OT_PND中断发生，post信号量，唤醒task
        ret = uart_bus->getbyte(&uart_rxbuf[0], 0);
        if (ret)
        {
            uart_rxcnt = 1;
            printf("get_byte: %02x\n", uart_rxbuf[0]);
            uart_bus->putbyte(uart_rxbuf[0]);
        }
#else
        //uart_bus->read()在尚未收到串口数据时会pend信号量，挂起task，直到UART_RX_PND或UART_RX_OT_PND中断发生，post信号量，唤醒task
        uart_rxcnt = uart_bus->read(uart_rxbuf, sizeof(uart_rxbuf), 0);
        if (uart_rxcnt)
        {
            printf("get_buffer[%d]:\n", uart_rxcnt);
            rx_command_write(uart_rxbuf, uart_rxcnt);
            os_sem_post(&uart_command_sem);

            // uart_bus->write(uart_rxbuf, uart_rxcnt);
#if 0
            printf("-fff3_rx(%d):", uart_rxcnt);
            rx_update_file_write(uart_rxbuf, uart_rxcnt);
            printf("rx_write finished: %d", rx_update_file_get_size());
            if (rx_update_file_get_size() >= 62400)
            {
                u32 rlen = 0;
                while (1)
                {
                    if (rx_update_file_get_size() >= 4096)
                        rlen = cbuf_read(&rx_update_cbuf, uart_rxbuf, 4096);
                    else
                        rlen = cbuf_read(&rx_update_cbuf, uart_rxbuf, rx_update_file_get_size());
                    if (rlen == 0)
                        break;
                    uart_bus->write(uart_rxbuf, rlen);
                }
            }
#endif
        }
#endif
    }
}

static void uart_command_task(void *arg)
{
    OS_SEM *sem = (OS_SEM *)arg;
    u32 step = 0x00;
    u8 buff_temp[64];
    u8 restart_flag = 0;
    while (1)
    {
        os_sem_pend(sem, step == 0x00 ? 0 : 5);  // 如果数据在解码中，则等待50ms，超时就重新解码

        if (step != 0x00)
        {
            restart_flag++;
            if (restart_flag > 1)
            {   // 串口接收分了2段以上，认为超时了，重新解码
                step = 0x00;
                restart_flag = 0;
            }
        }
        else
        {
            restart_flag = 0;
        }

__step_goon:

        switch (step)
        {
        case 0x00:  // 判断头
            if (rx_command_read(&buff_temp[0], 1, RX_COMMAND_READ_FULL) == 1)
            {
                if (buff_temp[0] == 0x55)
                    step = 0x01;
                else
                    step = 0x00;
                goto __step_goon;
            }
            break;

        case 0x01:  // 读取长度值
            if (rx_command_read(&buff_temp[1], 1, RX_COMMAND_READ_FULL) == 1)
            {
                if (buff_temp[1] > 0)
                    step = 0x02;
                else
                    step = 0x00;
                goto __step_goon;
            }
            break;

        case 0x02:  // 读取数据(根据0x01的长度值)
            if (rx_command_read(&buff_temp[2], buff_temp[1], RX_COMMAND_READ_FULL) == buff_temp[1])
            {
                step = 0x00;

                // CRC
                u8 crc = 0;
                for (u8 index = 0; index < (buff_temp[1] - 1); index++)
                {
                    crc += buff_temp[index + 2];
                }
                if (crc == buff_temp[2 + buff_temp[1] - 1])
                {
                    // Process the received data
                    for (u8 index = 0; index < ARRAY_SIZE(uart_command_name_table); index++)
                    {
                        if (uart_command_name_table[index].id == buff_temp[2])
                        {
                            printf("Command: %s\n", uart_command_name_table[index].name);
                            break;
                        }
                    }
                    switch (buff_temp[2])
                    {
                    case 0x0A:  // 开/关            APP->DEV
                    case 0x0B:  // 增加亮度         APP->DEV
                    case 0x0C:  // 减少亮度         APP->DEV
                    case 0x0D:  // 读参数           APP->DEV
                    case 0x0E:  // 读状态           APP->DEV
                    case 0x0F:  // 感应到人体       DEV->APP
                    case 0x10:  // 感应到人体离开   DEV->APP
                    case 0x20:  // 下载数据
                        trans_uart_rx_to_ble(buff_temp, 2 + buff_temp[1]);
                        break;

                    default:    // 错误命令
                        printf("Unknown command: 0x%02X\n", buff_temp[2]);
                        break;
                    }
                }

                goto __step_goon;
            }
            break;
        }
    }
}

void uart_task_init(void)
{
    const uart_bus_t *uart_bus;
    struct uart_platform_data_t u_arg = {0};
    u_arg.tx_pin = IO_PORTB_06;
    u_arg.rx_pin = IO_PORTB_07;
    u_arg.rx_cbuf = uart_cbuf;
    u_arg.rx_cbuf_size = UART_PACKET_MAX_SIZE;
    u_arg.frame_length = 0xFFFFFFFF;
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
        os_sem_create(&uart_command_sem, 0);
        os_task_create(uart_command_task, (void *)&uart_command_sem, 3, 512, 0, "utcmd_task");
#endif
#if UART_DEV_FLOW_CTRL
		os_task_create(uart_flow_ctrl_task, (void *)uart_bus, 31, 128, 0, "flow_ctrl");
#endif
    }
}
