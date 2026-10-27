// Link frames, see pbulk.h
// Copied into esphome-flexit-modbus-server by make -C sim esp_sync, edit it here.
#include "pbulk.h"
#include "plink.h"
#include <string.h>

_Static_assert(1 + PLINK_HELP_MAX <= PBULK_MSG_MAX, "a help text fits an ordinary window");
_Static_assert(PBULK_DATA_KEY == K_PIC_DATA, "picture data is what pbulk splits");

#define RESYNC_AFTER 3  // frames in a row from the other end that start past the one we want, before we follow

static int16_t diff(uint16_t a, uint16_t b) {  // a - b for message numbers, which wrap around
  return (int16_t) (uint16_t) (a - b);
}

// message numbers a message uses, DATA uses one per 4 bytes
static int count(uint16_t key, int len) { return key == PBULK_DATA_KEY ? len / 4 : 1; }

// Offset of the message after the one at i in p[n], -1 if that one is cut short
static int msg_next(const uint8_t *p, int n, int i) {
  if (n - i < PBULK_MSG_HEAD || n - i < PBULK_MSG_HEAD + p[i + 2])
    return -1;
  return i + PBULK_MSG_HEAD + p[i + 2];
}

static int count_all(const uint8_t *p, int n) {  // message numbers used by the payload p[n]
  int c = 0;
  for (int i = 0, j; (j = msg_next(p, n, i)) >= 0; i = j)
    c += count(plink_get16(p + i), p[i + 2]);
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
  for (int j; (j = msg_next(p, n, i)) >= 0; i = j)
    f(u, plink_get16(p + i), p + i + PBULK_MSG_HEAD, p[i + 2]);
  return i == n;
}

// The messages in p[n] are numbered from num. Copies the ones from *next on to out unless it's NULL, moves *next
// past them and returns their length. -1 if p is malformed.
static int skip(const uint8_t *p, int n, uint16_t num, uint16_t *next, uint8_t *out) {
  if (diff(num, *next) > 0)
    return 0;  // it starts after the one we want, so none of it follows on
  int i = 0, o = 0;
  uint16_t m = num;
  for (int j; (j = msg_next(p, n, i)) >= 0; i = j) {
    uint16_t key = plink_get16(p + i);
    int len = p[i + 2], c = count(key, len);
    int have = diff(*next, m);  // how many of this message's numbers we already have
    if (have < c) {
      int part = have > 0 ? have * 4 : 0;  // only DATA can be partly received
      if (out) {
        plink_put16(out + o, key);
        out[o + 2] = (uint8_t) (len - part);
        memcpy(out + o + PBULK_MSG_HEAD, p + i + PBULK_MSG_HEAD + part, len - part);
      }
      o += PBULK_MSG_HEAD + len - part;
    }
    m = (uint16_t) (m + c);
  }
  if (i != n)
    return -1;
  if (diff(m, *next) > 0)
    *next = m;
  return o;
}

static bool well_formed(const uint8_t *p, int n) {
  uint16_t next = 0;
  return skip(p, n, 0, &next, NULL) >= 0;
}

// The other end's messages from num on, n bytes of them, when we want *next. It shouldn't send past that. If it
// does RESYNC_AFTER times running we follow, and the ones in between are lost.
static void follow(uint16_t num, int n, uint16_t *next, uint8_t *ahead, uint32_t *resyncs) {
  if (n && diff(num, *next) > 0 && ++*ahead >= RESYNC_AFTER) {
    *next = num;
    (*resyncs)++;
  }
  if (diff(num, *next) <= 0)
    *ahead = 0;
}

// ---------------------------------------------------------------- panel side

void pbulk_panel_init(pbulk_panel_t *p, uint16_t session) {
  memset(p, 0, sizeof *p);
  p->psess = session ? session : 1;
}

static unsigned rx_room(const pbulk_panel_t *p) {
  return (unsigned) (PBULK_RX_BUF - 1 - (p->head - p->tail + PBULK_RX_BUF) % PBULK_RX_BUF);
}

