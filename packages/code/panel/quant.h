// 16 colours for a picture or an animation from its 4-bit-per-channel histogram, and a 16-colour version's error.
// Plain C++ without ESPHome.
#pragma once
#include <cstdint>
#include <vector>
#include "rgb.h"

namespace flexit::panel {

inline constexpr int BINS = 4096;  // colours at 4 bits per channel, see bin()
// RAM to count colours: the counts, the bins and the colour table
inline constexpr uint32_t COUNT_RAM = BINS * 4 + BINS * 2 + BINS;

// Median cut to `colours` colours, max 16. bins gets the colours that occur. Returns how many it made.
int median_cut(const std::vector<uint32_t> &hist, int colours, uint32_t *pal, std::vector<uint16_t> &bins);

// The colour table: each bin that has pixels to the nearest of pal's n colours
void fill_lut(std::vector<uint8_t> &lut, const std::vector<uint32_t> &hist, const uint32_t *pal, int n);

// Whether the panel's palette is nearly as good as the new one for bins. If so it stays and only the changed rows go.
bool nearly_as_good(const uint32_t *panel_pal, const uint32_t *new_pal, const std::vector<uint32_t> &hist,
                    const std::vector<uint16_t> &bins);

// For RGB a and b, the sum of their Y's, Cb's and Cr's products. ycc(d, d) is the squared difference d makes in
// Y, Cb and Cr, the units of pjpeg's error.
double ycc(const double *a, const double *b);

// A 16-colour version's pixels summed as they're mapped, all that means() and err() need
struct Sums {
  uint32_t sum[16][4] = {};  // per colour: R, G and B summed, and the pixel count
  uint64_t sq[6] = {};       // R*R, G*G, B*B, R*G, R*B and G*B summed over all pixels
  // Each colour to the mean of its pixels, which can only lower the error
  void means(uint32_t *pal) const;
  // Squared error over all pixels with these colours, in ycc()'s units
  double err(const uint32_t *pal) const;
};

}  // namespace flexit::panel
