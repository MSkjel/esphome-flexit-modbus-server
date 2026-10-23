// CU60 bus timing for the link, see pbus.h
#include "pbus.h"
#include <string.h>

// Table-driven because the panel checks frames in its UART interrupt. Bit by bit, a status frame took 0.2 ms.
static const uint16_t CRC_T[256] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241, 0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1,
    0xC481, 0x0440, 0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40, 0x0A00, 0xCAC1, 0xCB81, 0x0B40,
    0xC901, 0x09C0, 0x0880, 0xC841, 0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40, 0x1E00, 0xDEC1,
    0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41, 0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040, 0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1,
    0xF281, 0x3240, 0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441, 0x3C00, 0xFCC1, 0xFD81, 0x3D40,
    0xFF01, 0x3FC0, 0x3E80, 0xFE41, 0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840, 0x2800, 0xE8C1,
    0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41, 0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640, 0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0,
    0x2080, 0xE041, 0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240, 0x6600, 0xA6C1, 0xA781, 0x6740,
    0xA501, 0x65C0, 0x6480, 0xA441, 0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41, 0xAA01, 0x6AC0,
    0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840, 0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40, 0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1,
    0xB681, 0x7640, 0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041, 0x5000, 0x90C1, 0x9181, 0x5140,
    0x9301, 0x53C0, 0x5280, 0x9241, 0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440, 0x9C01, 0x5CC0,
    0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40, 0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40, 0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0,
    0x4C80, 0x8C41, 0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641, 0x8201, 0x42C0, 0x4380, 0x8341,
    0x4100, 0x81C1, 0x8081, 0x4040};

uint16_t pbus_crc16(const uint8_t *p, int n) {
  uint16_t c = 0xFFFF;
  while (n-- > 0)
    c = (uint16_t) (c >> 8 ^ CRC_T[(c ^ *p++) & 0xFF]);
  return c;
}

bool pbus_crc_ok(const uint8_t *f, int n) { return n >= 4 && pbus_crc16(f, n - 2) == (f[n - 2] | f[n - 1] << 8); }

static const uint8_t STATUS_HEAD[7] = {0x00, 0x10, 0x00, 0xBE, 0x00, 0x55, 0xAA};

bool pbus_is_status(const uint8_t *f, int n) {
  return n == PBUS_STATUS_LEN && !memcmp(f, STATUS_HEAD, sizeof STATUS_HEAD);
}

int pbus_status_part(const uint8_t *buf, int n) {
  for (int k = n < PBUS_STATUS_LEN - 1 ? n : PBUS_STATUS_LEN - 1; k > 0; k--)
    if (!memcmp(buf + n - k, STATUS_HEAD, k < 7 ? k : 7))
      return k;
  return 0;
}

// Length of the CU60 frame at the start of f[n], 0 if none. The CU60 sends broadcasts with FC 6, 16 and 0x65 to
// address 0, and requests to panels with FC 1, 3, 4 to address 1-3 from register 0x000-0x1FF. A reply has its
// byte count where the register's high byte would be.
static int cu60_len(const uint8_t *f, int n) {
  if (n < 8)
    return 0;
  int len = f[1] == 0x10 ? f[6] + 9 : 8;
  if (len > n || !pbus_crc_ok(f, len))
    return 0;
  bool broadcast = f[0] == 0 && (f[1] == 0x06 || f[1] == 0x10 || f[1] == 0x65);
  bool request = f[0] >= 1 && f[0] <= 3 && (f[1] == 1 || f[1] == 3 || f[1] == 4) && f[2] <= 1;
  return broadcast || request ? len : 0;
}

bool pbus_is_cu60(const uint8_t *f, int n) { return cu60_len(f, n) > 0; }

uint32_t pbus_since_status(uint32_t dt_us, int k, int rx_threshold, uint32_t isr_us) {
  // 86.806 us per byte, rounded so the result can't come out too small.
  // Nothing follows the status frame for a while, so its last byte is handed over 2 byte times after it arrives.
  int64_t a = (int64_t) dt_us + isr_us + 2 * 86806 / 1000 + 1;
  // Byte k + 1 wasn't readable then, so it arrived less than rx_threshold - 1 byte times before. The remaining
  // 178 - k bytes took at least a byte time each.
  int64_t b = (int64_t) dt_us + isr_us + (int64_t) (rx_threshold - 1) * 86806 / 1000 + 1 -
              (int64_t) (PBUS_STATUS_LEN - 1 - k) * 86805 / 1000;
  if (b < 0)
    b = 0;
  return (uint32_t) (a < b ? a : b);
}

