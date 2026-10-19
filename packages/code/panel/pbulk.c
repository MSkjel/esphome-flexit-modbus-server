// Link frames, see pbulk.h
#include "pbulk.h"
#include <string.h>

_Static_assert(1 + PLINK_HELP_MAX <= PBULK_MSG_MAX, "a help text fits an ordinary window");

static void put_crc(uint8_t *f, int n) {
  uint16_t c = pbus_crc16(f, n);
  f[n] = c & 0xFF;
  f[n + 1] = c >> 8;
}

static uint16_t get16(const uint8_t *b) { return (uint16_t) (b[0] | b[1] << 8); }

static void put16(uint8_t *b, uint16_t v) {
  b[0] = v & 0xFF;
  b[1] = v >> 8;
}

static int16_t diff(uint16_t a, uint16_t b) {  // a - b for message numbers, which wrap around
  return (int16_t) (uint16_t) (a - b);
}

// message numbers a message uses, DATA uses one per 4 bytes
static int count(uint16_t key, int len) { return key == PBULK_DATA_KEY ? len / 4 : 1; }

static int count_all(const uint8_t *p, int n) {  // message numbers used by the payload p[n]
  int c = 0;
  for (int i = 0; i + 3 <= n; i += 3 + p[i + 2])
    c += count(get16(p + i), p[i + 2]);
  return c;
}

// Next session after s. Xorshift visits every other nonzero 16-bit value before s comes round again
static uint16_t new_session(uint16_t s) {
  s ^= (uint16_t) (s << 7);
  s ^= s >> 9;
  s ^= (uint16_t) (s << 8);
  return s ? s : 1;
}

int pbulk_frame_len(const uint8_t *q, int n) {
  if (n < 3 || q[0] != PBULK_ADDR)
    return 0;
  if (q[1] == PBULK_FC_SEND && q[PBULK_N] <= PBULK_MAX)
    return PBULK_FRAME(q[PBULK_N]);
  if (q[1] == PBULK_FC_ANSWER && q[PBULK_N] <= PBULK_ANSWER_MAX)
    return PBULK_ANSWER(q[PBULK_N]);
  return 0;
}

bool pbulk_parse(const uint8_t *p, int n, void (*f)(void *u, uint16_t key, const uint8_t *b, int len), void *u) {
  int i = 0;
  while (i < n) {
    if (n - i < 3 || n - i < 3 + p[i + 2])
      return false;
    f(u, get16(p + i), p + i + 3, p[i + 2]);
    i += 3 + p[i + 2];
  }
  return true;
}

int pbulk_skip(const uint8_t *p, int n, uint16_t num, uint16_t *next, uint8_t *out) {
  if (diff(num, *next) > 0)
    return 0;  // it starts after the one we want, so none of it follows on
  int i = 0, o = 0;
  uint16_t m = num;
  while (i < n) {
    if (n - i < 3 || n - i < 3 + p[i + 2])
      return -1;
    uint16_t key = get16(p + i);
    int len = p[i + 2], c = count(key, len);
    int have = diff(*next, m);  // how many of this message's numbers we already have
    if (have < c) {
      int skip = have > 0 ? have * 4 : 0;  // only DATA can be partly received
      if (out) {
        put16(out + o, key);
        out[o + 2] = (uint8_t) (len - skip);
        memcpy(out + o + 3, p + i + 3 + skip, len - skip);
      }
      o += 3 + len - skip;
    }
    m = (uint16_t) (m + c);
    i += 3 + len;
  }
  if (diff(m, *next) > 0)
    *next = m;
  return o;
}

// ---------------------------------------------------------------- panel side

void pbulk_panel_init(pbulk_panel_t *p, uint16_t session) {
  memset(p, 0, sizeof *p);
  p->psess = session ? session : 1;
}

static unsigned rx_room(const pbulk_panel_t *p) {
  return (unsigned) (PBULK_RX_BUF - 1 - (p->head - p->tail + PBULK_RX_BUF) % PBULK_RX_BUF);
}

