#include "gif.h"
#include <cstring>

namespace flexit::panel::picture {

void Gif::put(uint8_t b) {
  if (len < cap)
    out[len] = b;
  len++;
}

void Gif::put16(unsigned v) {
  put((uint8_t) v);
  put((uint8_t) (v >> 8));
}

void Gif::flush_sub() {
  if (!nsub)
    return;
  put((uint8_t) nsub);
  for (int i = 0; i < nsub; i++)
    put(sub[i]);
  nsub = 0;
}

// Low bits first, into sub-blocks of up to 255 bytes
void Gif::code(unsigned c) {
  acc |= (uint32_t) c << n;
  n += (int) bits;
  while (n >= 8) {
    sub[nsub++] = (uint8_t) acc;
    if (nsub == sizeof sub)
      flush_sub();
    acc >>= 8;
    n -= 8;
  }
}

void Gif::reset_table() {
  table.assign(TABLE, 0);
  next = FIRST;
  bits = 8;
}

void Gif::begin(int w, int h, const uint32_t *pal, int count, uint8_t *buf, uint32_t size) {
  out = buf;
  cap = size;
  len = whole = 0;
  frames = 0;
  for (const char *s = "GIF89a"; *s; s++)
    put((uint8_t) *s);
  put16(w);
  put16(h);
  put(0x80 | 6 << 4 | 6);  // a table of 2^(6+1) colours, 7 bits each
  put(0);
  put(0);
  for (int i = 0; i < COLOURS; i++) {
    uint32_t c = i < count ? pal[i] : 0;
    put((uint8_t) (c >> 16));
    put((uint8_t) (c >> 8));
    put((uint8_t) c);
  }
  static const uint8_t LOOP[] = {0x21, 0xFF, 11, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0', 3, 1, 0, 0, 0};
  for (uint8_t b : LOOP)
    put(b);
  whole = len;
}

void Gif::frame(int x0, int y0, int w, int h, unsigned delay_cs) {
  put(0x21);  // graphic control: left in place, no transparency
  put(0xF9);
  put(4);
  put(1 << 2);
  delay_at = len;
  put16(delay_cs);
  put(0);
  put(0);
  put(0x2C);
  put16(x0);
  put16(y0);
  put16(w);
  put16(h);
  put(0);
  put(7);  // the minimum code size, for 128 colours
  fx = x0;
  fy = y0;
  fw = w;
  fh = h;
  x = y = 0;
  prefix = -1;
  acc = 0;
  n = 0;
  nsub = 0;
  reset_table();
  code(CLEAR);
}

void Gif::delay(unsigned cs) {
  if (delay_at + 1 < cap) {
    out[delay_at] = (uint8_t) cs;
    out[delay_at + 1] = (uint8_t) (cs >> 8);
  }
}

bool Gif::step(Pixel px, void *ctx, int count) {
  for (; count && y < fh; count--) {
    unsigned c = px(ctx, fx + x, fy + y);
    if (++x == fw) {
      x = 0;
      y++;
    }
    if (prefix < 0) {
      prefix = (int) c;
      continue;
    }
    uint32_t key = (uint32_t) prefix << 8 | c;
    unsigned i = (key * 2654435761u) >> 20;
    for (;; i = (i + 1) % TABLE) {
      uint32_t e = table[i];
      if (!e) {
        code((unsigned) prefix);
        if (next < LIMIT) {
          if (next == 1u << bits)
            bits++;
          table[i] = key << 12 | next++;
        } else {
          code(CLEAR);
          reset_table();
        }
        prefix = (int) c;
        break;
      }
      if (e >> 12 == key) {
        prefix = (int) (e & 0xFFF);
        break;
      }
    }
  }
  if (y < fh)
    return true;
  code((unsigned) prefix);
  if (next == 1u << bits && bits < 12)
    bits++;  // a decoder adds an entry for the last code too, and widens its codes if that fills them
  code(STOP);
  if (n)
    sub[nsub++] = (uint8_t) acc;
  flush_sub();
  put(0);
  if (!full()) {
    whole = len;
    frames++;
  }
  return false;
}

uint32_t Gif::end() {
  std::vector<uint32_t>().swap(table);
  if (!frames)
    return cap + 1;
  len = whole;
  put(0x3B);
  return len;
}

}  // namespace flexit::panel::picture
