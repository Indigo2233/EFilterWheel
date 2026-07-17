/**
 * @file wheel.cpp
 * @brief 滤镜轮状态机实现
 *
 * 状态转换图：
 *   BOOT ──[init]──> HOMING ──[home found]──> READY ──[goto]──> MOVING
 *     │                 │                        │                  │
 *     │                 │ fail                   │ fail             │ done
 *     │                 v                        v                  v
 *     └────────────> ERROR <──────────────────────┘              READY
 *
 * 回零策略：
 *   1. 快速搜索霍尔传感器（低速单向扫描）
 *   2. 首次触发后退出传感区
 *   3. 以更低速度二次接近，减少方向误差
 *   4. 记录原点位置
 */

#include "wheel.h"
#include "motion.h"
#include "hal.h"
#include "eeprom_config.h"

/* =========================================================================
 * 回零阶段
 * ========================================================================= */
typedef enum {
    HOMING_PHASE_SEARCH,    ///< 第一轮快速搜索
    HOMING_PHASE_BACKOFF,   ///< 退出传感区
    HOMING_PHASE_APPROACH,  ///< 第二轮精调接近
    HOMING_PHASE_DONE,      ///< 回零完成
    HOMING_PHASE_FAILED,    ///< 回零失败
} homing_phase_t;

/* =========================================================================
 * 内部状态
 * ========================================================================= */
static device_state_t  dev_state       = STATE_BOOT;
static error_code_t    last_error      = ERR_NONE;

static wheel_config_t  config;
static bool            config_loaded   = false;

static int8_t          current_slot    = -2;  ///< -2=未回零, -1=移动中
static int8_t          target_slot     = -1;

static int32_t         position_steps  = 0;   ///< 距离原点的步数
static int32_t         total_step_count = 0;  ///< 累计步数计数器

// 回零状态
static homing_phase_t  homing_phase    = HOMING_PHASE_SEARCH;
static uint32_t        homing_start_time = 0;
static bool            hall_triggered  = false;
static uint16_t        homing_backoff_steps = 50;  ///< 退出传感区的步数

// 移动完成标志
static volatile bool   move_completed  = false;
static volatile bool   move_success    = false;

/* =========================================================================
 * 内部回调
 * ========================================================================= */

static void on_move_done(bool success)
{
    move_completed = true;
    move_success   = success;
}

/* =========================================================================
 * 内部辅助
 * ========================================================================= */

/// 计算到目标槽位的步数（带符号：正=顺时针，负=逆时针）
static int32_t calc_steps_to_slot(uint8_t slot)
{
    if (slot >= config.num_slots) return 0;

    uint16_t slot_pos = config.slot_steps[slot];
    uint16_t full_circle = eeprom_config_get_full_circle_steps();
    int32_t cur_pos = position_steps % full_circle;
    if (cur_pos < 0) cur_pos += full_circle;

    int32_t diff_cw  = (int32_t)slot_pos - cur_pos;
    if (diff_cw < 0) diff_cw += full_circle;

    int32_t diff_ccw = diff_cw - full_circle;

    // 选择较短路径（取绝对值较小的）
    if (abs(diff_cw) <= abs(diff_ccw)) {
        return diff_cw;
    } else {
        return diff_ccw;
    }
}

/// 将状态步数转换为槽位号
static int8_t steps_to_slot(int32_t steps)
{
    if (!config_loaded || config.num_slots == 0) return -2;

    uint16_t full_circle = eeprom_config_get_full_circle_steps();
    int32_t pos = steps % full_circle;
    if (pos < 0) pos += full_circle;

    // 在配置的槽位步数中找到最近的匹配
    uint8_t best_slot = 0;
    int32_t best_diff = full_circle;

    for (uint8_t i = 0; i < config.num_slots; i++) {
        int32_t diff = (int32_t)config.slot_steps[i] - pos;
        if (diff < 0) diff = -diff;
        // 处理环绕：最短距离
        if (diff > (int32_t)(full_circle / 2)) {
            diff = full_circle - diff;
        }
        if (diff < best_diff) {
            best_diff = diff;
            best_slot = i;
        }
    }

    // 阈值：如果最近槽位也在半圈之外，认为不在任何槽位上
    if (best_diff > full_circle / config.num_slots / 2 + 50) {
        return -2;
    }

    return (int8_t)best_slot;
}

/// 进入错误状态
static void set_error(error_code_t err)
{
    dev_state  = STATE_ERROR;
    last_error = err;
    motion_stop_emergency();
    hal_led_blink_pattern(3, 200, 200);
}

/// 清除错误
static void clear_error(void)
{
    last_error = ERR_NONE;
}

/* =========================================================================
 * 初始化
 * ========================================================================= */