// How many bytes of our messages, from message `from` of out, fit in room bytes. *msgs gets their count.
static int out_fit(const pbulk_panel_t *p, int from, int room, int *msgs) {
  int i = 0, k = 0, b = 0, m = 0;
  while (i < p->out_n) {
    uint16_t key = get16(p->out + i);
    int len = p->out[i + 2];
    if (m >= from) {
      if (b + 3 + len > room)
        break;
      b += 3 + len;
      k += count(key, len);
    }
    m += count(key, len);
    i += 3 + len;
  }
  *msgs = k;
  return b;
}

static int out_bytes_from(const pbulk_panel_t *p, int from) {  // offset of message `from` in out
  int i = 0, m = 0;
  while (i < p->out_n && m < from) {
    m += count(get16(p->out + i), p->out[i + 2]);
    i += 3 + p->out[i + 2];
  }
  return i;
}

int pbulk_panel_frame(pbulk_panel_t *p, const uint8_t *f, int n, uint32_t left_us, uint8_t say, uint8_t *a) {
  p->frames++;
  if (pbulk_frame_len(f, n) != n || f[1] != PBULK_FC_SEND || !pbus_crc_ok(f, n) ||
      pbulk_skip(f + PBULK_PAYLOAD, f[PBULK_N], 0, &(uint16_t) {0}, NULL) < 0) {
    p->bad++;
    return 0;
  }
  uint8_t len = f[PBULK_N], st = say;
  uint16_t num = get16(f + PBULK_NUM), esess = get16(f + PBULK_ESESS);
  if (esess != p->esess) {  // the ESP restarted, start from this frame's first message
    p->esess = esess;
    p->enext = num;
  }
  // What the ESP has of ours
  int out_from = 0;  // our first message in out that it doesn't have
  if (get16(f + PBULK_PSESS) == p->psess) {
    uint16_t pnext = get16(f + PBULK_PNEXT), pnum = p->pnum;
    if (diff(pnext, pnum) > 0) {  // it wants what we never sent, so it heard an earlier session of ours
      p->psess = new_session(p->psess);
      p->sessions++;
    } else if (p->out_full) {
      int have = diff(pnext, p->out_num);
      if (have >= count_all(p->out, p->out_n))
        p->out_full = false;  // it has them all, the main loop can put the next ones
      else if (have > 0)
        out_from = have;
    }
  }
  if (st != PB_OK) {
    p->refused++;
  } else if (!len) {
    p->polls++;
  } else {
    // It shouldn't send past what we have. If it does three times running, resync to what it sends.
    if (diff(num, p->enext) > 0 && ++p->ahead >= 3) {
      p->enext = num;
      p->resyncs++;
    }
    if (diff(num, p->enext) <= 0)
      p->ahead = 0;
    uint16_t next = p->enext;
    int take = pbulk_skip(f + PBULK_PAYLOAD, len, num, &next, NULL);
    if (take > 0 && rx_room(p) < (unsigned) take + 1) {
      st = PB_BUSY;
      p->busy++;
    } else if (take > 0) {
      uint8_t tmp[PBULK_MAX];
      pbulk_skip(f + PBULK_PAYLOAD, len, num, &p->enext, tmp);
      uint16_t h = p->head;
      p->buf[h] = (uint8_t) take;
      h = (h + 1) % PBULK_RX_BUF;
      for (int i = 0; i < take; i++) {
        p->buf[h] = tmp[i];
        h = (h + 1) % PBULK_RX_BUF;
      }
      p->head = h;  // set last, the main loop may be reading
      p->taken++;
    } else {
      p->enext = next;  // nothing new in it, our last answer got lost
    }
  }
  // Only answer if it will be off the bus in time, with as many of our messages as fit
  if (left_us < PBUS_PANEL_US + PBULK_ANSWER(0) * PBUS_BYTE_US) {
    p->late++;
    return 0;
  }
  int k = 0, b = 0;
  if (p->out_full && st != PB_REFUSED && st != PB_NO_GAPS) {
    int room = (int) (left_us - PBUS_PANEL_US) / PBUS_BYTE_US - PBULK_ANSWER(0);
    b = out_fit(p, out_from, room < PBULK_ANSWER_MAX ? room : PBULK_ANSWER_MAX, &k);
  }
  unsigned want = p->waiting + (p->out_full ? p->out_n - out_bytes_from(p, out_from) - b : 0);
  a[0] = PBULK_ADDR;
  a[1] = PBULK_FC_ANSWER;
  a[PBULK_N] = (uint8_t) b;
  put16(a + PBULK_A_ESESS, esess);
  put16(a + PBULK_A_ENEXT, p->enext);
  a[PBULK_A_STATUS] = st;
  put16(a + PBULK_A_PSESS, p->psess);
  // Number of our first message in it. With none waiting, the number our next one gets, in case the ESP just
  // started.
  put16(a + PBULK_A_PNUM, (uint16_t) (p->out_full ? p->out_num + out_from : p->pnum));
  a[PBULK_A_WANT] = want > 255 ? 255 : (uint8_t) want;
  if (b)
    memcpy(a + PBULK_A_PAYLOAD, p->out + out_bytes_from(p, out_from), b);
  put_crc(a, PBULK_A_PAYLOAD + b);
  return PBULK_ANSWER(b);
}

