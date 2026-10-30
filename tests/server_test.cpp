// The component on a PC, ESPHome stood in by tests/stubs: frames in from the bus, what it answers and keeps.
// Needs the Modbus library, see the Makefile.
//   make -C tests server_test
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
#include "check.h"
#include "flexit_modbus_server.h"

using namespace esphome;
using namespace esphome::flexit_modbus_server;

namespace esphome {
ESPPreferences *global_preferences;
static uint32_t now_us;
uint32_t micros() { return now_us; }
uint32_t millis() { return now_us / 1000; }
}  // namespace esphome

// the CX60's status broadcast, with the filter and total hours
static std::vector<uint8_t> status(uint16_t filter_hours, uint16_t total_hours) {
  return status({{REG_HOURS_SINCE_FILTER_RESET, filter_hours}, {REG_HOURS_TOTAL_RUNTIME, total_hours}});
}

// The ESP at address 1, booted with what's in flash
struct Esp {
  uart::UARTComponent bus;
  FlexitModbusServer server;
  std::vector<size_t> frames;  // what the frame callback got
  std::vector<uint8_t> heard;  // and the bytes of it that weren't ours

  explicit Esp(uint32_t preference_hash = 0) {
    server.set_uart_parent(&bus);
    server.set_server_address(1);
    server.set_preference_hash(preference_hash);
    server.setup();
    server.add_on_frame_callback([this](const uint8_t *data, size_t len) {
      frames.push_back(len);
      if (!server.is_own_frame())
        heard.insert(heard.end(), data, data + len);
    });
  }
  ~Esp() { App.scheduler.clear(); }  // its intervals
  // bytes on the bus, then a loop
  void hear(const std::vector<uint8_t> &bytes) {
    bus.rx.insert(bus.rx.end(), bytes.begin(), bytes.end());
    now_us += 1000;
    App.scheduler.run();
    server.loop();
  }
  float filter_hours() { return server.read_holding_register_hours(REG_RUNTIME_FILTER_HIGH); }
  float total_hours() { return server.read_holding_register_hours(REG_RUNTIME_HIGH); }
  // the CX60 sends a counter's words, and we save them
  void save(uint16_t high, uint32_t seconds) {
    hear(fc6(high, seconds >> 16) + fc6(high + 1, seconds & 0xFFFF));
    now_us += 10000000;
    hear({});
    global_preferences->sync();
  }
};

