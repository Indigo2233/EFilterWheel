/**
 * @file motion.cpp
 * @brief 步进电机运动控制实现
 *
 * 使用 8 拍半步驱动序列（半步模式），每步 4 相信号。
 * 通过硬件定时器驱动脉冲输出，实现非阻塞控制。
 */

#include "motion.h"
#include "hal.h"

/* =========================================================================
 * 半步驱动序列表（8 步/循环）
 *
 * 4 相单极步进电机的 8 拍半步序列：
 *   相电流模式：A → AB → B → BC → C → CD → D → DA
 *   每个 bit 对应一个相线圈（IN1=bit0, IN2=bit1, IN3=bit2, IN4=bit3）
 *
 *   CW 方向：按表顺序递增
 *   CCW 方向：按表顺序递减
 * ========================================================================= */
static const uint8_t STEP_TABLE[8] = {
    0b0001, // Phase A
    0b0011, // Phase A+B
    0b0010, // Phase B
    0b0110, // Phase B+C
    0b0100, // Phase C
    0b1100, // Phase C+D
    0b1000, // Phase D
    0b1001, // Phase D+A
};

/* =========================================================================
 * 内部状态
 * ========================================================================= */
static motor_config_t  motor_cfg;
static motor_state_t   motor_state = MOTOR_IDLE;

static int32_t  target_steps    = 0;    ///< 目标步数（0 = 连续模式）
static int32_t  completed_steps = 0;    ///< 已完成步数
static int8_t   step_index      = 0;    ///< 当前在 STEP_TABLE 中的位置
static bool     moving_forward  = true; ///< 正向/反向

static uint16_t current_interval_us = 2000; ///< 当前脉冲间隔 (us)
static uint16_t target_interval_us  = 2000;
static uint16_t min_interval_us     = 2000;

// 加减速管理
static int32_t  ramp_step_counter  = 0;
static bool     ramping            = false;

// 回调
static void (*done_callback)(bool success) = nullptr;

// 超时
static uint32_t move_start_time = 0;

// ISR 重入保护（ESP8266 Ticker 回调可能与主循环并发）
static volatile bool step_in_progress = false;

/* =========================================================================
 * 内部函数
 * ========================================================================= */

/// 根据当前速度计算脉冲间隔 (us)
static uint16_t speed_to_interval_us(uint16_t speed_pps)
{
    if (speed_pps == 0) return 0;
    uint32_t interval = 1000000UL / speed_pps;
    if (interval > 65535) interval = 65535;
    if (interval < 200)   interval = 200;   // max ~5000 pps
    return (uint16_t)interval;
}

/// 输出当前步进相位
static void output_step(void)
{
    uint8_t phase = STEP_TABLE[step_index & 0x07];
    hal_motor_write(phase);
}

/// 前进一步
static void step_forward(void)
{
    step_index++;
    if (step_index >= 8) step_index = 0;
    output_step();
}

/// 后退一步
static void step_backward(void)
{
    step_index--;
    if (step_index < 0) step_index = 7;
    output_step();
}

/// 执行一步（带加减速），在定时器 ISR/回调上下文中调用
/// 完成后自动重新安排下一次触发
static void do_step(void)
{
    // 防止重入（ESP8266 上 Ticker 回调可能被 WiFi 中断）
    if (step_in_progress) return;
    step_in_progress = true;

    // 物理步进
    if (moving_forward) {
        step_forward();
    } else {
        step_backward();
    }

    completed_steps++;

    // 加减速：如果正在使用斜坡
    if (ramping && target_steps != 0) {
        int32_t remaining = target_steps - completed_steps;
        if (remaining <= 0) remaining = 0;

        // 加速阶段
        if (completed_steps <= motor_cfg.ramp_steps && remaining > motor_cfg.ramp_steps) {
            uint16_t range = target_interval_us - min_interval_us;
            uint32_t frac = (uint32_t)completed_steps * range / motor_cfg.ramp_steps;
            current_interval_us = target_interval_us - (uint16_t)frac;
        }
        // 减速阶段
        else if (remaining <= motor_cfg.ramp_steps && remaining > 0) {
            uint16_t range = target_interval_us - min_interval_us;
            uint32_t frac = (uint32_t)remaining * range / motor_cfg.ramp_steps;
            current_interval_us = target_interval_us - (uint16_t)frac;
        }
        // 匀速阶段
        else {
            current_interval_us = min_interval_us;
        }
    }

    // 检查到达目标
    if (target_steps > 0 && completed_steps >= target_steps) {
        step_in_progress = false;
        motion_stop();
        if (done_callback) {
            done_callback(true);
        }
        return;
    }

    step_in_progress = false;

    // 重新安排下一次步进（动态调整间隔实现加减速）
    // Nano: 直接在 ISR 中操作 Timer2 寄存器, 安全
    // ESP8266: Ticker.attach_us 内部使用 ets_timer_arm_new, 可从回调中安全调用
    hal_timer_set_step_callback(do_step, current_interval_us);
}

/// 步进定时器入口（直接作为回调传给 hal_timer_set_step_callback）
static void step_timer_cb(void)
{
    do_step();
}

/* =========================================================================
 * 公共 API
 * ========================================================================= */