int pbulk_panel_take(pbulk_panel_t *p, uint8_t *out) {
  uint16_t t = p->tail;
  if (t == p->head)
    return 0;
  int n = p->buf[t];
  t = (t + 1) % PBULK_RX_BUF;
  for (int i = 0; i < n; i++) {
    out[i] = p->buf[t];
    t = (t + 1) % PBULK_RX_BUF;
  }
  p->tail = t;
  return n;
}

bool pbulk_panel_put(pbulk_panel_t *p, const uint8_t *payload, int n) {
  if (p->out_full || n <= 0 || n > PBULK_ANSWER_MAX)
    return false;
  memcpy(p->out, payload, n);
  p->out_n = (uint8_t) n;
  p->out_num = p->pnum;
  __sync_synchronize();  // out is complete before the interrupt may send it
  p->out_full = true;
  __sync_synchronize();  // and only then the next number, which empty answers report
  p->pnum = (uint16_t) (p->pnum + count_all(payload, n));
  return true;
}

// ---------------------------------------------------------------- ESP side

#define ESP_ROOM (PBULK_ESP_BUF - 64)  // headroom for split(), which adds 3 bytes per DATA message it cuts

void pbulk_esp_init(pbulk_esp_t *t, uint16_t session) {
  memset(t, 0, sizeof *t);
  t->last = -1;
  t->session = session ? session : 1;
}

unsigned pbulk_esp_room(const pbulk_esp_t *t) { return t->qn < ESP_ROOM ? ESP_ROOM - t->qn : 0; }

// Bytes at the front of the queue in messages that have gone out, all or part of them
static int sent_bytes(const pbulk_esp_t *t) {
  int at = 0;
  for (uint16_t m = t->num; at + 3 <= t->qn && diff(t->unsent, m) > 0; at += 3 + t->q[at + 2])
    m = (uint16_t) (m + count(get16(t->q + at), t->q[at + 2]));
  return at;
}

bool pbulk_esp_pending(const pbulk_esp_t *t) { return t->qn > 0; }

bool pbulk_esp_send(pbulk_esp_t *t, uint16_t key, const uint8_t *b, int n, bool replace) {
  if (n < 0 || n > (key == PBULK_DATA_KEY ? 252 : PBULK_MSG_MAX) || (key == PBULK_DATA_KEY && n % 4))
    return false;  // DATA can be split, anything else has to fit the best window, or it would wait forever
  if (replace)     // a newer value for a message that hasn't gone out
    for (int at = sent_bytes(t); at + 3 <= t->qn; at += 3 + t->q[at + 2])
      if (get16(t->q + at) == key && t->q[at + 2] == n) {
        memcpy(t->q + at + 3, b, n);
        return true;
      }
  // picture bytes are appended to the DATA message before them
  if (key == PBULK_DATA_KEY && t->last >= 0 && get16(t->q + t->last) == PBULK_DATA_KEY &&
      t->q[t->last + 2] + n <= 252) {
    if (t->qn + n > ESP_ROOM)
      return false;
    memcpy(t->q + t->qn, b, n);
    t->qn = (uint16_t) (t->qn + n);
    t->q[t->last + 2] = (uint8_t) (t->q[t->last + 2] + n);
    return true;
  }
  if (t->qn + 3 + n > ESP_ROOM)
    return false;
  t->last = (int16_t) t->qn;
  put16(t->q + t->qn, key);
  t->q[t->qn + 2] = (uint8_t) n;
  memcpy(t->q + t->qn + 3, b, n);
  t->qn = (uint16_t) (t->qn + 3 + n);
  return true;
}

