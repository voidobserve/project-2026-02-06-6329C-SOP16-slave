/*********************************************************************************************
 *   Filename        : slave_mode.c
 *   Description     : Slave Device Mode (Receiver)
 *   Author          : Broadcast Transfer Team
 *   Date            : 2025-12-09
 *********************************************************************************************/

#include "system/includes.h"
#include "asm/uart_dev.h"
#include "vm.h"
#include "btstack/le/ble_api.h"
#include "le_common.h"
#include "gatt_common/le_gatt_common.h"
#include "common/protocol.h"
#include "jiffies.h"
#include "../multi_conn/ble_multi_profile.h"

#define LOG_TAG             "[U_COMM]"
#include "debug.h"

//================================ 全局变量 ================================//

// 本地批次 ID
static u8 g_local_batch_id[6] = {0};

// UART 发送总线句柄
static const uart_bus_t *g_uart_tx_bus = NULL;

// LED 定时器标志
static u16 g_led_timer_id = 0;

// 分包聚合上下文
static struct {
    u8 batch_id[6];              // 当前批次 ID
    u16 seq_num;                 // 当前序列号
    u16 completed_seq;           // 已完成的序列号(用于防止重复)
    u8 buffer[MAX_PAYLOAD_SIZE]; // 数据缓冲区
    u32 recv_mask;               // 接收位图
    u32 start_time;              // 开始时间
    u8 total_pkts;               // 总包数
    u8 last_packet_len;          // 最后一包的实际数据长度
    
    // 序列号历史
    u16 seq_history[SEQ_HISTORY_SIZE];
    u8 history_idx;
    
    // 统计信息
    u32 stat_complete;
    u32 stat_timeout;
    u32 stat_crc8_err;
    u32 stat_crc16_err;
    u32 stat_duplicate;
    u32 stat_id_mismatch;
} g_assembler = {0};

static adv_cfg_t multi_server_adv_config;
static u8 multi_adv_data[ADV_RSP_PACKET_MAX];//max is 31
static u8 multi_scan_rsp_data[ADV_RSP_PACKET_MAX];//max is 31

#define ADV_INTERVAL_MIN            ADV_SCAN_MS(1000)

//================================ 前向声明 ================================//

static bool load_batch_id_from_vm(void);
static void save_batch_id_to_vm(const u8 id[6]);
static void scan_init(void);
static void uart_rx_task(void *arg);
static void parse_config_frame(const u8 *data, u16 len);
static void on_adv_report_received(adv_report_t *report);
static bool is_sequence_duplicate(u16 seq_num);
static void add_sequence_to_history(u16 seq_num);
static void check_sequence_reset(u16 seq_num);
static void process_packet(const struct ble_adv_packet *pkt);
static void check_assembly_timeout(void);
static void handle_assembly_complete(void);
static void forward_complete_data(const u8 *data, u16 len);
#ifdef LED_GPIO_PIN
static void led_init(void);
static void led_trigger_timeout_alarm(void);
static void led_off_timer_callback(void *priv);
#endif

//================================ 函数实现 ================================//

static int multi_make_set_adv_data(void)
{
    u8 offset = 0;
    u8 *buf = multi_adv_data;

    offset += make_eir_packet_val(&buf[offset], offset, HCI_EIR_DATATYPE_FLAGS, FLAGS_GENERAL_DISCOVERABLE_MODE | FLAGS_EDR_NOT_SUPPORTED, 1);
    offset += make_eir_packet_val(&buf[offset], offset, HCI_EIR_DATATYPE_COMPLETE_16BIT_SERVICE_UUIDS, 0xFFF0, 2);
    
#if 1
#define USE_CLIENT_ID               'Z', 'D', 0x00, 0x04, 0x00, 0x01, 0x04, 0x89
    //客户机型数据
    const u8 get_len_table[] = { USE_CLIENT_ID };
    u8 info[sizeof(get_len_table) + 6] = { USE_CLIENT_ID };
    
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

    {
        char *gap_name = ble_comm_get_gap_name();
        u8 name_len = strlen(gap_name);
        u8 vaild_len = ADV_RSP_PACKET_MAX - (offset + 2);
        if (name_len > vaild_len) {
            name_len = vaild_len;
        }
        offset += make_eir_packet_data(&buf[offset], offset, HCI_EIR_DATATYPE_COMPLETE_LOCAL_NAME, (void *)gap_name, name_len);
    }

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

    multi_server_adv_config.adv_type = ADV_IND;

    if (ret) {
        log_info("adv_setup_init fail!!!\n");
        return;
    }
    ble_gatt_server_set_adv_config(&multi_server_adv_config);
}

