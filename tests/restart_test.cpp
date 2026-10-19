// Reboot test for controls.h, co2.h and fireplace.h, which change CS60 settings for a while, and the server's
// real/temporary settings. After hours of CO2, fireplace mode, changes by hand and reboots, the CS60 must still
// have the user's fan speeds and mode.
//   g++ -std=gnu++20 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -Ipackages/code
//     tests/restart_test.cpp -o restart_test && ./restart_test [runs] [quiet] [run to trace]
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <vector>
#include <optional>
#include <string>

// ---------------------------------------------------------------- just enough of ESPHome
template<typename T> T &id(T *p) { return *p; }
template<typename T> T &id(T &v) { return v; }
static bool quiet = true;
#define ESP_LOG_(c, tag, ...) \
  do { \
    if (!quiet) { \
      printf("    [%c][%s] ", c, tag); \
      printf(__VA_ARGS__); \
      printf("\n"); \
    } \
  } while (0)
#define ESP_LOGE(tag, ...) ESP_LOG_('E', tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) ESP_LOG_('W', tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) ESP_LOG_('I', tag, __VA_ARGS__)
template<typename T> T clamp(T v, T lo, T hi) { return v < lo ? lo : v > hi ? hi : v; }
template<typename T> using optional = std::optional<T>;

static uint32_t now_ms;
uint32_t millis() { return now_ms; }

namespace flexit_modbus_server {
enum HoldingRegisterIndex : uint16_t {
  REG_CMD_MODE = 0x00,
  REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL = 0x03,
  REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX = 0x04,
  REG_CMD_PERCENTAGE_EXTRACT_FAN_NORMAL = 0x08,
  REG_CMD_PERCENTAGE_EXTRACT_FAN_MAX = 0x09,
  REG_MODE = 0xBF,
};
inline std::string mode_to_string(int m) { return m == 0 ? "Stop" : m == 1 ? "Min" : m == 2 ? "Normal" : "Max"; }
}  // namespace flexit_modbus_server
using namespace flexit_modbus_server;

// ---------------------------------------------------------------- flash, as ESPHome's preferences
// The server's save() only queues its record, sync() writes what's queued: every flash_write_interval, when
// asked, and on a clean shutdown. A power cut loses what's queued.
static const int SETTINGS = 0x0F;  // the component's first saved range, with the mode and fan %
struct Saved {
  uint16_t values[SETTINGS];
  bool held[SETTINGS];
};
static Saved flash, queued;
static int flash_writes;  // syncs that changed what's in flash
static void sync() {
  if (memcmp(flash.values, queued.values, sizeof flash.values) || memcmp(flash.held, queued.held, sizeof flash.held))
    flash_writes++;
  flash = queued;
}

// ---------------------------------------------------------------- the CS60 and the server, as the component
static struct {
  uint16_t reg[SETTINGS];  // its settings, kept over its own restarts
  bool not_master;         // we're not the lowest panel address, so it keeps its own normal fan speeds
} CS60;

struct Server {
  uint16_t hold[0x200] = {};
  bool coil[0x200] = {};
  bool heard = false;
  Saved saved = {};
  bool dirty = false, must_sync = false;
  std::vector<std::function<void(uint16_t, uint16_t)>> setting_cbs;

  uint16_t read_holding_register(HoldingRegisterIndex r) { return hold[r]; }
  void add_on_setting_callback(void (*f)(uint16_t, uint16_t)) { setting_cbs.push_back(f); }
  bool synced() const { return heard; }

  void set_held(int r, bool h) {
    if (saved.held[r] == h)
      return;
    saved.held[r] = h;
    dirty = must_sync = true;
  }
  void set_real(int r, uint16_t v) {
    if (saved.values[r] != v) {
      saved.values[r] = v;
      dirty = must_sync = true;
    }
    for (auto &f : setting_cbs)
      f(r, v);
  }
  void bus_written(int r, uint16_t before) {  // bus_written_()
    if (r >= SETTINGS)
      return;
    if (hold[r] != before)
      set_real(r, hold[r]);
    if (hold[r] == saved.values[r])
      set_held(r, false);
  }
  void send_cmd(HoldingRegisterIndex r, uint16_t v, bool temporary = false) {
    uint16_t before = hold[r];
    hold[r] = v;
    coil[r] = true;
    if (r >= SETTINGS)
      return;
    if (temporary)
      set_held(r, true);
    else if (v != before || saved.held[r])
      set_real(r, v);
  }
  uint16_t real_value(HoldingRegisterIndex r) const { return r < SETTINGS ? saved.values[r] : hold[r]; }
  void restore(HoldingRegisterIndex r) {
    if (r >= SETTINGS || !saved.held[r] || hold[r] == saved.values[r])
      return;
    hold[r] = saved.values[r];
    coil[r] = true;
  }
  void save() {  // save_settings_(), at the start of every loop
    if (!dirty)
      return;
    queued = saved;
    if (must_sync)
      sync();
    dirty = must_sync = false;
  }
  void boot() {  // restore_settings_()
    saved = flash;
    for (int r = 0; r < SETTINGS; r++) {
      hold[r] = saved.values[r];
      coil[r] = saved.held[r];
    }
  }
  // The CS60 writing to us with FC6/FC16, every setting when it starts. A command of ours it hasn't read wins.
  void fc16(int r, uint16_t v) {
    if (coil[r])
      return;
    uint16_t before = hold[r];
    hold[r] = v;
    bus_written(r, before);
  }
  // FC65, the CS60's answer to a flagged register, ours or a panel's
  void fc65(int r, uint16_t v) {
    uint16_t before = hold[r];
    hold[r] = v;
    coil[r] = false;
    bus_written(r, before);
  }
} server_obj, *server = &server_obj;

