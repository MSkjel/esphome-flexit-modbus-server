// The component on a PC, ESPHome stood in by tests/stubs: frames in from the bus, what it answers and keeps.
// Needs the Modbus library, ESP-ModbusRTUServer, in $LIB.
//   g++ -std=gnu++20 -O1 -Wall -Wextra -Wno-unused-parameter -Werror -fsanitize=address,undefined -Itests/stubs
//     -Iesphome/components/flexit_modbus_server -I$LIB tests/server_test.cpp
//     esphome/components/flexit_modbus_server/flexit_modbus_server.cpp $LIB/ModbusRTUServer.cpp -o server_test
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
#include "flexit_modbus_server.h"

using namespace esphome;
using namespace esphome::flexit_modbus_server;

namespace esphome {
ESPPreferences *global_preferences;
static uint32_t now_us;
uint32_t micros() { return now_us; }
uint32_t millis() { return now_us / 1000; }
}  // namespace esphome

static int fails;
#define CHECK(c) \
  do { \
    if (!(c)) { \
      printf("%s:%d: %s\n", __FILE__, __LINE__, #c); \
      fails++; \
    } \
  } while (0)

static std::vector<uint8_t> frame(std::vector<uint8_t> f) {
  uint16_t crc = 0xFFFF;
  for (uint8_t b : f) {
    crc ^= b;
    for (int i = 0; i < 8; i++)
      crc = crc & 1 ? (crc >> 1) ^ 0xA001 : crc >> 1;
  }
  f.push_back(crc & 0xFF);
  f.push_back(crc >> 8);
  return f;
}

// the CS60 writing a register to everyone
static std::vector<uint8_t> fc6(uint16_t reg, uint16_t value) {
  return frame({0, 0x06, (uint8_t) (reg >> 8), (uint8_t) reg, (uint8_t) (value >> 8), (uint8_t) value});
}

// The ESP at address 1, booted with what's in flash
struct Esp {
  uart::UARTComponent bus;
  FlexitModbusServer server;
  std::vector<size_t> frames;  // what the frame callback got

  Esp() {
    server.set_uart_parent(&bus);
    server.set_server_address(1);
    server.setup();
    server.add_on_frame_callback([this](const uint8_t *, size_t len) { frames.push_back(len); });
  }
  // bytes on the bus, then a loop
  void hear(const std::vector<uint8_t> &bytes) {
    bus.rx.insert(bus.rx.end(), bytes.begin(), bytes.end());
    now_us += 1000;
    server.loop();
  }
  float filter_hours() { return server.read_holding_register_hours(REG_RUNTIME_FILTER_HIGH); }
};

static std::vector<uint8_t> operator+(std::vector<uint8_t> a, const std::vector<uint8_t> &b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

int main() {
  ESPPreferences flash;
  global_preferences = &flash;
  log_on = getenv("LOG") != nullptr;

  const auto settings_read = frame({2, 0x01, 0x00, 0x00, 0x01, 0x4C});  // panel 2's settings, 47 byte reply
  const auto counters_read = frame({2, 0x01, 0x01, 0x4C, 0x00, 0x14});  // panel 2's counters
  const auto our_read = frame({1, 0x01, 0x01, 0x4C, 0x00, 0x14});

  // Panel 2 doesn't answer, and the CS60's next read to it starts like the reply would. Ours after it is
  // answered right away.
  {
    auto esp = std::make_unique<Esp>();
    esp->hear(settings_read + counters_read + our_read);
    CHECK(esp->bus.tx.size() == 8 && esp->bus.tx[0] == 1 && esp->bus.tx[1] == 0x01);
  }

  // Panel 2 answers in pieces. The reply is one frame, and ours after it is answered.
  {
    auto esp = std::make_unique<Esp>();
    std::vector<uint8_t> reply = {2, 0x01, 42};
    for (int i = 0; i < 42; i++)
      reply.push_back((uint8_t) (i * 37));
    reply = frame(reply);
    esp->hear(settings_read);
    esp->hear(std::vector<uint8_t>(reply.begin(), reply.begin() + 20));
    esp->hear(std::vector<uint8_t>(reply.begin() + 20, reply.end()) + our_read);
    CHECK((esp->frames == std::vector<size_t>{8, 47, 8, 8}));
    CHECK(esp->bus.tx.size() == 8);
  }

  // The filter counter at 50000 s, then 60000 s, then an update. It comes back at 60000.
  {
    auto esp = std::make_unique<Esp>();
    esp->hear(fc6(REG_RUNTIME_FILTER_HIGH, 0) + fc6(REG_RUNTIME_FILTER_LOW, 50000));
    now_us += 10000000;
    esp->hear({});  // a new counter, saved
    flash.sync();
    esp->hear(fc6(REG_RUNTIME_FILTER_LOW, 60000));
    now_us += 10000000;
    esp->hear({});
    esp->server.on_safe_shutdown();
    flash.sync();
  }
  {
    auto esp = std::make_unique<Esp>();
    CHECK(esp->filter_hours() == 60000 / 3600.0f);
    // the low word wrapped while we were off
    esp->hear(fc6(REG_RUNTIME_FILTER_LOW, 100));
    CHECK(esp->filter_hours() == (65536 + 100) / 3600.0f);
  }
  // A filter reset. The CS60 passes the command on, then sends the low word, the high one was 0 already.
  {
    auto esp = std::make_unique<Esp>();
    esp->hear(frame({0, 0x65, 0x01, 0x16, 0x00, 0x00}) + fc6(REG_RUNTIME_FILTER_LOW, 0));
    CHECK(esp->filter_hours() == 0);
  }

  printf("server test: %s\n", fails ? "FAILED" : "ok");
  return fails != 0;
}
