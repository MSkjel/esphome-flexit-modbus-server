// A picture's piece or an animation on its way to the panel: its header, DATA and END, then the panel's K_PIC_ACK.
// The panel takes one at a time.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include "panel_link.h"
#include "plink.h"
#include "util.h"

namespace flexit::panel {

// Each transfer gets a new id to match the acks
inline uint8_t transfer_ids;

struct Transfer {
  static constexpr int TRIES = 2;            // goes again this often when lost or refused
  static constexpr unsigned CHUNK = 64;      // bytes of DATA at a time
  static constexpr uint32_t ACK_MS = 10000;  // the panel answers END within a second or so
  static inline const Transfer *on_link;     // the last one opened, the one on its way while its head is set
  enum Pumped { GOING, AGAIN, GAVE_UP };     // what pump() did, see there

  uint8_t id = 0;
  bool head = false, ended = false;
  uint32_t pos = 0, end_ms = 0;  // end_ms, see no_answer()
  int tries = 0;
  const uint8_t *data = nullptr;  // what goes, len bytes, set by open()
  uint32_t len = 0;

  // The header goes next. data has to stay put until it's done.
  void open(const uint8_t *d, uint32_t n) {
    id = ++transfer_ids;
    head = true;
    ended = false;
    pos = 0;
    data = d;
    len = n;
    on_link = this;
  }
  void reset() {
    head = false;
    tries = 0;
  }
  // To go again from its header, false once it had its tries
  bool retry() {
    head = false;
    return ++tries <= TRIES;
  }
  // The panel refused it with st, true if it goes again: after a bad CRC, the link's fault, or as the whole picture
  // for a patch. The panel would refuse anything else again.
  bool refused(uint8_t st, bool patch = false) {
    head = false;
    return (st == PIC_BAD_CRC || patch) && retry();
  }
  bool answers(uint32_t ack) const { return head && (ack >> 16 & 0xFF) == id; }  // a K_PIC_ACK for it
  bool blocked() const { return on_link && on_link != this && on_link->head; }   // the other one is on its way

  // True once the panel has had ACK_MS on a working link since END and hasn't answered. end_ms is when END went,
  // or the link last wasn't working.
  bool no_answer() {
    if (!head || !ended || !link::connected || link::tx.refused) {
      end_ms = millis();
      return false;
    }
    return millis() - end_ms > ACK_MS;
  }

  // For pump(): data as it is, from pos on
  unsigned raw(uint8_t *b, unsigned max) {
    unsigned k = std::min<uint32_t>(max, len - pos);
    memcpy(b, data + pos, k);
    pos += k;
    return k;
  }

  // On a working link: begin() opens it and sends the header once the link is free. Then DATA from next(b, max) as
  // there's room until deadline_us, and END with data's CRC. next() returns fewer than max bytes only at the end.
  // AGAIN or GAVE_UP when END got no answer.
  template<typename Begin, typename Next> Pumped pump(uint32_t deadline_us, Begin begin, Next next) {
    if (no_answer())  // the panel lost it or its answer
      return retry() ? AGAIN : GAVE_UP;
    if (!link::connected)
      return GOING;
    if (!head) {
      if (link::waiting() > link::BACKLOG || blocked())
        return GOING;  // let the other one finish first
      begin();
    }
    // Each turn has room for a chunk and the END's 4 bytes, so neither send fails
    while (!ended && link::waiting() < link::BACKLOG && link::fits(CHUNK + 4, 2) && before(deadline_us)) {
      uint8_t b[CHUNK] = {};  // DATA must be a multiple of 4 bytes, so the last chunk is padded
      unsigned n = next(b, sizeof b);
      if (n)
        link::send(K_PIC_DATA, b, (int) (n + 3) & ~3);
      if (n < sizeof b) {
        link::send(K_PIC_END, plink_crc32(0, data, len));
        ended = true;
      }
    }
    return GOING;
  }
  // pump() with data as it is
  template<typename Begin> Pumped pump(uint32_t deadline_us, Begin begin) {
    return pump(deadline_us, begin, [this](uint8_t *b, unsigned max) { return raw(b, max); });
  }
};

}  // namespace flexit::panel
