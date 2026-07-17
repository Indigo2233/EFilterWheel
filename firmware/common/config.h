/**
 * @file config.h
 * @brief 滤镜轮全局配置与常量定义
 *
 * 本文件定义与平台无关的滤镜轮参数、引脚映射和协议常量。
 * 平台相关宏通过 PLATFORM_NANO / PLATFORM_ESP8266 区分。
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* =========================================================================
 * 固件版本
 * ========================================================================= */
#define FW_VERSION_MAJOR    1
#define FW_VERSION_MINOR    0
#define FW_VERSION_PATCH    0
#define FW_VERSION_STRING   "1.0.0"
#define PROTOCOL_VERSION    "1.0"

/* =========================================================================
 * 设备标识
 * ========================================================================= */
#if defined(PLATFORM_NANO)
  #define DEVICE_NAME        "EFilterWheel-Nano"
#elif defined(PLATFORM_ESP8266)
  #define DEVICE_NAME        "EFilterWheel-ESP8266"
#else
  #define DEVICE_NAME        "EFilterWheel"
#endif

/* =========================================================================
 * 滤镜盘参数
 * ========================================================================= */
#define MAX_SLOTS            7       ///< 最大槽位数（7位滤镜盘）
#define MIN_SLOTS            5       ///< 最小槽位数（5位滤镜盘）
#define DEFAULT_SLOTS        7       ///< 默认 7 位滤镜盘

/* =========================================================================
 * 运动参数
 * ========================================================================= */
#define DEFAULT_SPEED_PPS    800     ///< 默认脉冲频率 (Hz)
#define MIN_SPEED_PPS        200     ///< 最小脉冲频率
#define MAX_SPEED_PPS        2000    ///< 最大脉冲频率
#define HOMING_SPEED_PPS     300     ///< 回零初始速度
#define HOMING_FINE_SPEED_PPS 150    ///< 回零精调速度
#define RAMP_STEPS           50      ///< 加减速步数

#define MAX_STEPS_PER_MOVE   10000   ///< 单次移动最大步数
#define MOVE_TIMEOUT_MS      30000   ///< 移动超时 (ms)
#define HOMING_TIMEOUT_MS    60000   ///< 回零超时 (ms)

#define COIL_RELEASE_DELAY_MS 200    ///< 停机后线圈释放延迟 (ms)

/* =========================================================================
 * 传感器消抖
 * ========================================================================= */
#define HALL_DEBOUNCE_MS     5       ///< 霍尔传感器消抖 (ms)
#define SWITCH_DEBOUNCE_MS   10      ///< 微动开关消抖 (ms)

/* =========================================================================
 * EEPROM 地址布局
 * ========================================================================= */
#define EEPROM_MAGIC_ADDR    0       ///< 魔数地址 (2 bytes)
#define EEPROM_VERSION_ADDR  2       ///< 配置版本 (1 byte)
#define EEPROM_CHECKSUM_ADDR 3       ///< 校验和 (1 byte)
#define EEPROM_SLOTS_ADDR    4       ///< 槽位数 (1 byte)
#define EEPROM_SPEED_ADDR    5       ///< 速度 (2 bytes)
#define EEPROM_DIR_ADDR      7       ///< 方向 (1 byte)
#define EEPROM_HOME_OFFSET   8       ///< 原点偏移 (2 bytes)
#define EEPROM_SLOT_STEPS    10      ///< 各槽位步数起始 (MAX_SLOTS * 2 bytes)
#define EEPROM_CONFIG_SIZE   (EEPROM_SLOT_STEPS + MAX_SLOTS * 2)

#define EEPROM_MAGIC         0xA55A  ///< 配置魔数

/* =========================================================================
 * 错误码
 * ========================================================================= */
typedef enum {
    ERR_NONE            = 0,    ///< 无错误
    ERR_UNKNOWN_CMD     = 1,    ///< 未知命令
    ERR_BAD_ARG         = 2,    ///< 参数错误
    ERR_NOT_READY       = 3,    ///< 设备未就绪
    ERR_BUSY            = 4,    ///< 设备忙
    ERR_HOMING_FAILED   = 5,    ///< 回零失败
    ERR_TIMEOUT         = 6,    ///< 移动超时
    ERR_SENSOR_FAULT    = 7,    ///< 传感器故障
    ERR_EEPROM_FAIL     = 8,    ///< EEPROM 读写失败
    ERR_INVALID_CONFIG  = 9,    ///< 无效配置
    ERR_MOTOR_FAULT     = 10,   ///< 电机故障
    ERR_STOPPED         = 11,   ///< 已停止
    ERR_OUT_OF_RANGE    = 12,   ///< 槽位超出范围
    ERR_INTERNAL        = 99,   ///< 内部错误
} error_code_t;

