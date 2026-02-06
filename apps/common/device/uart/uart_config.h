#ifndef _UART_CONFIG_H_
#define _UART_CONFIG_H_

enum {
    UT_CMD_START = 0,
    UT_CMD_CHECK_ONLINE,
    UT_CMD_ERASE_CHIP,
    UT_CMD_WRITE_DATA,
    UT_CMD_WRITE_DATA_FINISHED,
    
    UT_CMD_CHECK_READ_ONLINE,
    UT_CMD_READ_DATA,
    UT_CMD_READ_DATA_FINISHED,

    UT_CMD_CHECK_DATA_START,
    UT_CMD_CHECK_DATA,
    UT_CMD_CHECK_DATA_FINISHED,
};

void uart_dev_init(void);
void my_task_init(void);
void my_mutex_task_init(void);

#endif