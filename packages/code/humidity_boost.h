// Code for packages/humidity_boost.yaml
#pragma once
#include <cmath>
#include "controls.h"

namespace flexit::humidity_boost {

static constexpr const char *WHO = "humidity boost";  // its name with the mode arbiter

inline uint32_t minutes(float m) { return (uint32_t) (m * 60000.0f); }

// switch turned on, by hand or by tick()
inline void start() {
  if (id(humidity_boost_active))
    return;
  // Leave it alone if it's stopped
  if (id(server).read_holding_register(flexit_modbus_server::REG_MODE) == 0) {
    ESP_LOGW("humidity", "Not boosting, the unit is stopped");
    return;
  }
  id(humidity_boost_active) = true;
  id(humidity_boost_armed) = false;
  id(humidity_boost_start_ms) = millis();
  id(humidity_boost_below_release_since_ms) = 0;
  ESP_LOGI("humidity", "Boost on");
  controls::request(WHO, 3);  // max
  id(::humidity_boost).publish_state(true);
}

inline void stop() {
  if (!id(humidity_boost_active))
    return;
  id(humidity_boost_active) = false;
  id(humidity_boost_end_ms) = millis();
  id(humidity_boost_below_release_since_ms) = 0;
  ESP_LOGI("humidity", "Boost off");
  controls::release(WHO);  // back to the old mode if nobody else wants max
  id(::humidity_boost).publish_state(false);
}

// One sample a minute for the baseline. The median of the window is what we call normal humidity.
// A shower hardly moves a median, but it still follows the weather over the day.
template<typename Sensor> optional<float> baseline_sample(Sensor *humidity, int window) {
  if (id(humidity_boost_active))
    return {};  // none while boosting, or the shower ends up in the baseline
  if (!humidity->has_state() || std::isnan(humidity->state))
    return {};
  if (id(humidity_baseline_samples) < window)
    id(humidity_baseline_samples)++;
  return humidity->state;
}

template<typename Sensor> optional<float> above_baseline(Sensor *humidity) {
  if (!humidity->has_state() || !id(humidity_baseline).has_state())
    return {};
  float delta = humidity->state - id(humidity_baseline).state;
  if (std::isnan(delta))
    return {};
  return delta;
}

// every 10 s
template<typename Sensor> void tick(Sensor *sensor) {
  const uint32_t now = millis();

  // Reasons to stop come first. They have to work without a humidity reading, otherwise a dead sensor
  // could leave the unit in max forever.
  if (id(humidity_boost_active)) {
    const uint32_t runtime = now - id(humidity_boost_start_ms);
    if (!controls::requesting(WHO)) {  // the arbiter dropped us, mode changed by hand
      ESP_LOGW("humidity", "Stopping boost, mode was changed by hand");
      id(::humidity_boost).turn_off();
      return;
    }
    if (!id(humidity_boost_enable).state) {
      ESP_LOGI("humidity", "Stopping boost, it was disabled");
      id(::humidity_boost).turn_off();
      return;
    }
    // Gave it long enough. Laundry drying, a sensor that died, something like that.
    if (runtime >= minutes(id(humidity_boost_max_minutes).state)) {
      ESP_LOGW("humidity", "Stopping boost after %u min, humidity never came back down", (unsigned) (runtime / 60000));
      id(::humidity_boost).turn_off();
      return;
    }
  }

  // Wait for 10 minutes of baseline before trusting it
  if (id(humidity_baseline_samples) < 10)
    return;
  if (!sensor->has_state() || !id(humidity_baseline).has_state())
    return;
  const float humidity = sensor->state;
  const float baseline = id(humidity_baseline).state;
  if (std::isnan(humidity) || std::isnan(baseline))
    return;
  const float delta = humidity - baseline;

  // Release has to be below trigger, or a boost would end right after it started
  float release = id(humidity_release_delta).state;
  if (release >= id(humidity_trigger_delta).state)
    release = id(humidity_trigger_delta).state - 0.5f;

  if (!id(humidity_boost_active)) {
    if (!id(humidity_boost_armed)) {
      if (delta > release)
        return;
      id(humidity_boost_armed) = true;
    }
    if (!id(humidity_boost_enable).state)
      return;
    // Cooldown, so the end of one shower doesn't set off a second boost
    if (id(humidity_boost_end_ms) != 0 &&
        now - id(humidity_boost_end_ms) < minutes(id(humidity_boost_cooldown_minutes).state))
      return;
    if (id(server).read_holding_register(flexit_modbus_server::REG_MODE) == 0)
      return;  // stopped
    if (delta >= id(humidity_trigger_delta).state) {
      ESP_LOGI("humidity", "Humidity %.1f%% is %.1f%% above the baseline (%.1f%%)", humidity, delta, baseline);
      id(::humidity_boost).turn_on();
    }
    return;
  }

  // Boosting. Check if the humidity is back down.
  if (delta <= release) {
    if (id(humidity_boost_below_release_since_ms) == 0)
      id(humidity_boost_below_release_since_ms) = now;
  } else {
    id(humidity_boost_below_release_since_ms) = 0;  // went back up, start over
  }

  // Always run for the minimum time, so it doesn't flip in and out of max
  const uint32_t runtime = now - id(humidity_boost_start_ms);
  if (runtime < minutes(id(humidity_boost_min_minutes).state))
    return;

  // Back down, and stayed down for long enough
  if (id(humidity_boost_below_release_since_ms) != 0 &&
      now - id(humidity_boost_below_release_since_ms) >= minutes(id(humidity_release_minutes).state)) {
    ESP_LOGI("humidity", "Humidity back down to %.1f%% (%.1f%% above baseline) after %u min", humidity, delta,
             (unsigned) (runtime / 60000));
    id(::humidity_boost).turn_off();
  }
}

}  // namespace flexit::humidity_boost
