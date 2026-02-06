# BLE Broadcast Transfer 使用说明

## 项目概述

这是一个基于 AC63_BT_SDK_2.4.0 的 BLE 广播透传方案，支持单一固件通过硬件 GPIO 检测实现双角色（中心设备/子设备）。

### 主要特性

- **单固件双角色**: 通过 IO_PORTB_07 检测设备角色（低电平=子设备，高电平=中心设备）
- **灵活批次 ID**: 支持 6 字节批次 ID，0xFF 为通配符可实现分组通信
- **序列号去重**: 循环缓存 16 个历史序列号，自动检测序列号复位
- **双重校验**: CRC-8（单包）+ CRC-16（整体）确保数据可靠性
- **LED 报警**: 子设备接收超时时 LED 常亮 2 秒
- **实时统计**: 通过调试串口输出接收成功率和错误统计

## 目录结构

```
broadcast_transfer/
├── main.c                      # 主初始化、角色检测、路由
├── master_mode.c               # 中心设备逻辑（UART 接收、分包、广播）
├── slave_mode.c                # 子设备逻辑（扫描、聚合、校验、转发）
└── common/
    ├── protocol.h              # 协议定义、配置宏、日志宏
    └── crc_utils.c             # CRC 计算、批次 ID 匹配
```

## 编译配置

### 1. 修改 Makefile

在项目 Makefile 中添加源文件（参考 `apps/spp_and_le/board/bd19/Makefile`）：

```makefile
# 添加 broadcast_transfer 源文件
OBJS += ../../../../apps/spp_and_le/examples/broadcast_transfer/main.o
OBJS += ../../../../apps/spp_and_le/examples/broadcast_transfer/master_mode.o
OBJS += ../../../../apps/spp_and_le/examples/broadcast_transfer/slave_mode.o
OBJS += ../../../../apps/spp_and_le/examples/broadcast_transfer/common/crc_utils.o

# 添加头文件路径
INCLUDES += -I../../../../apps/spp_and_le/examples/broadcast_transfer
```

### 2. 配置参数调整

所有配置宏都在 `common/protocol.h` 中定义，可根据需要调整：

```c
// 广播时长（每包广播持续时间）
#define ADV_PACKET_DURATION_MS      2000    // 默认 2000ms

// 聚合超时时间
#define REASSEMBLY_TIMEOUT_MS       3000    // 默认 3000ms

// UART 波特率
#define UART_BAUD_RATE              115200  // 默认 115200

// 引脚配置
#define UART_TX_PIN                 IO_PORTA_01
#define UART_RX_PIN                 IO_PORTA_02
#define LED_GPIO_PIN                IO_PORTB_06
#define ROLE_DETECT_GPIO            IO_PORTB_07
```

## 硬件设计

### 角色检测电路

**IO_PORTB_07** 用于检测设备角色：

- **中心设备**: IO_PORTB_07 通过 10K 电阻上拉到 VDD（读取为高电平）
- **子设备**: IO_PORTB_07 接地或留空焊盘（读取为低电平）

```
中心设备 PCB:
    VDD ----[10K]---- IO_PORTB_07

子设备 PCB:
    GND ------------- IO_PORTB_07
```

### LED 指示灯

**IO_PORTB_06** 用于子设备超时报警：

```
IO_PORTB_06 ----[限流电阻]---- LED ---- GND
```

## 协议格式

### 1. MCU → 中心设备 (UART 输入)

**格式定义**:
```
+--------+------------------+-----------+--------+----------+
| Header |    Batch ID      | Data Len  |  Data  | CRC-16   |
| 1 byte |     6 bytes      |  2 bytes  | N bytes| 2 bytes  |
+--------+------------------+-----------+--------+----------+
  0xAA    [可含 0xFF 通配]   Little-End           Little-End
```

**示例 1**: 发送 5 字节数据 `0x11 0x22 0x33 0x44 0x55`，批次 ID 为 `0x01 0x02 0x03 0x04 0x05 0x06`

```
字节索引:  0    1    2    3    4    5    6    7    8    9    10   11   12   13   14   15   16
数据内容:  AA   01   02   03   04   05   06   05   00   11   22   33   44   55   6A   13
说明:     帧头  ----------批次ID-----------  长度    -------数据-------  ----CRC16----
                                           (5=0x0005)                    (小端:0x136A)

CRC-16 计算范围: batch_id(6字节) + data_len(2字节) + data(5字节) = 共13字节
计算数据: 01 02 03 04 05 06 05 00 11 22 33 44 55
CRC-16结果: 0x136A (小端存储为: 6A 13)
```

**示例 2**: 使用通配符批次 ID `0xFF 0xFF 0x03 0x04 0xFF 0xFF`（匹配中间两字节）

```
字节索引:  0    1    2    3    4    5    6    7    8    9    10   11   12
数据内容:  AA   FF   FF   03   04   FF   FF   03   00   A1   B2   C3   xx   xx
说明:     帧头  ----------批次ID-----------  长度    ---数据---  --CRC16--
                    (通配符可匹配多种设备)   (3=0x0003)
```

