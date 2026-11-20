// The component and a CX60 on a bus, for the tests of the packages that boost. One program per test, so the
// clock and flash are here too. Include it, then the package's entities, then its header.
#pragma once
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include "check.h"
#include "esphome/core/application.h"
#include "flexit_modbus_server.h"

using namespace esphome;
using namespace esphome::flexit_modbus_server;

static uint32_t now_ms;
static ESPPreferences flash;
namespace esphome {
ESPPreferences *global_preferences = &flash;
uint32_t micros() { return now_ms * 1000; }
uint32_t millis() { return now_ms; }
}  // namespace esphome

// The entities, as much as the packages use
struct Number {
  float state;
};
struct Sensor {
  float state = NAN;
  bool has = false;
  bool has_state() const { return has; }
  void publish_state(float v) {
    state = v;
    has = true;
  }
};
struct Switch {
  bool state;
  void (*on)() = nullptr;  // turn_on_action and turn_off_action, else it's optimistic
  void (*off)() = nullptr;
  void publish_state(bool s) { state = s; }
  void turn_on() { on ? on() : publish_state(true); }
  void turn_off() { off ? off() : publish_state(false); }
};

static uart::UARTComponent bus;
static std::unique_ptr<FlexitModbusServer> esp;
static FlexitModbusServer *server;

#include "controls.h"

// The simulated CX60
static struct {
  uint16_t setting;       // the mode setting, REG_CMD_MODE
  uint16_t mode;          // the mode it runs: timer_speed while the max timer runs, else the week program's, else
                          // the setting
  uint16_t week_program;  // the mode the week program sets now, MODE_STOP = nothing
  uint16_t timer_minutes;
  uint16_t timer_speed;              // the mode the max timer runs, Max Timer Speed
  uint32_t timer_s;                  // seconds left on the max timer
  bool no_address_1;                 // nothing answers at address 1, so no max timer and no week program
  float extract, outdoor, setpoint;  // C
  uint16_t heating;                  // %
} cx60;

static void cx60_run() {
  cx60.mode = cx60.timer_s                              ? cx60.timer_speed
              : cx60.week_program && !cx60.no_address_1 ? cx60.week_program
                                                        : cx60.setting;
}

// The bytes on the bus, the ESP's loop, and what it answered
static std::vector<uint8_t> hear(const std::vector<uint8_t> &bytes) {
  bus.rx.insert(bus.rx.end(), bytes.begin(), bytes.end());
  esp->loop();
  std::vector<uint8_t> reply;
  reply.swap(bus.tx);
  return reply;
}

// Someone sets the mode on a panel. While the week program sets the mode, the CX60 runs it only after that.
static void mode_on_panel(uint16_t mode) {
  cx60.setting = mode;
  cx60_run();
  hear(fc65(REG_CMD_MODE, mode));
}

// The CX60 reads a register we flagged, applies it, and passes the value on (FC65)
static void cx60_read(uint16_t reg) {
  auto read = hear(frame({1, 0x03, (uint8_t) (reg >> 8), (uint8_t) reg, 0x00, 0x01}));
  uint16_t v = read[3] << 8 | read[4];
  if (reg == REG_CMD_MODE)
    cx60.setting = v;
  else if (reg == REG_CMD_MINUTES_MAX_TIMER)
    cx60.timer_minutes = v;
  else if (reg == REG_CMD_MAX_TIMER_SPEED)
    cx60.timer_speed = v;
  else if (reg == REG_CMD_START_MAX_TIMER && v && !cx60.no_address_1)
    cx60.timer_s = cx60.timer_minutes * 60u;
  else if (reg == REG_CMD_STOP_MAX_TIMER && v)
    cx60.timer_s = 0;
  hear(fc65(reg, v));
}

// Our coils, read in one go from 0 like the CX60 does: bit `reg` set = we flagged it for it to read
static std::vector<uint8_t> cx60_coils() { return hear(frame({1, 0x01, 0x00, 0x00, 0x01, 0x4C})); }
static bool flagged(const std::vector<uint8_t> &coils, uint16_t reg) {
  return coils.size() > 3u + reg / 8 && (coils[3 + reg / 8] & (1 << (reg % 8)));
}

static uint16_t tenths(float c) { return (uint16_t) (int16_t) lroundf(c * 10); }

// One second: the CX60 reads the registers we flagged, runs its timer and mode, and sends its status. Then the ESP's
// timeouts run, and the arbiter every 2 s as in controls.yaml.
static void cx60_second() {
  now_ms += 1000;
  auto coils = cx60_coils();
  for (uint16_t reg : {REG_CMD_MODE, REG_CMD_MAX_TIMER_SPEED, REG_CMD_MINUTES_MAX_TIMER, REG_CMD_START_MAX_TIMER,
                       REG_CMD_STOP_MAX_TIMER})
    if (flagged(coils, reg))
      cx60_read(reg);
  if (cx60.timer_s)
    cx60.timer_s--;
  cx60_run();
  hear(status({{REG_MODE, cx60.mode},
               {REG_SECONDS_REMAINING_MAX_TIMER, (uint16_t) cx60.timer_s},
               {REG_TEMPERATURE_SETPOINT, tenths(cx60.setpoint)},
               {REG_TEMPERATURE_EXTRACT_AIR, tenths(cx60.extract)},
               {REG_TEMPERATURE_OUTDOOR_AIR, tenths(cx60.outdoor)},
               {REG_PERCENTAGE_HEATING, cx60.heating}}));
  App.scheduler.run();
  if (now_ms % 2000 == 0)
    flexit::controls::arbiter();
}

// A fresh flash, and the CX60 in normal sends the ESP its mode setting, Max Timer Speed and max timer length
static void boot() {
  now_ms = 0;
  flash = {};
  bus.rx.clear();
  bus.tx.clear();
  App.scheduler.clear();
  esp = std::make_unique<FlexitModbusServer>();
  server = esp.get();
  server->set_uart_parent(&bus);
  server->set_server_address(1);
  server->setup();
  flexit::controls::state = {};
  cx60 = {MODE_NORMAL, MODE_NORMAL, MODE_STOP, 180, MODE_MAX, 0, false, 22, 10, 20, 0};
  hear(fc16(REG_CMD_MODE, {cx60.setting}));
  hear(fc16(REG_CMD_MAX_TIMER_SPEED, {cx60.timer_speed, cx60.timer_minutes}));
}
