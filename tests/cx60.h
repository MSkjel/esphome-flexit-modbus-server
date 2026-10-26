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

// The CX60: the mode it runs (also its mode setting) and its status
static struct {
  uint16_t mode;
  float extract, outdoor, setpoint;  // C
  uint16_t heating;                  // %
} cx60;

// The bytes on the bus, the ESP's loop, and what it answered
static std::vector<uint8_t> hear(const std::vector<uint8_t> &bytes) {
  bus.rx.insert(bus.rx.end(), bytes.begin(), bytes.end());
  esp->loop();
  std::vector<uint8_t> reply;
  reply.swap(bus.tx);
  return reply;
}

// Somebody sets the mode on a panel, the CX60 passes it on
static void mode_on_panel(uint16_t mode) {
  cx60.mode = mode;
  hear(fc65(REG_CMD_MODE, mode));
}

static uint16_t tenths(float c) { return (uint16_t) (int16_t) lroundf(c * 10); }

// A second. The CX60 reads the mode if we flagged it, runs it and sends its status. Then the ESP's timeouts, and the
// arbiter every 2 s like controls.yaml.
static void cx60_second() {
  now_ms += 1000;
  auto coils = hear(frame({1, 0x01, 0x00, 0x00, 0x00, 0x01}));
  if (coils.size() > 3 && (coils[3] & 1)) {
    auto read = hear(frame({1, 0x03, 0x00, 0x00, 0x00, 0x01}));
    cx60.mode = read[3] << 8 | read[4];
    hear(fc65(REG_CMD_MODE, cx60.mode));
  }
  hear(status({{REG_MODE, cx60.mode},
               {REG_TEMPERATURE_SETPOINT, tenths(cx60.setpoint)},
               {REG_TEMPERATURE_EXTRACT_AIR, tenths(cx60.extract)},
               {REG_TEMPERATURE_OUTDOOR_AIR, tenths(cx60.outdoor)},
               {REG_PERCENTAGE_HEATING, cx60.heating}}));
  App.scheduler.run();
  if (now_ms % 2000 == 0)
    flexit::controls::arbiter();
}

// A fresh flash, and the CX60 in normal sends the ESP its mode setting
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
  cx60 = {MODE_NORMAL, 22, 10, 20, 0};
  hear(fc16(REG_CMD_MODE, {cx60.mode}));
}
