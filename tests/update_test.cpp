// The panel updater on a PC: the download, the FC4 reply matcher, the stream's name, what each panel gets, a panel
// left in its bootloader, the reset after a failure and millis() wrapping round.
//   make -C tests update_test
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>
#include "check.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "panel/plink_values.h"
#include "panel/update.h"
using namespace flexit::panel::update;

namespace esphome {
static uint32_t now_ms;
uint32_t millis() { return now_ms; }
uint32_t micros() { return now_ms * 1000; }
}  // namespace esphome
using namespace esphome;

// The component and settings.h, as much as update.cpp uses. Frames sent go to tx.
static bool settings_asked, settings_in = true, settings_back;
struct Server : Hooks {
  std::vector<std::vector<uint8_t>> tx;
  bool updating = false;
  uint8_t own = 1;
  uint32_t quiet_us = 100000;  // since the last frame on the bus
  void send_frame(uint8_t *f, size_t n) override { tx.emplace_back(f, f + n); }
  void expect_reply(uint8_t, uint8_t, size_t) override {}
  uint8_t server_address() override { return own; }
  void set_updating(bool u) override { updating = u; }
  uint32_t quiet_min_us() override { return quiet_us; }
  bool is_own_frame() override { return false; }
  void save_settings() override { settings_asked = true; }
  bool settings_saved() override { return settings_in; }
  void settings_flashed() override { settings_back = true; }
} server;

// the staged stream, written and read back through the partition API
#include "esp_ota_ops.h"
static std::vector<uint8_t> staged;
static std::vector<size_t> writes;  // the length of each write
static esp_partition_t slots[2] = {{0x100000, 4096}, {0x100000, 4096}};
const esp_partition_t *esp_ota_get_running_partition() { return &slots[0]; }
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *) { return &slots[1]; }
esp_err_t esp_partition_read(const esp_partition_t *, size_t off, void *dst, size_t len) {
  if (off + len > staged.size())
    return -1;
  memcpy(dst, staged.data() + off, len);
  return ESP_OK;
}
esp_err_t esp_partition_erase_range(const esp_partition_t *, size_t, size_t) { return ESP_OK; }
esp_err_t esp_partition_write(const esp_partition_t *, size_t off, const void *src, size_t len) {
  if (off % 16 || len % 16)  // like an encrypted flash
    return -1;
  staged.resize(std::max(staged.size(), off + len));
  memcpy(staged.data() + off, src, len);
  writes.push_back(len);
  return ESP_OK;
}

// [addr 04 08 ir0..ir3 crc]
static std::vector<uint8_t> fc4(uint8_t addr, uint16_t ir0, uint16_t ir1, uint16_t ir2, uint16_t ir3) {
  std::vector<uint8_t> f = {addr, 0x04, 8};
  for (uint16_t v : {ir0, ir1, ir2, ir3}) {
    f.push_back(v >> 8);
    f.push_back(v & 0xFF);
  }
  return frame(f);
}

static std::vector<uint8_t> stream(const std::string &name) {
  std::vector<uint8_t> s = {0x0a, (uint8_t) name.size()};
  s.insert(s.end(), name.begin(), name.end());
  s.insert(s.end(), 300, 0x55);  // the guard and ciphertext don't matter here
  return s;
}

// [01 64 03 02 state detail crc], the bootloader's status
static std::vector<uint8_t> bl_status(uint8_t state) { return frame({BL_ADDR, FC, SUB_STATUS, 2, state, 0}); }

static std::string last_status;

// the probe's reply in these bytes
static bool probe_reply_in(const uint8_t *t, size_t n, uint8_t addr, uint16_t *ir) {
  memcpy(F.rx_tail, t, n);
  F.rx_tail_len = n;
  return probe_reply(addr, ir);
}

