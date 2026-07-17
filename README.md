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

- Arduino Nano (ATmega328P) 或 Wemos D1 mini (ESP8266)
- 28BYJ-48 5V 步进电机 + ULN2003A 驱动模块
- 霍尔传感器 (开集电极输出, 如 A3144)
- 微动开关
- 5V / 1A+ 电源
- 无源蜂鸣器

### 接线 (Arduino Nano)

| 功能 | Nano 引脚 | 连接至 |
|------|----------|--------|
| 电机 IN1 | D4 | ULN2003A IN1 |
| 电机 IN2 | D5 | ULN2003A IN2 |
| 电机 IN3 | D6 | ULN2003A IN3 |
| 电机 IN4 | D7 | ULN2003A IN4 |
| 霍尔传感器 | D2 | 霍尔 OUT (上拉至 5V) |
| 微动开关 | D3 | 开关 (NO → D3, COM → GND) |
| 蜂鸣器 | D8 | 蜂鸣器 + |
| LED | D13 | 板载 |

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
2. 将 `firmware/nano/main.cpp` 重命名为 `.ino` 并打开
3. 选择板型: Arduino Nano (ATmega328P)
4. 编译并上传

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
