// Code for packages/co2.yaml, see there for what the control modes do.
#pragma once
#include <algorithm>
#include <cmath>
#include "controls.h"

namespace flexit::co2 {

enum Control { CONTROL_OFF, CONTROL_STEPPED, CONTROL_PROPORTIONAL, CONTROL_PI };  // co2_control's options

// Unarmed after somebody changed the mode during a boost, until the CO2 is down
inline controls::Boost boost{"co2", "co2"};
inline float pi_integral;   // PI: the integral part, in fan %
inline bool fans_up;        // proportional and PI: our normal fan speeds are on
inline bool fans_refused;   // the CX60 kept the real ones, ours stay off until the CO2 is down
inline int added;           // what we add to the normal fan speeds, -1 = send them again
inline int raised_from[2];  // the real supply and extract speeds ours went on top of

// The normal fan speeds `add` over the real ones
inline void raise_fans(int add) {
  for (int k = 0; k < 2; k++)
    raised_from[k] = controls::NORMAL_FANS.real(k);
  controls::NORMAL_FANS.set(std::min(raised_from[0] + add, 100), std::min(raised_from[1] + add, 100));
  added = add;
}

// Setting callback. A normal fan speed set by hand while ours are on is the new real one, ours go on top of it.
// Back to the one ours went on top of means the CX60 kept its own: it only takes them from the lowest panel
// address. Or somebody set it back by hand. Either way we leave them alone until the CO2 is down.
inline void setting(uint16_t reg, uint16_t value) {
  for (int k = 0; k < 2; k++) {
    if (!fans_up || reg != controls::NORMAL_FANS.regs[k])
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

// Above the high level we ask for a higher mode, until it is back under the target. Stepped asks for max. The fan
// modes ask for normal, which only does something when the unit is in min, where the normal fan speeds aren't used.
inline void boost_mode(int control, bool valid, float ppm, float low, float high) {
  const int wanted = control == CONTROL_STEPPED ? flexit_modbus_server::MODE_MAX
                     : control != CONTROL_OFF   ? flexit_modbus_server::MODE_NORMAL
                                                : flexit_modbus_server::MODE_STOP;
  if (boost.active) {
    if (boost.dropped()) {
      boost.stop();
    } else if (controls::requested(boost.who) != wanted) {
      ESP_LOGI("co2", "Stopping boost, CO2 control was changed");
      boost.stop();
    } else if (!valid) {
      ESP_LOGW("co2", "Stopping boost, no CO2 reading");
      boost.stop();
    } else if (ppm < low && boost.runtime_ms() >= 10 * 60000UL) {
      // Runs at least 10 minutes, so it doesn't flip back and forth
      ESP_LOGI("co2", "CO2 back down to %.0f ppm, boost off", ppm);
      boost.stop();
    }
  } else if (wanted == flexit_modbus_server::MODE_STOP) {
    boost.armed = true;
  } else if (valid) {
    if (!boost.armed) {
      if (ppm < low)
        boost.armed = true;
    } else if (ppm >= high && !controls::stopped()) {
      ESP_LOGI("co2", "CO2 at %.0f ppm, asking for %s", ppm, flexit_modbus_server::mode_to_string(wanted).c_str());
      boost.start(wanted);
    }
  }
}

// Proportional and PI: the normal fan speeds, up the further over the target the CO2 is
inline void fan_speeds(int control, bool valid, float ppm, float low, float high) {
  const float max_add = id(co2_max_fan_increase).state;
  int add = 0;
  if (control == CONTROL_PI && valid) {
    const float gain = id(co2_pi_gain).state / 100.0f;  // fan % per ppm
    const float error = ppm - low;

    // The integral only counts in normal mode, in the other modes the fan speeds we set aren't used. It
    // also stays between 0 and the max increase, so it can't build up while the fans are already as high
    // as they go.
    if (id(server).read_holding_register(flexit_modbus_server::REG_MODE) == flexit_modbus_server::MODE_NORMAL)
      pi_integral += gain * error * 0.5f / id(co2_pi_integral_time).state;  // 0.5 = 30 s in minutes
    pi_integral = clamp(pi_integral, 0.0f, max_add);

    add = (int) lroundf(clamp(gain * error + pi_integral, 0.0f, max_add));
  } else {
    pi_integral = 0;
    if (control == CONTROL_PROPORTIONAL && valid && ppm > low)  // nothing at the target, the full increase at high
      add = (int) lroundf(std::min((ppm - low) / (high - low), 1.0f) * max_add);
  }

  // Done when the CO2 is back at the target and there is nothing left to add
  const bool fans_done = control < CONTROL_PROPORTIONAL || !valid || (ppm <= low && add == 0);
  if (fans_done)
    fans_refused = false;

  if (fans_up) {
    if (fans_done || fans_refused) {
      controls::NORMAL_FANS.restore("co2");
      fans_up = false;
    } else if (added < 0 || abs(add - added) >= 2) {
      // Only in steps of 2% or more, so the fans don't change speed all the time
      ESP_LOGI("co2", "CO2 at %.0f ppm, fan speeds +%d%%", ppm, add);
      raise_fans(add);
    }
  } else if (control >= CONTROL_PROPORTIONAL && add >= 2 && !fans_refused && controls::NORMAL_FANS.known()) {
    ESP_LOGI("co2", "CO2 at %.0f ppm, fan speeds +%d%% (from supply %d%%, extract %d%%)", ppm, add,
             controls::NORMAL_FANS.real(0), controls::NORMAL_FANS.real(1));
    fans_up = true;
    raise_fans(add);
  }
}

// every 30 s
template<typename Sensor> void tick(Sensor *sensor) {
  const auto index = id(co2_control).active_index();
  const int control = index.has_value() ? (int) *index : CONTROL_OFF;
  const float ppm = sensor->state;
  const bool valid = sensor->has_state() && !std::isnan(ppm);
  const float low = id(co2_target).state;
  const float high = low + id(co2_band).state;
  boost_mode(control, valid, ppm, low, high);
  fan_speeds(control, valid, ppm, low, high);
}

}  // namespace flexit::co2
