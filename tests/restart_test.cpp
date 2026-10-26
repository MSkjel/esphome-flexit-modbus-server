// Reboot test for controls.h, co2.h and fireplace.h, which change CX60 settings for a while, on the component and
// ESPHome's flash as tests/stubs has them. After hours of CO2, fireplace mode, changes by hand and reboots, the CX60
// must still have the user's fan speeds and mode. Needs the Modbus library, ESP-ModbusRTUServer, in $LIB.
//   g++ -std=gnu++20 -O1 -Wall -Wextra -Wno-unused-parameter -Werror -fsanitize=address,undefined -Itests/stubs
//     -Ipackages/code -Iesphome/components/flexit_modbus_server -I$LIB tests/restart_test.cpp
//     esphome/components/flexit_modbus_server/flexit_modbus_server.cpp $LIB/ModbusRTUServer.cpp
//     packages/code/panel/pbus.c -o restart_test && ./restart_test [runs] [quiet] [run to trace]
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "check.h"
#include "esphome/core/application.h"
#include "flexit_modbus_server.h"

using namespace esphome;
using namespace esphome::flexit_modbus_server;

static uint32_t now_ms;
namespace esphome {
ESPPreferences *global_preferences;
uint32_t micros() { return now_ms * 1000; }
uint32_t millis() { return now_ms; }
}  // namespace esphome

// ---------------------------------------------------------------- the ESP and the packages' entities
static uart::UARTComponent bus;
static std::unique_ptr<FlexitModbusServer> esp;
static FlexitModbusServer *server;

struct Select {
  int index;
  optional<size_t> active_index() const { return (size_t) index; }
};
struct Number {
  float state;
};
struct Sensor {
  float state = NAN;
  bool has = false;
  bool has_state() const { return has; }
};

static Select co2_control{2};  // proportional
static Number co2_target{800}, co2_band{400}, co2_max_fan_increase{30}, co2_pi_gain{5}, co2_pi_integral_time{30};
static Sensor co2;
static Number fireplace_duration{60}, fireplace_supply_speed_control{90}, fireplace_extract_speed_control{20};

#include "controls.h"
#include "co2.h"
#include "fireplace.h"

// ---------------------------------------------------------------- the CX60
// Its settings, kept over its own restarts. The first 15 have the mode and fan speeds.
static const int SETTINGS = 0x0F;
static struct {
  uint16_t reg[SETTINGS];
  bool not_master;  // we're not the lowest panel address, so it keeps its own normal fan speeds
} CX60;

// The bytes on the bus, the ESP's loop, and what it answered
static std::vector<uint8_t> hear(const std::vector<uint8_t> &bytes) {
  bus.rx.insert(bus.rx.end(), bytes.begin(), bytes.end());
  esp->loop();
  std::vector<uint8_t> reply;
  reply.swap(bus.tx);
  return reply;
}

// It polls our coils, 0x14C of them but these are the ones that matter here, reads each register we flagged and
// passes on what it took with FC65, then sends its status
static void cx60_cycle() {
  static const auto poll = frame({1, 0x01, 0x00, 0x00, 0x00, SETTINGS});
  auto coils = hear(poll);
  for (int r = 0; r < SETTINGS; r++) {
    if (!(coils[3 + r / 8] & (1 << (r % 8))))
      continue;
    auto read = hear(frame({1, 0x03, 0x00, (uint8_t) r, 0x00, 0x01}));
    bool normal_fan = r == REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL || r == REG_CMD_PERCENTAGE_EXTRACT_FAN_NORMAL;
    if (!(CX60.not_master && normal_fan))
      CX60.reg[r] = read[3] << 8 | read[4];
    hear(fc65(r, CX60.reg[r]));
  }
  // every cycle on the bus, here only when the mode in it is news to the ESP, to keep the test quick
  if (!server->synced() || server->read_holding_register(REG_MODE) != CX60.reg[REG_CMD_MODE])
    hear(status({{REG_MODE, CX60.reg[REG_CMD_MODE]}}));
}

// It's restarted. It looks for panels, then sends every setting it has.
static void cx60_restart() { hear(fc16(0, std::vector<uint16_t>(CX60.reg, CX60.reg + SETTINGS))); }

