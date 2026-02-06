/*********************************************************************************************
 *   Filename        : master_mode.c
 *   Description     : Master Device Mode (Remote Controller)
 *   Author          : Broadcast Transfer Team
 *   Date            : 2025-12-09
 *********************************************************************************************/

#include "system/includes.h"
#include "asm/uart_dev.h"
#include "btstack/le/ble_api.h"
#include "common/protocol.h"
#include "le_common.h"
#include "gatt_common/le_gatt_common.h"
#include "../multi_conn/ble_multi_profile.h"

#define LOG_TAG             "[U_COMM]"
#include "debug.h"

//================================ 全局变量 ================================//

// UART 总线句柄
static const uart_bus_t *g_uart_bus = NULL;

// UART 接收缓冲区
// static u8 g_uart_rx_buffer[UART_RX_BUF_SIZE] __attribute__((aligned(4)));

// 全局序列号 (每次广播递增)
static u16 g_sequence_num = 0;

// 广播状态
static enum {
    BCAST_IDLE,
    BCAST_ACTIVE
} g_broadcast_state = BCAST_IDLE;

// 广播数据包缓存
static struct ble_adv_packet g_broadcast_packets[16];
static u8 g_broadcast_packet_valid_len = 0;
static u8 g_total_packets = 0;
static u8 g_current_packet_idx = 0;
static u16 g_broadcast_packet_send_timeout_id = 0;
static adv_cfg_t multi_server_adv_config;
static u8 multi_adv_data[ADV_RSP_PACKET_MAX];//max is 31
static u8 multi_scan_rsp_data[ADV_RSP_PACKET_MAX];//max is 31
static u8 rx_buffer[UART_RX_BUF_SIZE] __attribute__((aligned(4)));
static u16 user_adv_interval = ADV_PACKET_DURATION_MS;

#define ADV_INTERVAL_MIN            ADV_SCAN_MS(20)

//================================ 前向声明 ================================//

static void uart_rx_task(void *arg);
static void parse_mcu_frame(const u8 *data, u16 len);
static void parse_config_frame(const u8 *data, u16 len);
static void split_data_to_packets(const u8 batch_id[6], u16 seq_num, const u8 *data, u16 data_len);
static void start_broadcast(void);
static void broadcast_timer_callback(void *priv);
static void stop_broadcast(void);

//================================ 函数实现 ================================//

static int multi_make_set_adv_data(void)
{
    u8 offset = 0;
    u8 *buf = multi_adv_data;

    if (g_broadcast_packet_valid_len > 0)
    {
        memset(buf, 0, 31);
        memcpy(buf, &g_broadcast_packets[g_current_packet_idx], g_broadcast_packet_valid_len);
        offset = 31;
    }
    else
    {
        offset += make_eir_packet_val(&buf[offset], offset, HCI_EIR_DATATYPE_FLAGS, FLAGS_GENERAL_DISCOVERABLE_MODE | FLAGS_EDR_NOT_SUPPORTED, 1);
        offset += make_eir_packet_val(&buf[offset], offset, HCI_EIR_DATATYPE_COMPLETE_16BIT_SERVICE_UUIDS, 0xFFF0, 2);
    }
    
#if 0
#define USE_CLIENT_ID               'Z', 'D', 0x00, 0x01, 0x00, 0xFF, 0xFF, 0xFE
    //客户机型数据
    const u8 get_len_table[] = { USE_CLIENT_ID };
    u8 info[14] = { USE_CLIENT_ID };
    
    le_controller_get_mac(&info[sizeof(get_len_table)]);    //获取ble的蓝牙public地址

    offset += make_eir_packet_data(&buf[offset], offset, HCI_EIR_DATATYPE_MANUFACTURER_SPECIFIC_DATA, info, sizeof(get_len_table) + 6);
#endif

    if (offset > ADV_RSP_PACKET_MAX) {
        puts("***multi_adv_data overflow!!!!!!\n");
        return -1;
    }
    log_info("multi_adv_data(%d):", offset);
    log_info_hexdump(buf, offset);
    multi_server_adv_config.adv_data_len = offset;
    multi_server_adv_config.adv_data = multi_adv_data;
    return 0;
}

