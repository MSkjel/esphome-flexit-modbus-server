#include "quant.h"
#include <algorithm>
#include <utility>

namespace flexit::panel {

// Keeps splitting the box with the biggest spread times pixel count at its median
int median_cut(const std::vector<uint32_t> &hist, int colours, uint32_t *pal, std::vector<uint16_t> &bins) {
  bins.clear();
  for (int i = 0; i < BINS; i++)
    if (hist[i])
      bins.push_back((uint16_t) i);
  std::vector<std::pair<size_t, size_t>> boxes{{0, bins.size()}};
  auto ch = [](int bin, int c) { return bin >> (8 - 4 * c) & 15; };
  while ((int) boxes.size() < colours) {
    int best = -1, best_c = 0;
    uint64_t best_score = 0;
    for (size_t i = 0; i < boxes.size(); i++) {
      if (boxes[i].second - boxes[i].first < 2)
        continue;
      int lo[3] = {15, 15, 15}, hi[3] = {0, 0, 0};
      uint64_t n = 0;
      for (size_t k = boxes[i].first; k < boxes[i].second; k++) {
        for (int c = 0; c < 3; c++) {
          lo[c] = std::min(lo[c], ch(bins[k], c));
          hi[c] = std::max(hi[c], ch(bins[k], c));
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
    if (best < 0)
      break;
    size_t a = boxes[best].first, b = boxes[best].second;
    std::sort(bins.begin() + a, bins.begin() + b,
              [&](uint16_t p, uint16_t q) { return ch(p, best_c) < ch(q, best_c); });
    uint64_t n = 0, acc = 0;
    for (size_t k = a; k < b; k++)
      n += hist[bins[k]];
    size_t mid = a;
    while (mid < b - 1 && acc + hist[bins[mid]] <= n / 2)
      acc += hist[bins[mid++]];
    if (mid == a)
      mid = a + 1;
    boxes[best] = {a, mid};
    boxes.push_back({mid, b});
  }
  for (size_t i = 0; i < boxes.size(); i++) {
    uint64_t r = 0, g = 0, b = 0, n = 0;
    for (size_t k = boxes[i].first; k < boxes[i].second; k++) {
      uint32_t c = bin_rgb(bins[k]), cnt = hist[bins[k]];
      r += (c >> 16) * cnt;
      g += (c >> 8 & 255) * cnt;
      b += (c & 255) * cnt;
      n += cnt;
    }
    if (n)
      pal[i] = (uint32_t) (r / n) << 16 | (uint32_t) (g / n) << 8 | (uint32_t) (b / n);
  }
  return (int) boxes.size();
}

void fill_lut(std::vector<uint8_t> &lut, const std::vector<uint32_t> &hist, const uint32_t *pal, int n) {
  for (int i = 0; i < BINS; i++)
    lut[i] = hist[i] ? (uint8_t) nearest(bin_rgb(i), pal, n) : 0;
}

// Both palettes snapped to bin centres. The panel's colours are its pixels' means and would lose to colours made
// from the centres.
bool nearly_as_good(const uint32_t *panel_pal, const uint32_t *new_pal, const std::vector<uint32_t> &hist,
                    const std::vector<uint16_t> &bins) {
  uint64_t total = 0;
  for (uint16_t bn : bins)
    total += hist[bn];
  auto err = [&](const uint32_t *p) {
    uint64_t e = 0;
    for (uint16_t bn : bins) {
      uint32_t c = bin_rgb(bn);
      e += (uint64_t) colour_dist(c, bin_rgb(bin(p[nearest(c, p)]))) * hist[bn];
    }
    return e;
  };
  uint64_t e_new = err(new_pal);
  return total && err(panel_pal) <= e_new + e_new / 4 + total * 30;
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
