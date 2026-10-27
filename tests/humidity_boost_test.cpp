// humidity_boost.h on the component and a CX60 (cx60.h): when a boost starts and ends, and the CX60's mode.
// Needs the Modbus library, ESP-ModbusRTUServer, in $LIB.
//   g++ -std=gnu++20 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -Itests/stubs -Ipackages/code
//     -Iesphome/components/flexit_modbus_server -I$LIB tests/humidity_boost_test.cpp
//     esphome/components/flexit_modbus_server/flexit_modbus_server.cpp $LIB/ModbusRTUServer.cpp
//     packages/code/panel/pbus.c -o humidity_boost_test && ./humidity_boost_test
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include "cx60.h"

// humidity_boost.yaml's entities, at their defaults
static Sensor humidity, humidity_baseline;
static Switch humidity_boost, humidity_boost_enable;
static Number humidity_trigger_delta{8}, humidity_release_delta{3}, humidity_release_minutes{5},
    humidity_boost_min_minutes{5}, humidity_boost_max_minutes{45}, humidity_boost_cooldown_minutes{15};
static Number humidity_rise_trigger{0}, humidity_rise_minutes{5};  // off unless a case turns it on

#include "humidity_boost.h"

namespace hb = flexit::humidity_boost;

// The baseline sensor's median filter, window 60
static std::deque<float> window;

// Runs `s` seconds: the baseline sample every minute and the tick every 10 s, like humidity_boost.yaml
static void run(uint32_t s) {
  for (uint32_t i = 0; i < s; i++) {
    cx60_second();
    if (now_ms % 60000 == 0) {
      auto v = hb::baseline_sample(&humidity, 60);
      if (v.has_value()) {
        window.push_back(*v);
        if (window.size() > 60)
          window.pop_front();
        std::vector<float> sorted(window.begin(), window.end());
        std::sort(sorted.begin(), sorted.end());
        size_t n = sorted.size();
        humidity_baseline.publish_state(n % 2 ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2);
      }
    }
    if (now_ms % 10000 == 0)
      hb::tick(&humidity);
  }
}

static bool boosting() { return hb::state.boost.active; }

// Booted, with `minutes` of baseline at 50 %
static void start(int minutes) {
  boot();
  hb::state = {};
  window.clear();
  humidity_baseline = {};
  humidity_boost = {false, hb::start, hb::stop};
  humidity_boost_enable = {true};
  humidity_release_minutes.state = 5;
  humidity_boost_cooldown_minutes.state = 15;
  humidity.publish_state(50);
  humidity_rise_trigger.state = 0;
  run(minutes * 60);
}

// A shower: the humidity jumps and the boost starts
static void shower() {
  humidity.publish_state(70);
  run(15);
  CHECK(boosting() && humidity_boost.state);
  run(5);
  CHECK(cx60.mode == MODE_MAX);
}

int main() {
  log_on = getenv("LOG") != nullptr;

  // Nothing in the first 10 minutes, the baseline isn't trusted yet
  start(5);
  humidity.publish_state(70);
  run(4 * 60 + 30);
  CHECK(!boosting());

  // A shower: at least the min time, then until the humidity has been down for the release time. The baseline
  // leaves it out.
  start(15);
  shower();
  run(2 * 60);
  humidity.publish_state(51);
  run(4 * 60 + 30);
  CHECK(boosting());
  run(60);
  CHECK(!boosting() && !humidity_boost.state);
  run(5);
  CHECK(cx60.mode == MODE_NORMAL);
  CHECK(humidity_baseline.state == 50 && window.size() == 15);

  // Back down for the release time before the min time is up, it runs the min time
  start(15);
  humidity_release_minutes.state = 1;
  shower();
  humidity.publish_state(50);
  run(4 * 60 + 30);
  CHECK(boosting());
  run(60);
  CHECK(!boosting());

  // Up again before the release time is over, the release time starts over
  start(15);
  shower();
  run(6 * 60);
  humidity.publish_state(51);
  run(4 * 60);
  humidity.publish_state(60);
  run(60);
  humidity.publish_state(51);
  run(30);
  CHECK(boosting());
  run(4 * 60);
  CHECK(boosting());
  run(60);
  CHECK(!boosting());

  // Another shower in the cooldown waits for its end
  start(30);
  shower();
  humidity.publish_state(50);
  run(6 * 60);
  CHECK(!boosting());
  humidity.publish_state(70);
  run(13 * 60);
  CHECK(!boosting());
  run(3 * 60);
  CHECK(boosting());

  // The humidity stays up: it stops at the max time, and doesn't start again until the humidity has been down,
  // even with no cooldown.
  start(30);
  humidity_boost_cooldown_minutes.state = 0;
  shower();
  run(44 * 60);
  CHECK(boosting());
  run(2 * 60);
  CHECK(!boosting());
  run(10 * 60);
  CHECK(!boosting() && cx60.mode == MODE_NORMAL);
  humidity.publish_state(50);
  run(60);
  humidity.publish_state(70);
  run(20);
  CHECK(boosting());

  // A sensor that dies during a boost doesn't leave the unit in max
  start(15);
  shower();
  humidity.publish_state(NAN);
  run(46 * 60);
  CHECK(!boosting() && cx60.mode == MODE_NORMAL);

  // Turned off while boosting
  start(15);
  shower();
  humidity_boost_enable.turn_off();
  run(15);
  CHECK(!boosting());
  run(5);
  CHECK(cx60.mode == MODE_NORMAL);
  humidity_boost_enable.turn_on();
  run(60);
  CHECK(!boosting());  // still high, it waits for the humidity to come down

  // A mode set by hand during a boost wins. No boost again until the humidity is down.
  start(15);
  shower();
  mode_on_panel(MODE_MIN);
  run(15);
  CHECK(!boosting());
  run(10 * 60);
  CHECK(!boosting() && cx60.mode == MODE_MIN);

  // Never in a stopped unit, by itself or by hand
  start(15);
  mode_on_panel(MODE_STOP);
  run(5);
  humidity.publish_state(70);
  run(60);
  humidity_boost.turn_on();
  run(5);
  CHECK(!boosting() && cx60.mode == MODE_STOP);

  // Turned on by hand it ends like any other. The humidity is normal, so after the min time.
  start(15);
  humidity_boost.turn_on();
  run(5);
  CHECK(boosting() && cx60.mode == MODE_MAX);
  run(4 * 60 + 30);
  CHECK(boosting());
  run(60);
  CHECK(!boosting());

  // A fast rise starts it below the trigger delta: the humidity was a little up already, then rises the rise
  // trigger within the rise minutes
  start(15);
  humidity_rise_trigger.state = 5;
  humidity.publish_state(52);
  run(10 * 60);
  CHECK(!boosting());
  humidity.publish_state(57);  // 7 over the baseline, under the trigger delta, but up 5 in a minute
  run(60);
  CHECK(boosting() && cx60.mode == MODE_MAX);

  // The same rise with the rise trigger off doesn't
  start(15);
  humidity.publish_state(52);
  run(10 * 60);
  humidity.publish_state(57);
  run(60);
  CHECK(!boosting());

  printf("humidity boost test: %s\n", fails ? "FAILED" : "ok");
  return fails != 0;
}