// http_request's response on an ESP32, 1000 bytes a read. On silence read() blocks for its 4.5 s timeout.
struct Http {
  size_t content_length = 0;  // 0 = not given, like a chunked one
  int status_code = 200;
  std::vector<uint8_t> body;
  size_t pos = 0, stop = SIZE_MAX;  // the server sends `stop` bytes of the body, then
  int then = 0;                     // read() gives this, 0 = closed, READ_AGAIN = silent
  size_t pause_at = SIZE_MAX;       // it goes quiet here for `pauses` timeouts
  int pauses = 0;
  int read(uint8_t *buf, size_t max) {
    if (pos == pause_at && pauses > 0) {
      pauses--;
      now_ms += 4500;
      return READ_AGAIN;
    }
    size_t n = std::min({max, (size_t) 1000, body.size() - pos, stop > pos ? stop - pos : 0});
    if (!n) {
      if (then == READ_AGAIN)
        now_ms += 4500;
      return then;
    }
    memcpy(buf, body.data() + pos, n);
    pos += n;
    now_ms += 10;
    return (int) n;
  }
  bool is_read_complete() const { return pos == body.size(); }
};

static void get(Http h) {
  F.phase = IDLE;
  staged.clear();
  writes.clear();
  Http *r = &h;
  if (can_start())
    download(r);
}

// Starts an update with the stream staged and runs the probe, feeding the replies (empty = silence)
static void probe(const std::string &name, std::vector<std::vector<uint8_t>> answers,
                  std::vector<uint8_t> loader = {}) {
  staged = stream(name);
  staged_len = staged.size();
  staging = &slots[1];
  server.tx.clear();
  begin();
  for (int i = 0; i < 40 && F.phase != FAIL && F.phase != DONE && F.phase != WAIT_BL; i++) {
    pump();
    if (!F.awaiting)
      continue;
    auto &f = server.tx.back();
    auto &a = f[1] == FC ? loader : answers[f[0]];
    if (!a.empty())
      on_frame(a.data(), a.size());
    if (F.reply == R_NONE)  // no reply it takes, so let it time out
      now_ms += PROBE_TIMEOUT_MS + 1;
  }
}