//生成rsp包数据，写入buff
static int multi_make_set_rsp_data(void)
{
    u8 offset = 0;
    u8 *buf = multi_scan_rsp_data;

#if 0
    {
        char *gap_name = ble_comm_get_gap_name();
        u8 name_len = strlen(gap_name);
        u8 vaild_len = ADV_RSP_PACKET_MAX - (offset + 2);
        if (name_len > vaild_len) {
            name_len = vaild_len;
        }
        offset += make_eir_packet_data(&buf[offset], offset, HCI_EIR_DATATYPE_COMPLETE_LOCAL_NAME, (void *)gap_name, name_len);
    }
#endif

    if (offset > ADV_RSP_PACKET_MAX) {
        puts("***rsp_data overflow!!!!!!\n");
        return -1;
    }

    log_info("rsp_data(%d):", offset);
    log_info_hexdump(buf, offset);
    multi_server_adv_config.rsp_data_len = offset;
    multi_server_adv_config.rsp_data = multi_scan_rsp_data;
    return 0;
}

static void multi_adv_config_set(void)
{
    int ret = 0;
    ret |= multi_make_set_adv_data();
    ret |= multi_make_set_rsp_data();

    multi_server_adv_config.adv_interval = ADV_INTERVAL_MIN;
    multi_server_adv_config.adv_auto_do = 1;
    multi_server_adv_config.adv_channel = ADV_CHANNEL_ALL;

    multi_server_adv_config.adv_type = ADV_NONCONN_IND;

    if (ret) {
        log_info("adv_setup_init fail!!!\n");
        return;
    }
    ble_gatt_server_set_adv_config(&multi_server_adv_config);
}

/**
 * @brief 中心设备模式初始化
 */
void master_mode_init(void)
{
    LOG_MASTER("Initializing master mode...\n");
    
    // 配置 UART
    struct uart_platform_data_t uart_config = {
        .tx_pin = UART_TX_PIN,
        .rx_pin = UART_RX_PIN,
        .rx_cbuf = uart_cbuf,
        .rx_cbuf_size = UART_RX_BUF_SIZE,
        .frame_length = 0xFFFFFFFF,  // 不使用帧长度中断
        .rx_timeout = 10,             // 10ms 超时
        .isr_cbfun = NULL,            // 轮询模式
        .baud = UART_BAUD_RATE,
        .is_9bit = 0,
        .argv = NULL,
    };
    
    g_uart_bus = uart_dev_open(&uart_config);
    
    if (g_uart_bus != NULL) {
        LOG_MASTER("UART opened successfully (baud=%d)\n", UART_BAUD_RATE);
        
        // 创建 UART 接收任务
        int ret = os_task_create(uart_rx_task, (void *)g_uart_bus, 31, 512, 0, "master_uart");
        if (ret == OS_NO_ERR) {
            LOG_MASTER("UART RX task created\n");
        } else {
            LOG_MASTER("Failed to create UART RX task\n");
        }
    } else {
        LOG_MASTER("Failed to open UART\n");
    }
    
    ble_gatt_server_set_profile(multi_profile_data, sizeof(multi_profile_data));
    multi_adv_config_set();
    // g_broadcast_state = BCAST_ACTIVE;

    // 配置 BLE 广播参数
    LOG_MASTER("BLE ADV params configured (type=ADV_NONCONN_IND)\n");
    
    LOG_MASTER("Master mode initialized successfully\n");
}

/**
 * @brief 测试函数 - 生成并解析 MCU 帧
 * @param batch_id 批次 ID (6字节)
 * @param data 测试数据
 * @param data_len 数据长度
 * 
 * 使用示例:
 *   u8 id1[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
 *   u8 data1[] = {0x11, 0x22, 0x33, 0x44, 0x55};
 *   uart_rx_test_app(id1, data1, 5);
 * 
 *   // 使用通配符 ID
 *   u8 id2[6] = {0xFF, 0xFF, 0x03, 0x04, 0xFF, 0xFF};
 *   u8 data2[] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x11, 0x22};
 *   uart_rx_test_app(id2, data2, 8);
 */