// Bytes of whole messages from offset at of out that fit in room
static int out_fit(const pbulk_panel_t *p, int at, int room) {
  int i = at, j;
  while ((j = msg_next(p->out, p->out_n, i)) >= 0 && j - at <= room)
    i = j;
  return i - at;
}

static int out_bytes_from(const pbulk_panel_t *p, int from) {  // offset of message `from` in out
  int i = 0, m = 0;
  for (int j; m < from && (j = msg_next(p->out, p->out_n, i)) >= 0; i = j)
    m += count(plink_get16(p->out + i), p->out[i + 2]);
  return i;
}

// What the ESP has of ours, from frame f: the first of our messages in out that it doesn't have, counted from
// out_num. Lets the main loop put the next ones once it has them all.
static int acked(pbulk_panel_t *p, const uint8_t *f) {
  if (plink_get16(f + PBULK_PSESS) != p->psess)
    return 0;
  uint16_t pnext = plink_get16(f + PBULK_PNEXT);
  if (diff(pnext, p->pnum) > 0) {  // it wants what we never sent, so it heard an earlier session of ours
    p->psess = new_session(p->psess);
    return 0;
  }
  if (!p->out_full)
    return 0;
  int have = diff(pnext, p->out_num);
  if (have < count_all(p->out, p->out_n))
    return have > 0 ? have : 0;
  p->out_full = false;  // it has them all, the main loop can put the next ones
  return 0;
}

// Takes the new messages in frame f into buf for the main loop. PB_BUSY if they don't fit.
static uint8_t take(pbulk_panel_t *p, const uint8_t *f) {
  uint16_t num = plink_get16(f + PBULK_NUM);
  follow(num, f[PBULK_N], &p->enext, &p->ahead, &p->resyncs);
  uint16_t next = p->enext;
  uint8_t tmp[PBULK_MAX];
  int n = skip(f + PBULK_PAYLOAD, f[PBULK_N], num, &next, tmp);
  if (n > 0 && rx_room(p) < (unsigned) n + 1) {
    p->busy++;
    return PB_BUSY;
  }
  // Into the ring as n, then the messages. Nothing new in it means our last answer got lost.
  if (n > 0) {
    uint16_t h = p->head;
    p->buf[h] = (uint8_t) n;
    h = (h + 1) % PBULK_RX_BUF;
    for (int i = 0; i < n; i++) {
      p->buf[h] = tmp[i];
      h = (h + 1) % PBULK_RX_BUF;
    }
    __sync_synchronize();  // the batch is in buf before head says so
    p->head = h;           // set last, the main loop may be reading
  }
  p->enext = next;
  return PB_OK;
}

// Our answer to the ESP's frame, with as many of our messages from out_from on as fit in left_us
static int answer(pbulk_panel_t *p, uint8_t *a, uint32_t left_us, uint8_t st, uint32_t end_us, int out_from) {
  int at = p->out_full ? out_bytes_from(p, out_from) : 0, b = 0;
  if (p->out_full && st != PB_REFUSED && st != PB_NO_GAPS) {
    int room = (int) (left_us - PBUS_PANEL_US) / PBUS_BYTE_US - PBULK_ANSWER(0);
    b = out_fit(p, at, room < PBULK_ANSWER_MAX ? room : PBULK_ANSWER_MAX);
  }
  unsigned want = p->waiting + (p->out_full ? p->out_n - at - b : 0);
  a[0] = PBULK_ADDR;
  a[1] = PBULK_FC_ANSWER;
  a[PBULK_N] = (uint8_t) b;
  plink_put16(a + PBULK_A_ESESS, p->esess);
  plink_put16(a + PBULK_A_ENEXT, p->enext);
  uint32_t ext = end_us > PBUS_END_US ? (end_us - PBUS_END_US) / PBUS_END_STEP_US : 0;
  a[PBULK_A_STATUS] = (uint8_t) (st | (ext < 63 ? ext : 63) << 2);
  plink_put16(a + PBULK_A_PSESS, p->psess);
  // Number of our first message in it. With none waiting, the number our next one gets, in case the ESP just
  // started.
  plink_put16(a + PBULK_A_PNUM, (uint16_t) (p->out_full ? p->out_num + out_from : p->pnum));
  a[PBULK_A_WANT] = want > 255 ? 255 : (uint8_t) want;
  memcpy(a + PBULK_A_PAYLOAD, p->out + at, b);
  return pbus_put_crc(a, PBULK_A_PAYLOAD + b);
}

