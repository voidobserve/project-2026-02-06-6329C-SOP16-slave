/*********************************************************************************************
 *   Filename        : protocol.h
 *   Description     : BLE Broadcast Transfer Protocol Definitions
 *   Author          : Broadcast Transfer Team
 *   Date            : 2025-12-09
 *********************************************************************************************/

#ifndef __BROADCAST_TRANSFER_PROTOCOL_H__
#define __BROADCAST_TRANSFER_PROTOCOL_H__

#include "system/includes.h"
#include "asm/gpio.h"

//================================ 配置宏定义 ================================//

// VM 存储索引
#define VM_BATCH_ID_INDEX           0x10    // 批次 ID 在 VM 中的索引

// 序列号管理
#define SEQ_HISTORY_SIZE            16      // 序列号历史缓存大小
#define SEQ_RESET_THRESHOLD         1000    // 序列号复位检测阈值

// 广播配置
#define ADV_PACKET_DURATION_MS      200     // 每包广播持续时间 (ms): 时间要保证多设备接收
#define MAX_PAYLOAD_SIZE            608     // 最大载荷大小 (32包 × 19字节/包 = 608字节)

// 扫描超时配置
#define REASSEMBLY_TIMEOUT_MS       1000    // 单包超时时间 (ms)

// UART 配置
#define UART_BAUD_RATE              9600  // UART 波特率
#define UART_TX_PIN                 IO_PORTB_06  // UART TX 引脚
#define UART_RX_PIN                 IO_PORTB_07  // UART RX 引脚
#define UART_RX_BUF_SIZE            512     // UART 接收缓冲区大小
extern u8 uart_cbuf[UART_RX_BUF_SIZE];
extern u8 uart_rxbuf[UART_RX_BUF_SIZE];

// LED 配置
// #define LED_GPIO_PIN                IO_PORTA_00  // LED 报警引脚

// 角色检测
#define DEVICE_ROLE_MASTER          1       // 中心设备角色值
#define DEVICE_ROLE_SLAVE           0       // 子设备角色值
#define ROLE_DETECT_TYPE            1       // 角色检测类型 (0: GPIO, 1: 宏)
#if ROLE_DETECT_TYPE == 0
#define ROLE_DETECT_GPIO            IO_PORTB_07  // 角色检测引脚
#elif ROLE_DETECT_TYPE == 1
// #define ROLE_CURRENT                DEVICE_ROLE_MASTER
#define ROLE_CURRENT                DEVICE_ROLE_SLAVE
#endif

// 协议帧头
#define FRAME_HEADER                0xAA    // 数据帧起始标志
#define CONFIG_FRAME_HEADER         0xBB    // 配置帧起始标志

// 批次 ID 通配符
#define BATCH_ID_WILDCARD           0xFF    // 批次 ID 中的通配符

// 配置指令类型
enum config_cmd_type {
    CMD_SET_BATCH_ID = 0x01,        // 设置批次ID (从机)
    CMD_SET_ADV_INTERVAL = 0x02,    // 设置广播间隔 (主机)
    CMD_RESERVED = 0xFF             // 预留
};

// 配置数据最大长度
#define MAX_CONFIG_DATA_LEN         32

//================================ 日志宏定义 ================================//

#define LOG_TAG_CONST       U_COMM
#define LOG_v(t)  log_tag_const_v_ ## t
#define LOG_i(t)  log_tag_const_i_ ## t
#define LOG_d(t)  log_tag_const_d_ ## t
#define LOG_w(t)  log_tag_const_w_ ## t
#define LOG_e(t)  log_tag_const_e_ ## t
#define LOG_c(t)  log_tag_const_c_ ## t
#define LOG_tag(tag, n) n(tag)
extern const char LOG_tag(LOG_TAG_CONST,LOG_v) AT(.LOG_TAG_CONST);
extern const char LOG_tag(LOG_TAG_CONST,LOG_i) AT(.LOG_TAG_CONST);
extern const char LOG_tag(LOG_TAG_CONST,LOG_d) AT(.LOG_TAG_CONST);
extern const char LOG_tag(LOG_TAG_CONST,LOG_w) AT(.LOG_TAG_CONST);
extern const char LOG_tag(LOG_TAG_CONST,LOG_e) AT(.LOG_TAG_CONST);
extern const char LOG_tag(LOG_TAG_CONST,LOG_c) AT(.LOG_TAG_CONST);

#define LOG_MASTER(fmt, ...)        log_info("[MASTER_DEV] " fmt, ##__VA_ARGS__)
#define LOG_SLAVE(fmt, ...)         log_info("[SLAVE_DEV] " fmt, ##__VA_ARGS__)

//================================ 协议结构体定义 ================================//

// MCU 输入帧结构 (中心设备从 MCU 接收)
struct mcu_frame {
    u8 header;          // 帧头 0xAA
    u8 batch_id[6];     // 批次 ID (0xFF 为通配符)
    u16 data_len;       // 数据长度
    u8 data[];          // 可变长度数据 + CRC-16 (2 字节)
} __attribute__((packed));

// BLE 广播数据包结构 (总长度 31 字节)
// 字段布局: batch_id(6) + seq_num(2) + pkt_idx(1) + total_pkts(1) + data_len(1) + data(19) + crc8(1) = 31
struct ble_adv_packet {
    u8 batch_id[6];     // 批次 ID (6 字节)
    u16 seq_num;        // 序列号 (2 字节)
    u8 pkt_idx;         // 当前包索引 (1 字节)
    u8 total_pkts;      // 总包数 (1 字节)
    u8 data_len;        // 本包数据长度 (1 字节, 最大19)
    u8 data[19];        // 有效载荷 (19 字节, 非20!)
    u8 crc8;            // CRC-8 校验 (1 字节)
} __attribute__((packed));

// UART 输出帧结构 (子设备向 MCU 发送)
struct uart_tx_frame {
    u8 header;          // 帧头 0xAA
    u8 len_low;         // 长度低字节
    u8 len_high;        // 长度高字节
    u8 data[];          // 可变长度数据 + CRC-16 (2 字节)
} __attribute__((packed));

//================================ 函数声明 ================================//

/**
 * @brief 批次 ID 匹配函数 (支持 0xFF 通配符)
 * @param id1 批次 ID 1
 * @param id2 批次 ID 2
 * @return true 匹配, false 不匹配
 */
bool batch_id_match(const u8 id1[6], const u8 id2[6]);

/**
 * @brief 计算 CRC-8 校验值 (多项式 0x07)
 * @param data 数据指针
 * @param len 数据长度
 * @return CRC-8 校验值
 */
u8 user_crc8_calc(const u8 *data, u16 len);

/**
 * @brief 计算 CRC-16 校验值 (多项式 0x1021)
 * @param data 数据指针
 * @param len 数据长度
 * @return CRC-16 校验值
 */
u16 crc16_calc(const u8 *data, u16 len);

#endif // __BROADCAST_TRANSFER_PROTOCOL_H__
