/*********************************************************************************************
 *   Filename        : main.c
 *   Description     : BLE Broadcast Transfer Main Entry
 *   Author          : Broadcast Transfer Team
 *   Date            : 2025-12-09
 *********************************************************************************************/

#include "system/app_core.h"
#include "system/includes.h"
#include "app_config.h"
#include "btstack/btstack_task.h"
#include "btstack/bluetooth.h"
#include "user_cfg.h"
#include "vm.h"
#include "btcontroller_modules.h"
#include "bt_common.h"
#include "3th_profile_api.h"
#include "le_common.h"
#include "btstack/btstack_event.h"
#include "gatt_common/le_gatt_common.h"
#include "common/protocol.h"

#define LOG_TAG             "[U_COMM]"
#include "debug.h"
const char LOG_tag(LOG_TAG_CONST,LOG_v) AT(.LOG_TAG_CONST) = 0;
const char LOG_tag(LOG_TAG_CONST,LOG_i) AT(.LOG_TAG_CONST) = 1;
const char LOG_tag(LOG_TAG_CONST,LOG_d) AT(.LOG_TAG_CONST) = 1;
const char LOG_tag(LOG_TAG_CONST,LOG_w) AT(.LOG_TAG_CONST) = 1;
const char LOG_tag(LOG_TAG_CONST,LOG_e) AT(.LOG_TAG_CONST) = 1;
const char LOG_tag(LOG_TAG_CONST,LOG_c) AT(.LOG_TAG_CONST) = 1;

// 外部函数声明
extern const char *bt_get_local_name(void);
extern void clr_wdt(void);
extern const gatt_server_cfg_t mul_server_init_cfg;
extern const gatt_client_cfg_t mul_client_init_cfg;
extern void master_mode_init(void);
extern void slave_mode_init(void);
extern void uart_rx_test_app(const u8 batch_id[6], const u8 *data, u16 data_len);
extern void uart_rx_test_quick(void);

//================================ 全局变量 ================================//
u8 uart_cbuf[UART_RX_BUF_SIZE] __attribute__((aligned(4)));
u8 uart_rxbuf[UART_RX_BUF_SIZE] __attribute__((aligned(4)));

// 设备角色 (上电检测一次)
static u8 g_device_role = DEVICE_ROLE_SLAVE;

// ATT 配置
#define ATT_LOCAL_MTU_SIZE          517
#define ATT_PACKET_NUMS_MAX         2
#define ATT_SEND_CBUF_SIZE          (ATT_PACKET_NUMS_MAX * (ATT_PACKET_HEAD_SIZE + ATT_LOCAL_MTU_SIZE))
//输入passkey 加密
#define PASSKEY_ENABLE                     0

static const sm_cfg_t sm_init_config = {
    .master_security_auto_req = 1,
    .master_set_wait_security = 1,
    .slave_security_auto_req = 0,
    .slave_set_wait_security = 1,

#if PASSKEY_ENABLE
    .io_capabilities = IO_CAPABILITY_DISPLAY_ONLY,
#else
    .io_capabilities = IO_CAPABILITY_NO_INPUT_NO_OUTPUT,
#endif

    .authentication_req_flags = SM_AUTHREQ_BONDING | SM_AUTHREQ_MITM_PROTECTION,
    .min_key_size = 7,
    .max_key_size = 16,
    .sm_cb_packet_handler = NULL,
};

extern const gatt_server_cfg_t mul_server_init_cfg;
extern const gatt_client_cfg_t mul_client_init_cfg;

//gatt 控制块初始化
static gatt_ctrl_t mul_gatt_control_block = {
    //public
    .mtu_size = ATT_LOCAL_MTU_SIZE,
    .cbuffer_size = ATT_SEND_CBUF_SIZE,
    .multi_dev_flag	= 1,

    //config
#if CONFIG_BT_GATT_SERVER_NUM
    .server_config = &mul_server_init_cfg,
#else
    .server_config = NULL,
#endif

#if CONFIG_BT_GATT_CLIENT_NUM
    .client_config = &mul_client_init_cfg,
#else
    .client_config = NULL,
#endif

#if CONFIG_BT_SM_SUPPORT_ENABLE
    .sm_config = &sm_init_config,
#else
    .sm_config = NULL,
#endif
    //cbk,event handle
    .hci_cb_packet_handler = NULL,
};

