// The best JPEG quality that fits the panel's PIC_POOL, found in a few tries. Plain C++ without ESPHome.
#pragma once
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>
#include "pjpeg.h"

namespace flexit::panel::picture {

inline constexpr int TOP_QUALITY = 95;         // the highest quality tried, unless the picture has its own
inline constexpr int FIRST_QUALITY = 50;       // the first try for a picture the panel doesn't have as a JPEG
inline constexpr int SLOPE_GAP = 20;           // counted along with the first try this far below, for the slope
inline constexpr uint32_t JPEG_HEADERS = 600;  // about the bytes of a JPEG that aren't its scan

// Tries go a source line or an MCU per step(), so the loop gets back to the bus in time
struct Search {
  std::vector<std::pair<int, uint32_t>> sizes;  // qualities tried and their sizes
  int encodes = 0;                              // JPEGs made for this picture, for the log
  pjpeg_t enc;                                  // the JPEG being tried, written into out
  int ox = 0, oy = 0;                           // where the picture is in it, see place()
  std::vector<uint8_t> lines;                   // the 8 lines the encoder is on, RGB
  int line = 0;                                 // how many of them are filled
  int fw = 0, fh = 0;                           // the picture's size
  int top = TOP_QUALITY;                        // the highest quality to try
  int best = 0;                                 // once it's done: the best quality that fits, 0 = none
  uint8_t *out = nullptr;                       // PIC_POOL bytes

  // The first try, w x h into buf. At `last` if the panel has it as a JPEG, likely right again for a picture
  // downloaded again. Else FIRST_QUALITY, with SLOPE_GAP below counted along.
  void start(int w, int h, int top_quality, int last, uint8_t *buf);
  // A line or an MCU of the try, false once it's done. pixel(x, y) in the JPEG's coordinates, 0xRRGGBB.
  bool step(uint32_t (*pixel)(int x, int y));
  // After a try: true if another one started, else out holds best's bytes.
  bool next();
  double err() const;  // mean squared error per pixel of the picture, Y + Cb + Cr
  void drop();         // frees what the tries took

  void attempt(int q, std::initializer_list<int> also = {});
  void bounds(int &lo, int &hi) const;
  int guess(int lo, int hi) const;
};

// Where n pixels go in the JPEG's p: nearest the middle with an edge on an 8-pixel block
int place(int n, int p);

}  // namespace flexit::panel::picture
