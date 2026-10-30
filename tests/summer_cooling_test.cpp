// summer_cooling.h on the component and a CX60 (cx60.h): when cooling starts and stops, and the CX60's mode.
// Needs the Modbus library, see the Makefile.
//   make -C tests summer_cooling_test
#include <cstdio>
#include <cstdlib>
#include "cx60.h"

// summer_cooling.yaml's entities, at their defaults
static Switch summer_cooling_enable;
static Number summer_cooling_above{24}, summer_cooling_above_setpoint{1}, summer_cooling_min_difference{2},
    summer_cooling_min_outdoor{12};

#include "summer_cooling.h"

namespace sc = flexit::summer_cooling;

// Runs `s` seconds, the tick every 30 s like summer_cooling.yaml
static void run(uint32_t s) {
  for (uint32_t i = 0; i < s; i++) {
    cx60_second();
    if (now_ms % 30000 == 0)
      sc::tick();
  }
}

static bool cooling() { return sc::state.boost.active; }

// Booted on a warm evening: 25 C inside, 15 C outside, the setpoint at 20 C
static void start() {
  boot();
  sc::state = {};
  summer_cooling_enable = {true};
  cx60.extract = 25;
  cx60.outdoor = 15;
  cx60.setpoint = 20;
}

// It starts cooling at the next tick, and the CX60 runs max
static void starts() {
  run(35);
  CHECK(cooling());
  run(5);
  CHECK(cx60.mode == MODE_MAX);
}

int main() {
  log_on = getenv("LOG") != nullptr;

  // Cooled down: it runs at least 15 minutes, then stops half a degree under where it started
  start();
  starts();
  cx60.extract = 23.4f;
  run(5 * 60);
  CHECK(cooling());
  run(10 * 60);
  CHECK(!cooling());
  run(5);
  CHECK(cx60.mode == MODE_NORMAL);

  // Not quite cooled down, it keeps going
  start();
  starts();
  cx60.extract = 23.6f;
  run(30 * 60);
  CHECK(cooling());

  // The outdoor air warms up to near the inside, it's no use anymore
  start();
  starts();
  run(15 * 60);
  cx60.outdoor = 24.2f;
  run(35);
  CHECK(!cooling());

  // Stops at once when the heater comes on, or the inside is under the setpoint, or it's turned off
  start();
  starts();
  cx60.heating = 10;
  run(35);
  CHECK(!cooling());
  start();
  starts();
  cx60.setpoint = 26;
  run(35);
  CHECK(!cooling());
  start();
  starts();
  summer_cooling_enable.turn_off();
  run(35);
  CHECK(!cooling());
  run(5);
  CHECK(cx60.mode == MODE_NORMAL);

  // Not when any one says no: too cool inside, the setpoint too near the inside, outside not cool enough or too
  // cold, the heater on, turned off, the unit stopped
  for (int no = 0; no < 7; no++) {
    start();
    if (no == 0)
      cx60.extract = 23.9f;
    if (no == 1)
      cx60.setpoint = 24.5f;
    if (no == 2)
      cx60.outdoor = 23.5f;
    if (no == 3)
      cx60.outdoor = 11.5f;
    if (no == 4)
      cx60.heating = 5;
    if (no == 5)
      summer_cooling_enable.turn_off();
    if (no == 6)
      mode_on_panel(MODE_STOP);
    run(2 * 60);
    CHECK(!cooling());
    CHECK(cx60.mode != MODE_MAX);
  }

  // A mode set by hand while cooling wins until there's no reason to cool, then it cools again.
  start();
  starts();
  mode_on_panel(MODE_MIN);
  run(35);
  CHECK(!cooling());
  run(10 * 60);
  CHECK(!cooling() && cx60.mode == MODE_MIN);
  cx60.outdoor = 24;
  run(35);
  cx60.outdoor = 15;
  starts();

  printf("summer cooling test: %s\n", fails ? "FAILED" : "ok");
  return fails != 0;
}
