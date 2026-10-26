// Colours as 0xRRGGBB and at the panel's 4 bits per channel, for picture.h and met_scene.h. Plain C++ without
// ESPHome.
#pragma once
#include <cstdint>

namespace flexit::panel {

// A channel 0..255 rounded to 4 bits, n for n * 17: (v + 8) / 17
inline uint32_t q4(uint32_t v) { return (v + 8) * 241 >> 12; }

// 0xRRGGBB at 4 bits per channel, the bins of the colour counts, and back
inline uint16_t bin(uint32_t c) { return (uint16_t) (q4(c >> 16 & 255) << 8 | q4(c >> 8 & 255) << 4 | q4(c & 255)); }
inline uint32_t bin_rgb(int bin) {
  return (uint32_t) ((bin >> 8) * 17) << 16 | (uint32_t) ((bin >> 4 & 15) * 17) << 8 | (uint32_t) ((bin & 15) * 17);
}

// How far apart two colours look
inline uint32_t colour_dist(uint32_t a, uint32_t b) {
  int dr = (int) (a >> 16) - (int) (b >> 16), dg = (int) (a >> 8 & 255) - (int) (b >> 8 & 255),
      db = (int) (a & 255) - (int) (b & 255);
  return (uint32_t) (2 * dr * dr + 4 * dg * dg + 3 * db * db);
}

// The first of pal's n colours nearest c
inline int nearest(uint32_t c, const uint32_t *pal, int n = 16) {
  int best = 0;
  uint32_t bd = UINT32_MAX;
  for (int i = 0; i < n; i++) {
    uint32_t d = colour_dist(c, pal[i]);
    if (d < bd) {
      bd = d;
      best = i;
    }
  }
  return best;
}

}  // namespace flexit::panel
