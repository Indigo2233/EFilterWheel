/**
 * @file eeprom_config.cpp
 * @brief EEPROM 配置存储实现
 */

#include "eeprom_config.h"
#include "hal.h"
#include <string.h>

/* =========================================================================
 * 内部辅助
 * ========================================================================= */

/// 计算配置数据的简单校验和（XOR）
static uint8_t calc_checksum(const wheel_config_t* cfg)
{
    const uint8_t* ptr = (const uint8_t*)cfg;
    uint8_t sum = 0x55;
    for (uint16_t i = 0; i < sizeof(wheel_config_t); i++) {
        sum ^= ptr[i];
    }
    return sum;
}

/* =========================================================================
 * 公共 API
 * ========================================================================= */

void eeprom_config_init(void)
{
    // EEPROM 在 hal_init 中已初始化
    // 这里预留扩展空间
}

bool eeprom_config_is_valid(void)
{
    uint16_t magic = hal_eeprom_read_word(EEPROM_MAGIC_ADDR);
    return (magic == EEPROM_MAGIC);
}

void eeprom_config_defaults(wheel_config_t* cfg)
{
    memset(cfg, 0, sizeof(wheel_config_t));

    cfg->num_slots   = DEFAULT_SLOTS;
    cfg->speed_pps   = DEFAULT_SPEED_PPS;
    cfg->default_dir = DIR_CW;
    cfg->invert_dir  = false;
    cfg->home_offset = 0;

    // 均匀分布槽位步数（后续由标定程序填充精确值）
    // 假设一圈的总步数约为 4096（28BYJ-48: 64*63.7≈4076）
    uint16_t full_circle = 4096;
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        cfg->slot_steps[i]   = (uint32_t)full_circle * i / cfg->num_slots;
        cfg->focus_offsets[i] = 0;
        snprintf(cfg->slot_names[i], sizeof(cfg->slot_names[0]),
                 "Slot %d", i + 1);
    }
}

bool eeprom_config_load(wheel_config_t* cfg)
{
    if (!eeprom_config_is_valid()) {
        eeprom_config_defaults(cfg);
        return false;
    }

    // 校验版本
    uint8_t stored_version = hal_eeprom_read_byte(EEPROM_VERSION_ADDR);
    uint8_t stored_checksum = hal_eeprom_read_byte(EEPROM_CHECKSUM_ADDR);
    (void)stored_version;  // 保留用于未来版本迁移

    // 从 EEPROM 读出原始字节
    uint8_t* raw = (uint8_t*)cfg;
    uint16_t addr = EEPROM_SLOTS_ADDR;
    for (uint16_t i = 0; i < sizeof(wheel_config_t); i++) {
        raw[i] = hal_eeprom_read_byte(addr + i);
    }

    // 校验和验证
    uint8_t computed = calc_checksum(cfg);
    if (computed != stored_checksum) {
        eeprom_config_defaults(cfg);
        return false;
    }

    // 范围检查
    if (cfg->num_slots < MIN_SLOTS || cfg->num_slots > MAX_SLOTS) {
        eeprom_config_defaults(cfg);
        return false;
    }

    if (cfg->speed_pps < MIN_SPEED_PPS || cfg->speed_pps > MAX_SPEED_PPS) {
        cfg->speed_pps = DEFAULT_SPEED_PPS;
    }

    return true;
}

bool eeprom_config_save(const wheel_config_t* cfg)
{
    if (!cfg) return false;

    // 写入魔数
    hal_eeprom_write_word(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
    hal_eeprom_write_byte(EEPROM_VERSION_ADDR, 1);  // 配置版本 1
    hal_eeprom_write_byte(EEPROM_CHECKSUM_ADDR, calc_checksum(cfg));

    // 写入配置数据
    const uint8_t* raw = (const uint8_t*)cfg;
    uint16_t addr = EEPROM_SLOTS_ADDR;
    for (uint16_t i = 0; i < sizeof(wheel_config_t); i++) {
        hal_eeprom_write_byte(addr + i, raw[i]);
    }

    hal_eeprom_commit();
    return true;
}

void eeprom_config_factory_reset(void)
{
    // 擦除魔数标记，下次加载时自动使用默认值
    hal_eeprom_write_word(EEPROM_MAGIC_ADDR, 0x0000);
    hal_eeprom_commit();
}

uint16_t eeprom_config_get_slot_steps(uint8_t slot)
{
    if (slot >= MAX_SLOTS) return 0;
    uint16_t addr = EEPROM_SLOT_STEPS + slot * 2;
    return hal_eeprom_read_word(addr);
}

void eeprom_config_set_slot_steps(uint8_t slot, uint16_t steps)
{
    if (slot >= MAX_SLOTS) return;
    uint16_t addr = EEPROM_SLOT_STEPS + slot * 2;
    hal_eeprom_write_word(addr, steps);
}

uint16_t eeprom_config_get_full_circle_steps(void)
{
    // 从 EEPROM 加载配置获取
    wheel_config_t cfg;
    if (eeprom_config_load(&cfg)) {
        if (cfg.num_slots > 0) {
            // 槽位 0 到槽位 0 即一整圈
            // 最后一个槽位步数 + (一圈步数 - 最后一个槽位步数)
            // 简化：如果标定过，从 slot_steps 推算
            return cfg.slot_steps[cfg.num_slots - 1] * cfg.num_slots
                   / (cfg.num_slots - 1);
        }
    }
    return 4096;  // 默认 28BYJ-48 一圈步数
}