struct Select {
  int index;
  std::optional<size_t> active_index() const { return (size_t) index; }
};
struct Number {
  float state;
};
struct Sensor {
  float state = NAN;
  bool has = false;
  bool has_state() const { return has; }
};

// ---------------------------------------------------------------- the packages' globals and entities
static Select co2_control{2};  // proportional
static Number co2_target{800}, co2_band{400}, co2_max_fan_increase{30}, co2_pi_gain{5}, co2_pi_integral_time{30};
static Sensor co2;
static bool fireplace_mode_active;
static int fireplace_supply_fan_speed = 90, fireplace_extract_fan_speed = 20, fireplace_duration_minutes = 60;

#include "controls.h"
#include "co2.h"
#include "fireplace.h"

// ---------------------------------------------------------------- the ESP
static bool up;
static uint32_t booted_ms, fireplace_off_at, co2_known_at;

// fireplace.yaml's switch, same steps
static void fireplace_on() {
  flexit::fireplace::start();
  if (!fireplace_mode_active)
    return;
  flexit::fireplace::set_fans();
  flexit::controls::request(flexit::fireplace::WHO, 3);
  fireplace_off_at = now_ms + fireplace_duration_minutes * 60000u;
}
static void fireplace_off() {
  fireplace_off_at = 0;
  if (!fireplace_mode_active)
    return;
  fireplace_mode_active = false;
  flexit::fireplace::restore_fans();
  flexit::controls::release(flexit::fireplace::WHO);
}

static void boot() {
  up = true;
  booted_ms = now_ms;
  Server fresh;
  *server = fresh;
  server->boot();
  flexit::controls::requests.clear();
  flexit::controls::target = -1;
  flexit::controls::sent_ms = 0;
  flexit::controls::confirmed = true;
  flexit::controls::user_mode = -1;
  flexit::co2::boost_active = false;
  flexit::co2::boost_start_ms = 0;
  flexit::co2::boost_armed = true;
  flexit::co2::pi_integral = 0;
  flexit::co2::fans_up = false;
  flexit::co2::fans_refused = false;
  flexit::co2::added = 0;
  co2.has = false;  // Home Assistant sends it a while after we start
  co2_known_at = now_ms + 15000;
  fireplace_mode_active = false;
  flexit::fireplace::started_ms = 0;
  flexit::fireplace::changed_by_hand = false;
  fireplace_off_at = 0;
  // as in the on_boot of co2.yaml and fireplace.yaml
  server->add_on_setting_callback(flexit::co2::setting);
  server->add_on_setting_callback(flexit::fireplace::setting);
}

static void down(bool clean) {  // clean = an update, everything goes to flash. Else a power cut.
  if (clean) {
    server->save();  // on_safe_shutdown()
    sync();
  }
  up = false;
}

// Advances one second
static float co2_level = 600;
static void second() {
  now_ms += 1000;
  if (!up)
    return;
  server->save();
  // The CS60 polls every cycle, reads what the ESP flagged and answers with FC65, then sends its status
  for (int r = 0; r < SETTINGS; r++)
    if (server->coil[r]) {
      bool normal_fan = r == REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL || r == REG_CMD_PERCENTAGE_EXTRACT_FAN_NORMAL;
      if (!(CS60.not_master && normal_fan))
        CS60.reg[r] = server->hold[r];
      server->fc65(r, CS60.reg[r]);
    }
  server->hold[REG_MODE] = CS60.reg[REG_CMD_MODE];
  server->heard = true;
  if (now_ms >= co2_known_at) {
    co2.state = co2_level;
    co2.has = true;
  }
  uint32_t t = now_ms - booted_ms;
  if (t % 2000 == 0)
    flexit::controls::arbiter();
  if (t % 30000 == 0)
    flexit::co2::tick(&co2);
  if (fireplace_off_at && now_ms >= fireplace_off_at)
    fireplace_off();
  if (flexit::fireplace::conflict())
    fireplace_off();
  if (t % 60000 == 0)
    sync();  // flash_write_interval
}

// ---------------------------------------------------------------- the user
static int user_supply, user_extract, user_supply_max, user_extract_max, user_mode;

