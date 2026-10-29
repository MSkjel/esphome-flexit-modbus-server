// Link messages, see plink.h
// Copied into esphome-flexit-modbus-server by make -C sim esp_sync, edit it here.
#include "plink.h"
#include "pbulk.h"
#include "pdeflate.h"
#include <string.h>

uint32_t plink_crc32(uint32_t crc, const void *data, unsigned n) {
  static const uint32_t T[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
                                 0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                                 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};  // a nibble at a time
  const uint8_t *p = data;
  crc = ~crc;
  while (n--) {
    crc ^= *p++;
    crc = crc >> 4 ^ T[crc & 15];
    crc = crc >> 4 ^ T[crc & 15];
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
  out[ANIM_RECORD_DELAY] = delay;
  out[ANIM_RECORD_X] = (uint8_t) x0;
  out[ANIM_RECORD_Y] = (uint8_t) y0;
  out[ANIM_RECORD_W] = (uint8_t) (x1 - x0);
  out[ANIM_RECORD_H] = (uint8_t) (y1 - y0);
  if (y0 == y1)
    return ANIM_RECORD_HEAD;
  uint32_t size = (x1 - x0) * (y1 - y0),
           n = pdeflate(cur + y0 * rb + x0, x1 - x0, rb, size, out + ANIM_RECORD_HEAD, PDEFLATE_BOUND(size));
  return n ? ANIM_RECORD_HEAD + n : 0;
}

void plink_put_palette(uint8_t *b, const uint32_t *pal) {
  for (int i = 0; i < 16; i++, b += 3) {
    b[0] = (uint8_t) (pal[i] >> 16);
    b[1] = (uint8_t) (pal[i] >> 8);
    b[2] = (uint8_t) pal[i];
  }
}

// Puts pal after the head bytes in b unless it's NULL. Returns the message's length.
static int with_palette(uint8_t *b, int head, const uint32_t *pal) {
  if (!pal)
    return head;
  plink_put_palette(b + head, pal);
  return head + PIC_PAL_LEN;
}

int plink_pic_begin(uint8_t *b, uint8_t id, uint8_t format, uint16_t w, uint16_t h, uint32_t size,
                    const uint32_t *pal) {
  b[PIC_MSG_ID] = id;
  b[PIC_BEGIN_FORMAT] = format;
  plink_put16(b + PIC_BEGIN_W, w);
  plink_put16(b + PIC_BEGIN_H, h);
  plink_put32(b + PIC_BEGIN_SIZE, size);
  return with_palette(b, PIC_BEGIN_HEAD, pal);
}

int plink_pic_patch(uint8_t *b, uint8_t id, uint16_t y, uint16_t count, uint32_t size, const uint32_t *pal) {
  b[PIC_MSG_ID] = id;
  plink_put16(b + PIC_PATCH_Y, y);
  plink_put16(b + PIC_PATCH_COUNT, count);
  plink_put32(b + PIC_PATCH_SIZE, size);
  return with_palette(b, PIC_PATCH_HEAD, pal);
}

int plink_anim_begin(uint8_t *b, uint8_t id, uint8_t slot, uint16_t w, uint16_t h, uint8_t scale, uint8_t transp,
                     uint32_t size, const uint32_t *pal) {
  b[PIC_MSG_ID] = id;
  b[ANIM_BEGIN_SLOT] = slot;
  plink_put16(b + ANIM_BEGIN_W, w);
  plink_put16(b + ANIM_BEGIN_H, h);
  b[ANIM_BEGIN_SCALE] = scale;
  b[ANIM_BEGIN_TRANSP] = transp;
  plink_put32(b + ANIM_BEGIN_SIZE, size);
  return with_palette(b, ANIM_BEGIN_HEAD, pal);
}

int plink_kind(uint16_t key) {
  switch (key) {
    case K_ACTION:
    case K_CX60_REG:
    case K_SETTINGS:
    case K_PIC_END:
    case K_PIC_ACK:
    case K_PIC_CLEAR:
    case K_ANIM_CLEAR:
    case K_ANIM_GONE:
    case K_SHOT:
    case K_SHOT_DONE:
      return PLINK_EVENT;
    case K_NOTIFY:
    case K_PIC_BEGIN:
    case K_PIC_PATCH:
    case K_PIC_DATA:
    case K_ANIM_BEGIN:
      return PLINK_BYTES;
  }
  if (key >= K_ANIM_FRAMES && key <= K_ANIM_ROUNDS)
    return PLINK_BYTES;
  if (key >= K_SAVER_LINE && key < K_SAVER_LINE + SAVER_LINES)
    return PLINK_BYTES;
  if (key >= K_ITEM_DEF && key < K_ITEM_HELP + 0x40)  // ESP items, 0x40 keys a kind: all bytes but the values
    return K_ITEM_KIND(key) == K_ITEM_VALUE ? PLINK_VALUE : PLINK_BYTES;
  return PLINK_VALUE;
}

_Static_assert(N_ITEMS <= 0x40, "each kind of item has 0x40 keys");

unsigned plink_bulk_flags(uint16_t key) {
  return (plink_kind(key) == PLINK_VALUE ? PBULK_REPLACE : 0) | (key == K_HELLO ? PBULK_FIRST : 0);
}

uint32_t plink_number(const uint8_t *b, int n) {
  uint32_t v = 0;
  for (int i = 0; i < n && i < 4; i++)
    v |= (uint32_t) b[i] << (8 * i);
  return v;
}

void plink_init(plink_t *l) { memset(l, 0, sizeof *l); }

uint8_t plink_room(const plink_t *l) { return l->qn < PLINK_QUEUE - 1 ? (uint8_t) (PLINK_QUEUE - 1 - l->qn) : 0; }

static int number_len(uint32_t v) {
  int n = 0;
  for (; v; v >>= 8)
    n++;
  return n;
}

int plink_waiting(const plink_t *l) {
  int n = 0;
  for (int k = 0; k < l->qn; k++)
    n += 3 + number_len(l->value[k]);
  return n;
}

// Moves n waiting messages from index from to index to
static void shift(plink_t *l, int to, int from, int n) {
  memmove(&l->key[to], &l->key[from], (size_t) n * sizeof l->key[0]);
  memmove(&l->value[to], &l->value[from], (size_t) n * sizeof l->value[0]);
}

bool plink_send(plink_t *l, uint16_t key, uint32_t value) {
  int i = -1;  // a newer value or help request replaces one still waiting
  if (plink_kind(key) == PLINK_VALUE || key == K_ITEM_HELP)
    for (int k = 0; k < l->qn; k++)
      if (l->key[k] == key)
        i = k;
  if (i < 0 && l->qn >= PLINK_QUEUE - (key != K_HELLO))
    return false;
  if (i < 0)
    i = l->qn++;
  if (key == K_HELLO) {  // ahead of the rest, a restarted ESP ignores everything before a HELLO
    shift(l, 1, 0, i);
    i = 0;
  }
  l->key[i] = key;
  l->value[i] = value;
  return true;
}

void plink_forget(plink_t *l) {
  int k = 0;
  for (int i = 0; i < l->qn; i++) {
    uint16_t key = l->key[i];
    if (plink_kind(key) == PLINK_VALUE && !(key >= K_ITEM_VALUE && key < K_ITEM_VALUE + N_ITEMS)) {
      l->key[k] = key;
      l->value[k++] = l->value[i];
    }
  }
  l->qn = (uint8_t) k;
}

int plink_put_number(uint8_t *out, uint32_t v) {
  int n = number_len(v);
  for (int i = 0; i < n; i++)
    out[i] = (uint8_t) (v >> (8 * i));
  return n;
}

int plink_pack(plink_t *l, uint8_t *out, int max) {
  int n = 0, k = 0;
  for (; k < l->qn && n + 3 + number_len(l->value[k]) <= max; k++) {  // key, length, value
    plink_put16(out + n, l->key[k]);
    n += 2;
    out[n] = (uint8_t) plink_put_number(out + n + 1, l->value[k]);
    n += 1 + out[n];
  }
  shift(l, 0, k, l->qn - k);
  l->qn = (uint8_t) (l->qn - k);
  return n;
}
