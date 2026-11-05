// The maps the rovers' tracks are drawn on, from scripts/rover_maps.py. Each one's only linked in when rover.yaml
// names it, so a build with one rover has only its map.
#pragma once
#include <cstdint>

namespace flexit::panel::space::rover {

struct Mosaic {
  uint32_t rgb[16];           // its colours, 0xRRGGBB
  uint8_t px[320 * 240 / 2];  // two pixels a byte, the left one in the high nibble
};
extern const Mosaic perseverance_mosaic, curiosity_mosaic;

}  // namespace flexit::panel::space::rover