uint32_t pbus_wait_us(uint32_t quiet_us) { return quiet_us < PBUS_START_MIN_US ? PBUS_START_MIN_US - quiet_us : 0; }

uint32_t pbus_status_left(uint32_t age_us, uint32_t end_us) {
  return age_us < end_us - PBUS_ESP_START_US ? end_us - PBUS_ESP_START_US - age_us : 0;
}

// One more check of the CU60's timing. bad sets *off, and base good checks in a row clear it. That count doubles
// each time it's set again, up to 128x.
static void judge(bool *off, uint8_t *times, uint32_t *clean, bool bad, uint32_t base) {
  if (bad) {
    if (!*off && *times < 8)
      (*times)++;
    *off = true;
    *clean = 0;
  } else if (*off && ++*clean >= base << (*times - 1)) {
    *off = false;
  }
}

// ---------------------------------------------------------------- panel side

void pbus_panel_init(pbus_panel_t *w) {
  memset(w, 0, sizeof *w);
  w->quiet_min_us = 0xFFFFFFFF;
  w->end_us = PBUS_END_US;
}

// The deadline after a status broadcast, further out once enough of them had a longer quiet time after
static uint32_t end_for(const pbus_panel_t *w) {
  if (w->quiet_n < PBUS_END_SURE || w->quiet_min_us < PBUS_END_US + PBUS_END_MARGIN_US)
    return PBUS_END_US;
  uint32_t e = w->quiet_min_us - PBUS_END_MARGIN_US;
  e = e < PBUS_END_MAX_US ? e : PBUS_END_MAX_US;
  return e - e % PBUS_END_STEP_US;
}

// the last burst was a status broadcast, less than a second ago
static bool recent(const pbus_panel_t *w, uint32_t now_ms) { return w->after_status && now_ms - w->status_ms < 1000; }

void pbus_panel_burst(pbus_panel_t *w, const uint8_t *b, int n, uint32_t start_us, uint32_t idle_us, uint32_t now_ms) {
  if (recent(w, now_ms) && pbus_is_cu60(b, n)) {  // the CU60's next frame, not a late reply from a server
    uint32_t us = start_us - w->status_end_us;
    // only if the 1 ms tick agrees, in case the microsecond clock was off, like after a debugger halted the chip
    if (us + 2000 >= (now_ms - w->status_ms) * 1000) {
      bool was = w->refused;
      judge(&w->refused, &w->refusals, &w->clean, us < PBUS_CU60_MIN_US, PBUS_REARM);
      if (was && !w->refused)  // let back in, measure again
        w->quiet_n = 0;
      if (us < w->quiet_min_us || !w->quiet_n)
        w->quiet_min_us = us;
      if (w->quiet_n < PBUS_END_SURE)
        w->quiet_n++;
      w->end_us = end_for(w);
    }
  }
  // did the burst end with a status broadcast?
  w->after_status = n >= PBUS_STATUS_LEN && pbus_is_status(b + n - PBUS_STATUS_LEN, PBUS_STATUS_LEN) &&
                    pbus_crc_ok(b + n - PBUS_STATUS_LEN, PBUS_STATUS_LEN);
  if (w->after_status) {
    w->status_end_us = idle_us - PBUS_BYTE_US;
    w->status_ms = now_ms;
  }
  pbus_gaps_frame(&w->gaps, b, n, start_us, start_us);
}

uint32_t pbus_panel_window(pbus_panel_t *w, uint32_t idle_us, uint32_t now_ms, uint8_t *say) {
  *say = w->refused ? PBUS_REFUSED : PBUS_OK;
  if (recent(w, now_ms) && idle_us - w->status_end_us < w->end_us)  // in the quiet time after a status broadcast
    return w->end_us - (idle_us - w->status_end_us);
  uint32_t left = pbus_gaps_left(&w->gaps, idle_us);  // or in a gap after a poll
  if (left && w->gaps.off && *say == PBUS_OK)
    *say = PBUS_NO_GAPS;
  return left;
}

// ---------------------------------------------------------------- gaps after the CU60's polls

