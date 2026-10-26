#include "picture_snap.h"
#include <algorithm>
#include <cstring>
#include "picture_jpeg.h"
#include "plink.h"

namespace flexit::panel::picture {

// Below 128 that many + 1 bytes as they are, else that many - 126 of the next byte
void unpack_row(const uint8_t *in, uint8_t *out, size_t n) {
  size_t o = 0;
  while (o < n) {
    unsigned h = *in++;
    if (h < 128) {
      size_t k = std::min<size_t>(h + 1, n - o);
      memcpy(out + o, in, k);
      in += h + 1;
      o += k;
    } else {
      size_t k = std::min<size_t>(h - 126, n - o);
      memset(out + o, *in++, k);
      o += k;
    }
  }
}

// Padded to whole MCUs like Search::attempt, so it's placed as the panel's JPEGs are
void Snap::start(const Pic *p, int quality, std::vector<uint8_t> &out) {
  pic = p;
  int w = PIC_MAX_W, h = PIC_MAX_H;
  if (p) {
    w = (p->w + 15) & ~15;
    h = p->h % 8 ? (p->h + 15) & ~15 : p->h;
    ox = place(p->w, w);
    oy = place(p->h, h);
    rows.assign((size_t) 8 * ((p->w + 1) / 2), 0);
  }
  band = -1;
  pjpeg_begin(&enc, w, h, quality, out.data(), (uint32_t) out.size());
}

bool Snap::step() { return pjpeg_mcu(&enc, pixel, this); }

uint32_t Snap::finish() {
  std::vector<uint8_t>().swap(rows);
  return pjpeg_end(&enc);
}

uint32_t Snap::pixel(void *ctx, int x, int y) {
  auto *s = (Snap *) ctx;
  const Pic *p = s->pic;
  if (!p)
    return 0;
  const size_t row_n = (p->w + 1) / 2;
  if (y >> 3 != s->band) {  // the 8 rows of this MCU row
    s->band = y >> 3;
    for (int k = 0; k < 8; k++) {
      int py = s->band * 8 + k - s->oy;
      if (py >= 0 && py < p->h)
        unpack_row(p->data.data() + p->rows[py], &s->rows[k * row_n], row_n);
    }
  }
  x -= s->ox;
  int py = y - s->oy;
  if (x < 0 || py < 0 || x >= p->w || py >= p->h)
    return 0;
  uint8_t b = s->rows[(y & 7) * row_n + x / 2];
  return p->pal[x & 1 ? b & 15 : b >> 4];
}

}  // namespace flexit::panel::picture
