/**
 * @file hal.h
 * @brief 硬件抽象层 (Hardware Abstraction Layer)
 *
 * 封装所有 GPIO 操作、定时器、EEPROM 和串口访问。
 * Arduino Nano 与 ESP8266 通过条件编译共用本接口。
 */

#ifndef HAL_H
#define HAL_H

#include "config.h"

/* =========================================================================
 * 初始化
 * ========================================================================= */

/// 初始化所有硬件外设（GPIO、定时器、串口、EEPROM）
void hal_init(void);

/* =========================================================================
 * GPIO / 电机
 * ========================================================================= */

/// 设置电机 4 相输出 (bit0=IN1, bit1=IN2, bit2=IN3, bit3=IN4)
void hal_motor_write(uint8_t phase_bits);

/// 释放全部电机线圈（所有输出拉低）
void hal_motor_release(void);

/* =========================================================================
 * 传感器
 * ========================================================================= */

/// 读取霍尔传感器原始电平（true = 检测到磁铁）
bool hal_hall_read_raw(void);

/// 读取微动开关原始电平（true = 触发）
bool hal_switch_read_raw(void);

/// 读取霍尔传感器消抖后状态
bool hal_hall_read(void);

/// 读取微动开关消抖后状态
bool hal_switch_read(void);

/// 在后台 tick 中调用，用于消抖更新
void hal_sensors_update(void);

/* =========================================================================
 * 蜂鸣器
 * ========================================================================= */

/// 蜂鸣器 ON/OFF
void hal_buzzer_set(bool on);

/// 蜂鸣器短促提示音
void hal_buzzer_beep(uint16_t duration_ms);

/// 蜂鸣器播放启动音乐（非阻塞）
void hal_buzzer_play_startup(void);

/* =========================================================================
 * LED
 * ========================================================================= */

void hal_led_set(bool on);
void hal_led_toggle(void);
void hal_led_blink_pattern(uint8_t times, uint16_t on_ms, uint16_t off_ms);

/* =========================================================================
 * 定时器
 * ========================================================================= */

/// 获取启动以来的毫秒时间戳
uint32_t hal_millis(void);

/// 微秒级延时
void hal_delay_us(uint16_t us);

/// 毫秒级延时
void hal_delay_ms(uint16_t ms);

/// 注册步进定时器回调（每 step_interval_us 微秒触发）
void hal_timer_set_step_callback(void (*callback)(void), uint16_t interval_us);

/// 停止步进定时器
void hal_timer_stop_step(void);

/// 注册后台 tick 回调（约 1ms 周期）
void hal_timer_set_tick_callback(void (*callback)(void));

/* =========================================================================
 * EEPROM
 * ========================================================================= */

uint8_t  hal_eeprom_read_byte(uint16_t addr);
void     hal_eeprom_write_byte(uint16_t addr, uint8_t value);
uint16_t hal_eeprom_read_word(uint16_t addr);
void     hal_eeprom_write_word(uint16_t addr, uint16_t value);
void     hal_eeprom_commit(void);

/* =========================================================================
 * 串口
 * ========================================================================= */

/// 串口可用字节数
int16_t hal_serial_available(void);

/// 读取一个字节（阻塞等待）
uint8_t hal_serial_read(void);

/// 非阻塞读取一个字节；返回 true 表示读取成功
bool hal_serial_read_nb(uint8_t* ch);

/// 写入一个字节
void hal_serial_write(uint8_t ch);

/// 写入字符串
void hal_serial_print(const char* str);

/// 写入字符串并换行
void hal_serial_println(const char* str);

/// 刷新串口缓冲
void hal_serial_flush(void);

/* =========================================================================
 * 看门狗
 * ========================================================================= */

/// 使能看门狗（超时秒，0 禁用）
void hal_wdt_enable(uint8_t timeout_sec);

/// 喂狗
void hal_wdt_reset(void);

/* =========================================================================
 * 平台特定
 * ========================================================================= */

/// 进入主循环前的平台初始化
void hal_platform_init(void);

/// 后台循环中的平台任务（ESP8266 用于 WiFi/网络处理）
void hal_platform_yield(void);

void hal_tick_process(void);

/// 系统复位
void hal_system_reset(void);

#endif // HAL_H
