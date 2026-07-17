/**
 * @file hal.cpp
 * @brief 硬件抽象层实现 (Arduino Nano 为主，ESP8266 条件编译)
 */

#include "hal.h"
#include <Arduino.h>
#include <EEPROM.h>
#if defined(PLATFORM_NANO)
#include <avr/wdt.h>      // Nano 看门狗
#endif

/* =========================================================================
 * 内部状态
 * ========================================================================= */

// 传感器消抖状态
static struct {
    bool hall_raw;
    bool hall_stable;
    uint32_t hall_last_change;

    bool switch_raw;
    bool switch_stable;
    uint32_t switch_last_change;
} sensor_state;

// 步进定时器回调
static void (*step_callback)(void) = nullptr;
static void (*tick_callback)(void) = nullptr;

// LED 闪烁模式（非阻塞）
static struct {
    uint8_t  remaining;
    uint16_t on_ms;
    uint16_t off_ms;
    uint32_t next_toggle;
    bool     led_on;
} blink_state;

// 启动音乐音符
static const uint16_t startup_notes[] = { 523, 659, 784, 1047 };
static const uint16_t startup_durations[] = { 80, 80, 80, 200 };
static const uint8_t  startup_note_count = 4;

/* =========================================================================
 * 初始化
 * ========================================================================= */

void hal_init(void)
{
    // --- GPIO 初始化 ---
    pinMode(PIN_MOTOR_IN1, OUTPUT);
    pinMode(PIN_MOTOR_IN2, OUTPUT);
    pinMode(PIN_MOTOR_IN3, OUTPUT);
    pinMode(PIN_MOTOR_IN4, OUTPUT);
    hal_motor_release();

#if defined(PLATFORM_NANO)
    pinMode(PIN_HALL_SENSOR, INPUT_PULLUP);
    pinMode(PIN_LIMIT_SWITCH, INPUT_PULLUP);
#elif defined(PLATFORM_ESP8266)
    pinMode(PIN_HALL_SENSOR, INPUT);      // 需外部 3.3V 上拉电阻
    // GPIO16 在 ESP8266 上无内部上拉，需外部上拉或使用常开触点
    pinMode(PIN_LIMIT_SWITCH, INPUT);
#endif

#if BUZZER_ENABLED
    pinMode(PIN_BUZZER, OUTPUT);
    hal_buzzer_set(false);
#endif

    pinMode(PIN_LED, OUTPUT);
    hal_led_set(false);

    // --- 串口 ---
    Serial.begin(SERIAL_BAUD);
    // 等待串口稳定（USB 枚举）
    delay(100);

    // --- EEPROM ---
#if defined(PLATFORM_ESP8266)
    EEPROM.begin(512);    // ESP8266 需显式指定 EEPROM 大小
#else
    EEPROM.begin();       // Nano 自动分配
#endif

    // --- 传感器状态 ---
    memset(&sensor_state, 0, sizeof(sensor_state));
    sensor_state.hall_raw   = hal_hall_read_raw();
    sensor_state.hall_stable = sensor_state.hall_raw;
    sensor_state.switch_raw  = hal_switch_read_raw();
    sensor_state.switch_stable = sensor_state.switch_raw;

    // --- 平台特定 ---
    hal_platform_init();
}

/* =========================================================================
 * GPIO / 电机
 * ========================================================================= */

void hal_motor_write(uint8_t phase_bits)
{
    digitalWrite(PIN_MOTOR_IN1, (phase_bits & 0x01) ? HIGH : LOW);
    digitalWrite(PIN_MOTOR_IN2, (phase_bits & 0x02) ? HIGH : LOW);
    digitalWrite(PIN_MOTOR_IN3, (phase_bits & 0x04) ? HIGH : LOW);
    digitalWrite(PIN_MOTOR_IN4, (phase_bits & 0x08) ? HIGH : LOW);
}

void hal_motor_release(void)
{
    hal_motor_write(0x00);
}

/* =========================================================================
 * 传感器
 * ========================================================================= */

bool hal_hall_read_raw(void)
{
    // 霍尔传感器：有磁铁时拉低（开集电极输出 + 上拉）
    return (digitalRead(PIN_HALL_SENSOR) == LOW);
}

bool hal_switch_read_raw(void)
{
    // 微动开关：按下时拉低
    return (digitalRead(PIN_LIMIT_SWITCH) == LOW);
}

bool hal_hall_read(void)
{
    return sensor_state.hall_stable;
}

bool hal_switch_read(void)
{
    return sensor_state.switch_stable;
}

void hal_sensors_update(void)
{
    uint32_t now = millis();

    // 霍尔传感器消抖
    bool hall_cur = hal_hall_read_raw();
    if (hall_cur != sensor_state.hall_raw) {
        sensor_state.hall_raw = hall_cur;
        sensor_state.hall_last_change = now;
    }
    if ((now - sensor_state.hall_last_change) >= HALL_DEBOUNCE_MS) {
        sensor_state.hall_stable = sensor_state.hall_raw;
    }

    // 微动开关消抖
    bool sw_cur = hal_switch_read_raw();
    if (sw_cur != sensor_state.switch_raw) {
        sensor_state.switch_raw = sw_cur;
        sensor_state.switch_last_change = now;
    }
    if ((now - sensor_state.switch_last_change) >= SWITCH_DEBOUNCE_MS) {
        sensor_state.switch_stable = sensor_state.switch_raw;
    }
}

