/**
 * @file transport.cpp
 * @brief 串口命令协议实现 (支持 Serial 和 Buffer 双模输出)
 *
 * Serial 模式: respond() → hal_serial_println() → UART
 * Buffer 模式: respond() → 追加到内部环形缓冲 → HTTP 响应
 */

#include "transport.h"
#include "hal.h"
#include "wheel.h"
#include "eeprom_config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* =========================================================================
 * 缓冲常量
 * ========================================================================= */
#define CMD_BUF_SIZE      64
#define RESP_BUF_SIZE     256   ///< 单条响应上限
#define OUTPUT_BUF_SIZE   1024  ///< HTTP 输出累积缓冲

static char cmd_buf[CMD_BUF_SIZE];
static uint8_t cmd_len = 0;

/* =========================================================================
 * 输出路由状态
 * ========================================================================= */
static transport_output_mode_t output_mode = TRANSPORT_OUTPUT_SERIAL;
static char* output_buf    = nullptr;
static uint16_t output_size = 0;
static uint16_t output_pos  = 0;

/* =========================================================================
 * 内部辅助
 * ========================================================================= */

/// 跳过前导空白
static const char* skip_ws(const char* s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/// 解析命令名
static command_t parse_command(const char* s)
{
    if (strncmp(s, "ID?", 3) == 0 || strncmp(s, "ID", 2) == 0)      return CMD_ID;
    if (strncmp(s, "STATE?", 6) == 0 || strncmp(s, "STATE", 5) == 0) return CMD_STATE;
    if (strncmp(s, "POS?", 4) == 0 || strncmp(s, "POS", 3) == 0)    return CMD_POS;
    if (strncmp(s, "HOME", 4) == 0)   return CMD_HOME;
    if (strncmp(s, "GOTO", 4) == 0)   return CMD_GOTO;
    if (strncmp(s, "STOP", 4) == 0)   return CMD_STOP;
    if (strncmp(s, "SLOTS", 5) == 0)  return CMD_SLOTS;
    if (strncmp(s, "CAL", 3) == 0)    return CMD_CAL;
    if (strncmp(s, "SAVE", 4) == 0)   return CMD_SAVE;
    if (strncmp(s, "RESET", 5) == 0)  return CMD_RESET;
    if (strncmp(s, "HELP", 4) == 0 || strncmp(s, "?", 1) == 0) return CMD_HELP;
    return CMD_UNKNOWN;
}

/// 输出一行响应 (根据当前模式路由到串口或缓冲)
static void respond(const char* msg)
{
    if (output_mode == TRANSPORT_OUTPUT_BUFFER && output_buf && output_size > 0) {
        // Buffer 模式: 追加到累积缓冲
        uint16_t remain = output_size - output_pos - 1;  // 留 1 字节给 \0
        uint16_t len = (uint16_t)strlen(msg);
        if (len > remain) len = remain;
        if (len > 0) {
            memcpy(output_buf + output_pos, msg, len);
            output_pos += len;
            // 追加换行
            if (output_pos < output_size - 1) {
                output_buf[output_pos++] = '\n';
            }
            output_buf[output_pos] = '\0';
        }
    } else {
        // Serial 模式: 直接写串口
        hal_serial_println(msg);
    }
}

/// 格式化错误响应
static void respond_error(error_code_t code, const char* detail)
{
    char buf[RESP_BUF_SIZE];
    if (detail && detail[0]) {
        snprintf(buf, sizeof(buf), "ERR %d %s", (int)code, detail);
    } else {
        snprintf(buf, sizeof(buf), "ERR %d", (int)code);
    }
    respond(buf);
}

/* =========================================================================
 * 命令处理函数
 * ========================================================================= */

static void handle_cmd_id(void)
{
    char buf[RESP_BUF_SIZE];
    snprintf(buf, sizeof(buf), "OK %s FW:%s PROTO:%s",
             DEVICE_NAME, FW_VERSION_STRING, PROTOCOL_VERSION);
    respond(buf);
}

static void handle_cmd_state(void)
{
    char buf[RESP_BUF_SIZE];
    snprintf(buf, sizeof(buf), "OK %s ERR:%d",
             wheel_get_state_string(), (int)wheel_get_error());
    respond(buf);
}

static void handle_cmd_pos(void)
{
    char buf[RESP_BUF_SIZE];
    int8_t slot = wheel_get_current_slot();
    snprintf(buf, sizeof(buf), "OK %d", (int)slot);
    respond(buf);
}

static void handle_cmd_home(void)
{
    if (wheel_is_moving()) {
        respond_error(ERR_BUSY, "Device is moving");
        return;
    }
    wheel_start_homing();
    // 回零完成由 wheel_tick 异步发送 OK HOMED
}

static void handle_cmd_goto(const char* args)
{
    if (!wheel_is_ready()) {
        respond_error(ERR_NOT_READY, "Device not ready; home first");
        return;
    }
    if (wheel_is_moving()) {
        respond_error(ERR_BUSY, "Already moving");
        return;
    }

    args = skip_ws(args);
    int slot = atoi(args);

    if (slot < 0) {
        respond_error(ERR_BAD_ARG, "Slot must be >= 0");
        return;
    }

    if (!wheel_goto_slot((uint8_t)slot)) {
        respond_error(ERR_OUT_OF_RANGE, "Slot out of range");
        return;
    }
    // wheel_goto_slot 成功时内部已响应 OK GOTO n
}

static void handle_cmd_stop(void)
{
    wheel_stop();
    respond("OK STOPPED");
}

static void handle_cmd_slots(const char* args)
{
    if (wheel_is_moving()) {
        respond_error(ERR_BUSY, "Cannot change slots while moving");
        return;
    }

    args = skip_ws(args);
    int n = atoi(args);

    if (wheel_set_slots((uint8_t)n)) {
        char buf[RESP_BUF_SIZE];
        snprintf(buf, sizeof(buf), "OK SLOTS=%d", n);
        respond(buf);
    } else {
        respond_error(ERR_BAD_ARG, "Slots must be 5 or 7");
    }
}

static void handle_cmd_cal(const char* args)
{
    args = skip_ws(args);

    if (*args == '?' || *args == '\0') {
        const wheel_config_t* cfg = wheel_get_config();
        char buf[RESP_BUF_SIZE];
        int pos = snprintf(buf, sizeof(buf),
                 "OK SLOTS:%d SPEED:%d DIR:%s INVERT:%d",
                 cfg->num_slots, cfg->speed_pps,
                 cfg->default_dir == DIR_CW ? "CW" : "CCW",
                 cfg->invert_dir ? 1 : 0);

        for (uint8_t i = 0; i < cfg->num_slots && pos < (int)(sizeof(buf) - 20); i++) {
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                          " S%d:%d", i, cfg->slot_steps[i]);
        }
        respond(buf);
        return;
    }

    // CAL SLOT n m
    if (strncmp(args, "SLOT", 4) == 0) {
        args = skip_ws(args + 4);
        int slot = atoi(args);
        args = skip_ws(args);
        while (*args >= '0' && *args <= '9') args++;
        args = skip_ws(args);
        int steps = atoi(args);

        if (slot < 0 || slot >= MAX_SLOTS) {
            respond_error(ERR_BAD_ARG, "Slot out of range");
            return;
        }

        wheel_config_t cfg_copy = *wheel_get_config();
        cfg_copy.slot_steps[slot] = (uint16_t)steps;
        wheel_update_config(&cfg_copy);

        char buf[64];
        snprintf(buf, sizeof(buf), "OK CAL SLOT %d = %d", slot, steps);
        respond(buf);
        return;
    }

    // CAL SPEED n
    if (strncmp(args, "SPEED", 5) == 0) {
        args = skip_ws(args + 5);
        int speed = atoi(args);
        if (speed < MIN_SPEED_PPS || speed > MAX_SPEED_PPS) {
            respond_error(ERR_BAD_ARG, "Speed out of range");
            return;
        }

        wheel_config_t cfg_copy = *wheel_get_config();
        cfg_copy.speed_pps = (uint16_t)speed;
        wheel_update_config(&cfg_copy);

        char buf[64];
        snprintf(buf, sizeof(buf), "OK CAL SPEED=%d", speed);
        respond(buf);
        return;
    }

    // CAL DIR CW|CCW
    if (strncmp(args, "DIR", 3) == 0) {
        args = skip_ws(args + 3);
        direction_t dir;
        if (strncmp(args, "CW", 2) == 0)       dir = DIR_CW;
        else if (strncmp(args, "CCW", 3) == 0)  dir = DIR_CCW;
        else {
            respond_error(ERR_BAD_ARG, "Dir must be CW or CCW");
            return;
        }

        wheel_config_t cfg_copy = *wheel_get_config();
        cfg_copy.default_dir = dir;
        wheel_update_config(&cfg_copy);

        respond(dir == DIR_CW ? "OK CAL DIR=CW" : "OK CAL DIR=CCW");
        return;
    }

    // CAL OFFSET n
    if (strncmp(args, "OFFSET", 6) == 0) {
        args = skip_ws(args + 6);
        int offset = atoi(args);
        wheel_config_t cfg_copy = *wheel_get_config();
        cfg_copy.home_offset = (uint16_t)offset;
        wheel_update_config(&cfg_copy);

        char buf[64];
        snprintf(buf, sizeof(buf), "OK CAL OFFSET=%d", offset);
        respond(buf);
        return;
    }

    respond_error(ERR_UNKNOWN_CMD, "CAL subcommand: SLOT, SPEED, DIR, OFFSET, ?");
}

