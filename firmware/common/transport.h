/**
 * @file transport.h
 * @brief 通信传输层
 *
 * 实现基于换行分隔的 ASCII 串口命令协议。
 * 命令格式: CMD [args]\n
 * 响应格式: OK [data]\n 或 ERR <code> <message>\n
 *
 * 同时为 ESP8266 预留 HTTP/Alpaca 传输接口。
 */

#ifndef TRANSPORT_H
#define TRANSPORT_H

#include "config.h"
#include <stdint.h>

/* =========================================================================
 * 输出模式
 * ========================================================================= */
typedef enum {
    TRANSPORT_OUTPUT_SERIAL = 0,  ///< 输出到硬件串口
    TRANSPORT_OUTPUT_BUFFER = 1,  ///< 输出到内存缓冲区 (HTTP/Alpaca)
} transport_output_mode_t;

/* =========================================================================
 * 初始化
 * ========================================================================= */

/// 初始化传输层
void transport_init(void);

/// 在后台循环中调用，处理输入命令 (默认串口模式)
void transport_tick(void);

/* =========================================================================
 * 输出模式切换
 * ========================================================================= */

/// 设置输出模式和目标缓冲区
/// @param mode  输出模式
/// @param buf   缓冲区指针 (仅 BUFFER 模式需要)
/// @param size  缓冲区大小 (含 null terminator)
void transport_set_output(transport_output_mode_t mode, char* buf, uint16_t size);

/// 执行一条命令字符串
/// 在 BUFFER 模式下结果写入传输缓冲区; SERIAL 模式下写入串口
/// @param cmd  输入命令 (不含换行符)
void transport_execute(const char* cmd);

/* =========================================================================
 * HTTP/Alpaca 接口（ESP8266 使用）
 * ========================================================================= */

/// 获取设备信息 JSON
void transport_get_device_info_json(char* buf, uint16_t bufsz);

/// 获取状态 JSON
void transport_get_state_json(char* buf, uint16_t bufsz);

/// 执行 Alpaca 风格的 PUT 请求
/// @return 响应字符串
const char* transport_alpaca_put(const char* path, const char* body);

#endif // TRANSPORT_H
