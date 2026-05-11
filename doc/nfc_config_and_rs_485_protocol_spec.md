# NFC 配置帧与主机上位机上报帧格式说明

## 1. 文档目的

本文档用于说明手机 NFC 配置软件需要生成并写入 ST25 NFC EEPROM 的配置帧格式，以及主机通过 485 接口向上位机发送座椅状态的上报帧格式。

本文档不包含从机 LoRa 上报主机的内部通信格式。

---

## 2. 字节序和结构体对齐约定

### 2.1 字节序

多字节整数统一采用小端格式，即低字节在前，高字节在后。

例如：

```text
uint16_t value = 0x1234
存储顺序：34 12

uint32_t value = 470000000 = 0x1C055300
存储顺序：00 53 05 1C
```

### 2.2 结构体对齐

配置 payload 按照固件中的紧凑结构体布局写入，即字段之间不插入编译器对齐填充字节。

如果手机 NFC 配置软件不是直接使用 C 结构体生成字节流，而是手动拼接字节数组，应严格按照本文档中的偏移表写入。

如果使用 C/C++ 结构体生成配置帧，应确保结构体按 1 字节对齐，例如使用：

```c
#pragma pack(push, 1)
/* payload struct definitions */
#pragma pack(pop)
```

否则 `rf_param_t` 中的 `uint32_t freq` 可能因 4 字节对齐产生填充，导致 payload 长度和字段偏移错误。

---

## 3. NFC 配置帧总格式

NFC 配置帧写入 ST25 NFC EEPROM 起始地址 `0x0000`。

配置帧整体结构如下：

| 偏移    | 长度  | 字段        | 说明                                                 |
| -----:| ---:| --------- | -------------------------------------------------- |
| 0     | 1   | SOF       | 固定帧头，`0xAA`                                        |
| 1     | 4   | unix_time | 配置生成时间戳，小端                                         |
| 5     | 1   | type      | 配置类型：主机或从机                                         |
| 6     | N   | payload   | 主机或从机配置内容                                          |
| 6 + N | 4   | crc32     | 对 `[SOF + unix_time + type + payload]` 计算 CRC32，小端 |

宏定义：

```c
#define FRAME_SOF          0xAA
#define FRAME_MASTER_TYPE  0x01
#define FRAME_SLAVE_TYPE   0x02
```

CRC32 计算范围不包含最后的 CRC32 字段自身。

---

## 4. RF 参数结构 rf_param_t

主机和从机配置 payload 都以 `rf_param_t` 开头。

```c
typedef struct
{
    uint8_t  pwr;
    uint32_t freq;
    uint8_t  sf;
    uint8_t  bw;
    uint8_t  cr;
} rf_param_t;
```

紧凑布局长度：8 字节。

| 相对偏移 | 长度  | 字段   | 说明               |
| ----:| ---:| ---- | ---------------- |
| 0    | 1   | pwr  | LoRa 发射功率配置值     |
| 1    | 4   | freq | LoRa 频率，单位 Hz，小端 |
| 5    | 1   | sf   | LoRa 扩频因子配置值     |
| 6    | 1   | bw   | LoRa 带宽配置值       |
| 7    | 1   | cr   | LoRa 编码率配置值      |

`pwr / sf / bw / cr` 使用固件中已有的枚举或宏定义值，不写入字符串。

---

## 5. 主机配置 payload：master_payload_t

主机配置帧：

```c
type = FRAME_MASTER_TYPE = 0x01
```

主机 payload 结构：

```c
typedef struct
{
    rf_param_t rf;
    uint8_t reserved[RESERVED_LEN];

    uint8_t vehicle_id;

    rs485_port_config_t rs485_1;
    rs485_port_config_t rs485_2;
} master_payload_t;
```

主机 payload 长度：29 字节。

主机完整配置帧长度：

```text
1 + 4 + 1 + 29 + 4 = 39 字节
```

### 5.1 主机 payload 字段表

| payload 偏移 | 长度  | 字段         | 说明          |
| ----------:| ---:| ---------- | ----------- |
| 0          | 8   | rf         | LoRa RF 参数  |
| 8          | 16  | reserved   | 保留段，当前建议填 0 |
| 24         | 1   | vehicle_id | 车辆号         |
| 25         | 2   | rs485_1    | 485_1 配置    |
| 27         | 2   | rs485_2    | 485_2 配置    |

---

## 6. 485 口配置 rs485_port_config_t

两个 485 口使用相同配置结构。

```c
typedef struct
{
    uint8_t baudrate_index;
    uint8_t feature_flags;
} rs485_port_config_t;
```

长度：2 字节。