void uart_rx_test_app(const u8 batch_id[6], const u8 *data, u16 data_len)
{
    // 构造完整的 MCU 帧
    // 帧格式: Header(1) + BatchID(6) + Len(2) + Data(N) + CRC16(2)
    u16 frame_len = 1 + 6 + 2 + data_len + 2;
    static u8 frame_temp[256];
    u8 *frame = frame_temp;
    
    // 填充帧头
    frame[0] = FRAME_HEADER;  // 0xAA
    
    // 填充批次 ID
    memcpy(&frame[1], batch_id, 6);
    
    // 填充数据长度 (小端)
    frame[7] = data_len & 0xFF;         // 低字节
    frame[8] = (data_len >> 8) & 0xFF;  // 高字节
    
    // 填充数据
    memcpy(&frame[9], data, data_len);
    
    // 计算 CRC-16
    // 计算范围: batch_id(6) + len(2) + data(N)
    u16 crc = crc16_calc(&frame[1], 6 + 2 + data_len);
    
    // 填充 CRC-16 (小端)
    frame[9 + data_len] = crc & 0xFF;         // 低字节
    frame[9 + data_len + 1] = (crc >> 8) & 0xFF;  // 高字节
    
    // 打印测试帧
    LOG_MASTER("========== Test Frame ==========\n");
    LOG_MASTER("Batch ID: %02X:%02X:%02X:%02X:%02X:%02X\n",
              batch_id[0], batch_id[1], batch_id[2], 
              batch_id[3], batch_id[4], batch_id[5]);
    LOG_MASTER("Data Len: %d bytes\n", data_len);
    LOG_MASTER("CRC-16: 0x%04X (bytes: 0x%02X 0x%02X)\n", 
              crc, frame[9 + data_len], frame[9 + data_len + 1]);
    LOG_MASTER("Complete Frame (%d bytes):\n", frame_len);
    log_info_hexdump(frame, frame_len);
    LOG_MASTER("================================\n");
    
    // 解析帧
    parse_mcu_frame(frame, frame_len);
}

