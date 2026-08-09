/**
 * @file wheel.h
 * @brief 滤镜轮逻辑层
 *
 * 实现滤镜轮状态机、回零流程、槽位换算、误差补偿和故障处理。
 * 状态: BOOT → HOMING → READY → MOVING → ERROR
 */

#ifndef WHEEL_H
#define WHEEL_H

#include "config.h"
#include "eeprom_config.h"

/* =========================================================================
 * 滤镜轮状态（与全局 device_state_t 对应）
 * ========================================================================= */

/* =========================================================================
 * 初始化与生命周期
 * ========================================================================= */

/// 初始化滤镜轮子系统
void wheel_init(void);

/// 启动上电回零流程（进入 HOMING 状态）
void wheel_start_homing(void);

/// 在后台 tick 中调用，处理状态机
void wheel_tick(void);

/* =========================================================================
 * 运动控制
 * ========================================================================= */

/// 移动到指定槽位 (0-based)
/// 返回 true 表示命令已接受（不等于已完成）
bool wheel_goto_slot(uint8_t slot);

/// 停止当前移动
void wheel_stop(void);

/// 设置槽位数量（5 或 7）
bool wheel_set_slots(uint8_t num_slots);

/* =========================================================================
 * 状态查询
 * ========================================================================= */

/// 获取当前设备状态
device_state_t wheel_get_state(void);

/// 获取当前槽位（移动中返回 -1，未回零返回 -2）
int8_t wheel_get_current_slot(void);

/// 获取状态描述字符串
const char* wheel_get_state_string(void);

/// 获取最近的错误码
error_code_t wheel_get_error(void);

/// 获取错误描述
const char* wheel_get_error_string(void);

/// 是否已回零且就绪
bool wheel_is_ready(void);

/// 是否正在移动
bool wheel_is_moving(void);

/* =========================================================================
 * 标定
 * ========================================================================= */

/// 获取配置
const wheel_config_t* wheel_get_config(void);

/// 更新并保存配置
bool wheel_update_config(const wheel_config_t* cfg);

/// 手动设定当前槽位（用于标定）
void wheel_set_current_slot(uint8_t slot);

/// 记录当前槽位的步数偏移
bool wheel_calibrate_slot(uint8_t slot, uint16_t steps_from_home);

/// 保存配置到 EEPROM
bool wheel_save_config(void);

/// 恢复出厂设置
void wheel_factory_reset(void);

/* =========================================================================
 * 诊断
 * ========================================================================= */

/// 获取总步数计数器
int32_t wheel_get_total_steps(void);

/// 获取回零状态
bool wheel_is_homed(void);

/// 重置错误状态
void wheel_clear_error(void);

#endif // WHEEL_H