| 相对偏移 | 长度  | 字段             | 说明            |
| ----:| ---:| -------------- | ------------- |
| 0    | 1   | baudrate_index | 波特率索引         |
| 1    | 1   | feature_flags  | 功能开关 bit mask |

### 6.1 485 功能开关

```c
#define RS485_FEATURE_NONE                0x00u
#define RS485_FEATURE_JSON_OUT            0x01u  /* Output frame to upper computer. */
#define RS485_FEATURE_FORWARD_TO_OTHER    0x02u  /* Forward received data to the other 485 port. */
#define RS485_FEATURE_LOG_OUT             0x04u  /* Optional diagnostic output. */
```

这些功能是独立开关，不是互斥模式。一个 485 口可以同时开启多个功能。

| mask | 功能               | 说明                                      |
| ----:| ---------------- | --------------------------------------- |
| 0x00 | NONE             | 不启用功能                                   |
| 0x01 | JSON_OUT         | 向上位机输出座椅状态帧。当前实际输出为二进制帧，名称沿用 `JSON_OUT` |
| 0x02 | FORWARD_TO_OTHER | 将本 485 口收到的数据转发到另一个 485 口               |
| 0x04 | LOG_OUT          | 输出固件诊断日志                                |

示例：

```text
feature_flags = 0x01：仅向上位机输出座椅状态帧
feature_flags = 0x02：仅转发到另一个 485 口
feature_flags = 0x04：仅输出日志
feature_flags = 0x03：上位机输出 + 转发
feature_flags = 0x06：转发 + 日志
feature_flags = 0x00：无功能
```

---

## 7. 从机配置 payload：slave_payload_t

从机配置帧：

```c
type = FRAME_SLAVE_TYPE = 0x02
```

从机 payload 结构：

```c
typedef struct
{
    rf_param_t rf;
    uint8_t reserved[RESERVED_LEN];

    uint8_t vehicle_id;

    seat_port_config_t seat[2];
    seat_behavior_config_t seat_behavior;
    slave_contend_config_t contend;
} slave_payload_t;
```

从机 payload 长度：42 字节。

从机完整配置帧长度：

```text
1 + 4 + 1 + 42 + 4 = 52 字节
```

### 7.1 从机 payload 字段表

| payload 偏移 | 长度  | 字段            | 说明                |
| ----------:| ---:| ------------- | ----------------- |
| 0          | 8   | rf            | LoRa RF 参数        |
| 8          | 16  | reserved      | 保留段，当前建议填 0       |
| 24         | 1   | vehicle_id    | 车辆号               |
| 25         | 2   | seat[0]       | 座椅端口 0 配置         |
| 27         | 2   | seat[1]       | 座椅端口 1 配置         |
| 29         | 7   | seat_behavior | 座椅状态转移、异常、报警、滤波配置 |
| 36         | 6   | contend       | LoRa 主动上报竞争参数     |

---

## 8. 座椅端口配置 seat_port_config_t

```c
typedef struct
{
    uint8_t enable;     /* 1: alarm and report enabled; 0: no alarm and no report. */
    uint8_t seat_no;    /* Seat number configured by NFC. 0 is invalid/unused. */
} seat_port_config_t;
```

长度：2 字节。

| 相对偏移 | 长度  | 字段      | 说明                 |
| ----:| ---:| ------- | ------------------ |
| 0    | 1   | enable  | 座椅端口使能，0=不使能，非0=使能 |
| 1    | 1   | seat_no | 座椅号，0 表示无效或未使用     |

规则：

1. 座椅不使能时，不报警、不上报。
2. 座椅使能时，进行状态转移维护、异常判断、报警和上报。
3. 正常座椅号从 1 开始。
4. 同一辆车内，座椅号应不重复。
5. 一个从机的两个座椅号不要求连续。
6. 如果某个从机只接一个座椅，另一个座椅端口应配置为 `enable=0, seat_no=0`。

---

## 9. 座椅行为配置 seat_behavior_config_t

```c
typedef struct
{
    uint16_t abnormal_order_mask;  /* bit0 -> order_state 1, bit11 -> order_state 12. */
    uint16_t filter_order_mask;    /* bit0 -> order_state 1, bit11 -> order_state 12. */
    uint8_t  filter_time_s;        /* 0~200 s recommended. */
    uint8_t  alarm_delay_s;        /* 0~255 s. */
    uint8_t  alarm_duration_s;     /* 0: disabled, 255: until next confirmed state. */
} seat_behavior_config_t;
```

长度：7 字节。

