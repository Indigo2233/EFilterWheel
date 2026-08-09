/**
 * @file main.cpp
 * @brief Arduino Nano 滤镜轮固件入口
 *
 * 编译: PlatformIO (env:nano) 或 Arduino IDE
 *
 * 主循环流程:
 *   1. 初始化 HAL、运动、滤镜轮、传输层
 *   2. 进入主循环:
 *      - 处理传感器更新和后台 tick
 *      - 处理滤镜轮状态机
 *      - 处理串口命令
 *      - 喂狗
 */

#include <Arduino.h>
#include "hal.h"
#include "motion.h"
#include "wheel.h"
#include "transport.h"
#include "eeprom_config.h"

/* =========================================================================
 * 全局 tick 计数器
 * ========================================================================= */
static uint32_t last_tick_ms = 0;

/* =========================================================================
 * 初始化
 * ========================================================================= */
void setup(void)
{
    // 1. 硬件初始化
    hal_init();

    // 2. 简短延时让串口稳定
    hal_delay_ms(500);

    // 3. 播放启动提示
    hal_buzzer_play_startup();

    // 4. 初始化各子系统
    motion_init();
    transport_init();
    wheel_init();

    // 5. 使能看门狗 (4s)
    hal_wdt_enable(4);

    // 6. 发送启动信息
    hal_serial_println("");
    hal_serial_println("==============================");
    hal_serial_print("EFilterWheel-Nano FW ");
    hal_serial_println(FW_VERSION_STRING);
    hal_serial_println("==============================");
    hal_serial_print("Config loaded: ");
    hal_serial_println(eeprom_config_is_valid() ? "YES" : "NO (using defaults)");

    // 7. 自动开始回零
    hal_serial_println("Starting auto-homing...");
    wheel_start_homing();
}

/* =========================================================================
 * 主循环
 * ========================================================================= */
void loop(void)
{
    uint32_t now = hal_millis();

    // 1. 传感器更新 + LED 闪烁（每个 tick）
    if ((int32_t)(now - last_tick_ms) >= 1) {
        last_tick_ms = now;
        hal_tick_process();

        // 检查霍尔传感器状态用于诊断
        // (实际值由 hal_sensors_update 更新)
    }

    // 2. 滤镜轮状态机
    wheel_tick();

    // 3. 运动层超时检查
    motion_tick();

    // 4. 串口命令处理
    transport_tick();

    // 5. 空闲时释放电机
    motion_release_if_idle();

    // 6. 喂狗
    hal_wdt_reset();
}