void wheel_init(void)
{
    dev_state     = STATE_BOOT;
    last_error    = ERR_NONE;
    current_slot  = -2;
    target_slot   = -1;
    position_steps = 0;
    total_step_count = 0;
    move_completed = false;
    move_success   = false;

    // 加载配置
    config_loaded = eeprom_config_load(&config);
    if (!config_loaded) {
        eeprom_config_defaults(&config);
    }

    // 应用电机配置
    motor_config_t motor_cfg;
    motor_cfg.speed_pps   = config.speed_pps;
    motor_cfg.min_speed_pps = MIN_SPEED_PPS;
    motor_cfg.ramp_steps  = RAMP_STEPS;
    motor_cfg.direction   = config.default_dir;
    motor_cfg.invert_dir  = config.invert_dir;
    motion_set_config(&motor_cfg);
    motion_set_done_callback(on_move_done);

    // 初始化完成，准备回零
    dev_state = STATE_HOMING;
}

void wheel_start_homing(void)
{
    dev_state       = STATE_HOMING;
    homing_phase    = HOMING_PHASE_SEARCH;
    hall_triggered  = false;
    current_slot    = -2;
    position_steps  = 0;
    homing_start_time = hal_millis();

    // 以低速搜索霍尔传感器
    motion_set_speed(HOMING_SPEED_PPS);
    motion_set_direction(DIR_CW);
    motion_move_continuous(DIR_CW);

    hal_serial_println("OK HOMING started");
}

/* =========================================================================
 * 状态机 tick
 * ========================================================================= */

void wheel_tick(void)
{
    // 运动超时由 motion_tick 处理

    switch (dev_state) {
    case STATE_BOOT:
        // 不应在此状态停留
        break;

    case STATE_HOMING:
        {
            switch (homing_phase) {
            case HOMING_PHASE_SEARCH:
                // 检查霍尔传感器触发
                if (hal_hall_read()) {
                    if (!hall_triggered) {
                        hall_triggered = true;
                        // 记录首次触发位置
                        motion_stop_emergency();
                        hal_delay_ms(100);

                        // 进入退避阶段
                        homing_phase = HOMING_PHASE_BACKOFF;
                        motion_set_speed(HOMING_SPEED_PPS);
                        motion_move_steps(homing_backoff_steps);
                    }
                }
                // 超时检查
                if ((hal_millis() - homing_start_time) > HOMING_TIMEOUT_MS) {
                    homing_phase = HOMING_PHASE_FAILED;
                    set_error(ERR_HOMING_FAILED);
                    hal_serial_println("ERR 5 Homing failed: timeout searching for sensor");
                }
                break;

            case HOMING_PHASE_BACKOFF:
                // 等待退避完成
                if (motion_is_done()) {
                    hal_delay_ms(50);
                    // 进入精调接近阶段
                    homing_phase = HOMING_PHASE_APPROACH;
                    motion_set_speed(HOMING_FINE_SPEED_PPS);
                    motion_set_direction(DIR_CCW);  // 反向缓慢接近
                    motion_move_continuous(DIR_CCW);
                }
                break;

            case HOMING_PHASE_APPROACH:
                // 等待再次触发霍尔传感器
                if (hal_hall_read()) {
                    motion_stop_emergency();
                    homing_phase = HOMING_PHASE_DONE;

                    // 原点建立
                    position_steps  = config.home_offset;
                    current_slot    = 0;
                    dev_state       = STATE_READY;

                    hal_buzzer_beep(100);
                    hal_serial_println("OK HOMED");
                }
                // 超时
                if ((hal_millis() - homing_start_time) > HOMING_TIMEOUT_MS) {
                    homing_phase = HOMING_PHASE_FAILED;
                    set_error(ERR_HOMING_FAILED);
                    hal_serial_println("ERR 5 Homing failed: timeout approaching sensor");
                }
                break;

            default:
                break;
            }
        }
        break;

    case STATE_READY:
        // 空闲，等待命令
        break;

    case STATE_MOVING:
        // 检查移动完成
        if (move_completed) {
            move_completed = false;

            if (move_success) {
                // 更新当前位置
                current_slot = target_slot;
                // 更新步数偏移
                if (target_slot >= 0 && target_slot < config.num_slots) {
                    uint16_t full_circle = eeprom_config_get_full_circle_steps();
                    position_steps = config.slot_steps[target_slot] % full_circle;
                }
                dev_state = STATE_READY;

                hal_serial_print("OK GOTO ");
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", (int)current_slot);
                hal_serial_println(buf);
            } else {
                set_error(ERR_MOTOR_FAULT);
            }
        }
        break;

    case STATE_ERROR:
        // 等待清除错误
        break;

    default:
        break;
    }
}

/* =========================================================================
 * 运动控制
 * ========================================================================= */

bool wheel_goto_slot(uint8_t slot)
{
    if (dev_state == STATE_ERROR) {
        // 需要先清除错误
        return false;
    }

    if (dev_state == STATE_MOVING) {
        // 先停止再移动
        wheel_stop();
        hal_delay_ms(100);
    }

    if (!wheel_is_ready()) {
        return false;
    }

    if (slot >= config.num_slots) {
        return false;
    }

    if (slot == (uint8_t)current_slot) {
        // 已在目标槽位
        hal_serial_print("OK GOTO ");
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", (int)slot);
        hal_serial_println(buf);
        return true;
    }

    // 计算移动步数
    int32_t steps = calc_steps_to_slot(slot);
    if (steps == 0) {
        // 已在目标位置（可能由于环绕计算）
        current_slot = (int8_t)slot;
        return true;
    }

    // 启动移动
    target_slot     = (int8_t)slot;
    move_completed  = false;
    move_success    = false;
    dev_state       = STATE_MOVING;

    // 应用当前速度设置
    motion_set_speed(config.speed_pps);
    motion_move_steps(steps);

    // 在移动期间标记位置为未知
    current_slot = -1;

    return true;
}

