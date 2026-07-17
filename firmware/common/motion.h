/**
 * @file motion.h
 * @brief 步进电机运动控制层
 *
 * 实现 5 线单极步进电机的 4 相半步驱动序列、非阻塞定时步进、
 * 加减速控制、方向切换、超时保护和线圈释放。
 */

#ifndef MOTION_H
#define MOTION_H

#include "config.h"

/* =========================================================================
 * 电机参数结构
 * ========================================================================= */
typedef struct {
    uint16_t speed_pps;         ///< 目标速度 (pulses per second)
    uint16_t min_speed_pps;     ///< 启动速度
    uint16_t ramp_steps;        ///< 加减速步数
    direction_t direction;      ///< 运动方向
    bool     invert_dir;        ///< 方向反转标志
} motor_config_t;

/* =========================================================================
 * 电机状态
 * ========================================================================= */
typedef enum {
    MOTOR_IDLE      = 0,
    MOTOR_RUNNING   = 1,
    MOTOR_STOPPING  = 2,
    MOTOR_ERROR     = 3,
} motor_state_t;

/* =========================================================================
 * 运动层 API
 * ========================================================================= */

/// 初始化运动层
void motion_init(void);

/// 设置电机参数（速度、加减速等）
void motion_set_config(const motor_config_t* config);
void motion_get_config(motor_config_t* config);

/// 设置目标速度（pps），运行时立即生效
void motion_set_speed(uint16_t speed_pps);

/// 设置运动方向
void motion_set_direction(direction_t dir);

/// 相对移动指定步数（异步，立即返回）
/// 正数为指定方向，负数为反方向
void motion_move_steps(int32_t steps);

/// 持续旋转（直到调用 stop）
void motion_move_continuous(direction_t dir);

/// 紧急停止（立即停止脉冲，释放线圈）
void motion_stop_emergency(void);

/// 正常减速停止
void motion_stop(void);

/// 获取当前电机状态
motor_state_t motion_get_state(void);

/// 获取已完成的步数（本次移动）
int32_t motion_get_completed_steps(void);

/// 获取目标步数
int32_t motion_get_target_steps(void);

/// 运动是否完成
bool motion_is_done(void);

/// 在后台 tick 中调用（由定时器或主循环驱动）
void motion_tick(void);

/// 设置移动完成回调
void motion_set_done_callback(void (*callback)(bool success));

/// 释放电机线圈（节能/允许手动旋转）
void motion_release(void);

/// 如果电机空闲，释放线圈
void motion_release_if_idle(void);

#endif // MOTION_H