### 2. BLE 广播包格式 (31 字节)

**格式定义**:
```
+------------------+----------+----------+------------+----------+--------+-------+
|    Batch ID      | Seq Num  | Pkt Idx  | Total Pkts | Data Len |  Data  | CRC-8 |
|     6 bytes      | 2 bytes  | 1 byte   |  1 byte    | 1 byte   |20 bytes| 1 byte|
+------------------+----------+----------+------------+----------+--------+-------+
```

**示例 1**: 第 1 包，序列号 15，总共 2 包，本包 20 字节数据

```
字节索引:  0    1    2    3    4    5    6    7    8    9    10   11   ...  29   30
数据内容:  01   02   03   04   05   06   0F   00   00   02   14   11   ...  55   8C
说明:     ----------批次ID-----------  序列号  包索  总包  长度  -------数据20字节-------  CRC8
                                      (15)    (0)  (2)   (20)                             (0x8C)
         
详细说明:
  - Batch ID: 0x01 0x02 0x03 0x04 0x05 0x06
  - Seq Num:  0x000F (小端) = 15
  - Pkt Idx:  0x00 = 第 0 包（第 1 包）
  - Total:    0x02 = 共 2 包
  - Data Len: 0x14 = 20 字节
  - Data[0-19]: 0x11 0x22 ... 0x55
  - CRC-8:    0x8C
```

**示例 2**: 第 2 包（最后一包），本包 5 字节数据

```
字节索引:  0    1    2    3    4    5    6    7    8    9    10   11   12   13   14   15   ...  29   30
数据内容:  01   02   03   04   05   06   0F   00   01   02   05   56   57   58   59   5A   ...  00   2F
说明:     ----------批次ID-----------  序列号  包索  总包  长度  ----数据5字节----  填充0    ...       CRC8
                                      (15)    (1)  (2)   (5)                       (补齐20B)

详细说明:
  - Pkt Idx:  0x01 = 第 1 包（第 2 包）
  - Data Len: 0x05 = 5 字节（最后一包不满 20 字节）
  - Data[0-4]:  0x56 0x57 0x58 0x59 0x5A
  - Data[5-19]: 0x00 (填充 0，不传输)
  - CRC-8:    0x2F (计算前 30 字节的 CRC)
```

### 3. 子设备 → MCU (UART 输出)

**格式定义**:
```
+--------+-----------+-----------+--------+----------+
| Header | Len Low   | Len High  |  Data  | CRC-16   |
| 1 byte | 1 byte    | 1 byte    | N bytes| 2 bytes  |
+--------+-----------+-----------+--------+----------+
  0xAA                                     Little-End
```

**示例**: 转发 25 字节完整数据

```
字节索引:  0    1    2    3    4    5    ...  27   28   29   30
数据内容:  AA   19   00   11   22   33   ...  99   1A   2B
说明:     帧头  长度低  长度高  ----------数据25字节----------  --CRC16--
                (25)   (0)                                    (小端)

详细说明:
  - Header:   0xAA
  - Len Low:  0x19 = 25 (低字节)
  - Len High: 0x00 = 0  (高字节)
  - Length:   0x0019 = 25 字节
  - Data[0-24]: 0x11 0x22 ... 0x99
  - CRC-16:   0x2B1A (小端存储: 先发 0x1A, 后发 0x2B)
```

**大小端说明**:
- **长度字段**: 小端模式（低字节在前）
  - 例: 长度 300 (0x012C) → 存储为 `0x2C 0x01`
- **CRC-16**: 小端模式（低字节在前）
  - 例: CRC 0xA73C → 存储为 `0x3C 0xA7`
- **序列号**: 小端模式（低字节在前）
  - 例: 序列号 1000 (0x03E8) → 存储为 `0xE8 0x03`

## 批次 ID 灵活匹配

### 通配符规则

- **0xFF** 在批次 ID 任意位置表示该字节跳过校验
- 支持实现多种分组通信场景

### 应用示例

1. **全局广播**: 广播 ID 为 `FF:FF:FF:FF:FF:FF`，所有子设备都能接收

2. **分组广播**: 广播 ID 为 `01:02:FF:FF:FF:FF`，匹配前两字节为 `01:02` 的所有子设备

3. **精确匹配**: 广播 ID 为 `01:02:03:04:05:06`，只有完全匹配的子设备接收

4. **灵活组合**: 子设备 ID 为 `FF:FF:03:04:FF:FF`，可接收中间两字节为 `03:04` 的所有广播

## 子设备批次 ID 配置

### 方法 1: 通过 VM 预写入

生产时通过烧录工具直接写入 VM 区域（索引 0x10）

### 方法 2: UART 命令配置（待实现）

```
发送命令: SET_BATCH_ID:010203040506
```

### 方法 3: 编译时固定

修改 `slave_mode.c` 中的初始值：

```c
static u8 g_local_batch_id[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
```

## 调试日志

### 日志格式

