# EFilterWheel — DIY 电动滤镜轮

基于 3D 打印结构的可重复定位电动滤镜轮，支持 Arduino Nano (USB 串口) 和 ESP8266 (Wi-Fi / ASCOM Alpaca) 两种主控配置。

## 功能

- ✅ 7 位 × 36mm 或 5 位 × 2" 滤镜盘
- ✅ 摩擦轮传动，28BYJ-48 步进电机 + ULN2003A 驱动
- ✅ 霍尔传感器原点 + 微动开关独立校验
- ✅ 上电自动回零，断电位置记忆 (EEPROM)
- ✅ 非阻塞运动控制 + 加减速
- ✅ ASCII 串口协议 (57600 baud)
- ✅ ASCOM Alpaca HTTP API (ESP8266)
- ✅ Web 控制页面 (ESP8266)
- ✅ N.I.N.A. 兼容

## 项目结构

```
EFilterWheel/
├── platformio.ini              # PlatformIO 构建配置
├── firmware/
│   ├── common/                 # 共享代码 (HAL, 运动, 滤镜轮, 协议)
│   │   ├── config.h            # 全局配置与引脚映射
│   │   ├── hal.h / hal.cpp     # 硬件抽象层
│   │   ├── motion.h / motion.cpp      # 步进电机控制
│   │   ├── wheel.h / wheel.cpp        # 滤镜轮状态机
│   │   ├── eeprom_config.h / .cpp     # EEPROM 配置存储
│   │   └── transport.h / transport.cpp # 串口/网络协议
│   ├── nano/
│   │   └── main.cpp            # Arduino Nano 入口
│   └── esp8266/
│       └── main.cpp            # ESP8266 入口 (含 Web/Alpaca)
├── tools/
│   └── serial_test.py          # PC 串口测试工具
├── docs/
│   └── protocol.md             # 协议文档
├── plan.md                     # 实施计划
└── README.md
```

## 快速开始

### 硬件要求

- 主控: Arduino Nano (ATmega328P) **或** Wemos D1 mini (ESP8266) —— 二选一
- 电机: 28BYJ-48 5V 减速步进电机
- 驱动: ULN2003A 模块
- 传感器: 霍尔开关 (开集电极输出, 如 A3144) + 微动开关
- 电源: 5V / 1A+ (建议 2A 适配器留余量)
- 蜂鸣器: Nano 可用, ESP8266 不可用 (无安全 GPIO)

### 接线 — Arduino Nano

| 功能 | Nano 引脚 | 连接至 |
|------|----------|--------|
| 电机 IN1 | D4 | ULN2003A IN1 |
| 电机 IN2 | D5 | ULN2003A IN2 |
| 电机 IN3 | D6 | ULN2003A IN3 |
| 电机 IN4 | D7 | ULN2003A IN4 |
| 霍尔传感器 | D2 | 霍尔 OUT (上拉至 5V) |
| 微动开关 | D3 | 开关 NO → D3, COM → GND |
| 蜂鸣器 | D8 | 蜂鸣器 + (仅 Nano) |
| LED | D13 | 板载 |

### 接线 — ESP8266 (Wemos D1 mini)

> ⚠️ **ESP8266 是 3.3V 器件，所有 GPIO 只耐受 3.3V！**
> 好消息：霍尔和微动开关都是"开漏/触点"型，**不需要电平转换芯片**，只需把上拉电阻接到 3.3V 即可。

**为什么不需要电平转换？**

```
Nano (5V) 接法:                     ESP8266 (3.3V) 接法:
                                     ⚠️ 错误:              ✅ 正确:
VCC(5V)                               VCC(5V)               VCC(5V)
  │                                      │                     │
  ├─ 4.7kΩ ──┬── GPIO(D2)               ├─ 4.7kΩ ──┬── GPIO   ├─ 霍尔 VCC(5V)
  │          │                           │          │          │
霍尔 OUT ────┘               →         霍尔 OUT ────┘        霍尔 OUT ────┬── GPIO(D7)
  │                                      │                     │          │
 GND                                    GND                    └─ 4.7kΩ ── 3.3V
                                                               │
霍尔是开集电极输出:                  上拉接 5V → GPIO 承受 5V   上拉接 3.3V → GPIO
OUT 脚只拉低,不输出高电平              ❌ 烧 GPIO!              最多承受 3.3V ✅
```

- **霍尔传感器**：开集电极（Open-Collector）输出。OUT 引脚只负责"拉低到 GND"，**高电平完全由上拉电阻的电压决定**。把上拉接到 3.3V，GPIO 就永远看不到 5V。
- **微动开关**：纯机械触点，一端接 GPIO，一端接 GND。用 3.3V 上拉，按下时读到 LOW，松开时读到 3.3V。

**实际操作：**

```
Wemos D1 mini 引脚:
                        ┌──────────────┐
                        │  3V3    GND  │ ← 用这俩
                        │  D0(16) D4(2)│
          4.7kΩ         │  D5(14) D3(0)│
3.3V ──/\/\/\/──┬── D7(13)            │
                │                      │
     霍尔 OUT ──┘                      │
                │                      │
          4.7kΩ │                      │
3.3V ──/\/\/\/──┬── D0(16)            │
                │                      │
    微动 NO ────┘                      │
                │                      │
     微动 COM ── GND                   │
                        └──────────────┘
```

