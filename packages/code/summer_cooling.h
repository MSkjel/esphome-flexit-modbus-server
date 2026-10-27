// Code for packages/summer_cooling.yaml
#pragma once
#include <algorithm>
#include "controls.h"

namespace flexit::summer_cooling {

static constexpr const char *TAG = "flexit.summer_cooling";

// What a reboot loses
inline struct State {
  // Unarmed after somebody changed the mode during cooling, until there is no reason to cool anymore
  controls::Boost boost{"summer cooling", TAG};
} state;

// every 30 s
inline void tick() {
  auto &s = id(server);
  const float inside = s.read_holding_register_temperature(flexit_modbus_server::REG_TEMPERATURE_EXTRACT_AIR);
  const float outside = s.read_holding_register_temperature(flexit_modbus_server::REG_TEMPERATURE_OUTDOOR_AIR);
  const float setpoint = s.read_holding_register_temperature(flexit_modbus_server::REG_TEMPERATURE_SETPOINT);
  const bool heating = s.read_holding_register(flexit_modbus_server::REG_PERCENTAGE_HEATING) != 0;

  // Too warm means above the set temperature, and far enough above the setpoint
  const float above = std::max(id(summer_cooling_above).state, setpoint + id(summer_cooling_above_setpoint).state);
  const float difference = id(summer_cooling_min_difference).state;
  const float min_outdoor = id(summer_cooling_min_outdoor).state;

  if (state.boost.active) {
    if (state.boost.dropped()) {
      state.boost.stop();
    } else if (!id(summer_cooling_enable).state) {
      ESP_LOGI(TAG, "Stopping, it was disabled");
      state.boost.stop();
    } else if (heating) {
      ESP_LOGI(TAG, "Stopping, the heater is on");
      state.boost.stop();
    } else if (inside < setpoint) {
      ESP_LOGI(TAG, "Stopping, %.1f°C inside is under the setpoint (%.1f°C)", inside, setpoint);
      state.boost.stop();
    } else if (state.boost.runtime_ms() >= 15 * 60000UL) {
      // Runs at least 15 minutes. The stop levels are a bit past the start levels, so it doesn't flip in and
      // out of max.
      if (inside <= above - 0.5f) {
        ESP_LOGI(TAG, "Cooled down to %.1f°C, stopping", inside);
        state.boost.stop();
      } else if (outside >= inside - difference / 2 || outside < min_outdoor - 1.0f) {
        ESP_LOGI(TAG, "Outdoor air (%.1f°C) is no use anymore, stopping at %.1f°C", outside, inside);
        state.boost.stop();
      }
    }
    return;
  }

  const bool cool = id(summer_cooling_enable).state && !heating && inside >= above && outside <= inside - difference &&
                    outside >= min_outdoor;

  if (!state.boost.armed) {
    if (!cool)
      state.boost.armed = true;
    return;
  }

  if (cool && !controls::stopped()) {
    ESP_LOGI(TAG, "%.1f°C inside and %.1f°C outside, cooling", inside, outside);
    state.boost.start(flexit_modbus_server::MODE_MAX);
  }
}

}  // namespace flexit::summer_cooling
