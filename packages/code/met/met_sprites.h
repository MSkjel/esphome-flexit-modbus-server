// The weather animations' sprites, drawn a frame at a time, see met_live.h. Plain C++ without ESPHome.
#pragma once
#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>
#include "met_scene.h"

namespace flexit::panel::met::live {

// One frame being drawn: 0xFFRRGGBB per pixel, 0 = nothing there
struct Canvas {
  int w, h;
  uint32_t *px;
  void put(int x, int y, uint32_t rgb) const {
    if (x >= 0 && y >= 0 && x < w && y < h)
      px[y * w + x] = 0xFF000000u | rgb;
  }
};
// A colour that glows, which dimmed() leaves as it is: a troll's eyes
inline constexpr uint32_t LAMP = 0xF2E27A;

// A sprite's frames, drawn when they're wanted
struct Frames {
  int w = 0, h = 0;
  std::vector<uint16_t> delays;  // ms each frame shows, one per frame
  std::function<void(int f, const Canvas &c)> draw;
  int count() const { return (int) delays.size(); }
  // ms frames from to to - 1 show, to the last without to
  int ms(int from = 0, int to = INT_MAX) const {
    int t = 0;
    for (int f = from; f < std::min(to, count()); f++)
      t += delays[f];
    return t;
  }
};
// A box on the screen, its top left and size
struct Box {
  int x, y, w, h;
};

// ---------------------------------------------------------------- factories

struct Fall {  // rain_tile and snow_tile: streak length or flake size, px a frame, colour, how many per px
  uint8_t size;
  float step;
  uint32_t col;
  float dens;
};
// The aurora's bend in px at x of a piece w wide in frame k, nothing at both ends
int aur_wig(int x, int w, int k);

// The box round the bolt through n points, 2 px wide: from 3 px left of the points and 2 px above them
Box bolt_box(const int16_t *x, const int16_t *y, int n);
// The scene's own bolt through its n points, as many as it has at most, in its bolt_box(): a second of nothing,
// so it's on the panel before it starts, then a flash, a flicker, a second flash and an afterglow. x0 and y0 get
// its top left in the scene
Frames bolt(const int16_t *x, const int16_t *y, int n, int *x0, int *y0);

// The scene's cirrus: its flat ellipses in colour col, at most 5 like its clouds, each stretching a little now and
// then. Its left edge is 3 px left of the leftmost ellipse's, its top row y0 in the scene
Frames cirrus_cloud(const Ellipse *e, int n, int y0, int h, uint32_t col);
// The scene's cumulus: its round lobes on a flat base, at most 5, lit above the shade row and shaded below it, a
// lobe swelling a little now and then. top, base and shade are rows in the scene, like the lobes' y
Frames cumulus_cloud(const Ellipse *e, int n, int top, int base, int shade, uint32_t lit, uint32_t dark);

// A still tile of the scene's rain: streaks size long, leaning slant px every 8 rows, falling step px a frame along
// that lean. 8 frames, the last leads into the first. One or two kinds. What goes out at one side comes in at the
// other, so tiles side by side meet.
Frames rain_tile(int w, int h, int slant, std::initializer_list<Fall> kinds, uint32_t seed);
// The scene's snow, single flakes and a few 2 x 2, swaying as they fall and blown slant px every 8 rows. 32 frames,
// the last leads into the first. One or two kinds. Side by side they meet, as rain_tile's do.
Frames snow_tile(int w, int h, int slant, std::initializer_list<Fall> kinds, uint32_t seed);
// A bank of fog in k top row, main and bottom rows, its top billowing slowly, its ends thinning out
Frames fog_bank(int w, int h, int seed, std::array<uint32_t, 3> k);
// Every light glints in its turn, then glows softer, as many as the scene has at most. x0 and y0 get the
// sprite's top left in the scene
Frames lights(const Light *l, int n, int *x0, int *y0);

// Makes the sprite called name, false if there's none
bool make_sprite(const std::string &name, Frames &out);

}  // namespace flexit::panel::met::live