static void handle_cmd_save(void)
{
    if (wheel_save_config()) {
        respond("OK SAVED");
    } else {
        respond_error(ERR_EEPROM_FAIL, "Failed to save configuration");
    }
}

static void handle_cmd_reset(void)
{
    respond("OK RESET - Rebooting...");
    hal_delay_ms(100);
    hal_system_reset();
}

static void handle_cmd_help(void)
{
    const char* lines[] = {
        "OK Commands:",
        "  ID?        - Device info",
        "  STATE?     - Device state",
        "  POS?       - Current slot (-1=moving, -2=not homed)",
        "  HOME       - Start homing",
        "  GOTO n     - Move to slot n (0-based)",
        "  STOP       - Emergency stop",
        "  SLOTS 5|7  - Set filter wheel slots",
        "  CAL ...    - Calibration (CAL? for details)",
        "  SAVE       - Save config to EEPROM",
        "  RESET      - Reboot device",
        "OK End of help",
    };
    for (uint8_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        respond(lines[i]);
    }
}

/* =========================================================================
 * 传输层 API
 * ========================================================================= */

void transport_init(void)
{
    cmd_len    = 0;
    output_mode = TRANSPORT_OUTPUT_SERIAL;
    output_buf  = nullptr;
    output_size = 0;
    output_pos  = 0;
    memset(cmd_buf, 0, sizeof(cmd_buf));
}

