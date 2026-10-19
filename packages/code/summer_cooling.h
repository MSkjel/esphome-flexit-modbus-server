// Code for packages/summer_cooling.yaml
#pragma once
#include <algorithm>
#include "controls.h"

namespace flexit::summer_cooling {

static constexpr const char *WHO = "summer cooling";  // its name with the mode arbiter

// every 30 s
inline void tick() {
  const uint32_t now = millis();
  auto &s = id(server);
  const float inside = s.read_holding_register_temperature(flexit_modbus_server::REG_TEMPERATURE_EXTRACT_AIR);
  const float outside = s.read_holding_register_temperature(flexit_modbus_server::REG_TEMPERATURE_OUTDOOR_AIR);
  const float setpoint = s.read_holding_register_temperature(flexit_modbus_server::REG_TEMPERATURE_SETPOINT);
  const bool heating = s.read_holding_register(flexit_modbus_server::REG_PERCENTAGE_HEATING) != 0;

  // Too warm means above the set temperature, and far enough above the setpoint
  const float above = std::max(id(summer_cooling_above).state, setpoint + id(summer_cooling_above_setpoint).state);
  const float difference = id(summer_cooling_min_difference).state;
  const float min_outdoor = id(summer_cooling_min_outdoor).state;

  if (id(summer_cooling_active)) {
    bool stop = false;
    if (!controls::requesting(WHO)) {
      // The arbiter drops our request when somebody changes the mode by hand
      ESP_LOGW("cooling", "Stopping, mode was changed by hand");
      id(summer_cooling_armed) = false;
      stop = true;
    } else if (!id(summer_cooling_enable).state) {
      ESP_LOGI("cooling", "Stopping, it was disabled");
      stop = true;
    } else if (heating) {
      ESP_LOGI("cooling", "Stopping, the heater is on");
      stop = true;
    } else if (inside < setpoint) {
      ESP_LOGI("cooling", "Stopping, %.1f°C inside is under the setpoint (%.1f°C)", inside, setpoint);
      stop = true;
    } else if (now - id(summer_cooling_start_ms) >= 15 * 60000UL) {
      // Runs at least 15 minutes. The stop levels are a bit past the start levels, so it doesn't flip in and
      // out of max.
      if (inside <= above - 0.5f) {
        ESP_LOGI("cooling", "Cooled down to %.1f°C, stopping", inside);
        stop = true;
      } else if (outside >= inside - difference / 2 || outside < min_outdoor - 1.0f) {
        ESP_LOGI("cooling", "Outdoor air (%.1f°C) is no use anymore, stopping at %.1f°C", outside, inside);
        stop = true;
      }
    }
    if (stop) {
      id(summer_cooling_active) = false;
      controls::release(WHO);
    }
    return;
  }

  const bool cool = id(summer_cooling_enable).state && !heating && inside >= above && outside <= inside - difference &&
                    outside >= min_outdoor;

  if (!id(summer_cooling_armed)) {
    if (!cool)
      id(summer_cooling_armed) = true;
    return;
  }

  if (cool && s.read_holding_register(flexit_modbus_server::REG_MODE) != 0) {
    ESP_LOGI("cooling", "%.1f°C inside and %.1f°C outside, cooling", inside, outside);
    id(summer_cooling_active) = true;
    id(summer_cooling_start_ms) = now;
    controls::request(WHO, 3);  // max
  }
}

}  // namespace flexit::summer_cooling
