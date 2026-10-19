// Code for packages/fireplace.yaml. The steps and waits are in its switch.
#pragma once
#include "controls.h"

// weak symbol, the panel's weather picture uses it for the cabin's chimney
extern "C" bool flexit_fireplace_active() { return id(fireplace_mode_active); }

namespace flexit::fireplace {

static constexpr const char *WHO = "fireplace";  // its name with the mode arbiter
inline uint32_t started_ms;                      // when it was turned on
inline bool changed_by_hand;                     // a max fan speed was set while it's on

// Switch turned on. Does nothing if the unit is stopped or we don't know the max fan speeds yet. If it's
// already on the YAML just restarts the timer.
inline void start() {
  if (id(fireplace_mode_active))
    return;
  auto &s = id(server);
  if (s.read_holding_register(flexit_modbus_server::REG_MODE) == 0) {
    ESP_LOGW("fireplace", "Not starting fireplace mode, the unit is stopped");
    return;
  }
  const int supply = s.real_value(flexit_modbus_server::REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX);
  const int extract = s.real_value(flexit_modbus_server::REG_CMD_PERCENTAGE_EXTRACT_FAN_MAX);
  if (supply == 0 || extract == 0) {
    ESP_LOGW("fireplace", "Not starting fireplace mode, the max fan speeds aren't known yet");
    return;
  }
  ESP_LOGI("fireplace", "Fireplace mode on (max speeds are supply %d%%, extract %d%%)", supply, extract);
  changed_by_hand = false;
  id(fireplace_mode_active) = true;
  started_ms = millis();
}

// Our speeds as temporary max speeds, the server keeps the real ones
inline void set_fans() {
  id(server).send_cmd(flexit_modbus_server::REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX,
                      (uint16_t) id(fireplace_supply_fan_speed), true);
  id(server).send_cmd(flexit_modbus_server::REG_CMD_PERCENTAGE_EXTRACT_FAN_MAX,
                      (uint16_t) id(fireplace_extract_fan_speed), true);
}

// The real max fan speeds back. After a reboot in fireplace mode the server does that itself.
inline void restore_fans() {
  auto &s = id(server);
  ESP_LOGI("fireplace", "Max fan speeds back to supply %u%%, extract %u%%",
           s.real_value(flexit_modbus_server::REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX),
           s.real_value(flexit_modbus_server::REG_CMD_PERCENTAGE_EXTRACT_FAN_MAX));
  s.restore(flexit_modbus_server::REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX);
  s.restore(flexit_modbus_server::REG_CMD_PERCENTAGE_EXTRACT_FAN_MAX);
}

// Setting callback. A max fan speed set by someone while it's on is kept, and conflict() ends fireplace mode.
inline void setting(uint16_t reg, uint16_t value) {
  if (!id(fireplace_mode_active) || (reg != flexit_modbus_server::REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX &&
                                     reg != flexit_modbus_server::REG_CMD_PERCENTAGE_EXTRACT_FAN_MAX))
    return;
  ESP_LOGW("fireplace", "Max fan speed set to %u%%: keeping that", value);
  changed_by_hand = true;
}

// true if somebody changed the mode or a max fan speed by hand while it's on
inline bool conflict() {
  if (!id(fireplace_mode_active))
    return false;
  if (changed_by_hand)
    return true;
  // Our request only goes in after the fan speeds, so give it a few seconds
  if (millis() - started_ms < 5000)
    return false;
  return !controls::requesting(WHO);
}

}  // namespace flexit::fireplace