static void uart_rx_test_mutli_packet_timer_isr(void *priv)
{
    u8 id3[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    u8 data3[40];
    for (int i = 0; i < 40; i++) {
        data3[i] = i + 1;
    }
    uart_rx_test_app(id3, data3, sizeof(data3));
    sys_timeout_add(NULL, uart_rx_test_mutli_packet_timer_isr, user_adv_interval * 3 + 50);
}

/**
 * @brief 快速测试函数 - 使用预设数据
 */
void uart_rx_test_quick(void)
{
#if 0
    // 测试1: 5字节数据
    LOG_MASTER("\n>>> Test 1: 5 bytes data\n");
    u8 id1[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    u8 data1[] = {0x11, 0x22, 0x33, 0x44, 0x55};
    uart_rx_test_app(id1, data1, sizeof(data1));
    
    // 测试2: 使用通配符 ID, 8字节数据
    LOG_MASTER("\n>>> Test 2: Wildcard ID, 8 bytes data\n");
    u8 id2[6] = {0xFF, 0xFF, 0x03, 0x04, 0xFF, 0xFF};
    u8 data2[] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x11, 0x22};
    uart_rx_test_app(id2, data2, sizeof(data2));
    
    // 测试3: 40字节数据 (需要2个广播包)
    LOG_MASTER("\n>>> Test 3: 40 bytes data (2 packets)\n");
    u8 id3[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    u8 data3[40];
    for (int i = 0; i < 40; i++) {
        data3[i] = i + 1;
    }
    uart_rx_test_app(id3, data3, sizeof(data3));
#endif
    
    sys_timeout_add(NULL, uart_rx_test_mutli_packet_timer_isr, user_adv_interval * 3 + 50);
}

/**
 * @brief UART 接收任务
 */
static void uart_rx_task(void *arg)
{
    const uart_bus_t *uart_bus = (const uart_bus_t *)arg;
    u32 rx_count = 0;
    
    LOG_MASTER("UART RX task started\n");
    
    while (1) {
        // 阻塞式读取 UART 数据
        rx_count = uart_bus->read(rx_buffer, sizeof(rx_buffer), 0);
        
        if (rx_count > 0) {
            LOG_MASTER("RX %d bytes from UART\n", rx_count);
            
            // 解析帧（支持0xAA数据帧和0xBB配置帧）
            parse_mcu_frame(rx_buffer, rx_count);
            parse_config_frame(rx_buffer, rx_count);
        }
        
        // 喂狗
        clr_wdt();
    }
}

/**
 * @brief 解析 MCU 帧
 */
static void parse_mcu_frame(const u8 *data, u16 len)
{
    // 查找帧头 0xAA
    for (u16 i = 0; i < len; i++) {
        if (data[i] == FRAME_HEADER && (i + 9) <= len) {  // 最小帧长度: 1+6+2=9 字节 (不含数据和CRC)
            const u8 *frame_start = &data[i];
            
            // 提取批次 ID
            u8 batch_id[6];
            memcpy(batch_id, frame_start + 1, 6);
            
            // 提取数据长度
            u16 data_len = (frame_start[8] << 8) | frame_start[7];
            
            // 检查长度是否有效
            if ((i + 9 + data_len + 2) > len) {
                LOG_MASTER("Invalid frame length: %d\n", data_len);
                continue;
            }
            
            const u8 *payload = frame_start + 9;
            const u8 *crc_ptr = payload + data_len;
            u16 received_crc = (crc_ptr[1] << 8) | crc_ptr[0];
            
            // 计算 CRC-16 (从 batch_id 到 payload 结束)
            u16 calculated_crc = crc16_calc(frame_start + 1, 6 + 2 + data_len);
            
            if (received_crc == calculated_crc) {
                LOG_MASTER("RX MCU frame: ID=%02X:%02X:%02X:%02X:%02X:%02X len=%d\n",
                          batch_id[0], batch_id[1], batch_id[2], 
                          batch_id[3], batch_id[4], batch_id[5], data_len);
                
                // 递增序列号
                g_sequence_num++;
                LOG_MASTER("New broadcast seq=%d\n", g_sequence_num);
                
                // 分包
                split_data_to_packets(batch_id, g_sequence_num, payload, data_len);
                
                // 启动广播
                start_broadcast();
            } else {
                LOG_MASTER("CRC16 mismatch: rx=0x%04X calc=0x%04X\n", received_crc, calculated_crc);
            }
            
            // 跳过已处理的帧
            i += 9 + data_len + 2 - 1;
        }
    }
}

/**
 * @brief 解析配置帧（0xBB开头）
 */
static void parse_config_frame(const u8 *data, u16 len)
{
    // 查找配置帧头 0xBB
    for (u16 i = 0; i < len; i++) {
        if (data[i] == CONFIG_FRAME_HEADER && (i + 5) <= len) {  // 最小帧长度: 1+2+0+2=5字节
            const u8 *frame_start = &data[i];
            
            // 提取数据长度
            u16 data_len = (frame_start[2] << 8) | frame_start[1];
            
            // 检查长度是否有效
            if (data_len > MAX_CONFIG_DATA_LEN || (i + 3 + data_len + 2) > len) {
                LOG_MASTER("Invalid config frame length: %d\n", data_len);
                continue;
            }
            
            const u8 *payload = frame_start + 3;
            const u8 *crc_ptr = payload + data_len;
            u16 received_crc = (crc_ptr[1] << 8) | crc_ptr[0];
            
            // 计算 CRC-16 (len + data)
            u16 calculated_crc = crc16_calc(frame_start + 1, 2 + data_len);
            
            if (received_crc == calculated_crc) {
                LOG_MASTER("RX Config frame: len=%d\n", data_len);
                
                if (data_len < 1) {
                    LOG_MASTER("Config frame too short\n");
                    continue;
                }
                
                u8 cmd = payload[0];
                
                switch (cmd) {
                    case CMD_SET_ADV_INTERVAL:
                        if (data_len >= 3) {  // cmd(1) + interval_ms(2)
                            u16 new_interval = (payload[2] << 8) | payload[1];
                            if (new_interval >= 20 && new_interval <= 2000) {
                                // 更新广播间隔
                                LOG_MASTER("ADV interval update requested: %d ms\n", new_interval);
                                user_adv_interval = new_interval;
                            } else {
                                LOG_MASTER("Invalid ADV interval: %d ms (range: 20-2000)\n", new_interval);
                            }
                        } else {
                            LOG_MASTER("Invalid ADV interval length\n");
                        }
                        break;
                        
                    default:
                        LOG_MASTER("Unknown config cmd: 0x%02X\n", cmd);
                        break;
                }
            } else {
                LOG_MASTER("Config CRC16 mismatch: rx=0x%04X calc=0x%04X\n", received_crc, calculated_crc);
            }
            
            // 跳过已处理的帧
            i += 3 + data_len + 2 - 1;
        }
    }
}

/**
 * @brief 将数据分包
 */
static void split_data_to_packets(const u8 batch_id[6], u16 seq_num, const u8 *data, u16 data_len)
{
    // 计算需要的包数 (每包最多 19 字节)
    g_total_packets = (data_len + 18) / 19;
    
    if (g_total_packets > 16) {
        LOG_MASTER("Data too large: %d bytes -> %d packets (max 16)\n", data_len, g_total_packets);
        g_total_packets = 16;
    }
    
    LOG_MASTER("Splitting %d bytes into %d packets\n", data_len, g_total_packets);
    
    // 分包
    for (u8 i = 0; i < g_total_packets; i++) {
        struct ble_adv_packet *pkt = &g_broadcast_packets[i];
        
        // 复制批次 ID
        memcpy(pkt->batch_id, batch_id, 6);
        
        // 设置序列号
        pkt->seq_num = seq_num;
        
        // 设置包索引和总数
        pkt->pkt_idx = i;
        pkt->total_pkts = g_total_packets;
        
        // 计算本包数据长度
        u16 offset = i * 19;
        u16 remaining = data_len - offset;
        pkt->data_len = (remaining > 19) ? 19 : remaining;
        
        // 复制数据
        memcpy(pkt->data, data + offset, pkt->data_len);
        
        // 填充剩余字节为 0
        if (pkt->data_len < 19) {
            memset(pkt->data + pkt->data_len, 0, 19 - pkt->data_len);
        }
        
        // 计算 CRC-8 (batch_id + seq_num + pkt_idx + total_pkts + data_len + data)
        pkt->crc8 = user_crc8_calc((const u8 *)pkt, 30);  // 31 字节 - 1 字节 CRC
        
        LOG_MASTER("Packet %d: len=%d crc8=0x%02X\n", i, pkt->data_len, pkt->crc8);
    }
}

/**
 * @brief 启动广播
 */
static void start_broadcast(void)
{
    if (g_broadcast_state == BCAST_ACTIVE) {
        LOG_MASTER("Broadcast already active, stopping previous\n");
        stop_broadcast();
    }
    
    g_broadcast_state = BCAST_ACTIVE;
    g_current_packet_idx = 0;
    if (g_broadcast_packet_send_timeout_id) {
        sys_timeout_del(g_broadcast_packet_send_timeout_id);
        g_broadcast_packet_send_timeout_id = 0;
    }

    LOG_MASTER("Starting broadcast: %d packets, %d ms per packet\n", 
              g_total_packets, user_adv_interval);
    
    // 发送第一个包
    broadcast_timer_callback(NULL);
}

/**
 * @brief 广播定时器回调
 */
static void broadcast_timer_callback(void *priv)
{
    g_broadcast_packet_send_timeout_id = 0;

    if (g_broadcast_state != BCAST_ACTIVE) {
        return;
    }
    
    if (g_current_packet_idx < g_total_packets) {
        // 改变广播数据需要先关闭广播
        ble_gatt_server_module_do_enable(0);
        // 更新广播数据
        // ble_op_set_adv_data(31, (u8 *)&g_broadcast_packets[g_current_packet_idx]);
        g_broadcast_packet_valid_len = 31;
        multi_adv_config_set();
        g_broadcast_packet_valid_len = 0;
        
        // 启用广播
        ble_gatt_server_module_do_enable(1);
        
        LOG_MASTER("Broadcasting pkt %d/%d\n", g_current_packet_idx + 1, g_total_packets);
        
        // 递增索引
        g_current_packet_idx++;
        
        // 设置下一个包的定时器
        g_broadcast_packet_send_timeout_id = sys_timeout_add(NULL, broadcast_timer_callback, user_adv_interval);
    } else {
        // 所有包发送完成
        stop_broadcast();
        LOG_MASTER("Broadcast completed\n");
    }
}

/**
 * @brief 停止广播
 */
static void stop_broadcast(void)
{
    ble_gatt_server_module_do_enable(0);
    g_broadcast_state = BCAST_IDLE;
}
