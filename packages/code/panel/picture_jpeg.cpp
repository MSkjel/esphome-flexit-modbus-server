#include "picture_jpeg.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include "plink.h"

namespace flexit::panel::picture {

// Exactly in the middle both edges can be inside blocks, and those sharp edges cost 3-7% more bytes
int place(int n, int p) {
  int at = 0;
  for (int o = 1; o <= p - n; o++)
    if ((o % 8 == 0 || (o + n) % 8 == 0) && abs(2 * o - (p - n)) < abs(2 * at - (p - n)))
      at = o;
  return at;
}

void Search::start(int w, int h, int top_quality, int last, uint8_t *buf) {
  fw = w;
  fh = h;
  top = top_quality;
  out = buf;
  best = 0;
  int q = std::min(top, last ? last : FIRST_QUALITY);
  sizes.clear();
  encodes = 0;
  attempt(q, {q + 1, last ? 0 : q - SLOPE_GAP});
}

// The JPEG at quality q into out, its size at the qualities in also counted along. Padded to whole MCUs
// around the picture, to a height in 16s if at all, so the panel's 8-line rows centre it.
void Search::attempt(int q, std::initializer_list<int> also) {
  int w = (fw + 15) & ~15, h = fh % 8 ? (fh + 15) & ~15 : fh;
  ox = place(fw, w);
  oy = place(fh, h);
  pjpeg_begin(&enc, w, h, q, out, PIC_POOL);
  encodes++;
  for (int a : also)
    if (a >= 1 && a <= top && a != q)
      pjpeg_also(&enc, a);
  lines.resize((size_t) w * 8 * 3);
  line = 0;
}

bool Search::step(uint32_t (*pixel)(int x, int y)) {
  if (line < 8) {  // the next line for this row of MCUs
    int y = enc.row * 8 + line;
    uint8_t *l = &lines[(size_t) line * enc.w * 3];
    for (int x = 0; x < enc.w; x++, l += 3) {
      uint32_t c = pixel(x, y);
      l[0] = (uint8_t) (c >> 16);
      l[1] = (uint8_t) (c >> 8);
      l[2] = (uint8_t) c;
    }
    line++;
    return true;
  }
  bool more = pjpeg_mcu(
      &enc,
      [](void *ctx, int x, int y) {
        const Search &s = *(const Search *) ctx;
        const uint8_t *l = &s.lines[((size_t) (y % 8) * s.enc.w + x) * 3];
        return (uint32_t) l[0] << 16 | (uint32_t) l[1] << 8 | l[2];
      },
      this);
  if (!enc.col)
    line = 0;  // on to the next row
  return more;
}

// The best quality that fits so far (0 = none) and the lowest above it that doesn't (past the highest = none)
void Search::bounds(int &lo, int &hi) const {
  lo = 0;
  hi = top + 1;
  for (auto &s : sizes)
    if (s.second <= PIC_POOL)
      lo = std::max(lo, s.first);
  for (auto &s : sizes)
    if (s.second > PIC_POOL && s.first > lo)
      hi = std::min(hi, s.first);
}

// The next quality to try, the one whose JPEG should just fit. Its size less the headers goes about as libjpeg's
// quantizer scale to a power, found from lo's and hi's sizes, or from the nearest try at least 3 off the one there is.
int Search::guess(int lo, int hi) const {
  auto x = [](int q) { return logf(std::max(1.0f, q < 50 ? 5000.0f / q : 200.0f - 2 * q)); };
  auto y = [](uint32_t n) { return logf(std::max(1.0f, (float) n - JPEG_HEADERS)); };
  const std::pair<int, uint32_t> *a = nullptr, *b = nullptr;
  for (auto &s : sizes)
    if (s.first == (lo ? lo : hi))
      a = &s;
    else if (s.first == hi)
      b = &s;
  if (!lo || hi > top) {
    b = nullptr;
    for (auto &s : sizes)
      if (abs(s.first - a->first) >= 3 && (!b || abs(s.first - a->first) < abs(b->first - a->first)))
        b = &s;
  }
  float slope = -0.65f;
  if (b && x(b->first) != x(a->first))
    slope = std::min(-0.2f, std::max(-1.5f, (y(b->second) - y(a->second)) / (x(b->first) - x(a->first))));
  float scale = expf(x(a->first) + (logf(PIC_POOL - JPEG_HEADERS) - y(a->second)) / slope);
  int q = (int) (scale > 100 ? 5000 / scale : 100 - scale / 2);
  return std::min(hi - 1, std::max(std::max(lo, 1), q));
}

// Another try until the best that fits is right below one that doesn't, and its bytes are in out
bool Search::next() {
  pjpeg_end(&enc);
  for (int i = 0; i < enc.n; i++)
    sizes.push_back({enc.o[i].quality, enc.o[i].len});
  int lo, hi, q = enc.o[0].quality;
  bounds(lo, hi);
  if (lo + 1 < hi) {
    q = guess(lo, hi);
    attempt(q, {q + 1 < hi ? q + 1 : 0});
    return true;
  }
  if (lo && q != lo) {  // lo is the best, but out has another try's bytes
    attempt(lo);
    return true;
  }
  best = lo;
  return false;
}

double Search::err() const { return pjpeg_mse(&enc) * enc.w * enc.h / ((double) fw * fh); }

void Search::drop() {
  std::vector<uint8_t>().swap(lines);
  std::vector<std::pair<int, uint32_t>>().swap(sizes);
}

}  // namespace flexit::panel::picture
