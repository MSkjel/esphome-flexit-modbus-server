// heater_limit.h on the component and a CX60 (cx60.h): when the heater is held off, when the CX60 holds the supply
// air at the limit, and the real heater and setpoint coming back. Needs the Modbus library, see the Makefile.
//   make -C tests heater_limit_test
#include <cstdio>
#include <cstdlib>
#include "cx60.h"

// heater_limit.yaml's entities, at their defaults
static Switch heater_limit_enable{true};
static Number heater_limit_supply{20};

#include "heater_limit.h"

namespace hl = flexit::heater_limit;

// Runs `s` seconds, the tick every 10 s like heater_limit.yaml
static void run(uint32_t s) {
  for (uint32_t i = 0; i < s; i++) {
    cx60_second();
    if (now_ms % 10000 == 0)
      hl::tick();
  }
}

static bool heater() { return cx60.heater[0] || cx60.heater[1] || cx60.heater[2]; }
// The rotor alone: the heater held off, the real setpoint
static bool rotor() { return !heater() && cx60.setpoint == 25; }
// The CX60 holds the supply air at the limit
static bool at_limit() { return heater() && cx60.setpoint == 20; }

// Booted on supply air regulation at a 25 C setpoint, the heater on, and the rotor alone giving 21 C
static void start() {
  boot();
  hl::state = {};
  heater_limit_enable = {true};
  cx60.setpoint = 25;
  cx60.heater[0] = 1;
  cx60.supply = 21;
  hear(fc16(REG_CMD_TEMPERATURE_SETPOINT, {tenths(cx60.setpoint)}));
  hear(fc16(REG_CMD_HEATER_PANEL1, {1, 0, 0}));
  hear(fc16(REG_CMD_TEMPERATURE_SUPPLY_AIR_CONTROL, {1}));
  run(20);
}

// The ESP restarts right after a tick, with its flash
static void restart() {
  while (now_ms % 10000)
    run(1);
  esp_up();
  hl::state = {};
}

int main() {
  log_on = getenv("LOG") != nullptr;

  // The rotor manages: the heater is held off, and the real one kept
  start();
  CHECK(rotor());
  CHECK(server->real_value(REG_CMD_HEATER_PANEL1) == 1);

  // Under the limit, but not by half a degree: still the rotor alone
  cx60.supply = 19.6f;
  run(30);
  CHECK(rotor());

  // Half a degree under: the heater is back and the CX60 holds the limit, the real setpoint kept
  cx60.supply = 19.4f;
  run(15);
  CHECK(at_limit());
  CHECK(server->real_value(REG_CMD_TEMPERATURE_SETPOINT) == 250);

  // While it heats, or idles under the limit before the CX60 starts it, it stays
  cx60.heating = 30;
  cx60.supply = 20;
  run(10 * 60);
  CHECK(at_limit());
  cx60.heating = 0;
  cx60.supply = 19.7f;
  run(10 * 60);
  CHECK(at_limit());

  // Idle at the limit for 3 minutes: the rotor alone again
  cx60.supply = 20.1f;
  run(2 * 60);
  CHECK(at_limit());
  run(2 * 60);
  CHECK(rotor());

  // The heater turned on by hand is taken back
  cx60.heater[0] = 1;
  hear(fc65(REG_CMD_HEATER_PANEL1, 1));
  run(15);
  CHECK(rotor());

  // A restart sends the CX60 the real heater before the first tick, then it's held off again
  restart();
  run(5);
  CHECK(heater() && cx60.setpoint == 25);
  run(10);
  CHECK(rotor());

  // And the real setpoint while it held the limit
  cx60.supply = 19.4f;
  run(15);
  CHECK(at_limit());
  restart();
  run(5);
  CHECK(heater() && cx60.setpoint == 25);

  // It stands aside, the real heater and setpoint back, while it's off, the CX60 regulates on the extract air, the
  // heater is a water coil, or there's no supply air temperature
  static void (*const aside[])(bool) = {
      [](bool on) { heater_limit_enable = {!on}; },
      [](bool on) { hear(fc65(REG_CMD_TEMPERATURE_SUPPLY_AIR_CONTROL, !on)); },
      [](bool on) { cx60.water = on; },
      [](bool on) { cx60.supply = on ? -200 : 21; },
  };
  for (auto set : aside) {
    start();
    set(true);
    run(15);
    CHECK(heater() && cx60.setpoint == 25);
    set(false);
    run(15);
    CHECK(rotor());
  }

  // Or the setpoint isn't above the limit
  start();
  cx60.setpoint = 20;
  hear(fc65(REG_CMD_TEMPERATURE_SETPOINT, 200));
  run(15);
  CHECK(heater() && cx60.setpoint == 20);

  // The heater turned off by hand while the CX60 holds the limit: it stays off, at the real setpoint
  start();
  cx60.supply = 19.4f;
  run(15);
  CHECK(at_limit());
  cx60.heater[0] = 0;
  hear(fc65(REG_CMD_HEATER_PANEL1, 0));
  run(15);
  CHECK(!heater() && cx60.setpoint == 25);
  cx60.supply = 18;
  run(60);
  CHECK(!heater() && cx60.setpoint == 25);

  printf("heater limit test: %s\n", fails ? "FAILED" : "ok");
  return fails != 0;
}
