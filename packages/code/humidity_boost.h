// Code for packages/humidity_boost.yaml
#pragma once
#include <cmath>
#include "controls.h"

namespace flexit::humidity_boost {

// Unarmed from when a boost starts until the humidity is back to normal. Without that a boost that hit the max
// runtime would just start again after the cooldown.
inline controls::Boost boost{"humidity boost", "humidity"};
inline uint32_t end_ms;                  // 0 until the first boost has ended, so there is no cooldown before that
inline uint32_t below_release_since_ms;  // when the humidity got back under the release level, 0 if it hasn't yet
inline int baseline_samples;             // how many samples the baseline has so far

inline uint32_t minutes(float m) { return (uint32_t) (m * 60000.0f); }

// switch turned on, by hand or by tick()
inline void start() {
  if (boost.active)
    return;
  // Leave it alone if it's stopped
  if (controls::stopped()) {
    ESP_LOGW("humidity", "Not boosting, the unit is stopped");
    return;
  }
  boost.armed = false;
  below_release_since_ms = 0;
  ESP_LOGI("humidity", "Boost on");
  boost.start(flexit_modbus_server::MODE_MAX);
  id(::humidity_boost).publish_state(true);
}

inline void stop() {
  if (!boost.active)
    return;
  end_ms = millis();
  below_release_since_ms = 0;
  ESP_LOGI("humidity", "Boost off");
  boost.stop();  // back to the old mode if nobody else wants max
  id(::humidity_boost).publish_state(false);
}

// One sample a minute for the baseline. The median of the window is what we call normal humidity.
// A shower hardly moves a median, but it still follows the weather over the day.
template<typename Sensor> optional<float> baseline_sample(Sensor *humidity, int window) {
  if (boost.active)
    return {};  // none while boosting, or the shower ends up in the baseline
  if (!humidity->has_state() || std::isnan(humidity->state))
    return {};
  if (baseline_samples < window)
    baseline_samples++;
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
  if (boost.active) {
    if (boost.dropped()) {
      id(::humidity_boost).turn_off();
      return;
    }
    if (!id(humidity_boost_enable).state) {
      ESP_LOGI("humidity", "Stopping boost, it was disabled");
      id(::humidity_boost).turn_off();
      return;
    }
    // Gave it long enough. Laundry drying, a sensor that died, something like that.
    if (boost.runtime_ms() >= minutes(id(humidity_boost_max_minutes).state)) {
      ESP_LOGW("humidity", "Stopping boost after %u min, humidity never came back down",
               (unsigned) (boost.runtime_ms() / 60000));
      id(::humidity_boost).turn_off();
      return;
    }
  }

  // Wait for 10 minutes of baseline before trusting it
  if (baseline_samples < 10)
    return;
  const auto above = above_baseline(sensor);
  if (!above.has_value())
    return;
  const float delta = *above;

  // Release has to be below trigger, or a boost would end right after it started
  float release = id(humidity_release_delta).state;
  if (release >= id(humidity_trigger_delta).state)
    release = id(humidity_trigger_delta).state - 0.5f;

  if (!boost.active) {
    if (!boost.armed) {
      if (delta > release)
        return;
      boost.armed = true;
    }
    if (!id(humidity_boost_enable).state)
      return;
    // Cooldown, so the end of one shower doesn't set off a second boost
    if (end_ms != 0 && now - end_ms < minutes(id(humidity_boost_cooldown_minutes).state))
      return;
    if (controls::stopped())
      return;
    if (delta >= id(humidity_trigger_delta).state) {
      ESP_LOGI("humidity", "Humidity %.1f%% is %.1f%% above the baseline (%.1f%%)", sensor->state, delta,
               id(humidity_baseline).state);
      id(::humidity_boost).turn_on();
    }
    return;
  }

  // Boosting. Check if the humidity is back down.
  if (delta <= release) {
    if (below_release_since_ms == 0)
      below_release_since_ms = now;
  } else {
    below_release_since_ms = 0;  // went back up, start over
  }

  // Always run for the minimum time, so it doesn't flip in and out of max
  const uint32_t runtime = boost.runtime_ms();
  if (runtime < minutes(id(humidity_boost_min_minutes).state))
    return;

  // Back down, and stayed down for long enough
  if (below_release_since_ms != 0 && now - below_release_since_ms >= minutes(id(humidity_release_minutes).state)) {
    ESP_LOGI("humidity", "Humidity back down to %.1f%% (%.1f%% above baseline) after %u min", sensor->state, delta,
             (unsigned) (runtime / 60000));
    id(::humidity_boost).turn_off();
  }
}

}  // namespace flexit::humidity_boost
