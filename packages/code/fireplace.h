// Code for packages/fireplace.yaml
#pragma once
#include "controls.h"

namespace flexit::fireplace {

static constexpr const char *WHO = "fireplace";  // its name with the mode arbiter
static constexpr const char *TAG = "flexit.fireplace";
inline const char STEP = 0, TIMER = 0;  // scheduler keys: the mode a moment after the fan speeds, and the timer

// What a reboot loses
inline struct State {
  bool active = false;
  uint32_t started_ms = 0;       // when it was turned on
  bool changed_by_hand = false;  // a max fan speed was set while it's on
} state;

// The real max fan speeds back, then the old mode once they're out
inline void turn_off() {
  App.scheduler.cancel_timeout(&TIMER);
  if (!state.active) {
    ESP_LOGI(TAG, "Fireplace mode is already off");
    return;
  }
  ESP_LOGI(TAG, "Fireplace mode off");
  state.active = false;
  controls::MAX_FANS.restore(TAG);
  // The arbiter goes back to the old mode if nobody else wants max
  App.scheduler.set_timeout(&STEP, 200, [] { controls::release(WHO); });
}

// Our fan speeds as the max ones, then max once they're out, for the set time. Not if the unit is stopped or we
// don't know the max fan speeds yet. If it's on already the timer starts over.
inline void turn_on() {
  if (!state.active) {
    if (controls::stopped()) {
      ESP_LOGW(TAG, "Not starting fireplace mode, the unit is stopped");
      return;
    }
    if (!controls::MAX_FANS.known()) {
      ESP_LOGW(TAG, "Not starting fireplace mode, the max fan speeds aren't known yet");
      return;
    }
    ESP_LOGI(TAG, "Fireplace mode on (max speeds are supply %d%%, extract %d%%)", controls::MAX_FANS.real(0),
             controls::MAX_FANS.real(1));
    state.changed_by_hand = false;
    state.active = true;
    state.started_ms = millis();
  }
  controls::MAX_FANS.set((int) id(fireplace_supply_speed_control).state,
                         (int) id(fireplace_extract_speed_control).state);
  App.scheduler.set_timeout(&STEP, 200, [] { controls::request(WHO, flexit_modbus_server::MODE_MAX); });
  const float minutes = id(fireplace_duration).state;
  ESP_LOGI(TAG, "Fireplace timer started, %.0f min", minutes);
  App.scheduler.set_timeout(&TIMER, (uint32_t) (minutes * 60000), [] {
    ESP_LOGI(TAG, "Fireplace timer done, turning off");
    turn_off();
  });
}

// Setting callback. A max fan speed set by someone while it's on is kept, and conflict() ends fireplace mode.
inline void setting(uint16_t reg, uint16_t value) {
  if (!state.active || (reg != controls::MAX_FANS.regs[0] && reg != controls::MAX_FANS.regs[1]))
    return;
  ESP_LOGI(TAG, "Max fan speed set to %u%%: keeping that", value);
  state.changed_by_hand = true;
}

// true if somebody changed the mode or a max fan speed by hand while it's on
inline bool conflict() {
  if (!state.active)
    return false;
  if (state.changed_by_hand)
    return true;
  // Our request only goes in after the fan speeds, so give it a few seconds
  if (millis() - state.started_ms < 5000)
    return false;
  return controls::requested(WHO) < 0;
}

}  // namespace flexit::fireplace

// weak symbol, the panel's weather picture uses it for the cabin's chimney
extern "C" bool flexit_fireplace_active() { return flexit::fireplace::state.active; }