int pbulk_panel_frame(pbulk_panel_t *p, uint8_t addr, const uint8_t *f, int n, uint32_t left_us, uint8_t say,
                      uint32_t end_us, uint8_t *a) {
  bool ok = pbulk_frame_len(f, n) == n && pbus_crc_ok(f, n);
  if (ok && (f[1] != PBULK_FC_SEND || f[PBULK_PANEL] != addr))
    return 0;  // another panel's link: the ESP's frames to it, or its answers
  p->frames++;
  if (!ok || !well_formed(f + PBULK_PAYLOAD, f[PBULK_N])) {
    p->bad++;
    return 0;
  }
  uint16_t esess = plink_get16(f + PBULK_ESESS);
  if (esess != p->esess) {  // the ESP restarted, start from this frame's first message
    p->esess = esess;
    p->enext = plink_get16(f + PBULK_NUM);
  }
  int out_from = acked(p, f);
  uint8_t st = say;
  if (st != PB_OK)
    p->refused++;
  else if (f[PBULK_N])
    st = take(p, f);
  // Only answer if it will be off the bus in time
  if (left_us < PBUS_PANEL_US + PBULK_ANSWER(0) * PBUS_BYTE_US) {
    p->late++;
    return 0;
  }
  return answer(p, a, left_us, st, end_us, out_from);
}