/* =========================================================================
 * 蜂鸣器
 * ========================================================================= */

void hal_buzzer_set(bool on)
{
#if BUZZER_ENABLED
    digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
#else
    (void)on;
#endif
}

void hal_buzzer_beep(uint16_t duration_ms)
{
#if BUZZER_ENABLED
    hal_buzzer_set(true);
    delay(duration_ms);
    hal_buzzer_set(false);
#else
    (void)duration_ms;
#endif
}

void hal_buzzer_play_startup(void)
{
#if BUZZER_ENABLED
    for (uint8_t i = 0; i < startup_note_count; i++) {
        uint16_t freq = startup_notes[i];
        uint16_t duration = startup_durations[i];
        uint32_t period_us = 1000000UL / freq / 2;

        uint32_t end = micros() + (uint32_t)duration * 1000UL;
        while (micros() < end) {
            hal_buzzer_set(true);
            delayMicroseconds(period_us);
            hal_buzzer_set(false);
            delayMicroseconds(period_us);
        }
        delay(20);
    }
#endif
}

/* =========================================================================
 * LED
 * ========================================================================= */

void hal_led_set(bool on)
{
    digitalWrite(PIN_LED, on ? HIGH : LOW);
}

void hal_led_toggle(void)
{
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
}

void hal_led_blink_pattern(uint8_t times, uint16_t on_ms, uint16_t off_ms)
{
    blink_state.remaining = times;
    blink_state.on_ms     = on_ms;
    blink_state.off_ms    = off_ms;
    blink_state.next_toggle = millis() + on_ms;
    blink_state.led_on    = true;
    hal_led_set(true);
}

static void hal_led_tick(void)
{
    if (blink_state.remaining == 0) return;

    uint32_t now = millis();
    if ((int32_t)(now - blink_state.next_toggle) >= 0) {
        if (blink_state.led_on) {
            hal_led_set(false);
            blink_state.led_on = false;
            blink_state.next_toggle = now + blink_state.off_ms;
            blink_state.remaining--;
        } else {
            if (blink_state.remaining > 0) {
                hal_led_set(true);
                blink_state.led_on = true;
                blink_state.next_toggle = now + blink_state.on_ms;
            }
        }
    }
}

/* =========================================================================
 * 定时器
 * ========================================================================= */

uint32_t hal_millis(void)
{
    return millis();
}

void hal_delay_us(uint16_t us)
{
    delayMicroseconds(us);
}

void hal_delay_ms(uint16_t ms)
{
    delay(ms);
}

#if defined(PLATFORM_NANO)
// --- Arduino Nano: 使用 Timer2 (8-bit) 产生步进脉冲 ---
// Timer2 用于 CTC 模式，OCR2A 为比较值

static void (*nano_step_cb)(void) = nullptr;

void hal_timer_set_step_callback(void (*callback)(void), uint16_t interval_us)
{
    nano_step_cb = callback;

    if (callback == nullptr || interval_us == 0) {
        hal_timer_stop_step();
        return;
    }

    cli();

    // Timer2 CTC 模式, 预分频 64
    // f_CPU = 16 MHz, 预分频后 = 250 kHz, 周期 = 4 us
    // OCR2A = interval_us / 4 - 1
    uint32_t ocr = (uint32_t)interval_us * 250UL / 1000UL;
    if (ocr > 255) ocr = 255;
    if (ocr < 1)  ocr = 1;

    TCCR2A = (1 << WGM21);              // CTC mode
    TCCR2B = (1 << CS22);               // prescaler 64
    OCR2A  = (uint8_t)(ocr - 1);
    TIMSK2 |= (1 << OCIE2A);            // 使能比较中断

    sei();
}

void hal_timer_stop_step(void)
{
    TIMSK2 &= ~(1 << OCIE2A);
    TCCR2B = 0;  // 停止定时器
    nano_step_cb = nullptr;
}

// Timer2 比较中断
ISR(TIMER2_COMPA_vect)
{
    if (nano_step_cb) {
        nano_step_cb();
    }
}

// --- Arduino Nano: 使用 Timer1 (16-bit) 产生 1ms tick ---
static void (*nano_tick_cb)(void) = nullptr;

void hal_timer_set_tick_callback(void (*callback)(void))
{
    nano_tick_cb = callback;

    if (callback == nullptr) return;

    cli();

    // Timer1 CTC 模式, 预分频 64
    // OCR1A = 16000000 / 64 / 1000 - 1 = 249
    TCCR1A = 0;
    TCCR1B = (1 << WGM12) | (1 << CS11) | (1 << CS10);  // CTC, prescaler 64
    OCR1A  = 249;
    TIMSK1 |= (1 << OCIE1A);

    sei();
}