static void set_by_hand(HoldingRegisterIndex r, int v, bool on_a_panel) {
  if (on_a_panel) {
    // The CS60 passes a panel's change on with FC65. One that's the same as a boost's value right now can't be
    // told apart from the CS60 answering us. Not tested.
    if (up && server->hold[r] == v)
      return;
    // A panel change while the ESP restarts is never seen, the CS60 only sends its settings when it starts.
    // The packages may put back what they knew. Not tested.
    if (!up)
      return;
    CS60.reg[r] = (uint16_t) v;
    server->fc65(r, (uint16_t) v);
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

// unit power cycled. It looks for panels, then sends every setting it has
static void unit_restart() {
  if (!up)
    return;
  for (int r = 0; r < SETTINGS; r++)
    server->fc16(r, CS60.reg[r]);
  server->hold[REG_MODE] = CS60.reg[REG_CMD_MODE];
}

static void start_over() {
  now_ms = 0;
  memset(&CS60, 0, sizeof CS60);
  CS60.reg[REG_CMD_MODE] = user_mode = 2;
  CS60.reg[REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL] = user_supply = 40;
  CS60.reg[REG_CMD_PERCENTAGE_EXTRACT_FAN_NORMAL] = user_extract = 40;
  CS60.reg[REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX] = user_supply_max = 100;
  CS60.reg[REG_CMD_PERCENTAGE_EXTRACT_FAN_MAX] = user_extract_max = 100;
  memset(&flash, 0, sizeof flash);
  memcpy(flash.values, CS60.reg, sizeof flash.values);  // the CS60 sent them all when it started
  queued = flash;
  co2_level = 600;
  boot();
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

// Lets everything settle, then checks that the CS60 has the user's speeds and mode. Returns the mismatches.
static int check(const char *what) {
  co2_level = 600;
  if (!up)
    boot();
  run(5);  // let fireplace see a change by hand first
  if (fireplace_mode_active)
    fireplace_off();
  run(30 * 60);
  int bad = 0;
  auto want = [&](const char *name, int got, int w) {
    if (got != w) {
      printf("  %s: %s is %d, should be %d\n", what, name, got, w);
      bad++;
    }
  };
  want("normal supply", CS60.reg[REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL], user_supply);
  want("normal extract", CS60.reg[REG_CMD_PERCENTAGE_EXTRACT_FAN_NORMAL], user_extract);
  want("max supply", CS60.reg[REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX], user_supply_max);
  want("max extract", CS60.reg[REG_CMD_PERCENTAGE_EXTRACT_FAN_MAX], user_extract_max);
  want("mode", CS60.reg[REG_CMD_MODE], user_mode);
  return bad;
}

static uint32_t rnd_state = 1;
static uint32_t rnd() {
  rnd_state = rnd_state * 1664525u + 1013904223u;
  return rnd_state >> 8;
}

int main(int argc, char **argv) {
  int runs = argc > 1 ? atoi(argv[1]) : 2000, failed = 0;

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
         set_by_hand(REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL, server->hold[REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL], false);
         run(10 * 60);
       }},
      {"fireplace mode, power cut",
       [] {
         fireplace_on();
         run(5 * 60);
         down(false);
         run(10);
         boot();
         run(60);
       }},
      {"fireplace mode, update",
       [] {
         fireplace_on();
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
         fireplace_on();
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
         for (int i = 0; i < 40 * 60 && flexit::co2::fans_up; i++)
           second();
         down(false);
         run(10);
         boot();
         run(60);
         co2_level = 1300;
         run(10 * 60);
       }},
      {"fireplace mode ends, power cut before the CS60 has it",
       [] {
         fireplace_on();
         run(5 * 60);
         fireplace_off();
         server->save();  // the ESP's next loop, the CS60 hasn't polled yet
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
         fireplace_on();
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
         fireplace_on();
         run(10 * 60);
         fireplace_off();
       }},
      {"mode changed by hand during fireplace mode",
       [] {
         fireplace_on();
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
         expect("mode a minute later", CS60.reg[REG_CMD_MODE], CS60.reg[REG_CMD_MODE] == 1);
       }},
      {"CO2 fan speeds at an address the CS60 doesn't take them from",
       [] {
         CS60.not_master = true;
         co2_level = 1300;
         run(60);
         int writes = flash_writes;
         run(60 * 60);
         expect("flash writes in an hour", flash_writes - writes, flash_writes - writes <= 2);
       }},
  };
  quiet = argc > 2;
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
    quiet = k != trace;
    start_over();
    for (int e = 0; e < 40; e++) {
      run(1 + rnd() % 600);
      int ev = (int) (rnd() % 10);
      if (!quiet)
        printf("  %5.0f s: event %d (up %d, co2 %.0f, CS60 normal %d/%d max %d/%d mode %d; ESP %d/%d)\n",
               now_ms / 1000.0, ev, up, co2_level, CS60.reg[3], CS60.reg[8], CS60.reg[4], CS60.reg[9], CS60.reg[0],
               server->hold[3], server->hold[8]);
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
          if (up && !fireplace_mode_active)
            fireplace_on();
          break;
        case 5:
          if (up)
            fireplace_off();
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
  printf("%d random runs of 40 events: %d with the CS60 left wrong\n", runs, fuzz_failed);
  return failed || fuzz_failed;
}