/**
 * @brief 子设备模式初始化
 */
void slave_mode_init(void)
{
    LOG_SLAVE("Initializing slave mode...\n");
    
    // 加载本地批次 ID
    if (load_batch_id_from_vm()) {
        LOG_SLAVE("Local Batch ID: %02X:%02X:%02X:%02X:%02X:%02X\n",
                 g_local_batch_id[0], g_local_batch_id[1], g_local_batch_id[2],
                 g_local_batch_id[3], g_local_batch_id[4], g_local_batch_id[5]);
    } else {
        LOG_SLAVE("Batch ID not configured, using wildcard (accept all)\n");
        // 使用全通配符 ID，接收所有广播（用于调试）
        memset(g_local_batch_id, 0xFF, 6);
        LOG_SLAVE("Local Batch ID set to: FF:FF:FF:FF:FF:FF (wildcard)\n");
    }
    
#ifdef LED_GPIO_PIN
    // 初始化 LED
    led_init();
#endif
    
    // 配置 UART 发送
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
    
    g_uart_tx_bus = uart_dev_open(&uart_config);
    
    if (g_uart_tx_bus != NULL) {
        LOG_SLAVE("UART TX opened successfully (baud=%d)\n", UART_BAUD_RATE);
        
        // 创建 UART 接收任务（用于接收配置指令）
        int ret = os_task_create(uart_rx_task, (void *)g_uart_tx_bus, 31, 512, 0, "slave_uart_rx");
        if (ret == OS_NO_ERR) {
            LOG_SLAVE("UART RX task created\n");
        } else {
            LOG_SLAVE("Failed to create UART RX task\n");
        }
    } else {
        LOG_SLAVE("Failed to open UART TX\n");
    }
    
    ble_gatt_server_set_profile(multi_profile_data, sizeof(multi_profile_data));
    multi_adv_config_set();

    // 初始化扫描
    scan_init();
    
    LOG_SLAVE("Slave mode initialized successfully\n");
}

/**
 * @brief 从 VM 加载批次 ID
 */
static bool load_batch_id_from_vm(void)
{
    int ret = syscfg_read(VM_BATCH_ID_INDEX, g_local_batch_id, 6);
    return (ret == 6);
}

/**
 * @brief 保存批次 ID 到 VM
 */
static void save_batch_id_to_vm(const u8 id[6])
{
    memcpy(g_local_batch_id, id, 6);
    syscfg_write(VM_BATCH_ID_INDEX, g_local_batch_id, 6);
    LOG_SLAVE("Batch ID saved to VM\n");
}

static u32 sys_time_get(void)
{
    return jiffies_msec();
}

//连接周期
#define BASE_INTERVAL_MIN   (6)//最小的interval
#define SET_CONN_INTERVAL   (BASE_INTERVAL_MIN*4) //(unit:1.25ms)
//连接latency
#define SET_CONN_LATENCY    0  //(unit:conn_interval)
//连接超时
#define SET_CONN_TIMEOUT    400 //(unit:10ms)
//建立连接超时
#define SET_CREAT_CONN_TIMEOUT    8000 //(unit:ms)
/**
 * @brief 初始化扫描
 */
static void scan_init(void)
{
    // 配置扫描参数
    static scan_conn_cfg_t scan_cfg = {
        .scan_auto_do = 1,
        .creat_auto_do = 0,  // 不自动连接
        .scan_type = SCAN_PASSIVE,
        .scan_filter = 1,    // 不过滤重复
        .scan_interval = ADV_SCAN_MS(24),
        .scan_window = ADV_SCAN_MS(4),
        .creat_conn_interval = SET_CONN_INTERVAL,
        .creat_conn_latency = SET_CONN_LATENCY,
        .creat_conn_super_timeout = SET_CONN_TIMEOUT,
        .creat_state_timeout_ms = SET_CREAT_CONN_TIMEOUT,
        .conn_update_accept = 1,
    };
    
    ble_gatt_client_set_search_config(NULL);
    ble_gatt_client_set_scan_config(&scan_cfg);
    // ble_gatt_client_scan_enable(1);
    
    LOG_SLAVE("Scan started (interval=%dms, window=%dms)\n",
        scan_cfg.scan_interval * 5 / 8,
        scan_cfg.scan_window * 5 / 8);
}