void pbulk_esp_drop_unsent(pbulk_esp_t *t, bool (*keep)(uint16_t key)) {
  int at = 0, o = 0, sent = sent_bytes(t);
  t->last = -1;
  while (at + 3 <= t->qn) {
    int len = 3 + t->q[at + 2];
    if (at < sent || keep(get16(t->q + at))) {
      memmove(t->q + o, t->q + at, len);
      t->last = (int16_t) o;
      o += len;
    }
    at += len;
  }
  t->qn = (uint16_t) o;
}

// Splits the DATA message at `at` in two: `part` bytes, then the rest
static void split(pbulk_esp_t *t, int at, int part) {
  int cut = at + 3 + part;
  memmove(t->q + cut + 3, t->q + cut, t->qn - cut);
  t->q[cut] = t->q[at];
  t->q[cut + 1] = t->q[at + 1];
  t->q[cut + 2] = (uint8_t) (t->q[at + 2] - part);
  t->q[at + 2] = (uint8_t) part;
  t->qn += 3;
  if (t->last == at)
    t->last = (int16_t) cut;
  else if (t->last > at)
    t->last += 3;
}

// Bytes from the front of the queue that fit in max: whole messages, DATA split on a 4-byte boundary
static int fill(pbulk_esp_t *t, int max) {
  int at = 0;
  while (at < t->qn) {
    int whole = 3 + t->q[at + 2];
    if (at + whole <= max) {
      at += whole;
      continue;
    }
    int part = (max - at - 3) & ~3;
    if (get16(t->q + at) == PBULK_DATA_KEY && part >= 4 && t->qn + 3 <= PBULK_ESP_BUF) {
      split(t, at, part);
      at += 3 + part;
    }
    break;
  }
  return at;
}

// The panel has our messages before `next`: drop them
static void drop(pbulk_esp_t *t, uint16_t next) {
  int have = diff(next, t->num), at = 0;
  while (have > 0 && at < t->qn) {
    uint16_t key = get16(t->q + at);
    int len = t->q[at + 2], c = count(key, len);
    if (c <= have) {
      at += 3 + len;
      have -= c;
      t->num = (uint16_t) (t->num + c);
      continue;
    }
    // it has part of a DATA message: cut that part off the front
    memmove(t->q + at + 3, t->q + at + 3 + have * 4, t->qn - (at + 3 + have * 4));
    t->q[at + 2] = (uint8_t) (len - have * 4);
    t->qn -= (uint16_t) (have * 4);
    if (t->last > at)
      t->last -= (int16_t) (have * 4);
    t->num = (uint16_t) (t->num + have);
    have = 0;
  }
  memmove(t->q, t->q + at, t->qn - at);
  t->qn -= (uint16_t) at;
  t->last = t->last >= at ? (int16_t) (t->last - at) : -1;
}