- **中心设备**: `[MASTER_DEV] ...`
- **子设备**: `[SLAVE_DEV] ...`

### 启动日志示例

**中心设备**:
```
Device role detected: MASTER
[MASTER_DEV] ╔════════════════════════╗
[MASTER_DEV] ║  MASTER MODE STARTING  ║
[MASTER_DEV] ╚════════════════════════╝
[MASTER_DEV] Initializing master mode...
[MASTER_DEV] UART opened successfully (baud=115200)
[MASTER_DEV] BLE ADV params configured (type=ADV_NONCONN_IND)
```

**子设备**:
```
Device role detected: SLAVE
[SLAVE_DEV] ╔═══════════════════════╗
[SLAVE_DEV] ║  SLAVE MODE STARTING  ║
[SLAVE_DEV] ╚═══════════════════════╝
[SLAVE_DEV] Initializing slave mode...
[SLAVE_DEV] Local Batch ID: 01:02:03:04:05:06
[SLAVE_DEV] Scan started (interval=50ms, window=50ms)
```

### 运行时日志示例

**中心设备广播**:
```
[MASTER_DEV] RX 50 bytes from UART
[MASTER_DEV] RX MCU frame: ID=01:02:03:04:05:06 len=40
[MASTER_DEV] New broadcast seq=1
[MASTER_DEV] Splitting 40 bytes into 2 packets
[MASTER_DEV] Broadcasting pkt 1/2
[MASTER_DEV] Broadcasting pkt 2/2
[MASTER_DEV] Broadcast completed
```

**子设备接收**:
```
[SLAVE_DEV] ADV received: seq=1 idx=0/2 len=20
[SLAVE_DEV] Packet 1/2 received, mask=0x1
[SLAVE_DEV] ADV received: seq=1 idx=1/2 len=20
[SLAVE_DEV] Packet 2/2 received, mask=0x3
[SLAVE_DEV] Complete! seq=1 40 bytes | Stats: OK=1 TO=0 CRC8=0 CRC16=0 DUP=0 MIS=0
[SLAVE_DEV] UART TX: 40 bytes
```

**超时场景**:
```
[SLAVE_DEV] ADV received: seq=2 idx=0/3 len=20
[SLAVE_DEV] Packet 1/3 received, mask=0x1
[SLAVE_DEV] Timeout! seq=2 mask=0x1/3 pkts
[SLAVE_DEV] LED alarm ON
[SLAVE_DEV] LED alarm OFF
```

## 测试流程

### 1. 编译固件

```bash
cd AC63_BT_SDK_2.4.0
.vscode/winmk.bat ac632n_spp_and_le
```

### 2. 烧录测试

1. 将固件烧录到两个板子
2. 中心设备板子: IO_PORTB_07 上拉
3. 子设备板子: IO_PORTB_07 接地
4. 上电观察串口日志确认角色

### 3. 功能测试

**中心设备测试**:
1. 通过 UART 发送测试帧
2. 使用手机 BLE 扫描工具（如 nRF Connect）验证广播

**子设备测试**:
1. 配置批次 ID
2. 观察接收日志和统计信息
3. 验证 UART 输出数据

**联调测试**:
1. 中心设备发送不同长度数据
2. 子设备验证完整性
3. 测试超时和 LED 报警
4. 多子设备并发接收测试

## 性能参数

- **单包广播时长**: 2000ms（可调）
- **最大载荷**: 256 字节
- **分包大小**: 20 字节/包
- **最大包数**: 16 包
- **扫描间隔**: 50ms
- **聚合超时**: 3000ms
- **序列号缓存**: 16 个

## 注意事项

1. **角色检测**: 仅在上电时检测一次，运行中不可切换
2. **批次 ID**: 子设备必须先配置批次 ID 才能正常接收
3. **调试串口**: 确保调试串口（UART0）与业务 UART 不冲突
4. **内存占用**: 单个固件包含双角色代码，但运行时只激活一个
5. **广播时长**: 根据现场测试调整 `ADV_PACKET_DURATION_MS` 参数

## 故障排查

### 问题: 设备角色识别错误

- 检查 IO_PORTB_07 硬件电路
- 确认上拉电阻或接地连接

### 问题: 子设备无法接收

- 检查批次 ID 是否配置
- 验证批次 ID 是否匹配（含通配符）
- 查看 `stat_id_mismatch` 统计

### 问题: 接收频繁超时

- 增加 `ADV_PACKET_DURATION_MS`
- 检查信号强度（RSSI）
- 减少子设备数量测试

### 问题: CRC 错误

- 检查 MCU 发送的数据格式
- 验证 CRC 计算算法一致性

## 后续优化

1. 实现子设备 UART 命令配置批次 ID
2. 添加固件版本信息和查询命令
3. 支持动态调整广播时长（通过 UART 命令）
4. 添加更详细的错误码和诊断信息
5. 优化功耗（子设备周期扫描模式）

---

**开发团队**: Broadcast Transfer Team  
**日期**: 2025-12-09  
**SDK 版本**: AC63_BT_SDK_2.4.0