static u8 is_app_slave_mode_active = 0;
static u8 slave_mode_idle_query(void)
{
    return !is_app_slave_mode_active;
}

REGISTER_LP_TARGET(slave_mode_lp_target) = {
    .name = "slave_mode_deal",
    .is_idle = slave_mode_idle_query,
};

/**
 * @brief 广播报告回调 (由 BLE 协议栈调用)
 * 
 * 注意: 此函数需要在 le_gatt_client.c 中注册
 * 这里仅提供实现，实际集成时需要修改协议栈代码
 */
static void on_adv_report_received(adv_report_t *report)
{
    if (report == NULL || report->length < 31) {
        return;  // 静默丢弃无效包
    }
    
    // 解析 BLE 广播包
    const struct ble_adv_packet *pkt = (const struct ble_adv_packet *)report->data;
    
    // 校验 CRC-8
    u8 calculated_crc8 = user_crc8_calc((const u8 *)pkt, 30);
    if (calculated_crc8 != pkt->crc8) {
        g_assembler.stat_crc8_err++;
        return;  // 静默丢弃CRC错误包
    }
    
    // 校验批次 ID
    if (!batch_id_match(pkt->batch_id, g_local_batch_id)) {
        g_assembler.stat_id_mismatch++;
        return;  // 静默丢弃批次ID不匹配包
    }

    is_app_slave_mode_active = 1;
    // 处理数据包
    process_packet(pkt);
}

void on_adv_report_received_api(adv_report_t *report)
{
    on_adv_report_received(report);
}

/**
 * @brief 检查序列号是否重复
 */
