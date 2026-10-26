// What the panel shows as a JPEG for Home Assistant's camera, see snapshot.h. A 16-colour picture is unpacked 8 rows
// at a time and encoded an MCU per step. Plain C++ without ESPHome.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "picture_plan.h"
#include "pjpeg.h"

namespace flexit::panel::picture {

// A row packed by plink_pack_row back into its n bytes
void unpack_row(const uint8_t *in, uint8_t *out, size_t n);

struct Snap {
  pjpeg_t enc;
  const Pic *pic = nullptr;  // 16 colours, has to stay as it is until it's done. nullptr = a black screen
  int ox = 0, oy = 0;        // where the picture is in the JPEG, as Search places it
  int band = -1;             // the MCU row whose 8 rows are in rows
  std::vector<uint8_t> rows;

  // p as a JPEG at quality into out, as big as out is
  void start(const Pic *p, int quality, std::vector<uint8_t> &out);
  bool step();        // an MCU, false when it's done
  uint32_t finish();  // the JPEG's length, over out's size if it didn't fit. Frees rows.

  static uint32_t pixel(void *ctx, int x, int y);
};

}  // namespace flexit::panel::picture
