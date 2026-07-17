/**
 * @file eeprom_config.h
 * @brief EEPROM 配置存储管理
 *
 * 负责滤镜轮标定参数的持久化存储，包括魔数校验、版本管理、
 * 槽位步数表、速度、方向等配置的读写和校验。
 */

#ifndef EEPROM_CONFIG_H
#define EEPROM_CONFIG_H

#include "config.h"

/* =========================================================================
 * 配置数据结构
 * ========================================================================= */
typedef struct {
    uint8_t  num_slots;                          ///< 槽位数 (5 或 7)
    uint16_t speed_pps;                          ///< 默认速度
    direction_t default_dir;                     ///< 默认方向
    bool     invert_dir;                         ///< 方向反转
    uint16_t home_offset;                        ///< 原点偏移 (steps)
    uint16_t slot_steps[MAX_SLOTS];              ///< 每个槽位距原点的步数
    int16_t  focus_offsets[MAX_SLOTS];           ///< 每个槽位的对焦偏移 (ticks)
    char     slot_names[MAX_SLOTS][16];          ///< 槽位滤镜名称
} wheel_config_t;

/* =========================================================================
 * API
 * ========================================================================= */

/// 初始化 EEPROM 配置系统
void eeprom_config_init(void);

/// 加载配置，返回 true 表示成功
bool eeprom_config_load(wheel_config_t* cfg);

/// 保存配置（包含魔数、版本和校验）
bool eeprom_config_save(const wheel_config_t* cfg);

/// 检查 EEPROM 中是否有有效配置
bool eeprom_config_is_valid(void);

/// 加载默认配置
void eeprom_config_defaults(wheel_config_t* cfg);

/// 重置 EEPROM 为出厂状态
void eeprom_config_factory_reset(void);

/// 获取当前槽位步数（origin 到 slot n 的总步数）
uint16_t eeprom_config_get_slot_steps(uint8_t slot);

/// 设置某个槽位的步数
void eeprom_config_set_slot_steps(uint8_t slot, uint16_t steps);

/// 计算完整一圈的总步数
uint16_t eeprom_config_get_full_circle_steps(void);

#endif // EEPROM_CONFIG_H