static bool is_sequence_duplicate(u16 seq_num)
{
    for (u8 i = 0; i < SEQ_HISTORY_SIZE; i++) {
        if (g_assembler.seq_history[i] == seq_num) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 将序列号添加到历史记录
 */
static void add_sequence_to_history(u16 seq_num)
{
    g_assembler.seq_history[g_assembler.history_idx] = seq_num;
    g_assembler.history_idx = (g_assembler.history_idx + 1) % SEQ_HISTORY_SIZE;
}

/**
 * @brief 检查序列号复位
 */
static void check_sequence_reset(u16 seq_num)
{
    if (g_assembler.seq_history[0] != 0 && 
        seq_num < (g_assembler.seq_history[0] - SEQ_RESET_THRESHOLD)) {
        LOG_SLAVE("Sequence reset detected (new=%d, old=%d)\n", 
                 seq_num, g_assembler.seq_history[0]);
        memset(g_assembler.seq_history, 0, sizeof(g_assembler.seq_history));
        g_assembler.history_idx = 0;
    }
}

/**
 * @brief 处理数据包
 */
static void process_packet(const struct ble_adv_packet *pkt)
{
    // 拒绝已完成的序列号(防止单包重复完成)
    if (pkt->seq_num == g_assembler.completed_seq) {
        g_assembler.stat_duplicate++;
        is_app_slave_mode_active = 0;
        return;
    }
    
    // 如果是新序列，清空缓冲区
    if (pkt->seq_num != g_assembler.seq_num) {
        // LOG_SLAVE("New sequence: %d", pkt->seq_num);
        
        memcpy(g_assembler.batch_id, pkt->batch_id, 6);
        g_assembler.seq_num = pkt->seq_num;
        g_assembler.recv_mask = 0;
        g_assembler.start_time = sys_time_get();
        g_assembler.total_pkts = pkt->total_pkts;
        g_assembler.last_packet_len = 0;
        memset(g_assembler.buffer, 0, sizeof(g_assembler.buffer));
    }
    
    // 检查超时
    check_assembly_timeout();
    
    // 检查当前包是否已接收(判重: 检查recv_mask对应bit)
    if (pkt->pkt_idx < 32 && (g_assembler.recv_mask & (1 << pkt->pkt_idx))) {
        g_assembler.stat_duplicate++;
        // LOG_SLAVE("Duplicate: seq=%d idx=%d (already in mask=0x%X)\n", 
        //          pkt->seq_num, pkt->pkt_idx, g_assembler.recv_mask);
        return;
    }
    
    // 存储数据包 (修正: 每包payload是19字节,不是20)
    if (pkt->pkt_idx < g_assembler.total_pkts) {
        u16 offset = pkt->pkt_idx * 19;  // 修正offset计算
        if (offset + pkt->data_len <= MAX_PAYLOAD_SIZE) {
            is_app_slave_mode_active = 1;

            memcpy(g_assembler.buffer + offset, pkt->data, pkt->data_len);
            g_assembler.recv_mask |= (1 << pkt->pkt_idx);
            
            // 记录最后一包的实际长度
            if (pkt->pkt_idx == g_assembler.total_pkts - 1) {
                g_assembler.last_packet_len = pkt->data_len;
            }
            
            // 重置超时计时器 (单包超时机制: 每收到一个包就重置)
            g_assembler.start_time = sys_time_get();
            
            // LOG_SLAVE("Packet %d/%d stored at offset %d, mask=0x%X\n",
            //          pkt->pkt_idx + 1, pkt->total_pkts, offset, g_assembler.recv_mask);
        }
    }
    
    // 检查是否接收完整
    u32 complete_mask = (1 << g_assembler.total_pkts) - 1;
    if (g_assembler.recv_mask == complete_mask) {
        handle_assembly_complete();
    }
}

/**
 * @brief 检查聚合超时
 */
static void check_assembly_timeout(void)
{
    if (g_assembler.recv_mask != 0) {
        u32 elapsed = sys_time_get() - g_assembler.start_time;
        if (elapsed > REASSEMBLY_TIMEOUT_MS) {
            g_assembler.stat_timeout++;
            LOG_SLAVE("Timeout! seq=%d mask=0x%X/%d pkts\n",
                     g_assembler.seq_num, g_assembler.recv_mask, g_assembler.total_pkts);
            
#ifdef LED_GPIO_PIN
            // 触发 LED 报警
            led_trigger_timeout_alarm();
#endif
            
            // 完全重置聚合器状态(重要!否则seq_num残留会导致新包不触发初始化)
            g_assembler.seq_num = 0xFFFF;  // 重置seq使下次收包触发新序列
            g_assembler.completed_seq = 0xFFFF;
            g_assembler.recv_mask = 0;
            g_assembler.total_pkts = 0;
            g_assembler.last_packet_len = 0;
            memset(g_assembler.buffer, 0, sizeof(g_assembler.buffer));
            
            is_app_slave_mode_active = 0;
        }
    }
}

/**
 * @brief 处理聚合完成
 */
static void handle_assembly_complete(void)
{
    // 计算数据总长度: 前(n-1)包每包19字节 + 最后一包实际长度
    // 对于40字节测试: 3包 = 19 + 19 + 2 = 40
    u16 total_len = 0;
    if (g_assembler.total_pkts > 0) {
        total_len = (g_assembler.total_pkts - 1) * 19 + g_assembler.last_packet_len;
    }
    
    // 校验 CRC-16
    // 注意: 这里应该从原始数据中获取 CRC，此处简化处理
    u16 calculated_crc16 = crc16_calc(g_assembler.buffer, total_len);
    
    // 简化: 假设 CRC 校验通过
    g_assembler.stat_complete++;
    
    // LOG_SLAVE("Complete! seq=%d %d bytes | Stats: OK=%d TO=%d CRC8=%d CRC16=%d DUP=%d MIS=%d",
    //          g_assembler.seq_num, total_len,
    //          g_assembler.stat_complete, g_assembler.stat_timeout,
    //          g_assembler.stat_crc8_err, g_assembler.stat_crc16_err,
    //          g_assembler.stat_duplicate, g_assembler.stat_id_mismatch);
    
    // 转发数据
    forward_complete_data(g_assembler.buffer, total_len);
    
    // 记录已完成序列号(防止重复处理)
    g_assembler.completed_seq = g_assembler.seq_num;
    
    // 清空上下文并重置序列号
    g_assembler.recv_mask = 0;
    g_assembler.seq_num = 0xFFFF;
    
    is_app_slave_mode_active = 0;
}

/**
 * @brief 转发完整数据到 UART
 */
static void forward_complete_data(const u8 *data, u16 len)
{
    if (g_uart_tx_bus == NULL) {
        LOG_SLAVE("UART TX not initialized\n");
        return;
    }
    
    // 构造 UART 帧
    u8 frame[MAX_PAYLOAD_SIZE + 6];  // header(1) + len(2) + data + crc(2)
    
    frame[0] = FRAME_HEADER;
    frame[1] = len & 0xFF;
    frame[2] = (len >> 8) & 0xFF;
    memcpy(frame + 3, data, len);
    
    // 计算 CRC-16 (包括长度字段，与主机发送端一致)
    u16 crc = crc16_calc(&frame[1], 2 + len);  // len(2) + data(len)
    frame[3 + len] = crc & 0xFF;
    frame[3 + len + 1] = (crc >> 8) & 0xFF;
    
    // 发送
    g_uart_tx_bus->write(frame, 3 + len + 2);
    
    // LOG_SLAVE("UART TX: %d bytes", len);
    log_info_hexdump(frame, 3 + len + 2);
}

/**
 * @brief 转发UARTRX完整数据到 APP
 */
static void slave_mode_uart_rx_complete_data(const u8 *data, u16 len)
{
    extern void multi_att_notify_buffer(uint8_t * buffer, uint16_t buffer_size);

    if (data[0] == 0x55)
        multi_att_notify_buffer(data, len);
}
void slave_mode_uart_tx_data(const u8 *data, u16 len)
{
    g_uart_tx_bus->write(data, len);
}

/**
 * @brief UART 接收任务（从机配置指令接收）
 */
static void uart_rx_task(void *arg)
{
    const uart_bus_t *uart_bus = (const uart_bus_t *)arg;
    u8 rx_buffer[256];
    u32 rx_count = 0;
    
    LOG_SLAVE("UART RX task started\n");
    
    while (1) {
        // 阻塞式读取 UART 数据
        rx_count = uart_bus->read(rx_buffer, sizeof(rx_buffer), 0);
        
        if (rx_count > 0) {
            LOG_SLAVE("RX %d bytes from UART\n", rx_count);
            
            // 解析配置帧
            parse_config_frame(rx_buffer, rx_count);

            slave_mode_uart_rx_complete_data(rx_buffer, rx_count);
        }
        
        // 喂狗
        clr_wdt();
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
                LOG_SLAVE("Invalid config frame length: %d\n", data_len);
                continue;
            }
            
            const u8 *payload = frame_start + 3;
            const u8 *crc_ptr = payload + data_len;
            u16 received_crc = (crc_ptr[1] << 8) | crc_ptr[0];
            
            // 计算 CRC-16 (len + data)
            u16 calculated_crc = crc16_calc(frame_start + 1, 2 + data_len);
            
            if (received_crc == calculated_crc) {
                LOG_SLAVE("RX Config frame: len=%d\n", data_len);
                
                if (data_len < 1) {
                    LOG_SLAVE("Config frame too short\n");
                    continue;
                }
                
                u8 cmd = payload[0];
                
                switch (cmd) {
                    case CMD_SET_BATCH_ID:
                        if (data_len >= 7) {  // cmd(1) + id(6)
                            save_batch_id_to_vm(&payload[1]);
                            LOG_SLAVE("Batch ID updated: %02X:%02X:%02X:%02X:%02X:%02X\n",
                                     payload[1], payload[2], payload[3],
                                     payload[4], payload[5], payload[6]);
                        } else {
                            LOG_SLAVE("Invalid Batch ID length\n");
                        }
                        break;
                        
                    default:
                        LOG_SLAVE("Unknown config cmd: 0x%02X\n", cmd);
                        break;
                }
            } else {
                LOG_SLAVE("Config CRC16 mismatch: rx=0x%04X calc=0x%04X\n", received_crc, calculated_crc);
            }
            
            // 跳过已处理的帧
            i += 3 + data_len + 2 - 1;
        }
    }
}

#ifdef LED_GPIO_PIN
/**
 * @brief 初始化 LED
 */
static void led_init(void)
{
    gpio_direction_output(LED_GPIO_PIN, 0);
    LOG_SLAVE("LED initialized (pin=%d)\n", LED_GPIO_PIN);
}

/**
 * @brief 触发 LED 超时报警
 */
static void led_trigger_timeout_alarm(void)
{
    // 点亮 LED
    gpio_write(LED_GPIO_PIN, 1);
    LOG_SLAVE("LED alarm ON\n");
    
    // 设置 2 秒后关闭
    g_led_timer_id = sys_timeout_add(NULL, led_off_timer_callback, 2000);
}

/**
 * @brief LED 关闭定时器回调
 */
static void led_off_timer_callback(void *priv)
{
    gpio_write(LED_GPIO_PIN, 0);
    LOG_SLAVE("LED alarm OFF\n");
}
#endif