- 在 D7 和 3V3 之间焊一个 4.7kΩ 电阻
- 在 D0 和 3V3 之间焊一个 4.7kΩ 电阻
- 霍尔 VCC 仍接 5V（传感器需要 5V 供电），OUT 接 D7
- 微动 NO 接 D0, COM 接 GND
- **不需要任何电平转换模块**

**引脚映射：**

| 功能 | GPIO | D1 mini 丝印 | 连接至 | 注意事项 |
|------|------|-------------|--------|---------|
| 电机 IN1 | 5 | D1 | ULN2003A IN1 | |
| 电机 IN2 | 4 | D2 | ULN2003A IN2 | |
| 电机 IN3 | 14 | D5 | ULN2003A IN3 | |
| 电机 IN4 | 12 | D6 | ULN2003A IN4 | |
| 霍尔传感器 | 13 | D7 | 霍尔 OUT | **必须接 3.3V 上拉电阻 (4.7kΩ)** |
| 微动开关 | 16 | D0 | 开关 NO → D0, COM → GND | **必须接外部 3.3V 上拉电阻** |
| LED | 2 | D4 | 板载 | 板载 LED, 低电平点亮 |
| 蜂鸣器 | — | — | **不接** | GPIO15(D8) 启动需 LOW, 无安全引脚可用 |

**电平处理要点：**

- **霍尔传感器**：VCC 接 **5V**（传感器供电），OUT 接 GPIO，上拉电阻接 **3.3V**
- **微动开关**：GPIO16 无内部上拉，外加 4.7kΩ 到 3.3V
- **ULN2003A**：数据手册输入高电平阈值 ≥ 2.4V，ESP8266 的 3.3V GPIO 满足要求；若驱动力不足可将 COM 端接 5V
- **不需要额外的电平转换芯片或分压电路**

**GPIO 启动约束（必须遵守）：**

| GPIO | 启动要求 | 当前用途 | 安全性 |
|------|---------|---------|--------|
| GPIO0 (D3) | 必须 HIGH | 未使用 | ✅ |
| GPIO2 (D4) | 必须 HIGH | 板载 LED (已有上拉) | ✅ |
| GPIO15 (D8) | 必须 LOW | 未使用 | ✅ |
| GPIO16 (D0) | 无约束 | 微动开关输入 | ✅ |

### ESP8266 焊接步骤

参考图片：`ref/3、焊接参考_正面.jpg` 和 `ref/3、焊接参考_反面.jpg`

1. **先焊贴片、后焊插件** — 优先焊完所有贴片元件，再处理直插元件
2. **电机插座** — 5Pin 带锁插座，注意方向避免反插
3. **传感器连接** — 霍尔用 2Pin/3Pin 带锁插座；微动开关引脚可压弯
4. **上拉电阻** — 在霍尔和微动开关旁边就近焊接 4.7kΩ 上拉电阻到 3.3V
5. **不要焊蜂鸣器** — 无安全 GPIO
6. **电容** — 未标注容值的均为 100nF；电机驱动旁路电容 100~470µF
7. **保险丝** — 自恢复保险丝可选但建议焊接；不焊则短接焊盘
8. **TVS/肖特基二极管** — 丝印有横杠的一端为负极，对齐 PCB 丝印
9. **固定螺丝** — 轻微拧紧即可，务必放垫片防止磨损 PCB
10. **上电前检查** — 用万用表确认所有传感器引脚电压 ≤ 3.3V

### 编译与烧录

**PlatformIO**:

```bash
# Arduino Nano
pio run -e nano -t upload

# ESP8266
pio run -e esp8266 -t upload

# 串口监视
pio device monitor -b 57600
```

**Arduino IDE**:

1. 将 `firmware/common/` 中的 `.cpp` 和 `.h` 文件复制到 Sketch 目录
2. Nano: 将 `firmware/nano/main.cpp` 重命名为 `.ino` 并打开
3. ESP8266: 将 `firmware/esp8266/main.cpp` 重命名为 `.ino`，需安装 ESP8266 板支持包
4. 选择对应板型: Nano (ATmega328P) 或 Wemos D1 R1
5. 编译并上传

### 首次使用

1. 上电后设备自动开始回零
2. 串口终端 (57600 baud) 发送 `ID?` 验证通信
3. 发送 `STATE?` 确认状态为 `READY`
4. 发送 `GOTO 0` 移动到第一个槽位

### 标定

每台设备需要独立标定，因为打印收缩率和电机减速比存在差异：

```bash
# 设置滤镜盘槽位数
SLOTS 7

# 标定各槽位步数（从原点起算）
CAL SLOT 0 0
CAL SLOT 1 512
CAL SLOT 2 1024
# ...

# 保存配置
SAVE
```

## ESP8266 网络功能

ESP8266 首次上电时启动 AP 模式用于 Wi-Fi 配置：

1. 连接 WiFi `EFilterWheel-Setup` (密码: `filterwheel`)
2. 浏览器访问 `http://192.168.4.1/wifi`
3. 输入路由器 SSID 和密码
4. 设备重启后自动连接

设备就绪后：
- Web 控制: `http://<ip>/`
- Alpaca API: `http://<ip>/api/v1/filterwheel/0/`
- mDNS: `http://efilterwheel.local/`

## 许可证

本项目代码基于参考项目的非商业授权许可。个人制作、学习和非营利协作可按许可进行。商业用途需取得原作者书面授权。

原始项目作者: Eric (微信: ericbo)