| 相对偏移 | payload 偏移 | 长度  | 字段                  | 说明                             |
| ----:| ----------:| ---:| ------------------- | ------------------------------ |
| 0    | 29         | 2   | abnormal_order_mask | 配置 order_state=1~12 是否判定为异常，小端 |
| 2    | 31         | 2   | filter_order_mask   | 配置 order_state=1~12 是否开启滤波，小端  |
| 4    | 33         | 1   | filter_time_s       | 滤波时间，单位秒，建议范围 0~200            |
| 5    | 34         | 1   | alarm_delay_s       | 进入异常后延迟报警时间，单位秒，范围 0~255       |
| 6    | 35         | 1   | alarm_duration_s    | 报警持续时间                         |

### 9.1 abnormal_order_mask

`abnormal_order_mask` 使用低 12 位，对应 `order_state = 1~12`。

```text
bit0  -> order_state 1
bit1  -> order_state 2
...
bit11 -> order_state 12
```

对应 bit 为 0：该状态转移为正常。  
对应 bit 为 1：该状态转移为异常。

示例：

```text
仅 order_state=5 为异常：
abnormal_order_mask = 0x0010
写入字节：10 00

order_state=1,5,6,8 为异常：
abnormal_order_mask = 0x00B1
写入字节：B1 00
```

### 9.2 filter_order_mask

`filter_order_mask` 使用低 12 位，对应 `order_state = 1~12`。

```text
bit0  -> order_state 1
bit1  -> order_state 2
...
bit11 -> order_state 12
```

对应 bit 为 0：该状态转移不启用滤波。  
对应 bit 为 1：该状态转移启用滤波。

启用滤波后，候选状态必须持续达到 `filter_time_s` 后才正式确认。

未通过滤波的跳变：

1. 不产生上报；
2. 不触发报警；
3. 不更新稳定状态。

### 9.3 filter_time_s

状态滤波时间，单位秒。

```text
0：不延迟，状态变化立即生效
1~200：候选状态持续对应秒数后才生效
```

### 9.4 alarm_delay_s

进入异常状态后，延迟多少秒开始报警。

```text
0：进入异常后立即开始报警
1~255：延迟对应秒数后开始报警
```

### 9.5 alarm_duration_s

报警持续时间。

```text
0：不报警
1~254：报警持续对应秒数
255：一直报警，直到进入新的确认状态
```

---

## 10. order_state 定义

每个座位由两个传感器组成：

```text
seat_status：座椅状态，0=无人，1=有人
belt_status：安全带状态，0=打开，1=闭合
```

组合状态使用二进制 `belt_status seat_status` 表示：

| 状态  | 含义   |
| --- | ---- |
| 00  | 无人未扣 |
| 01  | 有人未扣 |
| 10  | 无人已扣 |
| 11  | 有人已扣 |

`order_state` 表示稳定状态之间的转移顺序。

| order_state | 转移       | 说明           |
| -----------:| -------- | ------------ |
| 1           | 00 -> 01 | 人员坐下，安全带仍未扣  |
| 2           | 01 -> 00 | 人员离座，未扣状态离开  |
| 3           | 01 -> 11 | 坐下后扣安全带      |
| 4           | 11 -> 01 | 在座状态下解开安全带   |
| 5           | 11 -> 10 | 人员离座但安全带未解开  |
| 6           | 10 -> 11 | 安全带已扣状态下坐下   |
| 7           | 10 -> 00 | 空座状态下解开安全带   |
| 8           | 00 -> 10 | 空座状态下扣上安全带   |
| 9           | 10 -> 01 | 空座已扣直接变为有人未扣 |
| 10          | 01 -> 10 | 有人未扣直接变为空座已扣 |
| 11          | 00 -> 11 | 空座未扣直接变为有人已扣 |
| 12          | 11 -> 00 | 有人已扣直接变为空座未扣 |

`order_state = 0` 保留。主机向上位机上报时，某个座椅的 `order_state = 0` 可表示该座椅无有效状态转移或未启用。

---

## 11. LoRa 主动上报竞争参数 slave_contend_config_t

```c
typedef struct
{
    uint8_t  slot_ms;               /* Discrete contention slot length. Default 5 ms. */
    uint8_t  contend_slot_count_n;  /* random(N) returns 0~N-1. */
    uint8_t  idle_confirm_ms;       /* Continuous idle time before contention starts. */
    uint16_t ack_timeout_ms;        /* ACK timeout after TX end. */
    uint8_t  max_retry;             /* 255 means retry forever. */
} slave_contend_config_t;
```

长度：6 字节。

| 相对偏移 | payload 偏移 | 长度  | 字段                   | 说明                |
| ----:| ----------:| ---:| -------------------- | ----------------- |
| 0    | 36         | 1   | slot_ms              | 单个竞争时隙长度，单位 ms    |
| 1    | 37         | 1   | contend_slot_count_n | 随机竞争时隙数量 N        |
| 2    | 38         | 1   | idle_confirm_ms      | 连续空闲确认时间，单位 ms    |
| 3    | 39         | 2   | ack_timeout_ms       | ACK 超时时间，单位 ms，小端 |
| 5    | 41         | 1   | max_retry            | 最大重试次数，255 表示一直重试 |

