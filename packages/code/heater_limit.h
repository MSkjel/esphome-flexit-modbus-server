// Code for packages/heater_limit.yaml
#pragma once
#include <cmath>
#include <cstdint>

namespace flexit::heater_limit {

static constexpr const char *TAG = "flexit.heater_limit";

// The heater gets the supply air back this far under the limit, and gives it up after idling this long at it
static constexpr float BELOW = 0.5f;
static constexpr uint32_t IDLE_MS = 3 * 60000;

// What a reboot loses. The server sends the CX60 the real heater flags and setpoint again then.
inline struct State {
  // OFF: the real heater and setpoint. ROTOR: the heater held off. LIMIT: the setpoint at the limit.
  enum { OFF, ROTOR, LIMIT } mode = OFF;
  uint32_t busy_ms = 0;  // when the heater last ran, or the supply air was under the limit
} state;

// The heater as it's set: on if any panel's flag is
inline bool heater_set() {
  for (uint16_t r = flexit_modbus_server::REG_CMD_HEATER_PANEL1; r <= flexit_modbus_server::REG_CMD_HEATER_PANEL3; r++)
    if (id(server).real_value(r))
      return true;
  return false;
}

// Holds the heater off, or the setpoint at the limit, as the mode says. The rest gets its real value back.
inline void apply() {
  namespace fms = flexit_modbus_server;
  auto &s = id(server);
  for (uint16_t r = fms::REG_CMD_HEATER_PANEL1; r <= fms::REG_CMD_HEATER_PANEL3; r++) {
    if (state.mode != State::ROTOR)
      s.restore(r);
    else if (s.read_holding_register(r))
      s.send_cmd(r, 0, true);
  }
  const auto limit = (uint16_t) lroundf(id(heater_limit_supply).state * 10);
  if (state.mode != State::LIMIT)
    s.restore(fms::REG_CMD_TEMPERATURE_SETPOINT);
  else if (s.read_holding_register(fms::REG_CMD_TEMPERATURE_SETPOINT) != limit)
    s.send_cmd(fms::REG_CMD_TEMPERATURE_SETPOINT, limit, true);
}

// every 10 s
inline void tick() {
  namespace fms = flexit_modbus_server;
  auto &s = id(server);
  if (!s.fresh())
    return;  // nothing to go by, and the CX60 isn't reading us either
  const float supply = s.read_sensor_temperature(fms::REG_TEMPERATURE_SUPPLY_AIR);
  const float limit = id(heater_limit_supply).state;
  const float setpoint = (int16_t) s.real_value(fms::REG_CMD_TEMPERATURE_SETPOINT) / 10.0f;
  // Electric only, a water coil's frost protection may need it. On extract air regulation the setpoint is the room's.
  const bool on = id(heater_limit_enable).state && heater_set() && setpoint > limit && !std::isnan(supply) &&
                  s.read_holding_register(fms::REG_CMD_TEMPERATURE_SUPPLY_AIR_CONTROL) != 0 &&
                  s.read_holding_register(fms::REG_DS1_HEATING_TYPE) == 1;

  const uint32_t now = millis();
  if (supply < limit || s.read_holding_register(fms::REG_PERCENTAGE_HEATING))
    state.busy_ms = now;
  const auto was = state.mode;
  if (!on)
    state.mode = State::OFF;
  else if (state.mode != State::LIMIT && supply < limit - BELOW)
    state.mode = State::LIMIT;
  else if (state.mode == State::OFF || (state.mode == State::LIMIT && now - state.busy_ms >= IDLE_MS))
    state.mode = State::ROTOR;

  if (state.mode == State::LIMIT && was != State::LIMIT)
    ESP_LOGI(TAG, "Supply air at %.1f°C, the heater holds it at %.1f°C", supply, limit);
  else if (state.mode == State::ROTOR && was != State::ROTOR)
    ESP_LOGI(TAG, "Supply air at %.1f°C, the rotor alone up to %.1f°C", supply, setpoint);
  else if (state.mode == State::OFF && was != State::OFF)
    ESP_LOGI(TAG, "Stopped, the heater and setpoint are as set");
  if (state.mode != State::OFF || was != State::OFF)
    apply();
}

}  // namespace flexit::heater_limit
