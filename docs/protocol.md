# EFilterWheel 串口控制协议 v1.0

## 概述

EFilterWheel 使用基于换行 (`\n`) 分隔的 ASCII 文本协议进行串口通信。

- **物理层**: UART, 57600 baud, 8N1
- **命令格式**: `CMD [args]\n`
- **响应格式**: `OK [data]\n` 或 `ERR <code> <message>\n`
- **编码**: UTF-8

## 设备状态

| 状态 | 含义 | 可接受命令 |
|------|------|-----------|
| `BOOT` | 启动中 | 无 |
| `HOMING` | 回零中 | `STATE?`, `POS?`, `ID?`, `STOP` |
| `READY` | 就绪 | 全部命令 |
| `MOVING` | 移动中 | `STATE?`, `POS?`, `ID?`, `STOP` |
| `ERROR` | 错误 | `STATE?`, `ID?`, `RESET` |

## 命令参考

### ID? — 查询设备信息

```
> ID?
< OK EFilterWheel-Nano FW:1.0.0 PROTO:1.0
```

### STATE? — 查询设备状态

```
> STATE?
< OK READY ERR:0
```

错误码为 0 表示无错误，其他值见错误码表。

### POS? — 查询当前槽位

```
> POS?
< OK 2
```

- `0..N-1`: 当前槽位号（从 0 开始）
- `-1`: 移动中
- `-2`: 未回零

### HOME — 启动回零

```
> HOME
< OK HOMING started
  ... (等待中)
< OK HOMED
```

回零过程：
1. 快速搜索霍尔传感器
2. 首次触发后退出传感区
3. 低速二次精调接近

回零超时默认 60 秒。

### GOTO n — 移动到槽位 n

```
> GOTO 3
< OK GOTO 3
```

- 槽位从 0 开始编号
- 自动选择最短路径
- 移动超时默认 30 秒

### STOP — 紧急停止

```
> STOP
< OK STOPPED
```

停止后设备进入 `ERROR` 状态，需要重新回零。

### SLOTS 5|7 — 设置滤镜盘槽位数

```
> SLOTS 5
< OK SLOTS=5
```

### CAL — 标定参数管理

查询当前标定：

```
> CAL?
< OK SLOTS:7 SPEED:800 DIR:CW INVERT:0 S0:0 S1:512 S2:1024 ...
```

设置槽位步数：

```
> CAL SLOT 1 500
< OK CAL SLOT 1 = 500
```

设置速度：

```
> CAL SPEED 1000
< OK CAL SPEED=1000
```

设置方向：

```
> CAL DIR CW
< OK CAL DIR=CW
```

设置原点偏移：

```
> CAL OFFSET 10
< OK CAL OFFSET=10
```

### SAVE — 保存配置到 EEPROM

```
> SAVE
< OK SAVED
```

### RESET — 重启设备

```
> RESET
< OK RESET - Rebooting...
```

### HELP — 显示命令帮助

```
> HELP
< OK Commands:
<   ID?        - Device info
<   STATE?     - Device state
  ...
< OK End of help
```

## 错误码

| 码 | 名称 | 说明 |
|----|------|------|
| 0 | ERR_NONE | 无错误 |
| 1 | ERR_UNKNOWN_CMD | 未知命令 |
| 2 | ERR_BAD_ARG | 参数错误 |
| 3 | ERR_NOT_READY | 设备未就绪 |
| 4 | ERR_BUSY | 设备忙 |
| 5 | ERR_HOMING_FAILED | 回零失败 |
| 6 | ERR_TIMEOUT | 移动超时 |
| 7 | ERR_SENSOR_FAULT | 传感器故障 |
| 8 | ERR_EEPROM_FAIL | EEPROM 故障 |
| 9 | ERR_INVALID_CONFIG | 无效配置 |
| 10 | ERR_MOTOR_FAULT | 电机故障 |
| 11 | ERR_STOPPED | 已停止 |
| 12 | ERR_OUT_OF_RANGE | 槽位超出范围 |
| 99 | ERR_INTERNAL | 内部错误 |

## ASCOM 兼容性

对于 Arduino Nano + USB 串口连接，可以通过 ASCOM 串口驱动或 Alpaca 桥接程序连接到 N.I.N.A.。

对于 ESP8266 版本，设备直接提供 ASCOM Alpaca FilterWheel HTTP API：

```
GET  /api/v1/filterwheel/0/position
PUT  /api/v1/filterwheel/0/position  (body: slot number)
GET  /api/v1/filterwheel/0/names
GET  /api/v1/filterwheel/0/focusoffsets
GET  /api/v1/filterwheel/0/connected
GET  /api/v1/filterwheel/0/interfaceversion
GET  /api/v1/filterwheel/0/driverinfo
```

Alpaca 设备发现通过 mDNS (`efilterwheel.local`) 实现。