int main() {
  ESPPreferences flash;
  global_preferences = &flash;
  log_on = getenv("LOG") != nullptr;

  const auto settings_read = frame({2, 0x01, 0x00, 0x00, 0x01, 0x4C});  // panel 2's settings, 47 byte reply
  const auto counters_read = frame({2, 0x01, 0x01, 0x4C, 0x00, 0x14});  // panel 2's counters
  const auto our_read = frame({1, 0x01, 0x01, 0x4C, 0x00, 0x14});

  // Panel 2 doesn't answer, and the CX60's next read to it starts like the reply would. Ours after it is
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

  // The updater's probe of 3: its 13-byte reply a byte at a time, then silence. It arrives whole.
  {
    auto esp = std::make_unique<Esp>();
    esp->server.set_updating(true);
    uint8_t probe[8] = {3, 0x04, 0, 0, 0, 4};
    esp->server.send_frame(probe, 6);
    esp->server.expect_reply(3, 0x04, 13);
    for (uint8_t b : frame({3, 0x04, 8, 0x43, 0x49, 0, 0, 0, 5, 1, 2}))
      esp->hear({b});
    esp->hear({});
    CHECK((esp->frames == std::vector<size_t>{8, 13}));  // ours, then the reply
  }

  // A reply after a junk byte or without its first byte, then silence, then a whole one
  {
    auto esp = std::make_unique<Esp>();
    esp->server.set_updating(true);
    const auto reply = frame({3, 0x04, 8, 0x43, 0x49, 0, 0, 0, 5, 1, 2});
    for (const auto &bytes :
         {std::vector<uint8_t>{0xFF} + reply, std::vector<uint8_t>(reply.begin() + 1, reply.end())}) {
      esp->heard.clear();
      esp->server.expect_reply(3, 0x04, 13);
      esp->hear(bytes);
      esp->hear({});
      CHECK(esp->heard.size() < bytes.size());  // could still be a frame coming in
      esp->hear({});
      esp->hear({});
      CHECK(esp->heard == bytes);
    }
    esp->frames.clear();
    esp->heard.clear();
    esp->server.expect_reply(3, 0x04, 13);
    esp->hear(reply);
    CHECK((esp->frames == std::vector<size_t>{13}) && esp->heard == reply);
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
  // A filter reset. The CX60 passes the command on, then sends the low word, the high one was 0 already.
  {
    auto esp = std::make_unique<Esp>();
    esp->hear(fc65(REG_CMD_CLEAR_FILTER_ALARM, 0) + fc6(REG_RUNTIME_FILTER_LOW, 0));
    CHECK(esp->filter_hours() == 0);
  }

  // A filter reset while we were off: the low word went back, but the status says 0 h, so no wrap
  flash.flash.clear();
  std::make_unique<Esp>()->save(REG_RUNTIME_FILTER_HIGH, 65536 + 40000);
  {
    auto esp = std::make_unique<Esp>();
    esp->hear(fc6(REG_RUNTIME_FILTER_LOW, 100) + status(0, 0));
    CHECK(esp->filter_hours() == 100 / 3600.0f);
  }

  // Off for 20 h, the low word wrapped past the saved one
  std::make_unique<Esp>()->save(REG_RUNTIME_FILTER_HIGH, 10000);
  {
    auto esp = std::make_unique<Esp>();
    esp->hear(fc6(REG_RUNTIME_FILTER_LOW, (10000 + 72000) & 0xFFFF) + status(82000 / 3600, 0));
    CHECK(esp->filter_hours() == 82000 / 3600.0f);
  }

  // Off for 3 days at 75693 h, past where the total's status hours wrap
  const uint32_t total = 4157u * 65536 + 64100, later = total + 3 * 86400;
  std::make_unique<Esp>()->save(REG_RUNTIME_HIGH, total);
  {
    auto esp = std::make_unique<Esp>();
    esp->hear(fc6(REG_RUNTIME_LOW, later & 0xFFFF) + status(0, later / 3600 % 65536));
    CHECK(esp->total_hours() == later / 3600.0f);
  }

  // A fresh install: the filter's from the status, the total may be past 65536 h so it needs its high word
  flash.flash.clear();
  {
    auto esp = std::make_unique<Esp>();
    esp->hear(fc6(REG_RUNTIME_FILTER_LOW, 5000) + fc6(REG_RUNTIME_LOW, 5000) + status(201608 / 3600, 1));
    CHECK(esp->filter_hours() == 201608 / 3600.0f);
    CHECK(std::isnan(esp->total_hours()));
  }

  // Two servers keep their records apart
  std::make_unique<Esp>(1)->save(REG_RUNTIME_FILTER_HIGH, 50000);
  CHECK(std::make_unique<Esp>(1)->filter_hours() == 50000 / 3600.0f);
  CHECK(std::isnan(std::make_unique<Esp>(2)->filter_hours()));

  // Unread command and setting when the CX60 restarts: the setting stays ours, the command is dropped
  {
    auto esp = std::make_unique<Esp>();
    esp->server.send_cmd(REG_CMD_START_MAX_TIMER, 1);
    esp->server.send_cmd(REG_CMD_HEATER_PANEL1, 1);
    std::vector<uint8_t> sync = {0, 0x10, 0x00, 0xF6, 0x00, 0x56, 0xAC};
    sync.resize(sync.size() + 0xAC);
    esp->hear(frame(sync) + frame({1, 0x01, 0x00, 0x00, 0x01, 0x4C}));
    CHECK(esp->server.read_holding_register(REG_CMD_START_MAX_TIMER) == 0);
    CHECK(esp->server.read_holding_register(REG_CMD_HEATER_PANEL1) == 1);
    // our coils, 0x13A and 0x13F are bits 2 and 7 of byte 39
    CHECK(esp->bus.tx.size() == 47 && (esp->bus.tx[3 + 39] & 0x84) == 0x80);
  }

  // Ours changed again between the CX60's read and its FC65: still flagged, and the next read gets the new one
  {
    auto esp = std::make_unique<Esp>();
    const auto read = frame({1, 0x03, 0x00, REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX, 0x00, 0x01});
    const auto poll = frame({1, 0x01, 0x00, 0x00, 0x00, 0x08});
    esp->server.send_cmd(REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX, 80);
    esp->hear(read);
    esp->server.send_cmd(REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX, 90);
    esp->hear(fc65(REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX, 80));
    CHECK(esp->server.read_holding_register(REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX) == 90);
    esp->bus.tx.clear();
    esp->hear(poll);
    CHECK(esp->bus.tx.size() == 6 && (esp->bus.tx[3] & 1 << REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX));
    esp->bus.tx.clear();
    esp->hear(read);
    CHECK(esp->bus.tx.size() == 7 && esp->bus.tx[4] == 90);
    esp->hear(fc65(REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX, 90));
    esp->bus.tx.clear();
    esp->hear(poll);
    CHECK(esp->bus.tx.size() == 6 && !(esp->bus.tx[3] & 1 << REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX));
    CHECK(esp->server.real_value(REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX) == 90);
  }

  // A junk byte before a frame. The frame callback gets each on its own, and the frame is handled.
  {
    auto esp = std::make_unique<Esp>();
    const auto bytes = std::vector<uint8_t>{0x55} + fc6(REG_RUNTIME_FILTER_LOW, 1234);
    esp->hear(bytes);
    CHECK((esp->frames == std::vector<size_t>{1, 8}) && esp->heard == bytes);
    CHECK(esp->server.read_holding_register(REG_RUNTIME_FILTER_LOW) == 1234);
  }

  // Junk like an FC16 header whose byte count doesn't fit its register count. The frame after it is handled at
  // once, not after 264 bytes.
  {
    auto esp = std::make_unique<Esp>();
    esp->hear(std::vector<uint8_t>{2, 0x10, 0x00, 0x00, 0x00, 0x05, 0xFF} + fc6(REG_RUNTIME_FILTER_LOW, 4321));
    CHECK(esp->server.read_holding_register(REG_RUNTIME_FILTER_LOW) == 4321);
  }

  // An FC16 in pieces waits for its byte count, then for the rest
  {
    auto esp = std::make_unique<Esp>();
    const auto f = fc16(REG_RUNTIME_FILTER_HIGH, {0, 777});
    esp->hear(std::vector<uint8_t>(f.begin(), f.begin() + 5));
    esp->hear(std::vector<uint8_t>(f.begin() + 5, f.begin() + 9));
    CHECK(esp->frames.empty());
    esp->hear(std::vector<uint8_t>(f.begin() + 9, f.end()));
    CHECK((esp->frames == std::vector<size_t>{f.size()}));
    CHECK(esp->server.read_holding_register(REG_RUNTIME_FILTER_LOW) == 777);
  }

  // The bootloader's 0x64 frames go by their length byte. One too long for a 0x64 isn't one, and the frame after
  // it is handled at once.
  {
    auto esp = std::make_unique<Esp>();
    esp->hear(frame({3, 0x64, 0x01, 4, 1, 2, 3, 4}));
    CHECK((esp->frames == std::vector<size_t>{10}));
    esp->frames.clear();
    esp->hear(std::vector<uint8_t>{3, 0x64, 0x01, 250} + fc6(REG_RUNTIME_FILTER_LOW, 999));
    CHECK((esp->frames == std::vector<size_t>{4, 8}));
    CHECK(esp->server.read_holding_register(REG_RUNTIME_FILTER_LOW) == 999);
  }

  // The status: nothing before the first, live after it, and old 10 s after the last
  {
    auto esp = std::make_unique<Esp>();
    CHECK(!esp->server.fresh() && !esp->server.synced() && esp->server.data_age_ms() == UINT32_MAX);
    esp->hear(status(0, 0));
    CHECK(esp->server.fresh() && esp->server.data_age_ms() == 0);
    now_us += (FlexitModbusServer::STATUS_STALE_MS - 1) * 1000;
    CHECK(esp->server.fresh());
    now_us += 1000;
    CHECK(!esp->server.fresh() && esp->server.synced());
    esp->hear(status(0, 0));
    CHECK(esp->server.fresh());
  }

  // The select's options
  CHECK(string_to_mode("Stop") == MODE_STOP && string_to_mode("Min") == MODE_MIN && string_to_mode("Max") == MODE_MAX);
  CHECK(string_to_mode("Auto") == MODE_NORMAL);
  CHECK(mode_to_string(MODE_NORMAL) == "Normal" && mode_to_string(4) == "Invalid mode");

  printf("server test: %s\n", fails ? "FAILED" : "ok");
  return fails != 0;
}