int pbulk_panel_take(pbulk_panel_t *p, uint8_t *out) {
  uint16_t t = p->tail;
  if (t == p->head)
    return 0;
  __sync_synchronize();  // buf after head, which the interrupt sets last
  int n = p->buf[t];
  t = (t + 1) % PBULK_RX_BUF;
  for (int i = 0; i < n; i++) {
    out[i] = p->buf[t];
    t = (t + 1) % PBULK_RX_BUF;
  }
  __sync_synchronize();  // done with buf before tail frees it for the interrupt
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

void pbulk_esp_init(pbulk_esp_t *t, uint16_t session, uint8_t panel) {
  memset(t, 0, sizeof *t);
  t->last = -1;
  t->session = session ? session : 1;
  t->panel = panel;
  t->end_us = PBUS_END_US;
}

unsigned pbulk_esp_room(const pbulk_esp_t *t) { return t->qn < ESP_ROOM ? ESP_ROOM - t->qn : 0; }

// Bytes at the front of the queue in messages that have gone out, all or part of them
static int sent_bytes(const pbulk_esp_t *t) {
  int at = 0;
  uint16_t m = t->num;
  for (int j; diff(t->unsent, m) > 0 && (j = msg_next(t->q, t->qn, at)) >= 0; at = j)
    m = (uint16_t) (m + count(plink_get16(t->q + at), t->q[at + 2]));
  return at;
}

// The first message with this key from byte `from` of the queue on, -1 if there's none
static int find(const pbulk_esp_t *t, int from, uint16_t key) {
  for (int at = from, j; (j = msg_next(t->q, t->qn, at)) >= 0; at = j)
    if (plink_get16(t->q + at) == key)
      return at;
  return -1;
}

bool pbulk_esp_queued(const pbulk_esp_t *t, uint16_t key) { return find(t, 0, key) >= 0; }

// Takes out the unsent message at `at`
static void remove_msg(pbulk_esp_t *t, int at) {
  int len = PBULK_MSG_HEAD + t->q[at + 2];
  memmove(t->q + at, t->q + at + len, t->qn - at - len);
  t->qn = (uint16_t) (t->qn - len);
  t->last = t->last == at ? -1 : t->last > at ? (int16_t) (t->last - len) : t->last;
}

bool pbulk_esp_send(pbulk_esp_t *t, uint16_t key, const uint8_t *b, int n, unsigned flags) {
  if (n < 0 || n > (key == PBULK_DATA_KEY ? PBULK_DATA_MAX : PBULK_MSG_MAX) || (key == PBULK_DATA_KEY && n % 4))
    return false;  // DATA can be split, anything else has to fit the best window, or it would wait forever
  // a newer value or HELLO replaces one still waiting
  int old = flags & (PBULK_REPLACE | PBULK_FIRST) ? find(t, sent_bytes(t), key) : -1;
  if (old >= 0 && !(flags & PBULK_FIRST) && t->q[old + 2] == n) {
    memcpy(t->q + old + PBULK_MSG_HEAD, b, n);
    return true;
  }
  if (old >= 0)
    remove_msg(t, old);
  // picture bytes are appended to the DATA message before them
  if (key == PBULK_DATA_KEY && t->last >= 0 && plink_get16(t->q + t->last) == PBULK_DATA_KEY &&
      t->q[t->last + 2] + n <= PBULK_DATA_MAX) {
    if (t->qn + n > ESP_ROOM)
      return false;
    memcpy(t->q + t->qn, b, n);
    t->qn = (uint16_t) (t->qn + n);
    t->q[t->last + 2] = (uint8_t) (t->q[t->last + 2] + n);
    return true;
  }
  // a HELLO goes ahead of the unsent messages, a restarted panel ignores everything before one
  int at = flags & PBULK_FIRST ? sent_bytes(t) : t->qn, len = PBULK_MSG_HEAD + n;
  if (t->qn + len > ESP_ROOM)
    return false;
  memmove(t->q + at + len, t->q + at, t->qn - at);
  t->last = at == t->qn ? (int16_t) at : t->last >= at ? (int16_t) (t->last + len) : t->last;
  plink_put16(t->q + at, key);
  t->q[at + 2] = (uint8_t) n;
  memcpy(t->q + at + PBULK_MSG_HEAD, b, n);
  t->qn = (uint16_t) (t->qn + len);
  return true;
}

void pbulk_esp_drop_unsent(pbulk_esp_t *t, bool (*keep)(uint16_t key)) {
  int at = 0, o = 0, sent = sent_bytes(t);
  t->last = -1;
  for (int j; (j = msg_next(t->q, t->qn, at)) >= 0; at = j)
    if (at < sent || keep(plink_get16(t->q + at))) {
      memmove(t->q + o, t->q + at, j - at);
      t->last = (int16_t) o;
      o += j - at;
    }
  t->qn = (uint16_t) o;
}

// Splits the DATA message at `at` in two: `part` bytes, then the rest
static void split(pbulk_esp_t *t, int at, int part) {
  int cut = at + PBULK_MSG_HEAD + part;
  memmove(t->q + cut + PBULK_MSG_HEAD, t->q + cut, t->qn - cut);
  t->q[cut] = t->q[at];
  t->q[cut + 1] = t->q[at + 1];
  t->q[cut + 2] = (uint8_t) (t->q[at + 2] - part);
  t->q[at + 2] = (uint8_t) part;
  t->qn += PBULK_MSG_HEAD;
  if (t->last == at)
    t->last = (int16_t) cut;
  else if (t->last > at)
    t->last += PBULK_MSG_HEAD;
}

// Bytes from the front of the queue that fit in max: whole messages, DATA split on a 4-byte boundary
static int fill(pbulk_esp_t *t, int max) {
  int at = 0;
  for (int j; (j = msg_next(t->q, t->qn, at)) >= 0; at = j) {
    if (j <= max)
      continue;
    int part = (max - at - PBULK_MSG_HEAD) & ~3;
    if (plink_get16(t->q + at) == PBULK_DATA_KEY && part >= 4 && t->qn + PBULK_MSG_HEAD <= PBULK_ESP_BUF) {
      split(t, at, part);
      at += PBULK_MSG_HEAD + part;
    }
    break;
  }
  return at;
}

// The panel has our messages before `next`: drop them
static void drop(pbulk_esp_t *t, uint16_t next) {
  int have = diff(next, t->num), at = 0;
  for (int j; have > 0 && (j = msg_next(t->q, t->qn, at)) >= 0; at = j) {
    uint16_t key = plink_get16(t->q + at);
    int len = t->q[at + 2], c = count(key, len);
    if (c <= have) {
      have -= c;
      t->num = (uint16_t) (t->num + c);
      continue;
    }
    // it has part of a DATA message: cut that part off the front
    int from = at + PBULK_MSG_HEAD;
    memmove(t->q + from, t->q + from + have * 4, t->qn - (from + have * 4));
    t->q[at + 2] = (uint8_t) (len - have * 4);
    t->qn -= (uint16_t) (have * 4);
    if (t->last > at)
      t->last -= (int16_t) (have * 4);
    t->num = (uint16_t) (t->num + have);
    break;
  }
  memmove(t->q, t->q + at, t->qn - at);
  t->qn -= (uint16_t) at;
  t->last = t->last >= at ? (int16_t) (t->last - at) : -1;
}

void pbulk_esp_no_answer(pbulk_esp_t *t) {
  if (!t->sent)
    return;
  t->sent = false;
  t->missed++;
  t->end_us = PBUS_END_US;
  if (t->margin_us < 2000)
    t->margin_us += PBULK_MARGIN_US;
}

void pbulk_esp_retry(pbulk_esp_t *t) { t->refused = t->no_gaps = false; }

int pbulk_esp_window(pbulk_esp_t *t, uint32_t left_us, uint8_t *out) {
  pbulk_esp_no_answer(t);
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
  int head = msg_next(t->q, t->qn, 0);  // else a message that can't be split could wait behind theirs forever
  if (head > fit && head <= over && plink_get16(t->q) != PBULK_DATA_KEY)
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
  out[PBULK_PANEL] = t->panel;
  plink_put16(out + PBULK_ESESS, t->session);
  plink_put16(out + PBULK_NUM, t->num);
  plink_put16(out + PBULK_PSESS, t->psess);
  plink_put16(out + PBULK_PNEXT, t->pnext);
  memcpy(out + PBULK_PAYLOAD, t->q, n);
  t->sent = true;
  return pbus_put_crc(out, PBULK_PAYLOAD + n);
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
  if (n < 3 || pbulk_frame_len(a, n) != n || a[1] != PBULK_FC_ANSWER || !pbus_crc_ok(a, n) ||
      plink_get16(a + PBULK_A_ESESS) != t->session || !t->sent)
    return;
  t->sent = false;
  t->answered++;
  if (t->margin_us >= 50)
    t->margin_us -= 50;
  uint8_t st = a[PBULK_A_STATUS] & 3;
  uint32_t end = PBUS_END_US + (uint32_t) (a[PBULK_A_STATUS] >> 2) * PBUS_END_STEP_US;
  t->end_us = (uint16_t) (end < PBUS_END_MAX_US ? end : PBUS_END_MAX_US);
  if (st == PB_REFUSED || st == PB_NO_GAPS) {
    if (st == PB_REFUSED)
      t->refused = true;
    else
      t->no_gaps = true;
    return;
  }
  if (st == PB_BUSY)
    t->busy++;
  uint16_t enext = plink_get16(a + PBULK_A_ENEXT), pnum = plink_get16(a + PBULK_A_PNUM),
           psess = plink_get16(a + PBULK_A_PSESS);
  int have = diff(enext, t->num);
  if (have > diff(t->unsent, t->num)) {  // it wants what we never sent, so it heard an earlier session of ours
    t->session = new_session(t->session);
    t->sessions++;
    t->unsent = t->num;
  } else if (have > 0) {
    drop(t, enext);
  }
  t->want = a[PBULK_A_WANT];
  if (psess != t->psess) {  // the panel restarted or we hadn't heard it, start from this answer's first message
    t->psess = psess;
    t->pnext = pnum;
  }
  follow(pnum, a[PBULK_N], &t->pnext, &t->ahead, &t->resyncs);
  uint8_t mine[PBULK_ANSWER_MAX];
  int k = skip(a + PBULK_A_PAYLOAD, a[PBULK_N], pnum, &t->pnext, mine);
  if (k > 0) {
    t->got++;
    pbulk_parse(mine, k, f, u);
  }
}
