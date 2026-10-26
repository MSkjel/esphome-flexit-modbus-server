#include "update.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "esp_ota_ops.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "pbus.h"
#include "plink_values.h"

namespace flexit::panel::update {

using esphome::App;
using esphome::millis;
using esphome::RAMAllocator;

static const char *const TAG = "fwup";

void status(const std::string &s) {
  ESP_LOGI(TAG, "%s", s.c_str());
  if (on_status)
    on_status(s);
}

void progress(int pct) {
  if (pct == last_pct)
    return;
  last_pct = pct;
  if (on_progress)
    on_progress(pct);
}

// Whether now is at or past next_ms, across millis()'s wrap
static bool due(uint32_t now) { return (int32_t) (now - F.next_ms) >= 0; }

// The download goes in the ESP's spare app slot, where its next OTA update would go. That only holds the old ESP
// firmware, which isn't needed once the running one is confirmed. Until then the ESP may still go back to it.
// An ESP update during a panel flash overwrites the slot. That's fine, ESPHome checks its image before booting
// and the panel's bootloader rejects a stream whose tag doesn't match.
static const esp_partition_t *spare_slot() {
  const esp_partition_t *run = esp_ota_get_running_partition(), *spare = esp_ota_get_next_update_partition(nullptr);
  if (!spare || spare == run) {
    status("No spare app slot, the partition table needs two OTA slots");
    return nullptr;
  }
  esp_ota_img_states_t state;
  if (run && esp_ota_get_state_partition(run, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
    status("The ESP just updated itself, try again in a minute");
    return nullptr;
  }
  return spare;
}

static bool staged_read(size_t off, void *dst, size_t len) {
  return staging && esp_partition_read(staging, off, dst, len) == ESP_OK;
}

// sends a frame as it is, with room for the CRC the component adds. It drives the line too
static void send_raw(uint8_t *f, size_t n) {
  hooks->send_frame(f, n);
  F.sent_ms = millis();
  F.awaiting = true;
  F.probe_addr = 0;
  F.rx_tail_len = 0;  // only what comes after this request counts
  F.reply = R_NONE;
  F.weak_busy = false;
}

// sends [addr] FC sub [data...]
static void send(uint8_t addr, uint8_t sub, const uint8_t *data = nullptr, size_t n = 0) {
  uint8_t f[256];
  size_t len = 0;
  f[len++] = addr;
  f[len++] = FC;
  f[len++] = sub;
  f[len++] = (uint8_t) n;  // every 0x64 frame has a length byte, 0 = no data
  for (size_t i = 0; i < n; i++)
    f[len++] = data[i];
  send_raw(f, len);
  F.last_sub = sub;
}

// [addr 04 00 00 00 04 crc], the CX60's discovery read of input registers 0..3
static void send_probe(uint8_t addr) {
  uint8_t f[8] = {addr, 0x04, 0, 0, 0, 4};
  send_raw(f, 6);
  hooks->expect_reply(addr, 0x04, 13);
  F.probe_addr = addr;
}

// A len-byte frame, CRC included, that starts with head, anywhere in rx_tail with up to max_missing (<= head_n) of
// its first bytes lost. The bytes of the head that came have to match it. Copied whole to out.
static bool tail_frame(const uint8_t *head, size_t head_n, size_t len, size_t max_missing, uint8_t *out) {
  for (size_t missing = 0; missing <= max_missing; missing++) {
    size_t have = len - missing;
    for (size_t end = have; end <= F.rx_tail_len; end++) {
      memcpy(out, head, missing);
      memcpy(out + missing, F.rx_tail + end - have, have);
      if (!memcmp(out, head, head_n) && pbus_crc_ok(out, len))
        return true;
    }
  }
  return false;
}

// True if rx_tail ends with frame minus its first `missing` bytes, CRC included. Fewer missing match too.
static bool tail_is(const uint8_t *frame, size_t full, size_t missing) {
  size_t have = full - missing;
  return F.rx_tail_len >= have && !memcmp(F.rx_tail + F.rx_tail_len - have, frame + missing, have);
}

bool probe_reply(uint8_t addr, uint16_t *ir) {
  const uint8_t head[] = {addr, 0x04, 8};
  uint8_t f[13];
  if (!tail_frame(head, sizeof head, sizeof f, 3, f))
    return false;
  for (int i = 0; i < 4; i++)
    ir[i] = (uint16_t) (f[3 + 2 * i] << 8 | f[4 + 2 * i]);
  return true;
}

int32_t name_version(const uint8_t *s, size_t n) {
  // field 1: key 0x0a, a one-byte length (the bootloader takes at most 0xEF), the name
  if (n < 2 || s[0] != 0x0a || s[1] > 0xEF || n < 2u + s[1])
    return -1;
  char name[0xF0];
  memcpy(name, s + 2, s[1]);
  name[s[1]] = 0;
  unsigned maj, min, patch;
  char more;
  if (sscanf(name, "ci600 %3u.%3u.%3u%c", &maj, &min, &patch, &more) != 3 || maj > 255 || min > 255 || patch > 255)
    return -1;
  return (int32_t) (maj << 16 | min << 8 | patch);
}

static std::string version_text(uint32_t v) {
  return std::to_string(v >> 16 & 0xFF) + "." + std::to_string(v >> 8 & 0xFF) + "." + std::to_string(v & 0xFF);
}

static void answered(Reply r) {
  F.reply = r;
  F.awaiting = false;
}

// Matches a reply to the request that is out. Sets reply, and bl_state/bl_detail, reply_code or probe_ir.
static void match_reply() {
  if (F.probe_addr) {
    if (probe_reply(F.probe_addr, F.probe_ir[F.probe_i]))
      answered(R_PROBE);
    return;
  }
  if (F.last_sub == SUB_STATUS) {
    // status reply, up to 4 head bytes missing. State and detail have to be there.
    const uint8_t head[] = {BL_ADDR, FC, SUB_STATUS, 2};
    uint8_t f[8];
    if (tail_frame(head, sizeof head, sizeof f, 4, f) && f[4] <= ST_DONE) {
      F.bl_state = f[4];
      F.bl_detail = f[5];
      answered(R_STATUS);
      return;
    }
  } else {
    // ack [01 64 sub 00 crc crc], up to 3 head bytes missing. The 00 and the CRC have to be there. Not for a status
    // poll: during an update the echo filter is off, and on a transceiver that echoes the poll looks just like an ack.
    uint8_t f[6] = {BL_ADDR, FC, F.last_sub, 0};
    pbus_put_crc(f, 4);
    if (tail_is(f, 6, 3)) {
      answered(R_ACK);
      return;
    }
  }
  // exception [01 e4 code crc crc], up to 2 head bytes missing. The code has to be there.
  for (uint8_t code = 1; code <= 6; code++) {
    uint8_t f[5] = {BL_ADDR, (uint8_t) (FC | 0x80), code};
    pbus_put_crc(f, 3);
    if (tail_is(f, 5, 2)) {
      F.reply_code = code;
      answered(R_EXC);
      return;
    }
    if (code == EXC_BUSY && tail_is(f, 5, 3))  // only the CRC got through, just a hint
      F.weak_busy = true;
  }
}

void on_frame(const uint8_t *f, size_t n) {
  if (hooks->is_own_frame())  // our own frame echoed back, not a reply
    return;
  if (!running() || !F.awaiting)
    return;
  for (size_t i = 0; i < n; i++) {
    if (F.rx_tail_len == sizeof F.rx_tail) {
      memmove(F.rx_tail, F.rx_tail + 1, sizeof F.rx_tail - 1);
      F.rx_tail_len--;
    }
    F.rx_tail[F.rx_tail_len++] = f[i];
  }
  match_reply();
}

void enter_phase(Phase p) {
  F.phase = p;
  F.step_ms = millis();
  F.polls = 0;
  F.next_ms = F.step_ms;
}

void begin() {
  F = {};
  uint8_t head[2 + 0xEF];
  size_t n = std::min(staged_len, sizeof head);
  F.file_version = staged_read(0, head, n) ? name_version(head, n) : -1;
  // the address the panel reported if any, then the usual ones, never this ESP's own
  const uint8_t own = hooks->server_address(), usual[] = {panel_addr, 3, 1, 2};
  for (uint8_t a : usual)
    if (a && a != own && std::find(F.probe_addrs, F.probe_addrs + F.probe_n, a) == F.probe_addrs + F.probe_n)
      F.probe_addrs[F.probe_n++] = a;
  F.start_ms = millis();
  enter_phase(SAVE);
  progress(0);
  hooks->save_settings();
  status("Saving the panel's settings...");
}

bool can_start() {
  if (running()) {
    status("Already flashing");
    return false;
  }
  return (staging = spare_slot()) != nullptr;
}

static constexpr size_t BLK = 4096;  // one erase block at a time, esp_partition_write needs it erased first

static void stage(const Download &r, uint8_t *buf) {
  const esp_partition_t *part = staging;
  size_t total = r.content_length;
  auto fits = [part](size_t len) {
    if (len <= part->size)
      return true;
    status("Download failed, the file's too big");
    return false;
  };
  staged_len = 0;
  if (r.status_code != 200) {
    status("Download failed (HTTP " + std::to_string(r.status_code) + ")");
    return;
  }
  if (!fits(total))
    return;
  status("Downloading...");
  size_t fill = 0, off = 0;
  uint32_t t0 = millis(), heard = t0;
  bool end = false;
  last_pct = -1;
  while (!end && millis() - t0 < DOWNLOAD_TIMEOUT_MS) {
    int n = r.read(buf + fill, total ? std::min(BLK - fill, total - off - fill) : BLK - fill);
    App.feed_wdt();
    if (n == READ_AGAIN && millis() - heard < STALL_MS)
      continue;  // a slow server, give it until STALL_MS
    if (n < 0 || (n == 0 && !r.read_complete()))
      break;  // gone, or closed early
    if (n > 0) {
      heard = millis();
      if (!off && !fill && buf[0] != 0x0a) {  // a firmware stream starts with protobuf field 1
        end = true;                           // so it's none, like an empty one
        break;
      }
      fill += n;
    }
    end = n == 0 || off + fill == total;
    if (fill < BLK && !(end && fill))
      continue;
    size_t wlen = (fill + 15) & ~(size_t) 15;  // pad the tail to 16, encrypted flash writes 16-byte blocks
    if (!fits(off + wlen))
      return;
    std::fill(buf + fill, buf + wlen, 0xFF);
    if ((off % part->erase_size == 0 && esp_partition_erase_range(part, off, part->erase_size) != ESP_OK) ||
        esp_partition_write(part, off, buf, wlen) != ESP_OK) {
      status("Couldn't save the download on the ESP");
      return;
    }
    off += fill;
    fill = 0;
    if (total)
      progress((int) (100ull * off / total));
  }
  if (!end) {
    std::string of = total ? " of " + std::to_string(total) : "";
    status(millis() - t0 >= DOWNLOAD_TIMEOUT_MS
               ? "The download timed out"
               : "The download stopped at " + std::to_string(off + fill) + of + " bytes");
    return;
  }
  if (!off) {
    status("That's not a panel firmware file");
    return;
  }
  staged_len = off;
  ESP_LOGI(TAG, "Staged %u bytes", (unsigned) staged_len);
  begin();
}

void stage(const Download &d) {
  RAMAllocator<uint8_t> ram;
  uint8_t *buf = ram.allocate(BLK);
  if (!buf) {
    staged_len = 0;
    status("Download failed, the ESP is out of RAM");
    return;
  }
  stage(d, buf);
  ram.deallocate(buf, BLK);
}

// Ends the flash, gives the bus back
static void stop(Phase end, const std::string &why) {
  hooks->set_updating(false);
  F.phase = end;
  status(why);
  progress(end == DONE ? 100 : 0);
}

void finish(bool ok, const std::string &why) {
  if (ok) {
    hooks->settings_flashed();
    stop(DONE, why.empty() ? "Done, the panel is restarting" : why);
    return;
  }
  F.why = (why.empty() ? "Flash failed" : why) + ". Nothing's broken, just try again";
  if (!F.bl_seen) {
    stop(FAIL, F.why);
    return;
  }
  F.awaiting = false;  // request() resets the loader once the bus is quiet
  enter_phase(ABORT);
}

// Stops before anything is flashed, so the panel keeps running what it has
static void refuse(const std::string &why) { stop(FAIL, why); }

// the loader's detail byte in state 5
static std::string detail_text(uint8_t d) {
  switch (d) {
    case 1:
      return "The panel says this firmware isn't for it";
    case 2:
      return "The panel couldn't verify the firmware";
    case 4:
      return "The panel couldn't erase its flash";
    case 6:
    case 7:
      return "The panel couldn't write its flash";
    case 8:
      return "The firmware is too big for the panel";
    case 9:
      return "The firmware got cut off";
    default:
      return "The panel gave error " + std::to_string(d);
  }
}

// Stock answers the probe with 0, 0, 0 and its version, ours FW_TYPE, 0, the patch level and the version, see the
// firmware's mbserver.c. Another ESP with this component answers zeros.
static bool is_panel(const uint16_t *ir) { return ir[0] == FW_TYPE || (ir[0] == 0 && ir[3] != 0); }

// Every address has been asked, the first panel gets flashed
static void probed() {
  int t = -1;         // the first panel, else the first that answered
  bool at_1 = false;  // something runs at 1
  for (int i = 0; i < F.probe_n; i++) {
    if (F.probe_heard[i] && (t < 0 || (is_panel(F.probe_ir[i]) && !is_panel(F.probe_ir[t]))))
      t = i;
    at_1 |= F.probe_heard[i] && F.probe_addrs[i] == BL_ADDR;
  }
  // A panel left in its bootloader by a failed flash answers at 1. It goes first, a kick would put a second one there
  if (!at_1 && F.probe_i == F.probe_n) {
    enter_phase(PROBE);
    return;
  }
  if (t < 0) {
    refuse("No panel answered on the bus");
    return;
  }
  uint8_t addr = F.probe_addrs[t];
  const uint16_t *ir = F.probe_ir[t];
  std::string at = " at address " + std::to_string(addr);
  if (!is_panel(ir)) {
    char type[8];
    snprintf(type, sizeof type, "0x%04X", ir[0]);
    refuse(ir[0] ? std::string("Unknown panel type ") + type + at
                 : "No panel answered, only something" + at + " that looks like an ESP");
    return;
  }
  // Arming the loader at 1 is the same frame as the kick, so whatever runs at 1 would be kicked too
  if (at_1 && addr != BL_ADDR) {
    refuse("Something else is at address 1, where the bootloader answers. Take it or the panel" + at +
           " off the bus and try again");
    return;
  }
  if (ir[0] == 0) {
    std::string v = std::to_string(ir[3] >> 8) + "." + std::to_string(ir[3] & 0xFF);
    if (ir[3] >> 8 != 7 && !force) {
      refuse("Panel runs stock " + v + ", only 7.x is known to work. Panel Firmware Force flashes it anyway");
      return;
    }
    status("Stock panel " + v + at + ", flashing");
  } else {
    uint32_t have = (uint32_t) (ir[3] >> 8) << 16 | (ir[3] & 0xFF) << 8 | (ir[2] & 0xFF);
    std::string v = version_text(have);
    if ((int32_t) have == F.file_version && !force) {
      refuse("Panel already has " + v + ". Panel Firmware Force flashes it again");
      return;
    }
    std::string to = F.file_version < 0 ? "unknown version" : version_text((uint32_t) F.file_version);
    status("Panel " + v + " → " + to + at);
  }
  F.app_addr = addr;
  enter_phase(ENTER);
}

// Asks every address again
static void restart_probe() {
  F.probe_i = F.probe_tries = 0;
  memset(F.probe_heard, 0, sizeof F.probe_heard);
}

// PIECE, but the last piece is never shorter than MIN_PIECE. The loader finishes a short piece so fast that a
// probe might not catch it in state 3.
static size_t piece_len() {
  size_t left = staged_len - F.accepted;
  size_t n = left < PIECE ? left : PIECE;
  if (left > n && left - n < MIN_PIECE)
    n = left - MIN_PIECE;
  return n;
}

static void send_piece() {
  static uint8_t piece[PIECE];
  F.inflight = piece_len();
  if (!staged_read(F.accepted, piece, F.inflight)) {
    finish(false, "Couldn't read the download back");
    return;
  }
  send(BL_ADDR, SUB_PIECE, piece, F.inflight);
}

static void piece_delivered() {
  F.accepted += F.inflight;
  F.inflight = 0;
  progress((int) (100ull * F.accepted / staged_len));
  enter_phase(READY_WAIT);
}

static void poll_status() {
  send(BL_ADDR, SUB_STATUS);
  F.polls++;
  F.next_ms = millis() + POLL_GAP_MS;
}

// ---- what to do with what came back, one function per phase that waits for something --------------------

// The panel's settings come over the link, which runs until the bus is ours
static void save_wait(uint32_t now) {
  bool saved = hooks->settings_saved();
  if (!saved && now - F.step_ms < SAVE_TIMEOUT_MS)
    return;
  if (!saved) {
    ESP_LOGW(TAG, "The panel's settings didn't come, it starts from defaults after the flash");
  }
  hooks->set_updating(true);  // panel.h counts the panel as away from here
  enter_phase(PROBE);
  status("Looking for the panel...");
}

// An answer, or one more try at this address, then the next until all are asked
static void probe_wait(Reply got, bool timed_out) {
  if (got == R_STATUS) {
    status(std::count(F.probe_heard, F.probe_heard + F.probe_n, true)
               ? "Found a panel waiting in its bootloader at address 1, flashing that one first"
               : "Panel is in its bootloader, flashing");
    F.app_addr = 0;  // a failed flash left it there, so no kick
    enter_phase(WAIT_BL);
    return;
  }
  if (got == R_PROBE) {
    F.probe_heard[F.probe_i] = true;
  } else if (!timed_out) {
    return;
  } else if (++F.probe_tries < 2) {
    enter_phase(PROBE);
    return;
  }
  F.probe_tries = 0;
  if (++F.probe_i < F.probe_n)
    enter_phase(PROBE);
  else
    probed();
}

// After the kick or a reset, until the loader is idle and clean
static void wait_bl(Reply got, uint32_t now) {
  if (got == R_STATUS && F.bl_state == ST_BOOT) {
    F.fresh = true;  // still starting up, so it just booted with clean RAM
  } else if (got == R_STATUS) {
    // A loader that was already waiting can hold leftovers from an interrupted session, like a write pointer
    // or a half-read field, and arming doesn't clear them. So reset it unless we saw it boot or reset it
    // already. It comes back with zeroed RAM, in the bootloader if the app was erased, else in the app. ENTER
    // kicks that back into the bootloader, or PROBE finds it first if app_addr isn't known.
    if (!F.cleaned && (!F.fresh || F.bl_state != ST_IDLE)) {
      status("Resetting the bootloader...");
      enter_phase(CLEAN);
    } else if (F.bl_state == ST_IDLE) {
      enter_phase(ARM);
      status("Bootloader's ready...");
    } else if (F.clean_tries < 4) {
      enter_phase(CLEAN);  // busy and missed the reset, send it again
    } else {
      finish(false, "The panel's bootloader is stuck");
    }
  } else if (now - F.step_ms > ENTER_TIMEOUT_MS) {
    if (++F.enter_tries > 2) {
      finish(false, "The panel didn't go into its bootloader");
      return;
    }
    if (!F.app_addr)  // the reset started its app, which was never erased, so find it
      restart_probe();
    enter_phase(F.app_addr ? ENTER : PROBE);
  }
}

// Armed, the loader shows 6 and a poll moves it to 2. The arm's own ack shows in the polls.
static void arm_wait(Reply got, uint32_t now) {
  if (got == R_STATUS && F.bl_state == ST_READY) {
    enter_phase(F.accepted < staged_len ? PIECE_TX : END);
    status("Flashing...");
  } else if (got == R_STATUS && F.bl_state == ST_IDLE && F.polls >= 3) {
    enter_phase(ARM);  // the arm never got there
  } else if (now - F.step_ms > ARM_TIMEOUT_MS) {
    finish(false, "The panel's bootloader didn't get ready");
  }
}

// Polls until the loader is ready for the next piece
static void ready_wait(Reply got, uint32_t now) {
  if (got == R_STATUS && F.bl_state == ST_READY)
    enter_phase(F.accepted < staged_len ? PIECE_TX : END);
  else if (got == R_STATUS && F.bl_state == ST_IDLE)
    finish(false, "The panel dropped out of the update");
  else if (now - F.step_ms > WORK_TIMEOUT_MS)
    finish(false, "The panel stopped answering");
}

// The piece or a resend of it is out
static void piece_probe(Reply got, uint32_t now) {
  if (got == R_ACK || (got == R_EXC && F.reply_code == EXC_BUSY))  // accepted now, or earlier and being worked on
    piece_delivered();
  else if (got == R_EXC)
    finish(false, "The panel refused a frame (error " + std::to_string(F.reply_code) + ")");
  else if (F.weak_busy)
    enter_phase(PIECE_CONFIRM);  // probably delivered, ask once
  else if (now - F.step_ms > WORK_TIMEOUT_MS)
    finish(false, "The panel stopped answering");
}

// One status poll after a busy-looking tail
static void piece_confirm(Reply got, bool timed_out) {
  if (got == R_STATUS) {
    if (F.bl_state == ST_WORKING || F.bl_state == ST_DONE)
      piece_delivered();
    else if (F.bl_state == ST_READY)
      enter_phase(PIECE_TX);  // never arrived, send it again
    else
      finish(false, "The panel dropped out of the update");  // idle, or starting after a restart
  } else if (timed_out || got == R_EXC) {
    // Lost the poll's reply, or only an exception came. If the loader was in 6 the poll moved it to 2 and the
    // piece is in. If the piece never arrived it's in 2 as well. No way to tell, so we stop.
    finish(false, "Lost the panel's answer at a bad moment");
  }
}

// The loader commits and goes idle
static void end_wait(Reply got, uint32_t now) {
  if (got == R_STATUS && F.bl_state == ST_IDLE)
    enter_phase(RESET);
  else if (got == R_STATUS && F.bl_state == ST_READY && F.polls >= 3)
    enter_phase(END);  // the end never got there
  else if (now - F.step_ms > END_TIMEOUT_MS)
    finish(false, "The panel didn't finish the update");
}

// Done once the loader says it's starting or goes quiet. Else reset again after 2 s and 4 s, and give up 5 s after
// the third.
static void reset_wait(Reply got, uint32_t now) {
  if (got == R_STATUS)
    F.answer_ms = now;
  if ((got == R_STATUS && F.bl_state == ST_BOOT) || now - F.answer_ms > RESTARTED_MS) {
    finish(true);
  } else if (now - F.step_ms >= std::min(RESET_AGAIN_MS << F.reset_tries, RESET_AGAIN_MAX_MS)) {
    if (++F.reset_tries >= 3)  // the firmware's in already
      finish(true, "The new firmware is in, but the panel didn't restart. Power it off and on");
    else
      enter_phase(RESET);
  }
}

static void react(Reply got, bool timed_out, uint32_t now) {
  if (got == R_STATUS || got == R_ACK)
    F.bl_seen = true;
  // a failure ends the update, except when it's left over from an earlier session
  if (got == R_STATUS && F.bl_state == ST_FAILED && F.phase != WAIT_BL && F.phase != PROBE_WAIT) {
    ESP_LOGW(TAG, "Bootloader failed: detail %u at %u/%u bytes", F.bl_detail, (unsigned) F.accepted,
             (unsigned) staged_len);
    finish(false, detail_text(F.bl_detail));
    return;
  }
  switch (F.phase) {
    case PROBE_WAIT:
      probe_wait(got, timed_out);
      break;
    case WAIT_BL:
      wait_bl(got, now);
      break;
    case ARM_WAIT:
      arm_wait(got, now);
      break;
    case READY_WAIT:
      ready_wait(got, now);
      break;
    case PIECE_PROBE:
      piece_probe(got, now);
      break;
    case PIECE_CONFIRM:
      piece_confirm(got, timed_out);
      break;
    case END_WAIT:
      end_wait(got, now);
      break;
    case RESET_WAIT:
      reset_wait(got, now);
      break;
    default:
      break;
  }
}

// The next request for the phase, if it's time
static void request(uint32_t now) {
  switch (F.phase) {
    case PROBE:
      if (F.probe_i < F.probe_n)
        send_probe(F.probe_addrs[F.probe_i]);
      else
        send(BL_ADDR, SUB_STATUS);  // a bootloader left waiting by a failed flash
      enter_phase(PROBE_WAIT);
      break;
    case CLEAN:  // reset the loader, it restarts without a reply, then go in again
      send(BL_ADDR, SUB_RESET);
      F.cleaned = true;
      F.clean_tries++;
      F.awaiting = false;
      enter_phase(ENTER);
      F.next_ms = now + CLEAN_MS;
      break;
    case ENTER:  // kick the running app into the bootloader with 0x64/2
      if (!due(now))
        break;
      F.fresh = false;
      if (F.app_addr)
        send(F.app_addr, SUB_ARM);  // the app resets without answering, we poll next
      enter_phase(WAIT_BL);
      F.awaiting = false;
      F.next_ms = now + FIRST_POLL_MS;
      break;
    case WAIT_BL:
    case ARM_WAIT:
    case READY_WAIT:
    case END_WAIT:
    case RESET_WAIT:
      if (due(now))
        poll_status();
      break;
    case PIECE_CONFIRM:
      if (F.polls == 0)
        poll_status();
      break;
    case ARM:
      send(BL_ADDR, SUB_ARM);
      enter_phase(ARM_WAIT);
      break;
    case PIECE_TX:
      send_piece();
      if (F.phase == PIECE_TX)
        enter_phase(PIECE_PROBE);
      break;
    case PIECE_PROBE:  // no usable answer, send the same piece again
      if (due(now)) {
        send_piece();
        F.next_ms = now + POLL_GAP_MS;
      }
      break;
    case END:
      send(BL_ADDR, SUB_END);
      enter_phase(END_WAIT);
      break;
    case RESET:
      send(BL_ADDR, SUB_RESET);  // no reply, it resets right away
      F.awaiting = false;
      enter_phase(RESET_WAIT);
      F.answer_ms = F.step_ms;
      F.next_ms = now + RESET_POLL_MS;
      break;
    case ABORT:  // a clean loader for the next try, and the old app runs again if it was never erased
      send(BL_ADDR, SUB_RESET);
      F.awaiting = false;
      stop(FAIL, F.why);
      break;
    default:
      break;
  }
}

void pump() {
  if (!running())
    return;
  uint32_t now = millis();
  if (F.phase != ABORT && now - F.start_ms > TOTAL_TIMEOUT_MS) {
    finish(false, "Flashing timed out");
    return;
  }
  if (F.phase == SAVE) {
    save_wait(now);
    return;
  }
  bool timed_out = false;
  if (F.awaiting) {
    if (now - F.sent_ms <= (F.probe_addr ? PROBE_TIMEOUT_MS : REPLY_TIMEOUT_MS))
      return;
    F.awaiting = false;
    timed_out = true;
  }
  Reply got = F.reply;
  F.reply = R_NONE;
  react(got, timed_out, now);
  if (running() && hooks->quiet_min_us() >= BUS_QUIET_US)
    request(millis());
}

}  // namespace flexit::panel::update