// ---------------------------------------------------------------- the ESP
static ESPPreferences flash;  // save() queues, sync() writes what's queued
static bool up;
static uint32_t booted_ms, co2_known_at;

static void boot() {
  up = true;
  booted_ms = now_ms;
  bus.rx.clear();
  bus.tx.clear();
  esp = std::make_unique<FlexitModbusServer>();
  server = esp.get();
  server->set_uart_parent(&bus);
  server->set_server_address(1);
  App.scheduler.clear();
  server->setup();
  flexit::controls::state = {};
  flexit::co2::state = {};
  flexit::fireplace::state = {};
  co2.has = false;  // Home Assistant sends it a while after we start
  co2_known_at = now_ms + 15000;
  // as in the on_boot of co2.yaml and fireplace.yaml
  server->add_on_setting_callback(flexit::co2::setting);
  server->add_on_setting_callback(flexit::fireplace::setting);
}

static void down(bool clean) {  // clean = an update, everything goes to flash. Else a power cut.
  if (clean) {
    server->on_safe_shutdown();
    flash.sync();
  } else {
    flash.queued.clear();
  }
  up = false;
}

// Advances one second
static float co2_level = 600;
static void second() {
  now_ms += 1000;
  if (!up)
    return;
  App.scheduler.run();
  cx60_cycle();
  if (now_ms >= co2_known_at) {
    co2.state = co2_level;
    co2.has = true;
  }
  uint32_t t = now_ms - booted_ms;
  if (t % 2000 == 0)
    flexit::controls::arbiter();
  if (t % 30000 == 0)
    flexit::co2::tick(&co2);
  if (flexit::fireplace::conflict())
    flexit::fireplace::turn_off();  // fireplace.yaml's conflict sensor
  if (t % 60000 == 0)
    flash.sync();  // flash_write_interval
}

// ---------------------------------------------------------------- the user
static int user_supply, user_extract, user_supply_max, user_extract_max, user_mode;

static void set_by_hand(HoldingRegisterIndex r, int v, bool on_a_panel) {
  if (on_a_panel) {
    // The CX60 passes a panel's change on with FC65. One that's the same as a boost's value right now can't be
    // told apart from the CX60 answering us. Not tested.
    if (up && server->read_holding_register(r) == v)
      return;
    // A panel change while the ESP restarts is never seen, the CX60 only sends its settings when it starts.
    // The packages may put back what they knew. Not tested.
    if (!up)
      return;
    CX60.reg[r] = (uint16_t) v;
    hear(fc65(r, (uint16_t) v));
  } else if (up) {  // from Home Assistant, through the ESP
    server->send_cmd(r, (uint16_t) v);
  } else {
    return;
  }
  if (r == REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL)
    user_supply = v;
  if (r == REG_CMD_PERCENTAGE_EXTRACT_FAN_NORMAL)
    user_extract = v;
  if (r == REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX)
    user_supply_max = v;
  if (r == REG_CMD_PERCENTAGE_EXTRACT_FAN_MAX)
    user_extract_max = v;
  if (r == REG_CMD_MODE)
    user_mode = v;
}

static void run(uint32_t s) {
  for (uint32_t i = 0; i < s; i++)
    second();
}

// unit power cycled
static void unit_restart() {
  if (up)
    cx60_restart();
}

static void start_over() {
  now_ms = 0;
  memset(&CX60, 0, sizeof CX60);
  CX60.reg[REG_CMD_MODE] = user_mode = MODE_NORMAL;
  CX60.reg[REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL] = user_supply = 40;
  CX60.reg[REG_CMD_PERCENTAGE_EXTRACT_FAN_NORMAL] = user_extract = 40;
  CX60.reg[REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX] = user_supply_max = 100;
  CX60.reg[REG_CMD_PERCENTAGE_EXTRACT_FAN_MAX] = user_extract_max = 100;
  flash = {};
  co2_level = 600;
  boot();
  cx60_restart();  // so the ESP has them all
  run(60);
}

// A check inside a case, counted with check()'s
static int case_bad;
static void expect(const char *what, int got, bool ok) {
  if (!ok) {
    printf("  %s is %d\n", what, got);
    case_bad++;
  }
}