void pbus_gaps_frame(pbus_gaps_t *g, const uint8_t *f, int n, uint32_t start_lo_us, uint32_t start_hi_us) {
  // One frame at a time on the bus, so it started after the last one ended. Tighter than a late read can tell.
  if (g->end_known && (int32_t) (g->end_lo_us - start_lo_us) > 0 && (int32_t) (g->end_lo_us - start_hi_us) <= 0)
    start_lo_us = g->end_lo_us;
  g->end_lo_us = start_lo_us + (uint32_t) n * 86806u / 1000u;
  g->end_known = true;
  if (n >= 1 && f[0] == PBUS_LINK_ADDR)
    return;                                // a link frame
  bool ok = pbus_crc_ok(f, n);             // one whole frame with its CRC at the end
  bool probe = ok && n == 8 && f[1] == 4;  // the CU60 looking for panels, also at address 0xFE
  bool cu60 = probe || cu60_len(f, n) == n;
  g->open = false;
  if (cu60) {
    if (g->armed)  // did it come back sooner than its steps say?
      judge(&g->off, &g->offs, &g->clean,
            (int32_t) (start_hi_us - g->req_us) < (int32_t) (g->next_us - PBUS_GAP_EARLY_US), PBUS_GAP_REARM);
    g->armed = false;
  }
  uint8_t a = g->req;
  g->req = 0;
  if (!ok)
    return;  // can't tell what that was
  if (pbus_is_status(f, n)) {
    if (g->probing || (g->started && start_lo_us - g->status_us > 1000000)) {  // panels may have changed
      g->present = 0;
      g->cycles = 0;
    } else if (g->started && g->cycles < 2) {
      g->cycles++;
    }
    g->started = true;
    g->probing = false;
    g->status_us = start_lo_us;
    g->step_ok = true;
    g->step_lo_us = start_lo_us;
    g->step_hi_us = start_hi_us;
    return;
  }
  if (probe) {
    g->probing = true;
    g->cycles = 0;
  }
  if (cu60) {
    uint16_t from = (uint16_t) (f[2] << 8 | f[3]);
    bool poll = f[1] == 1 && f[0] >= 1 && f[0] <= 3 && (from == 0 || from == 0x14C);
    if (poll) {
      g->present |= (uint8_t) (1u << f[0]);
      // On time if right after another step and possibly on the grid. The time since that step is between lo
      // and hi.
      int32_t lo = (int32_t) (start_lo_us - g->step_hi_us), hi = (int32_t) (start_hi_us - g->step_lo_us);
      int32_t k = (lo / 2 + hi / 2 + PBUS_GAP_STEP_US / 2) / PBUS_GAP_STEP_US * PBUS_GAP_STEP_US;
      bool off_grid = lo > k + PBUS_GAP_GRID_US || hi < k - PBUS_GAP_GRID_US;
      if (g->step_ok && k > 0 && !off_grid)
        g->req = f[0];
      g->counters = from == 0x14C;
      g->req_us = start_lo_us;
    }
    g->step_ok = poll;  // a step to time the next request from
    g->step_lo_us = start_lo_us;
    g->step_hi_us = start_hi_us;
    return;
  }
  // A reply to the poll that flags nothing the CU60 would ask about next. The counters poll looks at the first
  // 20 coils, the settings poll at all of them.
  if (!a || f[0] != a || f[1] != 1 || f[2] + 5 != n || g->cycles < 2)
    return;
  for (int i = 0; i < f[2]; i++)
    if (f[3 + i] & (!g->counters ? 0xFF : i < 2 ? 0xFF : i == 2 ? 0x0F : 0))
      return;
  int steps = 2;    // the reply is read on the next step, the next poll goes out on the one after
  if (g->counters)  // plus the absent panels it would poll next, it goes from the highest address down
    for (int q = a - 1; q >= 1 && !(g->present & 1u << q); q--)
      steps += 2;
  g->next_us = (uint32_t) steps * PBUS_GAP_STEP_US;
  g->open = g->armed = true;
  g->gaps++;
}

uint32_t pbus_gaps_left(pbus_gaps_t *g, uint32_t now_us) {
  if (!g->open)
    return 0;
  int32_t left = (int32_t) (g->req_us + g->next_us - PBUS_GAP_SLACK_US - now_us);
  if (left <= 0) {
    g->open = false;
    return 0;
  }
  return (uint32_t) left;
}
