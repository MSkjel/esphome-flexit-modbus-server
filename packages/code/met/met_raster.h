// The scene drawn a row at a time, as indexes into its 16 colours. Plain C++ without ESPHome, see met_scene.h.
#pragma once
#include <cstdint>
#include "met_scene.h"

namespace flexit::panel::met {

// Draws row y of the scene into out, W indexes into pal
void row(const Scene &s, int y, uint8_t *out);

// The colour slots row() may draw in this scene, a bit each. More than it does only costs a count.
uint64_t drawn(const Scene &s);

// Gives each colour slot and sprite its place in the 16 colours. Should the scene have more, the one that's least
// missed goes to the colour nearest it until there are 16. Not the sprites' or other small things', unless there's
// nothing else. The colours that are in prev, the panel's palette, keep their places, so rows that didn't change
// stay the same. Sprites left out for the animations, see s.live, take none.
void palette(Scene &s, const uint32_t *prev);

// palette() a few rows at a time, so the loop gets back to the bus: start(), count() until it's true, then end().
// The scene mustn't change in between.
struct Palette {
  static constexpr int MAX = COLOURS + SPRITES;
  uint32_t c[MAX], used[MAX];
  bool key[MAX];
  int n = 0, y = H;  // colours, the next row to count
  void start(Scene &s);
  bool count(const Scene &s, int rows);  // true once every row is counted
  void end(Scene &s, const uint32_t *prev);
};

// Whether window k of row r on the city's building i is lit
bool win_lit(const Scene &s, int i, int r, int k);

// Calls fn(x, y, h, lit) for each window of the near row as row() draws them: top left, 2 wide, h tall
template<class F> void windows(const Scene &s, F fn) {
  for (int i = 0; i < s.nbld; i++) {
    const Bld &b = s.bld[i];
    for (int y = b.top; !b.back && y < H; y++) {
      int r = win_row(y, b.top), h = 0;
      if (r < 0 || win_row(y - 1, b.top) == r)  // from the top of each row of them only
        continue;
      while (win_row(y + h, b.top) == r)
        h++;
      for (int k = 0, x; (x = win_x(b, k)) >= 0 && x + 1 < W; k++)
        fn(x, y, h, win_lit(s, i, r, k));
    }
  }
}

}  // namespace flexit::panel::met
