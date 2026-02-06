/*********************************************************************************************
 *   Filename        : crc_utils.c
 *   Description     : CRC Calculation Utilities
 *   Author          : Broadcast Transfer Team
 *   Date            : 2025-12-09
 *********************************************************************************************/

#include "protocol.h"

/**
 * @brief 批次 ID 匹配函数 (支持 0xFF 通配符)
 * @param id1 批次 ID 1
 * @param id2 批次 ID 2
 * @return true 匹配, false 不匹配
 * 
 * 匹配规则:
 * - 如果任一字节为 0xFF，则该位置跳过校验
 * - 其他位置必须完全相同
 */
bool batch_id_match(const u8 id1[6], const u8 id2[6])
{
    for (int i = 0; i < 6; i++) {
        // 如果任一字节为通配符 0xFF，跳过该字节比较
        if (id1[i] == BATCH_ID_WILDCARD || id2[i] == BATCH_ID_WILDCARD) {
            continue;
        }
        // 如果字节不相同，返回不匹配
        if (id1[i] != id2[i]) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 计算 CRC-8 校验值
 * @param data 数据指针
 * @param len 数据长度
 * @return CRC-8 校验值
 * 
 * 多项式: 0x07 (x^8 + x^2 + x + 1)
 * 初始值: 0x00
 * 输出异或: 0x00
 */
u8 user_crc8_calc(const u8 *data, u16 len)
{
    u8 crc = 0x00;
    
    for (u16 i = 0; i < len; i++) {
        crc ^= data[i];
        for (u8 bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    
    return crc;
}

/**
 * @brief 计算 CRC-16 校验值
 * @param data 数据指针
 * @param len 数据长度
 * @return CRC-16 校验值
 * 
 * 多项式: 0x1021 (CRC-16-CCITT)
 * 初始值: 0xFFFF
 * 输出异或: 0x0000
 */
u16 crc16_calc(const u8 *data, u16 len)
{
    u16 crc = 0xFFFF;
    
    for (u16 i = 0; i < len; i++) {
        crc ^= (u16)data[i] << 8;
        for (u8 bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    
    return crc;
}
