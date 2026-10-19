// Code for packages/co2.yaml, see there for what the control modes do.
#pragma once
#include <algorithm>
#include <cmath>
#include "controls.h"

namespace flexit::co2 {

static constexpr const char *WHO = "co2";  // its name with the mode arbiter

inline bool boost_active;  // asking the arbiter for a higher mode
inline uint32_t boost_start_ms;
inline bool boost_armed = true;  // false after somebody changed the mode during a boost, until the CO2 is down
inline float pi_integral;        // PI: the integral part, in fan %
inline bool fans_up;             // proportional and PI: our normal fan speeds are on
inline bool fans_refused;        // the CS60 kept the real ones, ours stay off until the CO2 is down
inline int added;                // what we add to the normal fan speeds, -1 = send them again
inline int raised_from[2];       // the real supply and extract speeds ours went on top of

static constexpr flexit_modbus_server::HoldingRegisterIndex NORMAL_FANS[] = {
    flexit_modbus_server::REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL,
    flexit_modbus_server::REG_CMD_PERCENTAGE_EXTRACT_FAN_NORMAL};

// The normal fan speeds `add` over the real ones, as temporary values. The server keeps the real ones.
inline void raise_fans(int add) {
  for (int k = 0; k < 2; k++) {
    raised_from[k] = id(server).real_value(NORMAL_FANS[k]);
    id(server).send_cmd(NORMAL_FANS[k], std::min(raised_from[k] + add, 100), true);
  }
  added = add;
}

// Setting callback. A normal fan speed set by hand while ours are on is the new real one, ours go on top of it.
// Back to the one ours went on top of means the CS60 kept its own: it only takes them from the lowest panel
// address. Or somebody set it back by hand. Either way we leave them alone until the CO2 is down.
inline void setting(uint16_t reg, uint16_t value) {
  for (int k = 0; k < 2; k++) {
    if (!fans_up || reg != NORMAL_FANS[k])
      continue;
    if (value == raised_from[k]) {
      if (!fans_refused) {
        ESP_LOGW("co2", "Normal fan speed back at %u%%: leaving the fan speeds alone until the CO2 is down", value);
      }
      fans_refused = true;
    } else {
      ESP_LOGW("co2", "Normal fan speed set to %u%%: ours go on top of that", value);
      added = -1;
    }
  }
}

// every 30 s
template<typename Sensor> void tick(Sensor *sensor) {
  const uint32_t now = millis();
  auto &s = id(server);

  // 0 = Off, 1 = Stepped, 2 = Proportional, 3 = PI
  const auto index = id(co2_control).active_index();
  const int control = index.has_value() ? (int) *index : 0;

  const float ppm = sensor->state;
  const bool valid = sensor->has_state() && !std::isnan(ppm);

  const float low = id(co2_target).state;
  const float high = low + id(co2_band).state;

  // Above the high level we ask for a higher mode, until it is back under the target. Stepped asks for
  // max. The fan modes ask for normal, which only does something when the unit is in min, where the
  // normal fan speeds aren't used.
  const int wanted_mode = control == 1 ? 3 : control >= 2 ? 2 : 0;

  if (boost_active) {
    const auto request = controls::requests.find(WHO);
    bool stop = false;
    if (request == controls::requests.end()) {
      // The arbiter drops our request when somebody changes the mode by hand
      ESP_LOGW("co2", "Stopping boost, mode was changed by hand");
      boost_armed = false;
      stop = true;
    } else if (request->second != wanted_mode) {
      ESP_LOGI("co2", "Stopping boost, CO2 control was changed");
      stop = true;
    } else if (!valid) {
      ESP_LOGW("co2", "Stopping boost, no CO2 reading");
      stop = true;
    } else if (ppm < low && now - boost_start_ms >= 10 * 60000UL) {
      // Runs at least 10 minutes, so it doesn't flip back and forth
      ESP_LOGI("co2", "CO2 back down to %.0f ppm, boost off", ppm);
      stop = true;
    }
    if (stop) {
      boost_active = false;
      controls::release(WHO);
    }
  } else if (wanted_mode == 0) {
    boost_armed = true;
  } else if (valid) {
    if (!boost_armed) {
      if (ppm < low)
        boost_armed = true;
    } else if (ppm >= high && s.read_holding_register(flexit_modbus_server::REG_MODE) != 0) {
      ESP_LOGI("co2", "CO2 at %.0f ppm, asking for %s", ppm, flexit_modbus_server::mode_to_string(wanted_mode).c_str());
      boost_active = true;
      boost_start_ms = now;
      controls::request(WHO, wanted_mode);
    }
  }

  // Proportional and PI: the fan speeds
  const float max_add = id(co2_max_fan_increase).state;
  int add = 0;
  if (control == 3 && valid) {
    const float gain = id(co2_pi_gain).state / 100.0f;  // fan % per ppm
    const float error = ppm - low;

    // The integral only counts in normal mode, in the other modes the fan speeds we set aren't used. It
    // also stays between 0 and the max increase, so it can't build up while the fans are already as high
    // as they go.
    if (s.read_holding_register(flexit_modbus_server::REG_MODE) == 2)
      pi_integral += gain * error * 0.5f / id(co2_pi_integral_time).state;  // 0.5 = 30 s in minutes
    pi_integral = clamp(pi_integral, 0.0f, max_add);

    add = (int) lroundf(clamp(gain * error + pi_integral, 0.0f, max_add));
  } else {
    pi_integral = 0;
    if (control == 2 && valid && ppm > low)  // nothing at the target, the full increase at the high level
      add = (int) lroundf(std::min((ppm - low) / (high - low), 1.0f) * max_add);
  }

  // Done when the CO2 is back at the target and there is nothing left to add
  const bool fans_done = control < 2 || !valid || (ppm <= low && add == 0);
  const int supply = s.real_value(flexit_modbus_server::REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL);
  const int extract = s.real_value(flexit_modbus_server::REG_CMD_PERCENTAGE_EXTRACT_FAN_NORMAL);
  if (fans_done)
    fans_refused = false;

  if (fans_up) {
    if (fans_done || fans_refused) {
      ESP_LOGI("co2", "Fan speeds back to supply %d%%, extract %d%%", supply, extract);
      s.restore(flexit_modbus_server::REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL);
      s.restore(flexit_modbus_server::REG_CMD_PERCENTAGE_EXTRACT_FAN_NORMAL);
      fans_up = false;
    } else if (added < 0 || abs(add - added) >= 2) {
      // Only in steps of 2% or more, so the fans don't change speed all the time
      ESP_LOGI("co2", "CO2 at %.0f ppm, fan speeds +%d%%", ppm, add);
      raise_fans(add);
    }
  } else if (control >= 2 && add >= 2 && !fans_refused && supply != 0 && extract != 0) {  // 0 = not known yet
    ESP_LOGI("co2", "CO2 at %.0f ppm, fan speeds +%d%% (from supply %d%%, extract %d%%)", ppm, add, supply, extract);
    fans_up = true;
    raise_fans(add);
  }
}

}  // namespace flexit::co2