//================================ 函数实现 ================================//

/**
 * @brief 获取设备角色
 * @return 设备角色 (DEVICE_ROLE_MASTER 或 DEVICE_ROLE_SLAVE)
 */
u8 get_device_role(void)
{
    return g_device_role;
}

/**
 * @brief 检测设备角色
 * 
 * 通过 ROLE_DETECT_GPIO (IO_PORTB_07) 检测:
 * - 低电平 (接地): 子设备模式
 * - 高电平 (上拉): 中心设备模式
 */
static void detect_device_role(void)
{
#if ROLE_DETECT_TYPE == 0
    // 配置 GPIO 为输入，使能上拉
    gpio_set_die(ROLE_DETECT_GPIO, 1);
    gpio_set_direction(ROLE_DETECT_GPIO, 1);
    gpio_set_pull_up(ROLE_DETECT_GPIO, 1);
    
    // 等待稳定
    os_time_dly(1);
    
    // 读取 GPIO 电平
    if (gpio_read(ROLE_DETECT_GPIO) == 0) {
        g_device_role = DEVICE_ROLE_SLAVE;
    } else {
        g_device_role = DEVICE_ROLE_MASTER;
    }
    
    // 关闭上拉，节省功耗
    gpio_set_pull_up(ROLE_DETECT_GPIO, 0);
#elif ROLE_DETECT_TYPE == 1
    g_device_role = ROLE_CURRENT;
#endif
    
    log_info("Device role detected: %s\n", g_device_role ? "MASTER" : "SLAVE");
}

/**
 * @brief 协议栈初始化前的准备
 */
void bt_ble_before_start_init(void)
{
    log_info("%s\n", __FUNCTION__);
    
    // 根据设备角色配置 GATT 控制块
    if (g_device_role == DEVICE_ROLE_MASTER) {
        // 中心设备: 只需要 Server (广播)
        mul_gatt_control_block.server_config = &mul_server_init_cfg;
        mul_gatt_control_block.client_config = NULL;
        log_info("BLE stack config: MASTER mode (Server only)\n");
    } else {
        // 子设备: 需要 Client (扫描) + Server (广播)
        mul_gatt_control_block.server_config = &mul_server_init_cfg;
        mul_gatt_control_block.client_config = &mul_client_init_cfg;
        log_info("BLE stack config: SLAVE mode (Client + Server)\n");
    }
    
    ble_comm_init(&mul_gatt_control_block);
}

void bt_ble_detect_device_role(void)
{
    // 检测设备角色
    detect_device_role();
}

/**
 * @brief BLE 模块初始化
 */
void bt_ble_init(void)
{
    log_info("%s\n", __FUNCTION__);
    log_info("ble_file: %s\n", __FILE__);
    
    // 设置设备名称
    ble_comm_set_config_name(bt_get_local_name(), 0);
    
    // 根据角色初始化对应模式
    if (g_device_role == DEVICE_ROLE_MASTER) {
        LOG_MASTER("╔════════════════════════╗\n");
        LOG_MASTER("║  MASTER MODE STARTING  ║\n");
        LOG_MASTER("╚════════════════════════╝\n");
        master_mode_init();
    } else {
        LOG_SLAVE("╔═══════════════════════╗\n");
        LOG_SLAVE("║  SLAVE MODE STARTING  ║\n");
        LOG_SLAVE("╚═══════════════════════╝\n");
        slave_mode_init();
    }
    
    // 使能 BLE 模块
    ble_module_enable(1);

    if (g_device_role == DEVICE_ROLE_MASTER)
    {
        // 广播测试
        // uart_rx_test_quick();
        
        ble_gatt_server_module_do_enable(0);
    }
}

/**
 * @brief BLE 模块退出
 */
void bt_ble_exit(void)
{
    log_info("%s\n", __FUNCTION__);
    
    ble_module_enable(0);
    ble_comm_exit();
}

/**
 * @brief BLE 模块使能控制
 * @param en 1: 使能, 0: 禁用
 */
void ble_module_enable(u8 en)
{
    // ble_comm_module_enable(en);
    
    if (STACK_IS_SUPPORT_GATT_SERVER()) {
        ble_gatt_server_module_enable(en);
    }

    if (STACK_IS_SUPPORT_GATT_CLIENT()) {
        ble_gatt_client_module_enable(en);
    }
}
