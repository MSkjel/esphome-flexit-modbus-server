// What goes to the panel for a converted picture: all of it, or the changed rows in pieces, packed by pzip against
// what the panel holds. Plain C++ without ESPHome.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "pdeflate.h"
#include "plink.h"

namespace flexit::panel::picture {

// A converted picture. pal and rows are only for 16 colours.
struct Pic {
  std::string name;
  uint8_t format = PIC_PALETTE;
  uint16_t w = 0, h = 0;
  int quality = 0;  // JPEG only
  uint32_t pal[16] = {};
  std::vector<uint8_t> data;
  std::vector<uint16_t> rows;  // offset of each row in data, plus the end
  double err = 0;              // mean squared error per pixel, Y + Cb + Cr
};

struct Job {  // one transfer, the whole picture or rows y..y+count-1, with or without the palette
  bool full, pal;
  uint16_t y, count;
};

// The rows of made that differ from shown, in pieces into jobs, or all of it if they don't fit. Both 16 colours at
// the same size. Returns the pieces' bytes.
uint32_t patches(const Pic &made, const Pic &shown, std::vector<Job> &jobs);

// The panel spliced in job j's rows of made, so shown does too and the next piece matches the panel's
void apply(Pic &shown, const Pic &made, const Job &j);

// A 16-colour piece's bytes, packed by pzip against what the panel holds
struct Zip {
  static constexpr unsigned MAX = 64;  // the most next() is asked for at a time
  pzip_t z;
  std::vector<uint16_t> table;  // while sending, if there's the RAM
  uint8_t buf[MAX + PZIP_MAX];
  unsigned n;    // bytes in buf, not sent yet
  uint16_t row;  // the row a patch's next byte is in, for pzip's like()
  const Pic *made, *shown;
  uint32_t at;  // where the piece starts in made.data
  // Job j's len bytes from at in made. pzip matches a whole picture against itself, a patch against shown too.
  void begin(const Pic &made, const Pic &shown, const Job &j, uint32_t at, uint32_t len);
  // Up to max more bytes of the pieces into b, fewer only at the end
  unsigned next(uint8_t *b, unsigned max);
};

}  // namespace flexit::panel::picture
