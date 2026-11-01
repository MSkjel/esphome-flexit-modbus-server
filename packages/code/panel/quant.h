// 16 colours for a picture or an animation from its 4-bit-per-channel histogram, and a 16-colour version's error.
// Plain C++ without ESPHome.
#pragma once
#include <cstdint>
#include <utility>
#include <vector>
#include "rgb.h"

namespace flexit::panel {

inline constexpr int BINS = 4096;  // colours at 4 bits per channel, see bin()
// RAM to count colours: the counts, the bins and the colour table
inline constexpr uint32_t COUNT_RAM = BINS * 4 + BINS * 2 + BINS;

// Median cut to `colours` colours, max 16, a split at a time so the loop can get back to the bus between them.
// Keeps splitting the box with the biggest spread times pixel count at its median.
struct Cut {
  std::vector<uint16_t> bins;                        // the colours that occur, sorted box by box
  std::vector<std::pair<uint16_t, uint16_t>> boxes;  // each box's first bin and the one past its last
  int colours = 16;
  bool more = true;  // a box may still split
  void start(const std::vector<uint32_t> &hist, int colours);
  bool done() const { return !more || (int) boxes.size() >= colours; }
  void split(const std::vector<uint32_t> &hist);                         // one more box
  void palette(const std::vector<uint32_t> &hist, uint32_t *pal) const;  // each box's mean colour
  void drop();
};

// All of it at once. bins gets the colours that occur. Returns how many colours it made.
int median_cut(const std::vector<uint32_t> &hist, int colours, uint32_t *pal, std::vector<uint16_t> &bins);

// Whether the panel's palette is nearly as good as the new one for the bins, summed a few bins at a time. If so
// it stays and only the changed rows go. Both palettes snapped to bin centres: the panel's colours are its pixels'
// means and would lose to colours made from the centres.
struct Judge {
  uint64_t total = 0, e_new = 0, e_panel = 0;
  size_t at = 0;
  // Adds the next n bins, false until all are in
  bool step(const uint32_t *panel_pal, const uint32_t *new_pal, const std::vector<uint32_t> &hist,
            const std::vector<uint16_t> &bins, size_t n);
  bool nearly_as_good() const { return total && e_panel <= e_new + e_new / 4 + total * 30; }
};

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
