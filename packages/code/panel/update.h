// Flashes the CI600 panel over the bus through its stock bootloader, from stock or our firmware. No SWD or jumper.
// First settings.h saves our firmware's own settings, which go back once the panel starts again. Then it probes the
// panel like the CX60 does at discovery, and only flashes stock 7.x or ours, and ours only when the file is another
// version unless forced. Also finds one stuck in its bootloader after a failed flash. update_yaml.h hooks it up.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include "esphome/core/defines.h"

#ifndef USE_ESP32
#error "The panel packages need an ESP32"
#endif
#include "esp_partition.h"

namespace flexit::panel::update {

// What the flash needs from the rest of the ESP: the component's bus, and settings.h to keep the settings it erases
struct Hooks {
  virtual void send_frame(uint8_t *f, size_t n) = 0;                  // as it is, the component adds the CRC
  virtual void expect_reply(uint8_t addr, uint8_t fc, size_t n) = 0;  // so the component hands it on whole
  virtual bool is_own_frame() = 0;                                    // the slice on_frame() has is ours, echoed
  virtual uint8_t server_address() = 0;                               // this ESP's
  virtual void set_updating(bool updating) = 0;                       // the bus is ours, and the panel away
  virtual uint32_t quiet_min_us() = 0;                                // since the last frame on the bus
  virtual void save_settings() = 0;                                   // asks the panel for them
  virtual bool settings_saved() = 0;                                  // they're in, or there was no panel to ask
  virtual void settings_flashed() = 0;                                // they go back once the panel starts again
};
inline Hooks *hooks;

// ---- configuration (set from update.yaml) -------------------------------------------------------------
inline uint8_t panel_addr = 0;  // the bus address the panel reported over the link, 0 = none yet
inline bool force = false;      // flash a stock panel that isn't 7.x, or the version the panel already has

// ---- staging (the .fxup in the ESP's spare app slot) --------------------------------------------------
inline size_t staged_len = 0;  // bytes staged, 0 = nothing yet
inline const esp_partition_t *staging = nullptr;

// ---- reporting, to update.yaml's entities -------------------------------------------------------------
inline std::function<void(const std::string &)> on_status;
inline std::function<void(int)> on_progress;  // 0..100
inline int last_pct = -1;

void status(const std::string &s);
void progress(int pct);

// ---- the Modbus function 0x64 bootloader protocol -----------------------------------------------------
inline constexpr uint8_t BL_ADDR = 1;  // the bootloader always answers here
inline constexpr uint8_t FC = 0x64;
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
inline constexpr uint8_t SUB_ARM = 2, SUB_STATUS = 3, SUB_PIECE = 4, SUB_END = 6, SUB_RESET = 7;
inline constexpr uint8_t ST_BOOT = 0, ST_IDLE = 1, ST_READY = 2, ST_WORKING = 3, ST_FAILED = 5, ST_DONE = 6;
inline constexpr uint8_t EXC_BUSY = 6;
inline constexpr size_t PIECE = 240;                  // stream bytes per sub-4 frame, max 249
inline constexpr size_t MIN_PIECE = 128;              // shortest last piece, see piece_len
inline constexpr uint32_t REPLY_TIMEOUT_MS = 30;      // the loader answers within ~2 ms if it answers at all
inline constexpr uint32_t PROBE_TIMEOUT_MS = 100;     // what the CX60 gives a panel at discovery
inline constexpr uint32_t POLL_GAP_MS = 10;           // between status polls while the loader is working
inline constexpr uint32_t FIRST_POLL_MS = 60;         // after the kick, the loader only says "starting" for ~150 ms
inline constexpr uint32_t CLEAN_MS = 2500;            // after resetting the loader, for it and maybe the app to boot
inline constexpr uint32_t ENTER_TIMEOUT_MS = 12000;   // for the loader to answer after the kick or the reset
inline constexpr uint32_t ARM_TIMEOUT_MS = 10000;     // for the armed loader to get ready
inline constexpr uint32_t WORK_TIMEOUT_MS = 90000;    // the loader erases on the first piece and goes quiet for seconds
inline constexpr uint32_t END_TIMEOUT_MS = 30000;     // for the loader to commit the firmware
inline constexpr uint32_t RESET_POLL_MS = 300;        // after the last reset, the ROM says "starting" for a moment
inline constexpr uint32_t RESTARTED_MS = 1200;        // the loader quiet this long after the last reset, it restarted
inline constexpr uint32_t RESET_AGAIN_MS = 2000;      // else the reset goes again, then after twice as long
inline constexpr uint32_t RESET_AGAIN_MAX_MS = 5000;  // at most
inline constexpr uint32_t TOTAL_TIMEOUT_MS = 20 * 60000;
inline constexpr uint32_t SAVE_TIMEOUT_MS = 10000;  // for the panel's settings over the link
inline constexpr uint32_t DOWNLOAD_TIMEOUT_MS = 5 * 60000;
inline constexpr uint32_t STALL_MS = 15000;     // no data from the server for this long, it's gone
inline constexpr uint32_t BUS_QUIET_US = 2500;  // since the last frame on the bus before we send
inline constexpr int READ_AGAIN = -0x7007;      // -ESP_ERR_HTTP_EAGAIN, http_request's read() timed out with no data

enum Phase {
  IDLE,
  SAVE,
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
  RESET_WAIT,
  ABORT,  // failed, the loader gets its reset once the bus is quiet, then FAIL
  DONE,
  FAIL
};

// what came back for the request that is out
enum Reply { R_NONE, R_STATUS, R_ACK, R_EXC, R_PROBE };

// The flash, from begin() until it's DONE or FAIL
struct Flash {
  Phase phase = IDLE;
  size_t accepted = 0;        // stream bytes the loader has accepted
  size_t inflight = 0;        // bytes in the piece being delivered
  uint32_t start_ms = 0;      // when the flash began
  uint32_t step_ms = 0;       // when this phase began
  uint32_t next_ms = 0;       // earliest time for the next request
  uint32_t sent_ms = 0;       // when the last request went out
  uint32_t answer_ms = 0;     // the loader's last answer since the last reset
  bool awaiting = false;      // a request is out, waiting for its reply
  uint8_t last_sub = 0;       // the sub-command of the request that is out
  uint8_t polls = 0;          // status polls in the current phase
  uint8_t enter_tries = 0;    // the loader didn't show after the kick or the reset, tried again
  uint8_t reset_tries = 0;    // resets sent at the end, the loader can miss one
  uint8_t clean_tries = 0;    // reset requests sent. A loader busy erasing can miss one
  bool cleaned = false;       // we reset the loader this session, we always start from a clean one
  bool fresh = false;         // saw the loader starting up, so it just booted with clean RAM
  bool bl_seen = false;       // the loader has answered this session
  uint8_t app_addr = 0;       // where the panel answered the probe and gets the kick, 0 = in its bootloader
  int32_t file_version = -1;  // from the staged stream's name, -1 = none
  // the addresses to probe in order, probe_i == probe_n is the bootloader
  uint8_t probe_addrs[4] = {}, probe_n = 0, probe_i = 0, probe_tries = 0;
  uint8_t probe_addr = 0;        // the address a probe is out to, 0 = a bootloader request is out
  bool probe_heard[4] = {};      // which of probe_addrs answered
  uint16_t probe_ir[4][4] = {};  // the input registers each answered
  uint8_t rx_tail[32] = {};      // the bytes heard since our request went out, oldest first
  size_t rx_tail_len = 0;
  Reply reply = R_NONE;
  uint8_t bl_state = 0xFF, bl_detail = 0;
  uint8_t reply_code = 0;  // the exception code for R_EXC
  bool weak_busy = false;  // only the CRC of a "busy" got through, so just a hint
  std::string why;         // ABORT's status
};
inline Flash F;

inline bool running() { return F.phase != IDLE && F.phase != DONE && F.phase != FAIL; }

// For update.yaml's button, before it downloads. False with a status if a flash is running or there's no spare slot.
bool can_start();

// The download as stage() reads it, from http_request's response
struct Download {
  int status_code;
  size_t content_length;  // 0 when the server doesn't say, like a chunked one
  std::function<int(uint8_t *buf, size_t max)> read;
  std::function<bool()> read_complete;
};

// Writes the .fxup to the spare app slot a block at a time, which holds up the main loop until it's done, then
// starts the flash
void stage(const Download &d);

// update.yaml's http_request.get passes its response here, once can_start() said yes
template<typename Response> void download(Response &r) {
  stage({r->status_code, r->content_length, [&r](uint8_t *buf, size_t max) { return r->read(buf, max); },
         [&r]() { return r->is_read_complete(); }});
}

// With the firmware staged
void begin();

// The component's status and read callbacks. One bus action per call, and only once the bus has been quiet long
// enough not to collide with the CX60.
void pump();

// The component's frame callback, with every slice received, our own frames too. Collects the bootloader's bytes.
void on_frame(const uint8_t *f, size_t n);

// Ends the flash. A failure resets the loader first if it answered.
void finish(bool ok, const std::string &why = "");

void enter_phase(Phase p);

// The probe's reply [addr 04 08 ir0..ir3 crc] in rx_tail, up to 3 head bytes missing: the registers have to be
// there. Fills ir.
bool probe_reply(uint8_t addr, uint16_t *ir);

// The version in the stream's name field, "ci600 X.Y.Z" from mkupdate.py, as maj<<16 | min<<8 | patch. -1 if none.
int32_t name_version(const uint8_t *s, size_t n);

}  // namespace flexit::panel::update