void transport_set_output(transport_output_mode_t mode, char* buf, uint16_t size)
{
    output_mode = mode;
    output_buf  = buf;
    output_size = size;
    output_pos  = 0;
    if (buf && size > 0) {
        buf[0] = '\0';
    }
}

void transport_tick(void)
{
    uint8_t ch;
    while (hal_serial_read_nb(&ch)) {
        if (ch == '\r') continue;

        if (ch == '\n') {
            if (cmd_len > 0) {
                cmd_buf[cmd_len] = '\0';
                transport_execute(cmd_buf);
                cmd_len = 0;
                memset(cmd_buf, 0, sizeof(cmd_buf));
            }
            continue;
        }

        if (ch == '\b' || ch == 127) {
            if (cmd_len > 0) {
                cmd_len--;
                cmd_buf[cmd_len] = '\0';
            }
            continue;
        }

        if (cmd_len < CMD_BUF_SIZE - 1) {
            cmd_buf[cmd_len++] = (char)ch;
        } else {
            cmd_len = 0;
            respond_error(ERR_BAD_ARG, "Command too long");
        }
    }
}

void transport_execute(const char* cmd)
{
    if (!cmd || cmd[0] == '\0') return;

    cmd = skip_ws(cmd);

    // 提取命令名
    const char* args = cmd;
    while (*args && *args != ' ' && *args != '\t') args++;

    char cmd_name[16];
    uint8_t name_len = (uint8_t)(args - cmd);
    if (name_len >= sizeof(cmd_name)) name_len = sizeof(cmd_name) - 1;
    memcpy(cmd_name, cmd, name_len);
    cmd_name[name_len] = '\0';

    command_t c = parse_command(cmd_name);

    switch (c) {
    case CMD_ID:     handle_cmd_id(); break;
    case CMD_STATE:  handle_cmd_state(); break;
    case CMD_POS:    handle_cmd_pos(); break;
    case CMD_HOME:   handle_cmd_home(); break;
    case CMD_GOTO:   handle_cmd_goto(args); break;
    case CMD_STOP:   handle_cmd_stop(); break;
    case CMD_SLOTS:  handle_cmd_slots(args); break;
    case CMD_CAL:    handle_cmd_cal(args); break;
    case CMD_SAVE:   handle_cmd_save(); break;
    case CMD_RESET:  handle_cmd_reset(); break;
    case CMD_HELP:   handle_cmd_help(); break;
    default:
        respond_error(ERR_UNKNOWN_CMD, cmd_name);
        break;
    }
}

/* =========================================================================
 * HTTP/Alpaca JSON 接口
 * ========================================================================= */

void transport_get_device_info_json(char* buf, uint16_t bufsz)
{
    snprintf(buf, bufsz,
        "{"
        "\"DeviceName\":\"" DEVICE_NAME "\","
        "\"FirmwareVersion\":\"" FW_VERSION_STRING "\","
        "\"ProtocolVersion\":\"" PROTOCOL_VERSION "\""
        "}");
}

void transport_get_state_json(char* buf, uint16_t bufsz)
{
    const char* state_str;
    switch (wheel_get_state()) {
    case STATE_BOOT:   state_str = "BOOT";   break;
    case STATE_HOMING: state_str = "HOMING"; break;
    case STATE_READY:  state_str = "READY";  break;
    case STATE_MOVING: state_str = "MOVING"; break;
    case STATE_ERROR:  state_str = "ERROR";  break;
    default:           state_str = "UNKNOWN"; break;
    }

    snprintf(buf, bufsz,
        "{"
        "\"State\":\"%s\","
        "\"Position\":%d,"
        "\"ErrorCode\":%d,"
        "\"ErrorMessage\":\"%s\""
        "}",
        state_str,
        (int)wheel_get_current_slot(),
        (int)wheel_get_error(),
        wheel_get_error_string());
}

const char* transport_alpaca_put(const char* path, const char* body)
{
    (void)path;
    (void)body;
    return "ERR NOT_IMPLEMENTED";
}

