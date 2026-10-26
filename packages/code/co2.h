// Code for packages/co2.yaml, see there for what the control modes do.
#pragma once
#include <algorithm>
#include <cmath>
#include "controls.h"

namespace flexit::co2 {

enum Control { CONTROL_OFF, CONTROL_STEPPED, CONTROL_PROPORTIONAL, CONTROL_PI };  // co2_control's options

static constexpr const char *TAG = "flexit.co2";

// What a reboot loses
inline struct State {
  // Unarmed after somebody changed the mode during a boost, until the CO2 is down
  controls::Boost boost{"co2", TAG};
  float pi_integral = 0;      // PI: the integral part, in fan %
  bool fans_up = false;       // proportional and PI: our normal fan speeds are on
  bool fans_refused = false;  // the CX60 kept the real ones, ours stay off until the CO2 is down
  int added = 0;              // what we add to the normal fan speeds, -1 = send them again
  int raised_from[2] = {};    // the real supply and extract speeds ours went on top of
} state;

// The normal fan speeds `add` over the real ones
inline void raise_fans(int add) {
  for (int k = 0; k < 2; k++)
    state.raised_from[k] = controls::NORMAL_FANS.real(k);
  controls::NORMAL_FANS.set(std::min(state.raised_from[0] + add, 100), std::min(state.raised_from[1] + add, 100));
  state.added = add;
}

// Setting callback. A normal fan speed set by hand while ours are on is the new real one, ours go on top of it.
// Back to the one ours went on top of means the CX60 kept its own: it only takes them from the lowest panel
// address. Or somebody set it back by hand. Either way we leave them alone until the CO2 is down.
inline void setting(uint16_t reg, uint16_t value) {
  for (int k = 0; k < 2; k++) {
    if (!state.fans_up || reg != controls::NORMAL_FANS.regs[k])
      continue;
    if (value == state.raised_from[k]) {
      if (!state.fans_refused) {
        ESP_LOGW(TAG, "Normal fan speed back at %u%%: leaving the fan speeds alone until the CO2 is down", value);
      }
      state.fans_refused = true;
    } else {
      ESP_LOGI(TAG, "Normal fan speed set to %u%%: ours go on top of that", value);
      state.added = -1;
    }
  }
}

// Above the high level we ask for a higher mode, until it is back under the target. Stepped asks for max. The fan
// modes ask for normal, which only does something when the unit is in min, where the normal fan speeds aren't used.
inline void boost_mode(int control, bool valid, float ppm, float low, float high) {
  const int wanted = control == CONTROL_STEPPED ? flexit_modbus_server::MODE_MAX
                     : control != CONTROL_OFF   ? flexit_modbus_server::MODE_NORMAL
                                                : flexit_modbus_server::MODE_STOP;
  if (state.boost.active) {
    if (state.boost.dropped()) {
      state.boost.stop();
    } else if (controls::requested(state.boost.who) != wanted) {
      ESP_LOGI(TAG, "Stopping boost, CO2 control was changed");
      state.boost.stop();
    } else if (!valid) {
      ESP_LOGW(TAG, "Stopping boost, no CO2 reading");
      state.boost.stop();
    } else if (ppm < low && state.boost.runtime_ms() >= 10 * 60000UL) {
      // Runs at least 10 minutes, so it doesn't flip back and forth
      ESP_LOGI(TAG, "CO2 back down to %.0f ppm, boost off", ppm);
      state.boost.stop();
    }
  } else if (wanted == flexit_modbus_server::MODE_STOP) {
    state.boost.armed = true;
  } else if (valid) {
    if (!state.boost.armed) {
      if (ppm < low)
        state.boost.armed = true;
    } else if (ppm >= high && !controls::stopped()) {
      ESP_LOGI(TAG, "CO2 at %.0f ppm, asking for %s", ppm, flexit_modbus_server::mode_to_string(wanted).c_str());
      state.boost.start(wanted);
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
      state.pi_integral += gain * error * 0.5f / id(co2_pi_integral_time).state;  // 0.5 = 30 s in minutes
    state.pi_integral = clamp(state.pi_integral, 0.0f, max_add);

    add = (int) lroundf(clamp(gain * error + state.pi_integral, 0.0f, max_add));
  } else {
    state.pi_integral = 0;
    if (control == CONTROL_PROPORTIONAL && valid && ppm > low)  // nothing at the target, the full increase at high
      add = (int) lroundf(std::min((ppm - low) / (high - low), 1.0f) * max_add);
  }

  // Done when the CO2 is back at the target and there is nothing left to add
  const bool fans_done = control < CONTROL_PROPORTIONAL || !valid || (ppm <= low && add == 0);
  if (fans_done)
    state.fans_refused = false;

  if (state.fans_up) {
    if (fans_done || state.fans_refused) {
      controls::NORMAL_FANS.restore(TAG);
      state.fans_up = false;
    } else if (state.added < 0 || abs(add - state.added) >= 2) {
      // Only in steps of 2% or more, so the fans don't change speed all the time
      ESP_LOGI(TAG, "CO2 at %.0f ppm, fan speeds +%d%%", ppm, add);
      raise_fans(add);
    }
  } else if (control >= CONTROL_PROPORTIONAL && add >= 2 && !state.fans_refused && controls::NORMAL_FANS.known()) {
    ESP_LOGI(TAG, "CO2 at %.0f ppm, fan speeds +%d%% (from supply %d%%, extract %d%%)", ppm, add,
             controls::NORMAL_FANS.real(0), controls::NORMAL_FANS.real(1));
    state.fans_up = true;
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
