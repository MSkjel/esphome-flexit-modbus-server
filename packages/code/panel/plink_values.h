// Panel settings and unit values, known to both ends by key. Whatever the ESP adds itself is an item, see
// plink_keys.h. One X() per value:
//
//   X(id, key, field, min, max, flags, cu60, scale)
//
//   id     V_<id> on the panel. The ESP's name for it is the id in lower case
//   key    message key, also what it's saved under in the panel's flash
//   field  where the panel keeps it, the ESP ignores it
//   flags  VF_* below
//   cu60   CU60 register that holds it, for VF_CU60. scale = register value per unit
#pragma once
#include "plink_keys.h"

#define VF_SAVED 0x01      // kept in the panel's flash
#define VF_CU60 0x02       // lives in the CU60, written over the bus
#define VF_MASTER 0x04     // the CU60 only takes it from the lowest panel address, other panels send it via the ESP
#define VF_ESP 0x08        // an ESP item. A change on the panel is only a request, the ESP answers with the new state
#define VF_LINK_R 0x10     // the ESP is told when it changes
#define VF_LINK_W 0x20     // the ESP may set it
#define VF_RO 0x40         // shown but never edited on the panel
#define VF_PER_PANEL 0x80  // one register per panel address, cu60 + address - 1
#define VF_CMD 0x100       // command register, written but never read back. The CU60 broadcasts it as 0
#define VF_LAYOUT 0x200    // the screen is rebuilt when it changes
#define VF_CLOCK 0x400     // the panel's clock, kept by the RTC
#define VF_LINK_RW (VF_LINK_R | VF_LINK_W)
#define VF_PANEL (VF_SAVED | VF_LINK_RW)  // a panel setting shared with the ESP

// What the numbers mean
#define FW_TYPE 0x4349  // input register 0 at discovery, 'CI': this firmware. Stock leaves it 0
#define N_THEMES 3
#define N_LANGS 2     // English, Norwegian (ui/lang.c)
#define IDLE_STEPS 8  // IDLE_AFTER steps, 15 s .. 30 min, then never. See idle_seconds()
enum { MODE_STOP, MODE_MIN, MODE_NORMAL, MODE_MAX, MODE_COUNT };
enum { REG_EXTRACT, REG_SUPPLY, REG_COUNT };          // which temperature the setpoint is for, CU60 0x114
enum { INDOOR_EXTRACT, INDOOR_PANEL, INDOOR_COUNT };  // where the indoor temperature comes from
enum { SAVER_OFF, SAVER_CLOCK, SAVER_CLOCK_TEMPS, SAVER_PIC, SAVER_PIC_CLOCK, SAVER_PIC_ALL, SAVER_COUNT };
// What a dashboard tile shows: one of the unit's values, or TILE_ITEM + the id of an ESP item
enum {
  TILE_OUTDOOR,
  TILE_SUPPLY,
  TILE_EXTRACT,
  TILE_ROOM,
  TILE_ROTOR,
  TILE_FILTER,
  TILE_HEATER,
  TILE_RECOVERY,
  TILE_UNIT
};
#define TILE_ITEM 0x4000
#define TILE_MAX (TILE_ITEM + 0x3FFF)
// LEDS: 2 bits per LED, each an LED_* value. Alarm in the lowest bits, then filter, then OK. Not saved, the ESP
// sends it again after a hello.
enum { LED_AUTO, LED_OFF, LED_ON, LED_BLINK };