void wheel_stop(void)
{
    if (dev_state == STATE_MOVING || dev_state == STATE_HOMING) {
        motion_stop();
        dev_state = STATE_ERROR;
        last_error = ERR_STOPPED;
    }
}

bool wheel_set_slots(uint8_t num_slots)
{
    if (num_slots != 5 && num_slots != 7) return false;
    if (dev_state == STATE_MOVING) return false;

    config.num_slots = num_slots;

    // 重新计算槽位步数（均匀分布）
    uint16_t full_circle = eeprom_config_get_full_circle_steps();
    for (uint8_t i = 0; i < num_slots; i++) {
        config.slot_steps[i] = (uint32_t)full_circle * i / num_slots;
    }

    return true;
}

/* =========================================================================
 * 状态查询
 * ========================================================================= */

device_state_t wheel_get_state(void)
{
    return dev_state;
}

int8_t wheel_get_current_slot(void)
{
    return current_slot;
}

const char* wheel_get_state_string(void)
{
    switch (dev_state) {
    case STATE_BOOT:    return "BOOT";
    case STATE_HOMING:  return "HOMING";
    case STATE_READY:   return "READY";
    case STATE_MOVING:  return "MOVING";
    case STATE_ERROR:   return "ERROR";
    default:            return "UNKNOWN";
    }
}

error_code_t wheel_get_error(void)
{
    return last_error;
}

const char* wheel_get_error_string(void)
{
    switch (last_error) {
    case ERR_NONE:           return "No error";
    case ERR_UNKNOWN_CMD:    return "Unknown command";
    case ERR_BAD_ARG:        return "Bad argument";
    case ERR_NOT_READY:      return "Device not ready";
    case ERR_BUSY:           return "Device busy";
    case ERR_HOMING_FAILED:  return "Homing failed";
    case ERR_TIMEOUT:        return "Timeout";
    case ERR_SENSOR_FAULT:   return "Sensor fault";
    case ERR_EEPROM_FAIL:    return "EEPROM failure";
    case ERR_INVALID_CONFIG: return "Invalid configuration";
    case ERR_MOTOR_FAULT:    return "Motor fault";
    case ERR_STOPPED:        return "Stopped";
    case ERR_OUT_OF_RANGE:   return "Slot out of range";
    case ERR_INTERNAL:       return "Internal error";
    default:                 return "Unknown error";
    }
}

bool wheel_is_ready(void)
{
    return (dev_state == STATE_READY);
}

bool wheel_is_moving(void)
{
    return (dev_state == STATE_MOVING);
}

/* =========================================================================
 * 标定
 * ========================================================================= */

const wheel_config_t* wheel_get_config(void)
{
    return &config;
}

bool wheel_update_config(const wheel_config_t* cfg)
{
    if (!cfg) return false;
    config = *cfg;

    // 应用电机参数
    motor_config_t motor_cfg;
    motor_cfg.speed_pps   = config.speed_pps;
    motor_cfg.min_speed_pps = MIN_SPEED_PPS;
    motor_cfg.ramp_steps  = RAMP_STEPS;
    motor_cfg.direction   = config.default_dir;
    motor_cfg.invert_dir  = config.invert_dir;
    motion_set_config(&motor_cfg);

    return true;
}

void wheel_set_current_slot(uint8_t slot)
{
    if (slot < config.num_slots) {
        current_slot = (int8_t)slot;
        position_steps = config.slot_steps[slot];
    }
}

bool wheel_calibrate_slot(uint8_t slot, uint16_t steps_from_home)
{
    if (slot >= config.num_slots) return false;
    config.slot_steps[slot] = steps_from_home;
    return true;
}

bool wheel_save_config(void)
{
    if (!eeprom_config_save(&config)) {
        set_error(ERR_EEPROM_FAIL);
        return false;
    }
    return true;
}

void wheel_factory_reset(void)
{
    eeprom_config_factory_reset();
    eeprom_config_defaults(&config);
    config_loaded = false;
    hal_system_reset();
}

/* =========================================================================
 * 诊断
 * ========================================================================= */

int32_t wheel_get_total_steps(void)
{
    return total_step_count;
}

bool wheel_is_homed(void)
{
    return (current_slot >= 0);
}

void wheel_clear_error(void)
{
    if (dev_state == STATE_ERROR) {
        last_error = ERR_NONE;
        dev_state  = STATE_READY;

        // 回零状态丢失，需要重新回零
        current_slot = -2;
        dev_state = STATE_HOMING;
        wheel_start_homing();
    }
}