/* =========================================================================
 * 设备状态
 * ========================================================================= */
typedef enum {
    STATE_BOOT      = 0,    ///< 启动中
    STATE_HOMING    = 1,    ///< 回零中
    STATE_READY     = 2,    ///< 就绪
    STATE_MOVING    = 3,    ///< 移动中
    STATE_ERROR     = 4,    ///< 错误
} device_state_t;

/* =========================================================================
 * 运动方向
 * ========================================================================= */
typedef enum {
    DIR_CW   = 0,   ///< 顺时针
    DIR_CCW  = 1,   ///< 逆时针
} direction_t;

/* =========================================================================
 * 命令枚举
 * ========================================================================= */
typedef enum {
    CMD_ID          = 0,
    CMD_STATE       = 1,
    CMD_POS         = 2,
    CMD_HOME        = 3,
    CMD_GOTO        = 4,
    CMD_STOP        = 5,
    CMD_SLOTS       = 6,
    CMD_CAL         = 7,
    CMD_SAVE        = 8,
    CMD_RESET       = 9,
    CMD_HELP        = 10,
    CMD_UNKNOWN     = 0xFF,
} command_t;

/* =========================================================================
 * 蜂鸣器开关 (ESP8266 默认关闭: GPIO15 必须 LOW 才能正常启动)
 * ========================================================================= */
#if defined(PLATFORM_NANO)
  #define BUZZER_ENABLED   1     ///< Nano D8 安全, 蜂鸣器可用
#elif defined(PLATFORM_ESP8266)
  #define BUZZER_ENABLED   0     ///< ESP8266 无安全 GPIO, 关闭蜂鸣器
#endif

/* =========================================================================
 * 引脚映射 - Arduino Nano
 * ========================================================================= */
#if defined(PLATFORM_NANO)
  // 电机 4 相 (连接 ULN2003A)
  #define PIN_MOTOR_IN1   4
  #define PIN_MOTOR_IN2   5
  #define PIN_MOTOR_IN3   6
  #define PIN_MOTOR_IN4   7

  // 传感器
  #define PIN_HALL_SENSOR 2   ///< 霍尔原点 (INT0)
  #define PIN_LIMIT_SWITCH 3  ///< 微动开关 (INT1)

  // 执行器
  #if BUZZER_ENABLED
    #define PIN_BUZZER    8
  #endif
  #define PIN_LED         13  ///< 板载 LED (PB5)

  // 串口 (使用硬件 USART)
  #define SERIAL_BAUD     57600

/* =========================================================================
 * 引脚映射 - ESP8266 (Wemos D1 mini 标注)
 * 启动约束: GPIO0/GPIO2 需 HIGH, GPIO15 需 LOW
 * 已占用: GPIO5(D1)/GPIO4(D2)/GPIO14(D5)/GPIO12(D6)/GPIO13(D7)
 * ========================================================================= */
#elif defined(PLATFORM_ESP8266)
  // 电机 4 相
  #define PIN_MOTOR_IN1   5   ///< D1 (安全)
  #define PIN_MOTOR_IN2   4   ///< D2 (安全)
  #define PIN_MOTOR_IN3   14  ///< D5 (安全)
  #define PIN_MOTOR_IN4   12  ///< D6 (安全)

  // 传感器
  #define PIN_HALL_SENSOR 13  ///< D7 (安全, 需外部 3.3V 上拉)
  #define PIN_LIMIT_SWITCH 16 ///< D0 (无内部上拉, 需外部上拉电阻)

  // 执行器 (蜂鸣器已禁用: 无安全 GPIO 可用)
  #define PIN_LED         2   ///< D4 板载 LED (GPIO2, 启动需 HIGH, 已有外部上拉)

  // 传感器电压域
  #define SENSOR_VOLTAGE  3.3f

  #define SERIAL_BAUD     57600
#endif

/* =========================================================================
 * 宏工具
 * ========================================================================= */
#define ARRAY_SIZE(arr)  (sizeof(arr) / sizeof((arr)[0]))

#endif // CONFIG_H