// clang-format off: a table, laid out by hand
#define PLINK_VALUES(X) \
    /* display */ \
    X(THEME,           0x010, M.theme,              0, N_THEMES - 1, VF_PANEL | VF_LAYOUT, 0, 1) \
    X(BRIGHTNESS,      0x011, M.brightness,         0, 100,  VF_PANEL, 0, 1) \
    X(IDLE_BRIGHTNESS, 0x012, M.idle_brightness,    0, 100,  VF_PANEL, 0, 1) /* 0 = off */ \
    X(IDLE_AFTER,      0x013, M.idle_after,         0, IDLE_STEPS - 1, VF_PANEL, 0, 1) \
    X(SCREENSAVER,     0x014, M.screensaver,        0, SAVER_COUNT - 1, VF_PANEL | VF_LAYOUT, 0, 1) \
    X(HOME_VIEW,       0x015, M.home_view,          0, VIEW_COUNT - 1, VF_PANEL, 0, 1) \
    X(LANGUAGE,        0x016, M.language,           0, N_LANGS - 1, VF_PANEL | VF_LAYOUT, 0, 1) \
    X(CLOCK_24H,       0x017, M.clock_24h,          0, 1,    VF_PANEL, 0, 1) \
    X(KEY_HINTS,       0x018, M.key_hints,          0, 1,    VF_PANEL | VF_LAYOUT, 0, 1) \
    /* night */ \
    X(NIGHT,           0x020, M.night,              0, 1,    VF_PANEL, 0, 1) \
    X(NIGHT_FROM,      0x021, M.night_from,         0, 23,   VF_PANEL, 0, 1) \
    X(NIGHT_TO,        0x022, M.night_to,           0, 23,   VF_PANEL, 0, 1) \
    X(NIGHT_BRIGHTNESS, 0x023, M.night_brightness,  0, 100,  VF_PANEL, 0, 1) \
    X(NIGHT_IDLE_BRIGHTNESS, 0x024, M.night_idle_brightness, 0, 100, VF_PANEL, 0, 1) /* 0 = off */ \
    /* dashboard tiles */ \
    X(TILE1,           0x028, M.tiles[0],           0, TILE_MAX, VF_PANEL | VF_LAYOUT, 0, 1) \
    X(TILE2,           0x029, M.tiles[1],           0, TILE_MAX, VF_PANEL | VF_LAYOUT, 0, 1) \
    X(TILE3,           0x02A, M.tiles[2],           0, TILE_MAX, VF_PANEL | VF_LAYOUT, 0, 1) \
    X(TILE4,           0x02B, M.tiles[3],           0, TILE_MAX, VF_PANEL | VF_LAYOUT, 0, 1) \
    /* the panel */ \
    X(BUS_ADDR,        0x030, M.bus_addr,           1, 3,    VF_SAVED | VF_LINK_R, 0, 1) \
    X(KEY_LOCK,        0x031, M.key_lock,           0, 1,    VF_PANEL, 0, 1) \
    X(LEDS,            0x032, M.leds,               0, 0xFFFF, VF_LINK_W | VF_RO, 0, 1) \
    X(ROOM_OFFSET,     0x033, M.room_offset,      -90, 90,   VF_PANEL, 0, 1) \
    X(INDOOR_SOURCE,   0x034, M.indoor_source,      0, INDOOR_COUNT - 1, VF_PANEL, 0, 1) \
    X(ROOM_TEMP,       0x035, M.t_room,        -32768, 1000, VF_RO | VF_LINK_R, 0, 1) /* 0.1 C, INT16_MIN = none */ \
    X(FW_VERSION,      0x036, M.fw_version,         0, 0xFFFFFF, VF_RO | VF_LINK_R, 0, 1) /* maj<<16 | min<<8 | patch */ \
    /* the unit */ \
    X(MODE,            0x040, M.mode,               0, MODE_MAX,    VF_CU60 | VF_CMD | VF_LINK_RW, 0x000, 1) \
    X(BOOST,           0x041, M.boost_on,           0, 1,    VF_CU60 | VF_CMD | VF_LINK_RW, 0, 1) /* commands 0x13A/0x13B */ \
    X(BOOST_MINUTES,   0x042, M.boost_minutes,      0, 360,  VF_CU60 | VF_LINK_RW, 0x091, 1) \
    X(SETPOINT,        0x043, M.setpoint,         100, 300,  VF_CU60 | VF_MASTER | VF_LINK_RW, 0x00C, 1) \
    X(SUPPLY_MIN,      0x044, M.supply_min,        50, 250,  VF_CU60 | VF_LINK_RW, 0x00D, 1) \
    X(SUPPLY_MAX,      0x045, M.supply_max,       150, 450,  VF_CU60 | VF_LINK_RW, 0x00E, 1) \
    X(REGULATION,      0x046, M.regulation,         0, REG_COUNT - 1, VF_CU60 | VF_LINK_RW, 0x114, 1) \
    X(HEATER,          0x047, M.heater_enabled,     0, 1,    VF_CU60 | VF_PER_PANEL | VF_LINK_RW, 0x13F, 1) \
    X(COOLING,         0x048, M.cooling_enabled,    0, 1,    VF_CU60 | VF_LINK_RW, 0x115, 1) \
    X(FAN_MIN_SUP,     0x049, M.fan_pct[0][0],     20, 100,  VF_CU60 | VF_LINK_RW, 0x002, 1) \
    X(FAN_NORM_SUP,    0x04A, M.fan_pct[1][0],     20, 100,  VF_CU60 | VF_MASTER | VF_LINK_RW, 0x003, 1) \
    X(FAN_MAX_SUP,     0x04B, M.fan_pct[2][0],     20, 100,  VF_CU60 | VF_LINK_RW, 0x004, 1) \
    X(FAN_MIN_EXT,     0x04C, M.fan_pct[0][1],     20, 100,  VF_CU60 | VF_LINK_RW, 0x007, 1) \
    X(FAN_NORM_EXT,    0x04D, M.fan_pct[1][1],     20, 100,  VF_CU60 | VF_MASTER | VF_LINK_RW, 0x008, 1) \
    X(FAN_MAX_EXT,     0x04E, M.fan_pct[2][1],     20, 100,  VF_CU60 | VF_LINK_RW, 0x009, 1) \
    X(FILTER_INTERVAL, 0x04F, M.filter_days,       30, 360,  VF_CU60 | VF_LINK_RW, 0x016, 1) /* days */ \
    X(WEEK_PROGRAM,    0x050, M.week_program,       0, 1,    VF_CU60 | VF_LINK_RW, 0x117, 1)
// clang-format on

// K_<id>: each value's key by name
#define K_ENUM(id, key, ...) K_##id = key,
enum { PLINK_VALUES(K_ENUM) };
#undef K_ENUM
