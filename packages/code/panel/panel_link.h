// Our end of the panel link. plink.h, pbulk.h and pbus.h are copied unchanged from the panel firmware's proto/.
//
// Right after each CS60 status broadcast, about 9 times a second, we send the panel as many queued messages as fit,
// even none, so it can answer with its own. While there's more to send, extra frames go in the gaps after polls.
#pragma once
#include <utility>
#include <vector>
#include "pbulk.h"
#include "plink.h"

namespace flexit::panel::link {

// Max bytes queued before items, pictures and animations wait, so values and notes don't wait long behind them
static constexpr unsigned BACKLOG = 512;

inline pbulk_esp_t tx;                                 // outgoing message queue and link state
inline pbus_gaps_t gaps;                               // tracks the gaps after the CS60's polls
inline uint32_t heard_ms;                              // millis() of the panel's last answer
inline bool connected;                                 // the panel answers and has our link version, kept by panel.h
inline std::vector<std::pair<uint16_t, uint32_t>> in;  // from the panel, for panel.h's loop
inline uint32_t log_ms, gap_frames;

// The panel refuses the link, or frames in the gaps after polls, while the CS60 doesn't keep to its timing. We ask
// again a minute after it started, and it lets us back in once the CS60 has kept to it for a while. Each is logged
// once when it starts and once when it ends.
inline struct {
  uint32_t ms;      // when the last one started
  bool link, gaps;  // logged as started, not yet as ended
  bool in_gap;      // the last frame went in a gap after a poll
} refusal;

inline void refusals(bool was_refused, bool was_no_gaps, bool answered) {
  auto &r = refusal;
  if ((tx.refused && !was_refused) || (tx.no_gaps && !was_no_gaps))
    r.ms = millis();
  if (tx.refused && !r.link) {
    ESP_LOGE("panel", "The panel refuses the link for a while: it saw the CS60 send sooner than expected after its "
                      "status broadcast");
    r.link = true;
  }
  if (tx.no_gaps && !r.gaps) {
    ESP_LOGW("panel", "The panel saw the CS60 send sooner than expected after a poll: no gaps after polls for a while");
    r.gaps = true;
  }
  if (answered && !tx.refused && r.link) {
    ESP_LOGI("panel", "The panel lets the link back in");
    r.link = false;
  }
  if (answered && r.in_gap && !tx.no_gaps && r.gaps) {
    ESP_LOGI("panel", "The panel takes frames in the gaps after polls again");
    r.gaps = false;
  }
}

// pbulk callback, queues each message from the panel as a number
inline void got(void *, uint16_t key, const uint8_t *b, int n) { in.emplace_back(key, plink_number(b, n)); }

// left_us is the least time we have before the bus must be clear. The frame starts within PBUS_ESP_START_US.
inline void send_window(uint32_t left_us, bool in_gap) {
  uint8_t f[PBULK_FRAME_MAX];
  int n = pbulk_esp_window(&tx, left_us, f);
  if (!n)
    return;
  refusal.in_gap = in_gap;
  gap_frames += in_gap;
  uint32_t t0 = micros();
  id(server).send_frame(f, n - 2);  // send_frame appends the CRC itself, over the one pbulk wrote
  // Leave more room from now on if this took longer than start-up, the frame and 300 us to release the bus
  pbulk_esp_took(&tx, (int32_t) (micros() - t0) - n * PBUS_BYTE_US - (int32_t) PBUS_ESP_START_US - 300);
}

// Status callback, in the quiet after the CS60's status broadcast
inline void window() {
  if (id(server).updating())  // update.h is flashing the panel and has the bus
    return;
  if ((tx.refused || tx.no_gaps) && millis() - refusal.ms > 60000) {
    ESP_LOGD("panel", "Asking the panel again");
    tx.refused = tx.no_gaps = false;
  }
  delayMicroseconds(pbus_wait_us(id(server).status_min_age_us()));  // the panel is still handling the status frame
  send_window(pbus_status_left(id(server).status_age_us()), false);
}

// Read callback, for the gap after each CS60 poll
inline void gap() {
  if (id(server).updating() || !pbulk_esp_gap_left(&tx, &gaps, micros()))
    return;
  delayMicroseconds(pbus_wait_us(id(server).quiet_min_us()));  // stay PBUS_START_MIN_US clear of the last frame
  uint32_t left = pbulk_esp_gap_left(&tx, &gaps, micros());
  if (left)
    send_window(left, true);
}

// Frame callback, for every frame on the bus
inline void frame(const uint8_t *f, size_t n) {
  uint32_t now = micros(), age = id(server).frame_age_us();
  bool off = gaps.off;
  pbus_gaps_frame(&gaps, f, (int) n, now - age - (uint32_t) n * PBUS_BYTE_US, now - (uint32_t) n * 86);
  if (gaps.off != off) {
    if (gaps.off) {
      ESP_LOGW("panel", "The CS60 sent sooner than expected after a poll: no gaps after polls for a while");
    } else {
      ESP_LOGI("panel", "The CS60 keeps to its steps again: the gaps after polls are back");
    }
  }
  if (n < 3 || f[0] != PBULK_ADDR || f[1] != PBULK_FC_ANSWER)
    return;
  uint32_t answered = tx.answered, sessions = tx.sessions;
  bool refused = tx.refused, no_gaps = tx.no_gaps;
  pbulk_esp_answer(&tx, f, (int) n, got, nullptr);
  if (tx.answered != answered)
    heard_ms = millis();
  if (tx.sessions != sessions) {
    ESP_LOGI("panel", "The panel knew an earlier ESP by our session number: a new one");
  }
  refusals(refused, no_gaps, tx.answered != answered);
}

// Length of a link frame, so the component's splitter knows where it ends
inline size_t frame_length(const uint8_t *f, size_t available) {
  if (available < 3)
    return available + 1;  // wait for the length byte
  return (size_t) pbulk_frame_len(f, (int) available);
}

inline void setup(uint8_t panel_address) {
  pbulk_esp_init(&tx, (uint16_t) random_uint32(), panel_address);
  id(modbus_uart).set_rx_full_threshold(1);  // hand over every byte at once, so we know when the window began
  id(server).set_frame_length(PBULK_ADDR, frame_length);
  id(server).add_on_status_callback(window);
  id(server).add_on_frame_callback(frame);
  id(server).add_on_read_callback(gap);
}

// Queues bytes for the panel, like a text or picture data
inline bool send(uint16_t key, const uint8_t *b, int n) {
  if (pbulk_esp_send(&tx, key, b, n, plink_kind(key) == PLINK_VALUE))
    return true;
  ESP_LOGW("panel", "Too much waiting for the panel, dropped 0x%03X", key);
  return false;
}

inline bool send(uint16_t key, uint32_t value) {
  uint8_t b[4] = {(uint8_t) value, (uint8_t) (value >> 8), (uint8_t) (value >> 16), (uint8_t) (value >> 24)};
  return send(key, b, 4);
}

// After a hello, drop what's unsent except values. hello() sends the rest again.
inline void forget() {
  pbulk_esp_drop_unsent(&tx, [](uint16_t key) { return plink_kind(key) == PLINK_VALUE; });
}

inline unsigned waiting() { return tx.qn; }  // bytes queued that the panel hasn't confirmed yet
inline bool queued(uint16_t key) { return pbulk_esp_queued(&tx, key); }   // one with this key it hasn't confirmed
inline unsigned room() { return pbulk_esp_room(&tx); }                    // free bytes in the queue
inline bool online() { return heard_ms && millis() - heard_ms < 30000; }  // it normally answers every cycle

inline void log_stats() {
  if (millis() - log_ms < 60000)
    return;
  log_ms = millis();
  ESP_LOGD("panel",
           "Link: %u frames, %u polls, %u answered, %u unanswered, %u windows too short, %u from the panel, %u lost; "
           "%u gaps after polls, %u frames in them%s",
           (unsigned) tx.frames, (unsigned) tx.polls, (unsigned) tx.answered, (unsigned) tx.missed,
           (unsigned) tx.short_windows, (unsigned) tx.got, (unsigned) tx.resyncs, (unsigned) gaps.gaps,
           (unsigned) gap_frames, gaps.off || tx.no_gaps ? " (off)" : "");
}

}  // namespace flexit::panel::link
