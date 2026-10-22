// Link messages, see plink.h
#include "plink.h"
#include "pdeflate.h"
#include <string.h>

uint32_t plink_crc32(uint32_t crc, const void *data, unsigned n) {
  const uint8_t *p = data;
  crc = ~crc;
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++)
      crc = crc >> 1 ^ (0xEDB88320u & -(crc & 1));
  }
  return ~crc;
}

unsigned plink_pack_row(const uint8_t *in, unsigned n, uint8_t *out) {
  unsigned i = 0, o = 0;
  while (i < n) {
    unsigned run = 1;
    while (i + run < n && run < 129 && in[i + run] == in[i])
      run++;
    if (run >= 3) {
      out[o++] = (uint8_t) (run + 126);
      out[o++] = in[i];
      i += run;
      continue;
    }
    unsigned start = i, len = 0;  // literal bytes up to the next run of 3, at most 128
    while (i < n && len < 128 && !(i + 2 < n && in[i] == in[i + 1] && in[i] == in[i + 2])) {
      i++;
      len++;
    }
    out[o++] = (uint8_t) (len - 1);
    memcpy(out + o, in + start, len);
    o += len;
  }
  return o;
}

unsigned plink_anim_record(const uint8_t *prev, const uint8_t *cur, unsigned w, unsigned h, uint8_t delay,
                           uint8_t *out) {
  unsigned rb = w / 2, x0 = rb, x1 = 0, y0 = h, y1 = 0;
  for (unsigned y = 0; y < h; y++)
    for (unsigned x = 0; x < rb; x++)
      if (!prev || prev[y * rb + x] != cur[y * rb + x]) {
        x0 = x < x0 ? x : x0;
        x1 = x + 1 > x1 ? x + 1 : x1;
        y0 = y < y0 ? y : y0;
        y1 = y + 1;
      }
  if (y0 >= y1)
    x0 = x1 = y0 = y1 = 0;  // nothing changed, empty rectangle
  out[0] = delay;
  out[1] = (uint8_t) x0;
  out[2] = (uint8_t) y0;
  out[3] = (uint8_t) (x1 - x0);
  out[4] = (uint8_t) (y1 - y0);
  if (y0 == y1)
    return 5;
  uint32_t size = (x1 - x0) * (y1 - y0),
           n = pdeflate(cur + y0 * rb + x0, x1 - x0, rb, size, out + 5, PDEFLATE_BOUND(size));
  return n ? 5 + n : 0;
}

int plink_kind(uint16_t key) {
  switch (key) {
    case K_ACTION:
    case K_PIC_END:
    case K_PIC_ACK:
    case K_PIC_CLEAR:
    case K_ANIM_CLEAR:
      return PLINK_EVENT;
    case K_NOTIFY:
    case K_PIC_BEGIN:
    case K_PIC_PATCH:
    case K_PIC_DATA:
    case K_ANIM_BEGIN:
    case K_ANIM_MOVE:
      return PLINK_BYTES;
  }
  bool def = key >= K_ITEM_DEF && key < K_ITEM_DEF + N_ITEMS, text = key >= K_ITEM_LABEL && key < K_ITEM_UNIT + N_ITEMS,
       word = key >= K_ITEM_ON && key < K_ITEM_OFF + N_ITEMS, val = key >= K_ITEM_TEXT && key < K_ITEM_TEXT + N_ITEMS;
  return def || text || word || val || key == K_ITEM_HELP ? PLINK_BYTES : PLINK_VALUE;
}

uint32_t plink_number(const uint8_t *b, int n) {
  uint32_t v = 0;
  for (int i = 0; i < n && i < 4; i++)
    v |= (uint32_t) b[i] << (8 * i);
  return v;
}

void plink_init(plink_t *l) { memset(l, 0, sizeof *l); }

uint8_t plink_room(const plink_t *l) { return l->qn < PLINK_QUEUE - 1 ? (uint8_t) (PLINK_QUEUE - 1 - l->qn) : 0; }

int plink_waiting(const plink_t *l) { return l->qn * PLINK_NUMBER_BYTES; }

bool plink_send(plink_t *l, uint16_t key, uint32_t value) {
  int i = -1;  // a newer value or help request replaces one still waiting
  if (plink_kind(key) == PLINK_VALUE || key == K_ITEM_HELP)
    for (int k = 0; k < l->qn; k++)
      if (l->q[k].key == key)
        i = k;
  if (i < 0 && l->qn >= PLINK_QUEUE - (key != K_HELLO)) {
    l->dropped++;
    return false;
  }
  if (i < 0)
    i = l->qn++;
  if (key == K_HELLO) {  // ahead of the rest, a restarted ESP ignores everything before a HELLO
    memmove(&l->q[1], &l->q[0], (size_t) i * sizeof l->q[0]);
    i = 0;
  }
  l->q[i].key = key;
  l->q[i].value = value;
  return true;
}

void plink_forget(plink_t *l) {
  int k = 0;
  for (int i = 0; i < l->qn; i++) {
    uint16_t key = l->q[i].key;
    if (plink_kind(key) == PLINK_VALUE && !(key >= K_ITEM_VALUE && key < K_ITEM_VALUE + N_ITEMS))
      l->q[k++] = l->q[i];
  }
  l->qn = (uint8_t) k;
}

int plink_pack(plink_t *l, uint8_t *out, int max) {
  int n = 0, k = 0;
  for (; k < l->qn && n + PLINK_NUMBER_BYTES <= max; k++) {  // key, length 4, value, low bytes first
    out[n++] = l->q[k].key & 0xFF;
    out[n++] = l->q[k].key >> 8;
    out[n++] = 4;
    for (int b = 0; b < 4; b++)
      out[n++] = (uint8_t) (l->q[k].value >> (8 * b));
  }
  memmove(&l->q[0], &l->q[k], (l->qn - k) * sizeof l->q[0]);
  l->qn = (uint8_t) (l->qn - k);
  l->sent += (uint32_t) k;
  return n;
}
