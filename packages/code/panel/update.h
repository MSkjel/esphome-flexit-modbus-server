// Flashes the CI600 panel over the bus through its stock bootloader, from stock or our firmware. No SWD or jumper.
// First it probes the panel like the CU60 does at discovery, and only flashes stock 7.x or ours, and ours only
// when the file is another version unless forced.
#pragma once
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include "pbus.h"
#include "plink_values.h"

#ifndef USE_ESP32
#error "The panel packages need an ESP32"
#endif
#include "esp_ota_ops.h"
#include "esp_partition.h"

namespace flexit::panel::update {

static const char *const TAG = "fwup";

// ---- configuration (set from update.yaml) -------------------------------------------------------------
inline uint8_t panel_addr = 0;  // the bus address the panel reported over the link, 0 = none yet
inline bool force = false;      // flash a stock panel that isn't 7.x, or the version the panel already has

// ---- staging (the .fxup in the ESP's spare app slot) --------------------------------------------------
inline size_t staged_len = 0;  // bytes staged, 0 = nothing yet

// ---- reporting, to update.yaml's entities. This header is built without update.yaml too. --------------
inline std::function<void(const std::string &)> on_status;
inline std::function<void(int)> on_progress;  // 0..100
inline int last_pct = -1;

inline void status(const std::string &s) {
  ESP_LOGI(TAG, "%s", s.c_str());
  if (on_status)
    on_status(s);
}
inline void progress(int pct) {
  if (pct == last_pct)
    return;
  last_pct = pct;
  if (on_progress)
    on_progress(pct);
}

// ---- the Modbus function 0x64 bootloader protocol -----------------------------------------------------
static constexpr uint8_t BL_ADDR = 1;  // the bootloader always answers here
static constexpr uint8_t FC = 0x64;
// Sub-commands 2 arm, 3 status, 4 piece of the stream, 6 end, 7 reset, checked against the vendor ROM in an
// emulator. Status reply is [01 64 03 02 state detail crc crc]. States 0 starting, 1 idle, 2 ready for a piece,
// 3 working on a piece, 5 failed with the reason in detail, 6 piece done.
// A piece is only taken in state 2, otherwise it gets exception 6 (busy) and nothing changes. It moves the
// loader to 3 and then 6, and a status poll in 6 moves it back to 2. So the status poll is the only request
// with a side effect, and the only one where a lost reply hides something. We use the piece itself as the
// probe since resending it never hurts, only poll status once the piece is known to be in, and stop rather
// than guess when a lost reply leaves the position unknown.
//
// The first bytes of a reply can get lost while the ESP is still switching to receive, so replies are
// matched on their tail, CRC included, with up to 4 head bytes missing, 3 for the short acks.
static constexpr uint8_t SUB_ARM = 2, SUB_STATUS = 3, SUB_PIECE = 4, SUB_END = 6, SUB_RESET = 7;
static constexpr uint8_t ST_BOOT = 0, ST_IDLE = 1, ST_READY = 2, ST_WORKING = 3, ST_FAILED = 5, ST_DONE = 6;
static constexpr uint8_t EXC_BUSY = 6;
static constexpr size_t PIECE = 240;                // stream bytes per sub-4 frame, max 249
static constexpr size_t MIN_PIECE = 128;            // shortest last piece, see piece_len
static constexpr uint32_t REPLY_TIMEOUT_MS = 30;    // the loader answers within ~2 ms if it answers at all
static constexpr uint32_t PROBE_TIMEOUT_MS = 100;   // what the CU60 gives a panel at discovery
static constexpr uint32_t POLL_GAP_MS = 10;         // between status polls while the loader is working
static constexpr uint32_t WORK_TIMEOUT_MS = 90000;  // the loader erases on the first piece and goes quiet for seconds
static constexpr uint32_t TOTAL_TIMEOUT_MS = 20 * 60000;
static constexpr uint32_t DOWNLOAD_TIMEOUT_MS = 5 * 60000;
static constexpr uint32_t STALL_MS = 15000;  // no data from the server for this long, it's gone

enum Phase {
  IDLE,
  PROBE,
  PROBE_WAIT,
  ENTER,
  WAIT_BL,
  CLEAN,
  ARM,
  ARM_WAIT,
  READY_WAIT,
  PIECE_TX,
  PIECE_PROBE,
  PIECE_CONFIRM,
  END,
  END_WAIT,
  RESET,
  DONE,
  FAIL
};
inline Phase phase = IDLE;
inline size_t sent = 0;          // stream bytes the loader has accepted
inline size_t inflight = 0;      // bytes in the piece being delivered
inline uint32_t step_ms = 0;     // when this phase began
inline uint32_t start_ms = 0;    // when the flash began
inline uint32_t next_ms = 0;     // earliest time for the next request
inline uint32_t sent_ms = 0;     // when the last request went out
inline bool awaiting = false;    // a request is out, waiting for its reply
inline uint8_t tries = 0;        // enter retries
inline bool cleaned = false;     // we reset the loader this session, we always start from a clean one
inline uint8_t clean_tries = 0;  // reset requests sent. A loader busy erasing can miss one
inline bool fresh = false;       // saw the loader starting up, so it just booted with clean RAM
inline bool bl_seen = false;     // the loader has answered this session
inline uint8_t polls = 0;        // status polls in the current phase
inline uint8_t last_sub = 0;     // the sub-command of the request that is out
inline uint8_t app_addr = 0;     // where the panel answered the probe, and gets the kick into the bootloader
inline uint8_t probe_addrs[4], probe_n = 0, probe_i = 0, probe_tries = 0;  // the addresses to probe, in order
inline uint8_t probe_addr = 0;     // the address a probe is out to, 0 = a bootloader request is out
inline uint16_t probe_ir[4];       // the input registers it answered
inline int32_t file_version = -1;  // from the staged stream's name, -1 = none
inline uint8_t bl_state = 0xFF, bl_detail = 0;
inline uint8_t rx_tail[32];  // the bytes heard since our request went out, oldest first
inline size_t rx_tail_len = 0;

// what came back for the request that is out
enum Reply { R_NONE, R_STATUS, R_ACK, R_EXC, R_PROBE };
inline Reply reply = R_NONE;
inline uint8_t reply_code = 0;  // the exception code for R_EXC
inline bool weak_busy = false;  // only the CRC of a "busy" got through, so just a hint

inline const esp_partition_t *staging = nullptr;

// The download goes in the ESP's spare app slot, where its next OTA update would go. That only holds the old ESP
// firmware, which isn't needed once the running one is confirmed. Until then the ESP may still go back to it.
// An ESP update during a panel flash overwrites the slot. That's fine, ESPHome checks its image before booting
// and the panel's bootloader rejects a stream whose tag doesn't match.
inline const esp_partition_t *spare_slot() {
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

inline bool staged_read(size_t off, void *dst, size_t len) {
  return staging && esp_partition_read(staging, off, dst, len) == ESP_OK;
}

// sends a frame as it is, with room for the CRC the component adds. It drives the line too
inline void send_raw(uint8_t *f, size_t n) {
  id(server).send_frame(f, n);
  sent_ms = millis();
  awaiting = true;
  probe_addr = 0;
  rx_tail_len = 0;  // only what comes after this request counts
  reply = R_NONE;
  weak_busy = false;
}

// sends [addr] FC sub [data...]
inline void send(uint8_t addr, uint8_t sub, const uint8_t *data = nullptr, size_t n = 0) {
  uint8_t f[256];
  size_t len = 0;
  f[len++] = addr;
  f[len++] = FC;
  f[len++] = sub;
  f[len++] = (uint8_t) n;  // every 0x64 frame has a length byte, 0 = no data
  for (size_t i = 0; i < n; i++)
    f[len++] = data[i];
  send_raw(f, len);
  last_sub = sub;
}

// [addr 04 00 00 00 04 crc], the CU60's discovery read of input registers 0..3
inline void send_probe(uint8_t addr) {
  uint8_t f[8] = {addr, 0x04, 0, 0, 0, 4};
  send_raw(f, 6);
  probe_addr = addr;
}

// The probe's reply [addr 04 08 ir0..ir3 crc] anywhere in tail, up to 3 head bytes missing: the registers have
// to be there. Fills ir.
inline bool probe_reply(const uint8_t *tail, size_t n, uint8_t addr, uint16_t *ir) {
  for (size_t missing = 0; missing <= 3; missing++) {
    size_t have = 13 - missing;
    for (size_t end = have; end <= n; end++) {
      uint8_t f[13] = {addr, 0x04, 8};
      memcpy(f + missing, tail + end - have, have);
      if (f[0] != addr || f[1] != 0x04 || f[2] != 8 || pbus_crc16(f, 11) != (uint16_t) (f[11] | (f[12] << 8)))
        continue;
      for (int i = 0; i < 4; i++)
        ir[i] = (uint16_t) (f[3 + 2 * i] << 8 | f[4 + 2 * i]);
      return true;
    }
  }
  return false;
}

// The version in the stream's name field, "ci600 X.Y.Z" from mkupdate.py, as maj<<16 | min<<8 | patch. -1 if none.
inline int32_t name_version(const uint8_t *s, size_t n) {
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

inline std::string version_text(uint32_t v) {
  return std::to_string(v >> 16 & 0xFF) + "." + std::to_string(v >> 8 & 0xFF) + "." + std::to_string(v & 0xFF);
}

// True if rx_tail ends with frame minus its first `missing` bytes, CRC included
inline bool tail_is(const uint8_t *frame, size_t full, size_t missing) {
  size_t have = full - missing;
  if (rx_tail_len < have)
    return false;
  return memcmp(rx_tail + rx_tail_len - have, frame + missing, have) == 0;
}

// Matches a reply to the request that is out. Sets reply, and bl_state/bl_detail, reply_code or probe_ir.
inline void match_reply() {
  if (probe_addr) {
    if (probe_reply(rx_tail, rx_tail_len, probe_addr, probe_ir)) {
      reply = R_PROBE;
      awaiting = false;
    }
    return;
  }
  // status reply, up to 4 head bytes missing. State and detail have to be there.
  if (last_sub == SUB_STATUS) {
    for (size_t missing = 0; missing <= 4; missing++) {
      size_t have = 8 - missing;
      if (rx_tail_len < have)
        continue;  // too short for this offset, maybe not for a larger one
      uint8_t f[8] = {BL_ADDR, FC, SUB_STATUS, 2, 0, 0, 0, 0};
      memcpy(f + missing, rx_tail + rx_tail_len - have, have);
      if (f[3] != 2 || f[4] > ST_DONE)
        continue;
      if (pbus_crc16(f, 6) == (uint16_t) (f[6] | (f[7] << 8))) {
        bl_state = f[4];
        bl_detail = f[5];
        reply = R_STATUS;
        awaiting = false;
        return;
      }
    }
  }
  // ack [01 64 sub 00 crc crc], up to 3 head bytes missing. The 00 and the CRC have to be there. Not for a status
  // poll: during an update the echo filter is off, and on a transceiver that echoes the poll looks just like an ack.
  if (last_sub != SUB_STATUS) {
    uint8_t f[6] = {BL_ADDR, FC, last_sub, 0, 0, 0};
    uint16_t c = pbus_crc16(f, 4);
    f[4] = (uint8_t) c;
    f[5] = (uint8_t) (c >> 8);
    for (size_t missing = 0; missing <= 3; missing++) {
      if (tail_is(f, 6, missing)) {
        reply = R_ACK;
        awaiting = false;
        return;
      }
    }
  }
  // exception [01 e4 code crc crc], up to 2 head bytes missing. The code has to be there.
  for (uint8_t code = 1; code <= 6; code++) {
    uint8_t f[5] = {BL_ADDR, (uint8_t) (FC | 0x80), code, 0, 0};
    uint16_t c = pbus_crc16(f, 3);
    f[3] = (uint8_t) c;
    f[4] = (uint8_t) (c >> 8);
    for (size_t missing = 0; missing <= 2; missing++) {
      if (tail_is(f, 5, missing)) {
        reply = R_EXC;
        reply_code = code;
        awaiting = false;
        return;
      }
    }
    if (code == EXC_BUSY && tail_is(f, 5, 3))  // only the CRC got through, just a hint
      weak_busy = true;
  }
}

// The component calls this with every slice received, our own frames too. Collects the bootloader's bytes.
inline void on_frame(const uint8_t *f, size_t n) {
  if (id(server).is_own_frame())  // our own frame echoed back, not a reply
    return;
  if (phase == IDLE || phase == DONE || phase == FAIL || !awaiting)
    return;
  for (size_t i = 0; i < n; i++) {
    if (rx_tail_len == sizeof rx_tail) {
      memmove(rx_tail, rx_tail + 1, sizeof rx_tail - 1);
      rx_tail_len--;
    }
    rx_tail[rx_tail_len++] = f[i];
  }
  match_reply();
}

// With the firmware staged
inline void begin() {
  uint8_t head[2 + 0xEF];
  size_t n = std::min(staged_len, sizeof head);
  file_version = staged_read(0, head, n) ? name_version(head, n) : -1;
  // the address the panel reported if any, then the usual ones, never this ESP's own
  const uint8_t own = id(server).server_address(), usual[] = {panel_addr, 3, 1, 2};
  probe_n = 0;
  for (uint8_t a : usual)
    if (a && a != own && std::find(probe_addrs, probe_addrs + probe_n, a) == probe_addrs + probe_n)
      probe_addrs[probe_n++] = a;
  probe_i = probe_tries = 0;
  phase = PROBE;
  tries = 0;
  cleaned = fresh = bl_seen = false;
  clean_tries = 0;
  polls = 0;
  sent = 0;
  inflight = 0;
  step_ms = start_ms = millis();
  next_ms = 0;
  awaiting = false;
  id(server).set_updating(true);
  progress(0);
  status("Looking for the panel...");
}

// For update.yaml's button, before it downloads. False with a status if a flash is running or there's no spare slot.
inline bool can_start() {
  if (phase != IDLE && phase != DONE && phase != FAIL) {
    status("Already flashing");
    return false;
  }
  return (staging = spare_slot()) != nullptr;
}

// update.yaml's http_request.get passes its response here. Writes the .fxup to the spare app slot a block at a
// time, which holds up the main loop until it's done, then starts the flash.
template<typename Response> void download(Response &r) {
  if (!can_start())
    return;
  const esp_partition_t *part = staging;
  size_t total = r->content_length;
  staged_len = 0;
  if (r->status_code != 200) {
    status("Download failed (HTTP " + std::to_string(r->status_code) + ")");
    return;
  }
  if (!total || total > part->size) {
    status("Download failed, no file size or the file's too big");
    return;
  }
  status("Downloading...");
  static constexpr size_t BLK = 4096;  // one erase block at a time, esp_partition_write needs it erased first
  static uint8_t buf[BLK];
  size_t fill = 0, off = 0;
  uint32_t t0 = millis(), heard = t0;
  last_pct = -1;
  while (off < total && millis() - t0 < DOWNLOAD_TIMEOUT_MS && millis() - heard < STALL_MS) {
    int n = r->read(buf + fill, std::min(BLK - fill, total - off - fill));
    App.feed_wdt();
    if (n < 0)
      break;
    if (n == 0) {  // nothing yet with Arduino, the server closed early with ESP-IDF
      delay(1);
      continue;
    }
    heard = millis();
    fill += n;
    if (!off && fill == (size_t) n && buf[0] != 0x0a) {  // a firmware stream starts with protobuf field 1
      status("That's not a panel firmware file");
      return;
    }
    if (fill < BLK && off + fill < total)
      continue;
    size_t wlen = (fill + 3) & ~3u;  // 4-byte align the tail
    std::fill(buf + fill, buf + wlen, 0xFF);
    if (off % part->erase_size == 0)
      esp_partition_erase_range(part, off, part->erase_size);
    if (esp_partition_write(part, off, buf, wlen) != ESP_OK) {
      status("Couldn't save the download on the ESP");
      return;
    }
    off += fill;
    fill = 0;
    progress((int) (100ull * off / total));
  }
  if (off < total) {
    status(millis() - t0 >= DOWNLOAD_TIMEOUT_MS
               ? "The download timed out"
               : "The download stopped at " + std::to_string(off) + " of " + std::to_string(total) + " bytes");
    return;
  }
  staged_len = off;
  ESP_LOGI(TAG, "Staged %u bytes", (unsigned) staged_len);
  begin();
}

inline void finish(bool ok, const std::string &why = "") {
  if (!ok && bl_seen)          // leave the loader clean for the next try, and if the old app
    send(BL_ADDR, SUB_RESET);  // was never erased the panel goes back to running it
  id(server).set_updating(false);
  phase = ok ? DONE : FAIL;
  if (ok)
    status("Done, the panel is restarting");
  else
    status((why.empty() ? "Flash failed" : why) + ". Nothing's broken, just try again");
  progress(ok ? 100 : 0);
}

// Stops before anything is flashed, so the panel keeps running what it has
inline void refuse(const std::string &why) {
  id(server).set_updating(false);
  phase = FAIL;
  status(why);
  progress(0);
}

// the loader's detail byte in state 5
inline std::string detail_text(uint8_t d) {
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

inline void enter_phase(Phase p) {
  phase = p;
  step_ms = millis();
  polls = 0;
  next_ms = 0;
}

// The probe's answer decides. Stock answers 0, 0, 0 and its version, ours FW_TYPE, 0, the patch level and the
// version, see the firmware's mbslave.c.
inline void probed() {
  uint8_t addr = probe_addrs[probe_i];
  const uint16_t *ir = probe_ir;
  std::string at = " at address " + std::to_string(addr);
  if (ir[0] == 0) {
    std::string v = std::to_string(ir[3] >> 8) + "." + std::to_string(ir[3] & 0xFF);
    if (ir[3] >> 8 != 7 && !force) {
      refuse("Panel runs stock " + v + ", only 7.x is known to work. Panel Firmware Force flashes it anyway");
      return;
    }
    status("Stock panel " + v + at + ", flashing");
  } else if (ir[0] == FW_TYPE) {
    uint32_t have = (uint32_t) (ir[3] >> 8) << 16 | (ir[3] & 0xFF) << 8 | (ir[2] & 0xFF);
    std::string v = version_text(have);
    if ((int32_t) have == file_version && !force) {
      refuse("Panel already has " + v);
      return;
    }
    std::string to = file_version < 0 ? "unknown version" : version_text((uint32_t) file_version);
    status("Panel " + v + " → " + to + at);
  } else {
    char type[8];
    snprintf(type, sizeof type, "0x%04X", ir[0]);
    refuse(std::string("Unknown panel type ") + type);
    return;
  }
  app_addr = addr;
  enter_phase(ENTER);
}

// PIECE, but the last piece is never shorter than MIN_PIECE. The loader finishes a short piece so fast that a
// probe might not catch it in state 3.
inline size_t piece_len() {
  size_t left = staged_len - sent;
  size_t n = left < PIECE ? left : PIECE;
  if (left > n && left - n < MIN_PIECE)
    n = left - MIN_PIECE;
  return n;
}

inline void send_piece() {
  static uint8_t piece[PIECE];
  inflight = piece_len();
  if (!staged_read(sent, piece, inflight)) {
    finish(false, "Couldn't read the download back");
    return;
  }
  send(BL_ADDR, SUB_PIECE, piece, inflight);
}

inline void piece_delivered() {
  sent += inflight;
  inflight = 0;
  progress((int) (100ull * sent / staged_len));
  enter_phase(READY_WAIT);
}

inline void poll_status() {
  send(BL_ADDR, SUB_STATUS);
  polls++;
  next_ms = millis() + POLL_GAP_MS;
}

// Runs from the component's status and read callbacks. One bus action per call, and only once the bus has
// been quiet long enough not to collide with the CS60.
inline void pump() {
  if (phase == IDLE || phase == DONE || phase == FAIL)
    return;
  uint32_t now = millis();
  if (now - start_ms > TOTAL_TIMEOUT_MS) {
    finish(false, "Flashing timed out");
    return;
  }

  bool timed_out = false;
  if (awaiting) {
    if (reply != R_NONE)
      awaiting = false;
    else if (now - sent_ms > (probe_addr ? PROBE_TIMEOUT_MS : REPLY_TIMEOUT_MS)) {
      awaiting = false;
      timed_out = true;
    } else {
      return;
    }
  }
  Reply got = reply;
  reply = R_NONE;

  if (got == R_STATUS || got == R_ACK)
    bl_seen = true;
  // a failure ends the update, except in WAIT_BL where it's left over from an earlier session
  if (got == R_STATUS && bl_state == ST_FAILED && phase != WAIT_BL) {
    ESP_LOGW(TAG, "Bootloader failed: detail %u at %u/%u bytes", bl_detail, (unsigned) sent, (unsigned) staged_len);
    finish(false, detail_text(bl_detail));
    return;
  }

  // ---- react to what came back ----
  switch (phase) {
    case PROBE_WAIT:  // the panel answered, or one more try at this address, then the next
      if (got == R_PROBE) {
        probed();
        break;
      }
      if (!timed_out)
        break;
      if (++probe_tries < 2) {
        enter_phase(PROBE);
        break;
      }
      probe_tries = 0;
      if (++probe_i < probe_n) {
        enter_phase(PROBE);
        break;
      }
      refuse("No panel answered on the bus");
      return;
    case WAIT_BL:
      if (got == R_STATUS && bl_state == ST_BOOT) {
        fresh = true;  // still starting up, so it just booted with clean RAM
      } else if (got == R_STATUS) {
        // A loader that was already waiting can hold leftovers from an interrupted session, like a write pointer
        // or a half-read field, and arming doesn't clear them. So reset it unless we saw it boot or reset it
        // already. It comes back with zeroed RAM, in the bootloader if the app was erased, else in the app, and
        // ENTER kicks that back into the bootloader.
        if (!cleaned && (!fresh || bl_state != ST_IDLE)) {
          status("Resetting the bootloader...");
          enter_phase(CLEAN);
          break;
        }
        if (bl_state == ST_IDLE) {
          enter_phase(ARM);
          status("Bootloader's ready...");
        } else if (clean_tries < 4) {
          enter_phase(CLEAN);
        }  // busy and missed the reset, send it again
        else {
          finish(false, "The panel's bootloader is stuck");
          return;
        }
      } else if (now - step_ms > 12000) {
        if (++tries > 2) {
          finish(false, "The panel didn't go into its bootloader");
          return;
        }
        enter_phase(ENTER);
      }
      break;
    case ARM_WAIT:  // armed, the loader shows 6 and a poll moves it to 2
      if (got == R_STATUS) {
        if (bl_state == ST_READY) {
          enter_phase(sent < staged_len ? PIECE_TX : END);
          status("Flashing...");
        } else if (bl_state == ST_IDLE && polls >= 3)
          enter_phase(ARM);      // the arm never got there
      } else if (got == R_ACK) { /* the arm's own ack, the polls will show it */
      }
      if (now - step_ms > 10000) {
        finish(false, "The panel's bootloader didn't get ready");
        return;
      }
      break;
    case READY_WAIT:  // poll until the loader is ready for the next piece
      if (got == R_STATUS) {
        if (bl_state == ST_READY) {
          enter_phase(sent < staged_len ? PIECE_TX : END);
          break;
        }
        if (bl_state == ST_IDLE) {
          finish(false, "The panel dropped out of the update");
          return;
        }
      }
      if (now - step_ms > WORK_TIMEOUT_MS) {
        finish(false, "The panel stopped answering");
        return;
      }
      break;
    case PIECE_PROBE:  // the piece or a resend of it is out
      if (got == R_ACK) {
        piece_delivered();
        break;
      }  // accepted now
      if (got == R_EXC && reply_code == EXC_BUSY) {
        piece_delivered();
        break;
      }  // accepted earlier, being worked on
      if (got == R_EXC) {
        finish(false, "The panel refused a frame (error " + std::to_string(reply_code) + ")");
        return;
      }
      if (weak_busy) {
        enter_phase(PIECE_CONFIRM);
        break;
      }  // probably delivered, ask once
      if (now - step_ms > WORK_TIMEOUT_MS) {
        finish(false, "The panel stopped answering");
        return;
      }
      break;
    case PIECE_CONFIRM:  // one status poll after a busy-looking tail
      if (got == R_STATUS) {
        if (bl_state == ST_WORKING || bl_state == ST_DONE) {
          piece_delivered();
          break;
        }
        if (bl_state == ST_READY) {
          enter_phase(PIECE_TX);
          break;
        }  // never arrived, send it again
        if (bl_state == ST_IDLE) {
          finish(false, "The panel dropped out of the update");
          return;
        }
      } else if (timed_out) {
        // Lost the poll's reply. If the loader was in 6 the poll moved it to 2 and the piece is in. If the piece
        // never arrived it's in 2 as well. No way to tell, so we stop.
        finish(false, "Lost the panel's answer at a bad moment");
        return;
      }
      break;
    case END_WAIT:  // the loader commits and goes idle
      if (got == R_STATUS) {
        if (bl_state == ST_IDLE) {
          enter_phase(RESET);
          break;
        }
        if (bl_state == ST_READY && polls >= 3) {
          enter_phase(END);
          break;
        }  // the end never got there
      }
      if (now - step_ms > 30000) {
        finish(false, "The panel didn't finish the update");
        return;
      }
      break;
    default:
      break;
  }
  if (phase == DONE || phase == FAIL)
    return;

  if (id(server).quiet_min_us() < 2500)  // let the bus settle before we transmit
    return;
  now = millis();

  // ---- the next request ----
  switch (phase) {
    case PROBE:
      send_probe(probe_addrs[probe_i]);
      enter_phase(PROBE_WAIT);
      break;
    case CLEAN:  // reset the loader, it restarts without a reply, then go in again
      send(BL_ADDR, SUB_RESET);
      cleaned = true;
      clean_tries++;
      awaiting = false;
      enter_phase(ENTER);
      next_ms = now + 2500;  // time to restart, and for the app to boot if it does
      break;
    case ENTER:  // kick the running app into the bootloader with 0x64/2
      if (now < next_ms)
        break;
      fresh = false;
      send(app_addr, SUB_ARM);  // the app resets without answering, we poll next
      enter_phase(WAIT_BL);
      awaiting = false;
      next_ms = now + 60;  // poll early, the loader only says "starting" for ~150 ms after it boots
      break;
    case WAIT_BL:
    case ARM_WAIT:
    case READY_WAIT:
    case END_WAIT:
      if (now >= next_ms)
        poll_status();
      break;
    case PIECE_CONFIRM:
      if (polls == 0)
        poll_status();
      break;
    case ARM:
      send(BL_ADDR, SUB_ARM);
      enter_phase(ARM_WAIT);
      break;
    case PIECE_TX:
      send_piece();
      if (phase == PIECE_TX)
        enter_phase(PIECE_PROBE);
      break;
    case PIECE_PROBE:  // no usable answer, send the same piece again
      if (now >= next_ms) {
        send_piece();
        next_ms = now + POLL_GAP_MS;
      }
      break;
    case END:
      send(BL_ADDR, SUB_END);
      enter_phase(END_WAIT);
      break;
    case RESET:
      send(BL_ADDR, SUB_RESET);
      finish(true);
      break;
    default:
      break;
  }
}

// called from update.yaml on boot
inline void setup() {
  id(server).add_on_status_callback(pump);
  id(server).add_on_read_callback(pump);
  id(server).add_on_frame_callback(on_frame);
}

}  // namespace flexit::panel::update