void motion_init(void)
{
    // 默认参数
    motor_cfg.speed_pps     = DEFAULT_SPEED_PPS;
    motor_cfg.min_speed_pps = MIN_SPEED_PPS;
    motor_cfg.ramp_steps    = RAMP_STEPS;
    motor_cfg.direction     = DIR_CW;
    motor_cfg.invert_dir    = false;

    motor_state       = MOTOR_IDLE;
    step_index        = 0;
    completed_steps   = 0;
    target_steps      = 0;
    current_interval_us = speed_to_interval_us(DEFAULT_SPEED_PPS);
    target_interval_us  = current_interval_us;
    min_interval_us     = speed_to_interval_us(MAX_SPEED_PPS);

    hal_motor_release();
}

void motion_set_config(const motor_config_t* config)
{
    if (config) {
        motor_cfg = *config;
        target_interval_us = speed_to_interval_us(motor_cfg.speed_pps);
        min_interval_us    = speed_to_interval_us(MAX_SPEED_PPS);
        if (target_interval_us < min_interval_us) {
            target_interval_us = min_interval_us;
        }
    }
}

void motion_get_config(motor_config_t* config)
{
    if (config) {
        *config = motor_cfg;
    }
}

void motion_set_speed(uint16_t speed_pps)
{
    if (speed_pps < MIN_SPEED_PPS) speed_pps = MIN_SPEED_PPS;
    if (speed_pps > MAX_SPEED_PPS) speed_pps = MAX_SPEED_PPS;
    motor_cfg.speed_pps = speed_pps;
    target_interval_us = speed_to_interval_us(speed_pps);
}

void motion_set_direction(direction_t dir)
{
    motor_cfg.direction = dir;
}

void motion_move_steps(int32_t steps)
{
    if (steps == 0) return;
    if (motor_state == MOTOR_RUNNING) {
        motion_stop_emergency();
        hal_delay_ms(50);  // 等待线圈释放
    }

    // 计算方向和实际步数
    bool forward;
    int32_t abs_steps;

    if (steps > 0) {
        forward   = (motor_cfg.direction == DIR_CW);
        abs_steps = steps;
    } else {
        forward   = (motor_cfg.direction != DIR_CW);
        abs_steps = -steps;
    }

    if (motor_cfg.invert_dir) {
        forward = !forward;
    }

    // 设置运动参数
    moving_forward    = forward;
    target_steps      = abs_steps;
    completed_steps   = 0;
    ramp_step_counter = 0;
    ramping           = (motor_cfg.ramp_steps > 0);
    current_interval_us = target_interval_us;
    motor_state       = MOTOR_RUNNING;
    move_start_time   = hal_millis();

    // 启动定时器驱动
    hal_timer_set_step_callback(step_timer_cb, current_interval_us);
}

void motion_move_continuous(direction_t dir)
{
    if (motor_state == MOTOR_RUNNING) {
        motion_stop_emergency();
        hal_delay_ms(50);
    }

    bool forward = (dir == DIR_CW);
    if (motor_cfg.invert_dir) forward = !forward;

    moving_forward    = forward;
    target_steps      = 0;  // 连续模式
    completed_steps   = 0;
    ramp_step_counter = 0;
    ramping           = false;
    current_interval_us = min_interval_us;
    motor_state       = MOTOR_RUNNING;

    hal_timer_set_step_callback(step_timer_cb, current_interval_us);
}

void motion_stop_emergency(void)
{
    hal_timer_stop_step();
    motor_state = MOTOR_IDLE;
    hal_motor_release();
    target_steps    = 0;
    completed_steps = 0;

    if (done_callback) {
        done_callback(false);
    }
}

void motion_stop(void)
{
    // 正常停止：如果正在运动中且使用了加减速，先减速再停
    if (motor_state == MOTOR_RUNNING && target_steps == 0) {
        // 连续模式直接停
        motion_stop_emergency();
        return;
    }

    if (motor_state == MOTOR_RUNNING && target_steps > 0) {
        // 修改目标步数为当前已完成步数 + 减速距离
        int32_t decel_steps = (ramping && motor_cfg.ramp_steps > 0)
                            ? motor_cfg.ramp_steps : 0;
        int32_t new_target = completed_steps + decel_steps;
        if (new_target <= completed_steps) {
            // 已经足够近，直接停
            motion_stop_emergency();
        } else {
            target_steps = new_target;
        }
    }
}

motor_state_t motion_get_state(void)
{
    return motor_state;
}

int32_t motion_get_completed_steps(void)
{
    return completed_steps;
}

int32_t motion_get_target_steps(void)
{
    return target_steps;
}

bool motion_is_done(void)
{
    return (motor_state == MOTOR_IDLE);
}

void motion_tick(void)
{
    // 超时检查
    if (motor_state == MOTOR_RUNNING && target_steps > 0) {
        uint32_t elapsed = hal_millis() - move_start_time;
        if (elapsed > MOVE_TIMEOUT_MS) {
            motion_stop_emergency();
            hal_serial_println("ERR 6 Timeout: move exceeded maximum duration");
            if (done_callback) {
                done_callback(false);
            }
        }
    }
}

void motion_set_done_callback(void (*callback)(bool success))
{
    done_callback = callback;
}

void motion_release(void)
{
    motion_stop_emergency();
    hal_motor_release();
}

void motion_release_if_idle(void)
{
    if (motor_state == MOTOR_IDLE) {
        hal_motor_release();
    }
}