ISR(TIMER1_COMPA_vect)
{
    if (nano_tick_cb) {
        nano_tick_cb();
    }
}

#elif defined(PLATFORM_ESP8266)
// --- ESP8266: 使用 Ticker 或 os_timer ---
// 注意 ESP8266 的非精确定时限制；步进定时器使用硬件定时器

#include <Ticker.h>

static Ticker step_ticker;
static Ticker tick_ticker_static;
static void (*esp_step_cb)(void) = nullptr;

void hal_timer_set_step_callback(void (*callback)(void), uint16_t interval_us)
{
    esp_step_cb = callback;
    step_ticker.detach();

    if (callback == nullptr || interval_us == 0) return;

    // ESP8266 Ticker 支持微秒级
    step_ticker.attach_us(interval_us, callback);
}

void hal_timer_stop_step(void)
{
    step_ticker.detach();
    esp_step_cb = nullptr;
}

void hal_timer_set_tick_callback(void (*callback)(void))
{
    tick_ticker_static.detach();
    if (callback != nullptr) {
        tick_ticker_static.attach_ms(1, callback);
    }
}

#endif // PLATFORM_NANO / PLATFORM_ESP8266

/* =========================================================================
 * EEPROM
 * ========================================================================= */

uint8_t hal_eeprom_read_byte(uint16_t addr)
{
    return EEPROM.read(addr);
}

void hal_eeprom_write_byte(uint16_t addr, uint8_t value)
{
    EEPROM.write(addr, value);
}

uint16_t hal_eeprom_read_word(uint16_t addr)
{
    uint16_t val;
    val  = (uint16_t)EEPROM.read(addr) << 0;
    val |= (uint16_t)EEPROM.read(addr + 1) << 8;
    return val;
}

void hal_eeprom_write_word(uint16_t addr, uint16_t value)
{
    EEPROM.write(addr, (uint8_t)(value & 0xFF));
    EEPROM.write(addr + 1, (uint8_t)((value >> 8) & 0xFF));
}

void hal_eeprom_commit(void)
{
#if defined(PLATFORM_ESP8266)
    EEPROM.commit();
#endif
    // Nano 的 EEPROM 立即写入，无需 commit
}

/* =========================================================================
 * 串口
 * ========================================================================= */

int16_t hal_serial_available(void)
{
    return (int16_t)Serial.available();
}

uint8_t hal_serial_read(void)
{
    while (!Serial.available()) { /* 等待 */ }
    return (uint8_t)Serial.read();
}

bool hal_serial_read_nb(uint8_t* ch)
{
    if (Serial.available()) {
        *ch = (uint8_t)Serial.read();
        return true;
    }
    return false;
}

void hal_serial_write(uint8_t ch)
{
    Serial.write(ch);
}

void hal_serial_print(const char* str)
{
    Serial.print(str);
}

void hal_serial_println(const char* str)
{
    Serial.println(str);
}

void hal_serial_flush(void)
{
    Serial.flush();
}

/* =========================================================================
 * 看门狗
 * ========================================================================= */

void hal_wdt_enable(uint8_t timeout_sec)
{
#if defined(PLATFORM_NANO)
    if (timeout_sec == 0) {
        wdt_disable();
    } else {
        wdt_enable((uint8_t)(timeout_sec <= 2 ? WDTO_2S :
                    timeout_sec <= 4 ? WDTO_4S :
                    WDTO_8S));
    }
#elif defined(PLATFORM_ESP8266)
    // ESP8266 使用软件看门狗，由 SDK 自动管理
    // 可在这里配置 yield 超时
    (void)timeout_sec;
#endif
}

void hal_wdt_reset(void)
{
#if defined(PLATFORM_NANO)
    wdt_reset();
#elif defined(PLATFORM_ESP8266)
    yield();  // ESP8266 在 yield 中喂狗
#endif
}

/* =========================================================================
 * 平台特定
 * ========================================================================= */

void hal_platform_init(void)
{
#if defined(PLATFORM_ESP8266)
    // ESP8266 网络栈初始化在此进行
    // 由 esp8266/main.cpp 实现
#endif
}

void hal_platform_yield(void)
{
#if defined(PLATFORM_ESP8266)
    yield();  // 让出 CPU 给 WiFi 栈
#endif
    // Nano 不需要特殊 yield
}

void hal_system_reset(void)
{
#if defined(PLATFORM_NANO)
    wdt_enable(WDTO_15MS);
    while (1) { /* 等待看门狗复位 */ }
#elif defined(PLATFORM_ESP8266)
    ESP.restart();
#endif
}

/* =========================================================================
 * 内部 tick 处理（在主循环中调用或定时器驱动）
 * ========================================================================= */

/// 需要在 tick 中周期调用，处理 LED 闪烁、传感器更新等
void hal_tick_process(void)
{
    hal_sensors_update();
    hal_led_tick();
}