// Lets everything settle, then checks that the CX60 has the user's speeds and mode. Returns the mismatches.
static int check(const char *what) {
  co2_level = 600;
  if (!up)
    boot();
  run(5);  // let fireplace see a change by hand first
  if (flexit::fireplace::state.active)
    flexit::fireplace::turn_off();
  run(30 * 60);
  int bad = 0;
  auto want = [&](const char *name, int got, int w) {
    if (got != w) {
      printf("  %s: %s is %d, should be %d\n", what, name, got, w);
      bad++;
    }
  };
  want("normal supply", CX60.reg[REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL], user_supply);
  want("normal extract", CX60.reg[REG_CMD_PERCENTAGE_EXTRACT_FAN_NORMAL], user_extract);
  want("max supply", CX60.reg[REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX], user_supply_max);
  want("max extract", CX60.reg[REG_CMD_PERCENTAGE_EXTRACT_FAN_MAX], user_extract_max);
  want("mode", CX60.reg[REG_CMD_MODE], user_mode);
  return bad;
}

static uint32_t rnd_state = 1;
static uint32_t rnd() {
  rnd_state = rnd_state * 1664525u + 1013904223u;
  return rnd_state >> 8;
}

int main(int argc, char **argv) {
  int runs = argc > 1 ? atoi(argv[1]) : 500, failed = 0;

  // known cases first
  struct {
    const char *name;
    void (*f)();
  } cases[] = {
      {"normal speed changed, then a power cut, then CO2",
       [] {
         set_by_hand(REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL, 55, false);
         run(120);
         down(false);
         run(10);
         boot();
         run(60);
         co2_level = 1300;
         run(20 * 60);
       }},
      {"CO2 has the fans up, power cut",
       [] {
         co2_level = 1300;
         run(5 * 60);
         down(false);
         run(10);
         boot();
         run(5 * 60);
       }},
      {"CO2 has the fans up, update",
       [] {
         co2_level = 1300;
         run(5 * 60);
         down(true);
         run(30);
         boot();
         run(5 * 60);
       }},
      {"normal speed changed by hand while CO2 has the fans up",
       [] {
         co2_level = 1300;
         run(5 * 60);
         set_by_hand(REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL, 50, true);
         set_by_hand(REG_CMD_PERCENTAGE_EXTRACT_FAN_NORMAL, 45, false);
         run(10 * 60);
       }},
      {"normal speed set by hand to what CO2 has it at",
       [] {
         co2_level = 1300;
         run(5 * 60);
         set_by_hand(REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL,
                     server->read_holding_register(REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL), false);
         run(10 * 60);
       }},
      {"fireplace mode, power cut",
       [] {
         flexit::fireplace::turn_on();
         run(5 * 60);
         down(false);
         run(10);
         boot();
         run(60);
       }},
      {"fireplace mode, update",
       [] {
         flexit::fireplace::turn_on();
         run(5 * 60);
         down(true);
         run(30);
         boot();
         run(60);
       }},
      {"fireplace mode and CO2, power cut, CO2 again",
       [] {
         co2_level = 1300;
         run(3 * 60);
         flexit::fireplace::turn_on();
         run(5 * 60);
         down(false);
         run(10);
         boot();
         run(20 * 60);
       }},
      {"CO2 back down, power cut just as the fans go back",
       [] {
         co2_level = 1300;
         run(5 * 60);
         co2_level = 600;
         for (int i = 0; i < 40 * 60 && flexit::co2::state.fans_up; i++)
           second();
         down(false);
         run(10);
         boot();
         run(60);
         co2_level = 1300;
         run(10 * 60);
       }},
      {"fireplace mode ends, power cut before the CX60 has it",
       [] {
         flexit::fireplace::turn_on();
         run(5 * 60);
         flexit::fireplace::turn_off();
         hear({});  // the ESP's next loop saves it, the CX60 hasn't polled yet
         down(false);
         run(10);
         boot();
       }},
      {"CO2 has the fans up, power cut before the save, then the unit is restarted",
       [] {
         co2_level = 1300;
         run(45);
         down(false);
         run(10);
         boot();
         run(20);
         unit_restart();
         run(60);
         co2_level = 600;
         run(20 * 60);
       }},
      {"fireplace mode, power cut, the unit is restarted right after the ESP",
       [] {
         flexit::fireplace::turn_on();
         run(5 * 60);
         down(false);
         run(10);
         boot();
         unit_restart();
         run(60);
       }},
      {"max speed changed, power cut, fireplace",
       [] {
         set_by_hand(REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX, 80, false);
         run(90);
         down(false);
         run(10);
         boot();
         run(60);
         flexit::fireplace::turn_on();
         run(10 * 60);
         flexit::fireplace::turn_off();
       }},
      {"mode changed by hand during fireplace mode",
       [] {
         flexit::fireplace::turn_on();
         run(60);
         set_by_hand(REG_CMD_MODE, 1, true);
         run(10 * 60);
       }},
      {"CO2 asks for normal while it's in normal, then min is set by hand",
       [] {
         co2_level = 1300;
         run(5 * 60);
         set_by_hand(REG_CMD_MODE, 1, true);
         run(60);
         expect("mode a minute later", CX60.reg[REG_CMD_MODE], CX60.reg[REG_CMD_MODE] == 1);
       }},
      {"CO2 fan speeds at an address the CX60 doesn't take them from",
       [] {
         CX60.not_master = true;
         co2_level = 1300;
         run(60);
         int writes = flash.writes;
         run(60 * 60);
         expect("flash writes in an hour", flash.writes - writes, flash.writes - writes <= 2);
       }},
  };
  global_preferences = &flash;
  log_on = argc <= 2;
  for (auto &c : cases) {
    start_over();
    case_bad = 0;
    c.f();
    int bad = check(c.name) + case_bad;
    printf("%-74s %s\n", c.name, bad ? "FAILED" : "ok");
    failed += bad != 0;
  }

  // then random runs, hours of random events each
  int fuzz_failed = 0;
  int trace = argc > 3 ? atoi(argv[3]) : -1;
  for (int k = 0; k < runs; k++) {
    rnd_state = (uint32_t) k * 2654435761u + 1;
    log_on = k == trace;
    start_over();
    for (int e = 0; e < 40; e++) {
      run(1 + rnd() % 600);
      int ev = (int) (rnd() % 10);
      if (log_on)
        printf("  %5.0f s: event %d (up %d, co2 %.0f, CX60 normal %d/%d max %d/%d mode %d; ESP %d/%d)\n",
               now_ms / 1000.0, ev, up, co2_level, CX60.reg[3], CX60.reg[8], CX60.reg[4], CX60.reg[9], CX60.reg[0],
               up ? server->read_holding_register(3) : 0, up ? server->read_holding_register(8) : 0);
      switch (ev) {
        case 0:
          co2_level = 1300;
          break;
        case 1:
          co2_level = 600;
          break;
        case 2:
          set_by_hand(rnd() % 2 ? REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL : REG_CMD_PERCENTAGE_EXTRACT_FAN_NORMAL,
                      20 + rnd() % 40, rnd() % 2);
          break;
        case 3:
          set_by_hand(rnd() % 2 ? REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX : REG_CMD_PERCENTAGE_EXTRACT_FAN_MAX,
                      60 + rnd() % 40, rnd() % 2);
          break;
        case 4:
          if (up && !flexit::fireplace::state.active)
            flexit::fireplace::turn_on();
          break;
        case 5:
          if (up)
            flexit::fireplace::turn_off();
          break;
        case 6:
          set_by_hand(REG_CMD_MODE, 1 + rnd() % 3, rnd() % 2);
          break;
        case 7:
          if (up) {
            down(rnd() % 2);
            run(5 + rnd() % 30);
            boot();
          }
          break;
        case 8:
          unit_restart();
          break;
        default:
          break;
      }
    }
    char what[32];
    snprintf(what, sizeof what, "random run %d", k);
    int bad = check(what);
    fuzz_failed += bad != 0;
  }
  printf("%d random runs of 40 events: %d with the CX60 left wrong\n", runs, fuzz_failed);
  return failed || fuzz_failed;
}