int pbulk_esp_window(pbulk_esp_t *t, uint32_t left_us, uint8_t *out) {
  if (t->sent) {  // the last frame got no answer
    t->sent = false;
    t->missed++;
    if (t->margin_us < 2000)
      t->margin_us += PBULK_MARGIN_US;
  }
  if (t->refused)
    return 0;
  // What fits in left_us minus the margin and turnaround. That's our frame and the panel's answer, at least both
  // empty. Spare room goes first to what the panel has waiting, since it answers with as much as fits, then ours.
  if (left_us > 100000)
    left_us = 100000;
  int32_t left = (int32_t) left_us - PBUS_TURN_US - t->margin_us;
  int over = left / PBUS_BYTE_US - PBULK_FRAME(0) - PBULK_ANSWER(0);
  if (left < 0 || over < 0) {
    t->short_windows++;
    return 0;
  }
  int theirs = t->want < PBULK_ANSWER_MAX ? t->want : PBULK_ANSWER_MAX;
  int fit = over - (theirs < over ? theirs : over);
  int head = t->qn ? 3 + t->q[2] : 0;  // else a message that can't be split could wait behind theirs forever
  if (head > fit && head <= over && get16(t->q) != PBULK_DATA_KEY)
    fit = head;
  if (fit > PBULK_MAX)
    fit = PBULK_MAX;
  int n = fill(t, fit);
  if (n)
    t->frames++;
  else
    t->polls++;
  uint16_t end = (uint16_t) (t->num + count_all(t->q, n));
  if (diff(end, t->unsent) > 0)
    t->unsent = end;
  out[0] = PBULK_ADDR;
  out[1] = PBULK_FC_SEND;
  out[PBULK_N] = (uint8_t) n;
  put16(out + PBULK_ESESS, t->session);
  put16(out + PBULK_NUM, t->num);
  put16(out + PBULK_PSESS, t->psess);
  put16(out + PBULK_PNEXT, t->pnext);
  memcpy(out + PBULK_PAYLOAD, t->q, n);
  put_crc(out, PBULK_PAYLOAD + n);
  t->sent = true;
  return PBULK_FRAME(n);
}

uint32_t pbulk_esp_gap_left(const pbulk_esp_t *t, pbus_gaps_t *g, uint32_t now_us) {
  if (t->sent || t->refused || t->no_gaps || g->off || (!t->qn && !t->want))
    return 0;
  uint32_t left = pbus_gaps_left(g, now_us);
  return left > PBUS_ESP_START_US ? left - PBUS_ESP_START_US : 0;
}

void pbulk_esp_took(pbulk_esp_t *t, int32_t over_us) {
  if (over_us <= 0)
    return;
  if (over_us > 2000)
    over_us = 2000;
  if (t->margin_us < (uint16_t) over_us)
    t->margin_us = (uint16_t) over_us;
}

void pbulk_esp_answer(pbulk_esp_t *t, const uint8_t *a, int n,
                      void (*f)(void *u, uint16_t key, const uint8_t *b, int len), void *u) {
  if (pbulk_frame_len(a, n) != n || a[1] != PBULK_FC_ANSWER || !pbus_crc_ok(a, n) ||
      get16(a + PBULK_A_ESESS) != t->session || !t->sent)
    return;
  t->sent = false;
  t->answered++;
  if (t->margin_us >= 50)
    t->margin_us -= 50;
  uint8_t st = a[PBULK_A_STATUS];
  if (st == PB_REFUSED || st == PB_NO_GAPS) {
    if (st == PB_REFUSED)
      t->refused = true;
    else
      t->no_gaps = true;
    return;
  }
  if (st == PB_BUSY)
    t->busy++;
  uint16_t enext = get16(a + PBULK_A_ENEXT), pnum = get16(a + PBULK_A_PNUM);
  int have = diff(enext, t->num);
  if (have > diff(t->unsent, t->num)) {  // it wants what we never sent, so it heard an earlier session of ours
    t->session = new_session(t->session);
    t->sessions++;
    t->unsent = t->num;
  } else if (have > 0) {
    drop(t, enext);
  }
  t->want = a[PBULK_A_WANT];
  if (!t->heard ||
      get16(a + PBULK_A_PSESS) != t->psess) {  // the panel restarted, start from this answer's first message
    t->heard = true;
    t->psess = get16(a + PBULK_A_PSESS);
    t->pnext = pnum;
  }
  // It shouldn't send past what we have. If it does three times running, resync to what it sends.
  if (a[PBULK_N] && diff(pnum, t->pnext) > 0 && ++t->ahead >= 3) {
    t->pnext = pnum;
    t->resyncs++;
  }
  if (diff(pnum, t->pnext) <= 0)
    t->ahead = 0;
  uint8_t mine[PBULK_ANSWER_MAX];
  int k = pbulk_skip(a + PBULK_A_PAYLOAD, a[PBULK_N], pnum, &t->pnext, mine);
  if (k > 0) {
    t->got++;
    pbulk_parse(mine, k, f, u);
  }
}
