#include "quant.h"
#include <algorithm>
#include <utility>

namespace flexit::panel {

static int channel(int bin, int c) { return bin >> (8 - 4 * c) & 15; }

void Cut::start(const std::vector<uint32_t> &hist, int n) {
  bins.clear();
  for (int i = 0; i < BINS; i++)
    if (hist[i])
      bins.push_back((uint16_t) i);
  boxes.assign(1, {0, (uint16_t) bins.size()});
  colours = n;
  more = true;
}

void Cut::split(const std::vector<uint32_t> &hist) {
  int best = -1, best_c = 0;
  uint64_t best_score = 0;
  for (size_t i = 0; i < boxes.size(); i++) {
    if (boxes[i].second - boxes[i].first < 2)
      continue;
    int lo[3] = {15, 15, 15}, hi[3] = {0, 0, 0};
    uint64_t n = 0;
    for (size_t k = boxes[i].first; k < boxes[i].second; k++) {
      for (int c = 0; c < 3; c++) {
        lo[c] = std::min(lo[c], channel(bins[k], c));
        hi[c] = std::max(hi[c], channel(bins[k], c));
      }
      n += hist[bins[k]];
    }
    int c = hi[0] - lo[0] >= hi[1] - lo[1] && hi[0] - lo[0] >= hi[2] - lo[2] ? 0
            : hi[1] - lo[1] >= hi[2] - lo[2]                                 ? 1
                                                                             : 2;
    uint64_t score = n * (uint64_t) (hi[c] - lo[c]);
    if (score > best_score) {
      best_score = score;
      best = (int) i;
      best_c = c;
    }
  }
  if (best < 0) {
    more = false;
    return;
  }
  size_t a = boxes[best].first, b = boxes[best].second;
  std::sort(bins.begin() + a, bins.begin() + b,
            [&](uint16_t p, uint16_t q) { return channel(p, best_c) < channel(q, best_c); });
  uint64_t n = 0, acc = 0;
  for (size_t k = a; k < b; k++)
    n += hist[bins[k]];
  size_t mid = a;
  while (mid < b - 1 && acc + hist[bins[mid]] <= n / 2)
    acc += hist[bins[mid++]];
  if (mid == a)
    mid = a + 1;
  boxes[best] = {(uint16_t) a, (uint16_t) mid};
  boxes.push_back({(uint16_t) mid, (uint16_t) b});
}

void Cut::palette(const std::vector<uint32_t> &hist, uint32_t *pal) const {
  for (size_t i = 0; i < boxes.size(); i++) {
    uint64_t r = 0, g = 0, b = 0, n = 0;
    for (size_t k = boxes[i].first; k < boxes[i].second; k++) {
      uint32_t c = bin_rgb(bins[k]);
      uint64_t cnt = hist[bins[k]];
      r += (c >> 16) * cnt;
      g += (c >> 8 & 255) * cnt;
      b += (c & 255) * cnt;
      n += cnt;
    }
    if (n)
      pal[i] = (uint32_t) (r / n) << 16 | (uint32_t) (g / n) << 8 | (uint32_t) (b / n);
  }
}

void Cut::drop() {
  std::vector<uint16_t>().swap(bins);
  std::vector<std::pair<uint16_t, uint16_t>>().swap(boxes);
}

int median_cut(const std::vector<uint32_t> &hist, int colours, uint32_t *pal, std::vector<uint16_t> &bins) {
  Cut cut;
  cut.start(hist, colours);
  while (!cut.done())
    cut.split(hist);
  cut.palette(hist, pal);
  bins = std::move(cut.bins);
  return (int) cut.boxes.size();
}

bool Judge::step(const uint32_t *panel_pal, const uint32_t *new_pal, const std::vector<uint32_t> &hist,
                 const std::vector<uint16_t> &bins, size_t n) {
  for (size_t end = std::min(bins.size(), at + n); at < end; at++) {
    uint32_t c = bin_rgb(bins[at]), cnt = hist[bins[at]];
    total += cnt;
    e_new += (uint64_t) colour_dist(c, bin_rgb(bin(new_pal[nearest(c, new_pal)]))) * cnt;
    e_panel += (uint64_t) colour_dist(c, bin_rgb(bin(panel_pal[nearest(c, panel_pal)]))) * cnt;
  }
  return at >= bins.size();
}

double ycc(const double *a, const double *b) {
  static const double M[3][3] = {{0.299, 0.587, 0.114}, {-0.168736, -0.331264, 0.5}, {0.5, -0.418688, -0.081312}};
  double s = 0;
  for (const auto &m : M)
    s += (m[0] * a[0] + m[1] * a[1] + m[2] * a[2]) * (m[0] * b[0] + m[1] * b[1] + m[2] * b[2]);
  return s;
}

void Sums::means(uint32_t *pal) const {
  for (int i = 0; i < 16; i++) {
    const uint32_t *s = sum[i];
    if (s[3])
      pal[i] = (s[0] + s[3] / 2) / s[3] << 16 | (s[1] + s[3] / 2) / s[3] << 8 | (s[2] + s[3] / 2) / s[3];
  }
}

// For pixels c of colour p: sum ycc(c - p, c - p) = sum ycc(c, c) - 2 ycc(sum c, p) + n ycc(p, p)
double Sums::err(const uint32_t *pal) const {
  const uint64_t *q = sq;
  double c[3][3] = {{(double) q[0], (double) q[3], (double) q[4]},
                    {(double) q[3], (double) q[1], (double) q[5]},
                    {(double) q[4], (double) q[5], (double) q[2]}},
         e = 0;
  for (int k = 0; k < 3; k++) {
    double unit[3] = {};
    unit[k] = 1;
    e += ycc(unit, c[k]);
  }
  for (int i = 0; i < 16; i++) {
    const uint32_t *s = sum[i];
    uint32_t p = pal[i];
    double total[3] = {(double) s[0], (double) s[1], (double) s[2]},
           rgb[3] = {(double) (p >> 16), (double) (p >> 8 & 255), (double) (p & 255)};
    e += s[3] * ycc(rgb, rgb) - 2 * ycc(total, rgb);
  }
  return e;
}

}  // namespace flexit::panel
