// Panel settings, and the unit's settings the panel sends the ESP to write, known to both ends by key. Whatever the ESP
// adds itself is an item, see plink_keys.h. One X() per value, its field is in app/values.c:
//
//   X(id, key, min, max, flags, cx60)
//
//   id     V_<id> on the panel. The ESP's name for it is the id in lower case
//   key    message key, also what it's saved under in the panel's flash
//   flags  VF_* below
//   cx60   CX60 register that holds it, for VF_CX60
//
// Copied into esphome-flexit-modbus-server by make -C sim esp_sync, edit it here.
#pragma once
#include "plink_keys.h"

#define VF_SAVED 0x01      // kept in the panel's flash
#define VF_CX60 0x02       // lives in the CX60, written over the bus
#define VF_MASTER 0x04     // the CX60 only takes it from the lowest panel address, other panels send it via the ESP
#define VF_ESP 0x08        // an ESP item. A change on the panel is only a request, the ESP answers with the new state
#define VF_LINK_R 0x10     // the ESP is told when it changes
#define VF_LINK_W 0x20     // the ESP may set it
#define VF_RO 0x40         // shown but never edited on the panel
#define VF_PER_PANEL 0x80  // one register per panel address, cx60 + address - 1
#define VF_BY_HAND 0x100   // unit.c follows it on the bus itself, val_by_cx60() leaves it out
#define VF_LAYOUT 0x200    // the screen is rebuilt when it changes
#define VF_CLOCK 0x400     // the panel's clock, kept by the RTC
#define VF_ENUM 0x800      // one of a list of options, kept to min..max even from the CX60
#define VF_LINK_RW (VF_LINK_R | VF_LINK_W)
#define VF_PANEL (VF_SAVED | VF_LINK_RW)  // a panel setting shared with the ESP

// What the numbers mean
#define FW_TYPE 0x4349  // input register 0 at discovery, 'CI': this firmware. Stock leaves it 0
#define N_THEMES 3
#define N_LANGS 2     // English, Norwegian (ui/lang.c)
#define IDLE_STEPS 8  // IDLE_AFTER steps, 15 s .. 30 min, then never. See idle_seconds()
enum { MODE_STOP, MODE_MIN, MODE_NORMAL, MODE_MAX, MODE_COUNT };
enum { REG_EXTRACT, REG_SUPPLY, REG_COUNT };          // which temperature the setpoint is for, CX60 0x114
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
#define TILE_MAX (TILE_ITEM + ITEM_ID_MASK)
// LEDS: 2 bits per LED, each an LED_* value. Alarm in the lowest bits, then filter, then OK. Not saved, the ESP
// sends it again after a hello.
enum { LED_AUTO, LED_OFF, LED_ON, LED_BLINK };

// clang-format off: a table, laid out by hand
#define PLINK_VALUES(X) \
    /* display */ \
    X(THEME,           0x010,      0, N_THEMES - 1, VF_PANEL | VF_LAYOUT, 0) \
    X(BRIGHTNESS,      0x011,      0, 100,  VF_PANEL, 0) \
    X(IDLE_BRIGHTNESS, 0x012,      0, 100,  VF_PANEL, 0) /* 0 = off */ \
    X(IDLE_AFTER,      0x013,      0, IDLE_STEPS - 1, VF_PANEL, 0) \
    X(SCREENSAVER,     0x014,      0, SAVER_COUNT - 1, VF_PANEL | VF_LAYOUT, 0) \
    X(HOME_VIEW,       0x015,      0, VIEW_COUNT - 1, VF_PANEL, 0) \
    X(LANGUAGE,        0x016,      0, N_LANGS - 1, VF_PANEL | VF_LAYOUT, 0) \
    X(CLOCK_24H,       0x017,      0, 1,    VF_PANEL, 0) \
    X(KEY_HINTS,       0x018,      0, 1,    VF_PANEL | VF_LAYOUT, 0) \
    /* night */ \
    X(NIGHT,           0x020,      0, 1,    VF_PANEL, 0) \
    X(NIGHT_FROM,      0x021,      0, 23,   VF_PANEL, 0) \
    X(NIGHT_TO,        0x022,      0, 23,   VF_PANEL, 0) \
    X(NIGHT_BRIGHTNESS, 0x023,     0, 100,  VF_PANEL, 0) \
    X(NIGHT_IDLE_BRIGHTNESS, 0x024, 0, 100, VF_PANEL, 0) /* 0 = off */ \
    /* dashboard tiles */ \
    X(TILE1,           0x028,      0, TILE_MAX, VF_PANEL | VF_LAYOUT, 0) \
    X(TILE2,           0x029,      0, TILE_MAX, VF_PANEL | VF_LAYOUT, 0) \
    X(TILE3,           0x02A,      0, TILE_MAX, VF_PANEL | VF_LAYOUT, 0) \
    X(TILE4,           0x02B,      0, TILE_MAX, VF_PANEL | VF_LAYOUT, 0) \
    /* the panel */ \
    X(BUS_ADDR,        0x030,      1, 3,    VF_SAVED | VF_LINK_R, 0) \
    X(KEY_LOCK,        0x031,      0, 1,    VF_PANEL, 0) \
    X(LEDS,            0x032,      0, 0xFFFF, VF_LINK_W | VF_RO, 0) \
    X(ROOM_OFFSET,     0x033,    -90, 90,   VF_PANEL, 0) \
    X(INDOOR_SOURCE,   0x034,      0, INDOOR_COUNT - 1, VF_PANEL, 0) \
    X(ROOM_TEMP,       0x035, -32768, 1000, VF_RO | VF_LINK_R, 0) /* 0.1 C, INT16_MIN = none */ \
    X(FW_VERSION,      0x036,      0, 0xFFFFFF, VF_RO | VF_LINK_R, 0) /* maj<<16 | min<<8 | patch */ \
    /* the unit's settings the CX60 only takes from the lowest panel address. With the ESP at 1 that's the ESP, so */ \
    /* ours sends them there. The unit's other values are in app/values.h and go on the bus */ \
    X(SETPOINT,        0x043,    100, 300,  VF_CX60 | VF_MASTER | VF_LINK_R, 0x00C) \
    X(FAN_NORM_SUP,    0x04A,     20, 100,  VF_CX60 | VF_MASTER | VF_LINK_R, 0x003) \
    X(FAN_NORM_EXT,    0x04D,     20, 100,  VF_CX60 | VF_MASTER | VF_LINK_R, 0x008)
// clang-format on

// K_<id>: each value's key by name
#define K_ENUM(id, key, ...) K_##id = key,
enum { PLINK_VALUES(K_ENUM) };
#undef K_ENUM