int main() {
  hooks = &server;
  on_status = [](const std::string &s) { last_status = s; };

  // the download: a block, then a tail padded to 16, with two read timeouts in the middle
  Http h;
  h.body = stream("ci600 0.2.0");
  h.body.resize(5000, 0x55);
  h.content_length = h.body.size();
  h.pause_at = 2000;
  h.pauses = 2;
  get(h);
  CHECK(F.phase == SAVE && staged_len == 5000 && (writes == std::vector<size_t>{4096, 912}));
  CHECK(staged.size() >= h.body.size() && std::equal(h.body.begin(), h.body.end(), staged.begin()));
  // four timeouts are too many
  h.pauses = 4;
  get(h);
  CHECK(F.phase == IDLE && last_status == "The download stopped at 2000 of 5000 bytes");
  h.pauses = 0;
  // the server stops answering, or hangs up
  h.stop = 3000;
  h.then = READ_AGAIN;
  get(h);
  CHECK(last_status == "The download stopped at 3000 of 5000 bytes");
  h.then = 0;
  get(h);
  CHECK(last_status == "The download stopped at 3000 of 5000 bytes");
  h.stop = SIZE_MAX;
  // no RAM for the buffer
  no_ram = true;
  get(h);
  CHECK(F.phase == IDLE && staged.empty() && last_status == "Download failed, the ESP is out of RAM");
  no_ram = false;
  // chunked, no size
  h.content_length = 0;
  get(h);
  CHECK(F.phase == SAVE && staged_len == 5000);
  // not a firmware file
  h.body[0] = '<';
  get(h);
  CHECK(last_status == "That's not a panel firmware file");
  F.phase = IDLE;

  // the reply matcher: whole, with a missing head, with other traffic around it, and not another address
  uint16_t ir[4];
  auto r = fc4(3, FW_TYPE, 0, 5, 0x0102);
  CHECK(probe_reply_in(r.data(), r.size(), 3, ir) && ir[0] == FW_TYPE && ir[2] == 5 && ir[3] == 0x0102);
  CHECK(probe_reply_in(r.data() + 3, r.size() - 3, 3, ir) && ir[3] == 0x0102);
  CHECK(!probe_reply_in(r.data() + 4, r.size() - 4, 3, ir));
  std::vector<uint8_t> noisy = {0, 0x06, 0, 0x0C, 0, 200, 1, 2};
  noisy.insert(noisy.end(), r.begin(), r.end());
  noisy.insert(noisy.end(), {0, 0x10, 0, 0});
  CHECK(probe_reply_in(noisy.data(), noisy.size(), 3, ir) && ir[0] == FW_TYPE);
  CHECK(!probe_reply_in(r.data(), r.size(), 1, ir));
  r[7] ^= 1;
  CHECK(!probe_reply_in(r.data(), r.size(), 3, ir));

  // the name field
  auto s = stream("ci600 0.2.10");
  CHECK(name_version(s.data(), s.size()) == 0x00020A);
  s = stream("ci600");
  CHECK(name_version(s.data(), s.size()) == -1);
  s = stream("ci600 1.2");
  CHECK(name_version(s.data(), s.size()) == -1);
  s = stream("ci600 1.2.3 beta");
  CHECK(name_version(s.data(), s.size()) == -1);
  s = stream("ci600 1.2.300");
  CHECK(name_version(s.data(), s.size()) == -1);
  CHECK(name_version(s.data(), 1) == -1);
  s[0] = 0x12;
  CHECK(name_version(s.data(), s.size()) == -1);

  // Nobody home: every address twice, the ESP's own 1 skipped, the bootloader twice, then a refusal
  std::vector<std::vector<uint8_t>> none(4);
  probe("ci600 0.2.0", none);
  CHECK(F.phase == FAIL && last_status == "No panel answered on the bus");
  CHECK(server.tx.size() == 6);
  CHECK(server.tx[0][0] == 3 && server.tx[1][0] == 3 && server.tx[2][0] == 2 && server.tx[3][0] == 2);
  CHECK(server.tx[4] == std::vector<uint8_t>({BL_ADDR, FC, SUB_STATUS, 0}) && server.tx[5] == server.tx[4]);
  CHECK(!server.updating);

  // the panel reported its address, that goes first
  panel_addr = 2;
  probe("ci600 0.2.0", none);
  CHECK(server.tx[0][0] == 2 && server.tx[2][0] == 3);
  panel_addr = 0;

  // a stock 7.2 at 1, this ESP at 2
  server.own = 2;
  auto at = none;
  at[1] = fc4(1, 0, 0, 0, 0x0702);
  probe("ci600 0.2.0", at);
  CHECK(F.phase == WAIT_BL && last_status == "Stock panel 7.2 at address 1, flashing");
  CHECK(server.tx.size() == 4 && server.tx.back()[0] == 1 && server.tx.back()[1] == FC);  // 3 twice, 1, the kick
  CHECK(server.updating);
  server.updating = false;

  // another stock version
  at[1] = fc4(1, 0, 0, 0, 0x0601);
  probe("ci600 0.2.0", at);
  CHECK(F.phase == FAIL && last_status.rfind("Panel runs stock 6.1, only 7.x", 0) == 0);
  force = true;
  probe("ci600 0.2.0", at);
  CHECK(F.phase == WAIT_BL && last_status == "Stock panel 6.1 at address 1, flashing");
  force = false;
  server.updating = false;

  // another ESP with this component at 3 answers zeros, that's no panel
  at[1] = fc4(1, 0, 0, 0, 0x0702);
  at[3] = fc4(3, 0, 0, 0, 0);
  probe("ci600 0.2.0", at);
  CHECK(F.phase == WAIT_BL && last_status == "Stock panel 7.2 at address 1, flashing" && server.tx.back()[0] == 1);
  server.updating = false;
  at[1].clear();
  force = true;
  probe("ci600 0.2.0", at);
  CHECK(F.phase == FAIL && last_status == "No panel answered, only something at address 3 that looks like an ESP");
  force = false;

  // ours at 3 and a stock one at 1: the arm at 1 would kick that one too, so neither is flashed
  at[1] = fc4(1, 0, 0, 0, 0x0702);
  at[3] = fc4(3, FW_TYPE, 0, 0, 0x0001);
  for (bool f : {false, true}) {
    force = f;
    probe("ci600 0.2.0", at);
    CHECK(F.phase == FAIL &&
          last_status ==
              "Something else is at address 1, where the bootloader answers. Take it or the panel at address 3 off "
              "the bus and try again");
    CHECK(server.tx.size() == 2 && server.tx[0][1] == 0x04 && server.tx[1][1] == 0x04);
  }
  force = false;
  // the one at 1 can be flashed with another one elsewhere
  server.own = 3;
  at[2] = fc4(2, FW_TYPE, 0, 0, 0x0001);
  probe("ci600 0.2.0", at);
  CHECK(F.phase == WAIT_BL && last_status == "Stock panel 7.2 at address 1, flashing" && server.tx.back()[0] == 1);
  server.updating = false;

  // ours at 3: an older version is flashed, the same one isn't unless forced, an unversioned file always
  server.own = 1;
  at = none;
  at[3] = fc4(3, FW_TYPE, 0, 0, 0x0001);
  probe("ci600 0.2.0", at);
  CHECK(F.phase == WAIT_BL && last_status == "Panel 0.1.0 → 0.2.0 at address 3");
  at[3] = fc4(3, FW_TYPE, 0, 0, 0x0002);
  probe("ci600 0.2.0", at);
  CHECK(F.phase == FAIL && last_status == "Panel already has 0.2.0. Panel Firmware Force flashes it again");
  force = true;
  probe("ci600 0.2.0", at);
  CHECK(F.phase == WAIT_BL && last_status == "Panel 0.2.0 → 0.2.0 at address 3");
  force = false;
  probe("ci600", at);
  CHECK(F.phase == WAIT_BL && last_status == "Panel 0.2.0 → unknown version at address 3");

  // something else
  at[3] = fc4(3, 0x1234, 0, 0, 0x0702);
  probe("ci600 0.2.0", at);
  CHECK(F.phase == FAIL && last_status == "Unknown panel type 0x1234 at address 3");

  // a reply lost once is asked for again
  at[3] = fc4(3, 0, 0, 0, 0x0700);
  staged = stream("ci600 0.2.0");
  staged_len = staged.size();
  staging = &slots[1];
  server.tx.clear();
  begin();
  pump();  // past the settings, nothing to save them here
  pump();
  now_ms += PROBE_TIMEOUT_MS + 1;
  pump();
  CHECK(server.tx.size() == 2 && server.tx[1][0] == 3);
  on_frame(at[3].data(), at[3].size());
  for (int i = 0; i < 5; i++) {  // then address 2 and the bootloader, twice each
    pump();
    now_ms += PROBE_TIMEOUT_MS + 1;
  }
  CHECK(server.tx.size() == 7 && server.tx.back()[0] == 3 && server.tx.back()[1] == FC);
  CHECK(F.phase == WAIT_BL && last_status == "Stock panel 7.0 at address 3, flashing");

  auto resets = [] {
    return std::count_if(server.tx.begin(), server.tx.end(), [](auto &f) { return f[1] == FC && f[2] == SUB_RESET; });
  };
  // the loader misses all three resets after the firmware's in, that's done, not a failure. They go 2 and 4 s
  // apart, then 5 s to give up, time for a busy loader.
  const auto idle = frame({BL_ADDR, FC, SUB_STATUS, 2, ST_IDLE, 0});
  server.tx.clear();
  enter_phase(RESET);
  std::vector<uint32_t> reset_at;
  for (int i = 0; i < 200 && F.phase != DONE && F.phase != FAIL; i++) {
    long n = resets();
    pump();
    if (resets() > n)
      reset_at.push_back(now_ms);
    if (F.awaiting)
      on_frame(idle.data(), idle.size());
    now_ms += 100;
  }
  CHECK(F.phase == DONE && last_status == "The new firmware is in, but the panel didn't restart. Power it off and on");
  CHECK(resets() == 3 && last_pct == 100 && !server.updating);
  CHECK(reset_at.size() == 3 && reset_at[1] - reset_at[0] >= 2000 && reset_at[2] - reset_at[1] >= 4000 &&
        now_ms - reset_at[2] >= 5000);
  // one that answers once more, then restarts: done once it's quiet, with no second reset
  server.tx.clear();
  server.updating = true;
  F.reset_tries = 0;
  enter_phase(RESET);
  bool answered = false;
  for (int i = 0; i < 100 && F.phase != DONE && F.phase != FAIL; i++) {
    pump();
    if (F.awaiting && !answered) {
      on_frame(idle.data(), idle.size());
      answered = true;
    }
    now_ms += 100;
  }
  CHECK(F.phase == DONE && last_status == "Done, the panel is restarting" && resets() == 1 && answered);

  // the poll after a busy-looking tail gets "starting" or an exception, that ends it right away
  for (const auto &odd : {frame({BL_ADDR, FC, SUB_STATUS, 2, ST_BOOT, 0}), frame({BL_ADDR, FC | 0x80, EXC_BUSY})}) {
    server.tx.clear();
    server.updating = F.bl_seen = true;
    F.awaiting = false;
    enter_phase(PIECE_CONFIRM);
    pump();
    CHECK(F.awaiting && server.tx.size() == 1 && server.tx[0][2] == SUB_STATUS);
    on_frame(odd.data(), odd.size());
    pump();
    CHECK(F.phase == FAIL && resets() == 1 && !server.updating);
    CHECK(last_status ==
          (odd[1] == FC ? "The panel dropped out of the update" : "Lost the panel's answer at a bad moment") +
              std::string(". Nothing's broken, just try again"));
  }

  // a failure right after the CX60 sent: the loader's reset waits for a quiet bus, like every request
  server.tx.clear();
  server.updating = F.bl_seen = true;
  F.awaiting = false;
  enter_phase(PIECE_CONFIRM);
  pump();
  server.quiet_us = 0;
  const auto busy = frame({BL_ADDR, FC | 0x80, EXC_BUSY});
  on_frame(busy.data(), busy.size());
  pump();
  CHECK(F.phase == ABORT && resets() == 0 && server.updating);
  server.quiet_us = 100000;
  pump();
  CHECK(F.phase == FAIL && resets() == 1 && !server.updating &&
        last_status == "Lost the panel's answer at a bad moment. Nothing's broken, just try again");

  // millis() wrapping round while the loader restarts after its reset: the kick still waits CLEAN_MS
  server.tx.clear();
  server.updating = true;
  now_ms = UINT32_MAX - 1000;
  F.phase = IDLE;
  F.start_ms = now_ms;
  F.app_addr = 3;
  F.awaiting = false;
  enter_phase(CLEAN);
  pump();
  CHECK(F.phase == ENTER && resets() == 1 && server.tx.size() == 1);
  now_ms += 100;
  pump();
  CHECK(F.phase == ENTER && server.tx.size() == 1);
  now_ms += CLEAN_MS;  // past the wrap
  pump();
  CHECK(F.phase == WAIT_BL && server.tx.size() == 2 && server.tx[1] == std::vector<uint8_t>({3, FC, SUB_ARM, 0}));
  pump();
  CHECK(server.tx.size() == 2);  // the first poll FIRST_POLL_MS after the kick
  now_ms += FIRST_POLL_MS;
  pump();
  CHECK(server.tx.size() == 3 && server.tx[2][2] == SUB_STATUS);
  server.updating = false;

  // a panel left in its bootloader by a failed flash: found by its status, reset and armed with no kick
  auto arm = [] {
    for (int i = 0; i < 100 && F.phase != ARM_WAIT && F.phase != FAIL; i++) {
      pump();
      if (F.awaiting) {
        auto st = bl_status(F.cleaned ? ST_IDLE : ST_FAILED);  // the reset clears it
        on_frame(st.data(), st.size());
      }
      now_ms += 100;
    }
    return F.phase == ARM_WAIT;
  };
  server.own = 2;
  at = none;
  at[1] = frame({1, 0x84, 1});
  probe("ci600 0.2.0", at, bl_status(ST_FAILED));
  CHECK(F.phase == WAIT_BL && last_status == "Panel is in its bootloader, flashing");
  CHECK(server.tx.size() == 6 && server.tx[4] == std::vector<uint8_t>({BL_ADDR, FC, SUB_STATUS, 0}));  // and WAIT_BL's
  CHECK(arm());
  int arms = 0;
  for (size_t i = 4; i < server.tx.size(); i++) {
    auto &f = server.tx[i];
    CHECK(f[0] == BL_ADDR && f[1] == FC);
    arms += f[2] == SUB_ARM;
  }
  CHECK(resets() == 1 && arms == 1 && server.tx.back()[2] == SUB_ARM);
  // the reset started its unerased app instead: found by the probe and kicked as usual
  at = none;
  at[3] = fc4(3, FW_TYPE, 0, 0, 0x0001);
  probe("ci600 0.2.0", none, bl_status(ST_IDLE));
  CHECK(F.phase == WAIT_BL && F.app_addr == 0);
  for (int i = 0; i < 400 && F.phase != PROBE_WAIT; i++) {
    pump();
    if (F.awaiting && !F.cleaned) {
      auto st = bl_status(ST_IDLE);
      on_frame(st.data(), st.size());
    }
    now_ms += 100;
  }
  CHECK(F.phase == PROBE_WAIT && server.tx.back()[0] == 3);
  on_frame(at[3].data(), at[3].size());
  for (int i = 0; i < 5; i++) {  // then address 1 and the bootloader, twice each
    pump();
    now_ms += PROBE_TIMEOUT_MS + 1;
  }
  CHECK(F.phase == WAIT_BL && F.app_addr == 3 && server.tx.back() == std::vector<uint8_t>({3, FC, SUB_ARM, 0}));
  server.updating = false;

  // with another panel at 3 the one in its bootloader goes first, and the one at 3 never gets the kick
  at[1] = frame({1, 0x84, 1});
  for (uint8_t own : {1, 2}) {
    server.own = own;
    probe("ci600 0.2.0", at, bl_status(ST_FAILED));
    CHECK(F.phase == WAIT_BL && F.app_addr == 0 &&
          last_status == "Found a panel waiting in its bootloader at address 1, flashing that one first");
    CHECK(arm() && std::none_of(server.tx.begin(), server.tx.end(), [](auto &f) { return f[0] == 3 && f[1] == FC; }));
    server.updating = false;
  }

  // the panel's settings go to the ESP first, over the link while the bus is still the CX60's, and back after the
  // flash. 10 s without them and it flashes anyway.
  settings_asked = settings_in = settings_back = false;
  server.tx.clear();
  begin();
  pump();
  now_ms += 5000;
  pump();
  CHECK(settings_asked && F.phase == SAVE && !server.updating && server.tx.empty() &&
        last_status == "Saving the panel's settings...");
  settings_in = true;
  pump();
  CHECK(F.phase == PROBE && server.updating && last_status == "Looking for the panel...");
  settings_in = false;
  begin();
  now_ms += SAVE_TIMEOUT_MS;
  pump();
  CHECK(F.phase == PROBE);
  finish(false);
  CHECK(!settings_back);
  finish(true);
  CHECK(settings_back && F.phase == DONE && !server.updating);

  printf("%s\n", fails ? "FAILED" : "update_test: all passed");
  return fails ? 1 : 0;
}