建议初始配置：

```text
slot_ms = 5
contend_slot_count_n = 50
idle_confirm_ms = 10
ack_timeout_ms = 35 或更大
max_retry = 255
```

---

## 12. NFC 配置帧示例说明

### 12.1 主机 payload 排列

主机 payload 的字节排列为：

```text
rf[8]
reserved[16]
vehicle_id
rs485_1.baudrate_index
rs485_1.feature_flags
rs485_2.baudrate_index
rs485_2.feature_flags
```

主机配置帧整体为：

```text
AA
unix_time[4]
01
master_payload[29]
crc32[4]
```

### 12.2 从机 payload 排列

从机 payload 的字节排列为：

```text
rf[8]
reserved[16]
vehicle_id
seat0.enable
seat0.seat_no
seat1.enable
seat1.seat_no
abnormal_order_mask[2]
filter_order_mask[2]
filter_time_s
alarm_delay_s
alarm_duration_s
slot_ms
contend_slot_count_n
idle_confirm_ms
ack_timeout_ms[2]
max_retry
```

从机配置帧整体为：

```text
AA
unix_time[4]
02
slave_payload[42]
crc32[4]
```

---

## 13. 主机向上位机发送的 485 帧格式

主机收到从机上报并确认有效后，会通过配置了 `RS485_FEATURE_JSON_OUT` 的 485 口向上位机发送座椅状态帧。

当前实际发送的是二进制紧凑帧，不是 JSON 字符串。

### 13.1 上位机上报帧格式

| 偏移                 | 长度             | 字段         | 说明                                |
| ------------------:| --------------:| ---------- | --------------------------------- |
| 0                  | 1              | head       | 固定 `0xAA`                         |
| 1                  | 1              | seat_count | 本帧包含的座椅数量                         |
| 2                  | 2 × seat_count | data       | 每个座椅 2 字节：`seat_no + order_state` |
| 2 + 2 × seat_count | 1              | checksum   | 校验和                               |

每个座椅数据项格式：

| 长度  | 字段          | 说明       |
| ---:| ----------- | -------- |
| 1   | seat_no     | 座椅号      |
| 1   | order_state | 座椅状态转移编号 |

checksum 计算方式：

```text
checksum = 从 head 到 data 末尾所有字节累加和的低 8 位
```

即：

```c
checksum = (head + seat_count + 所有 seat_no/order_state 字节) & 0xFF;
```

### 13.2 上位机上报帧示例

上报两个座椅：

```text
seat_no=1, order_state=5
seat_no=2, order_state=8
```

数据帧为：

```text
AA 02 01 05 02 08 BC
```

解释：

| 字节  | 含义                   |
| --- | -------------------- |
| AA  | 帧头                   |
| 02  | 本帧包含 2 个座椅           |
| 01  | 座椅号 1                |
| 05  | 座椅 1 的 order_state=5 |
| 02  | 座椅号 2                |
| 08  | 座椅 2 的 order_state=8 |
| BC  | 校验和                  |

校验和：

```text
0xAA + 0x02 + 0x01 + 0x05 + 0x02 + 0x08 = 0xBC
```

### 13.3 单座椅上报示例

只上报一个座椅：

```text
seat_no=3, order_state=1
```

数据帧为：

```text
AA 01 03 01 AF
```

校验和：

```text
0xAA + 0x01 + 0x03 + 0x01 = 0xAF
```

---

## 14. 手机 NFC 配置软件注意事项

1. 必须从地址 `0x0000` 写入完整配置帧。
2. 主机帧长度固定为 39 字节。
3. 从机帧长度固定为 52 字节。
4. `unix_time` 应在每次生成配置时更新，固件会优先采用更新时间较新的有效配置。
5. `reserved[16]` 保留，当前建议全部填 0。
6. CRC32 必须对 `[SOF + unix_time + type + payload]` 计算，不包含 CRC32 字段自身。
7. 主机和从机通过 `type` 区分，手机软件应根据用户选择生成对应 payload。
8. 座椅未启用时，建议 `enable=0, seat_no=0`。
9. 如果只安装一个座椅端口，另一个座椅端口应配置为未启用。
10. 所有 `uint16_t` 和 `uint32_t` 字段均使用小端写入。
11. `abnormal_order_mask` 和 `filter_order_mask` 只使用低 12 位，高 4 位建议填 0。
12. 485 口功能为 bit mask，可组合配置，不是单选模式。
13. 生成字节流时必须按照紧凑布局，不要包含结构体对齐填充字节。
