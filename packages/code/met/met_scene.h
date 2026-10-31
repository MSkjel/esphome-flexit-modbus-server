// The weather as a 320x240 picture in at most 16 flat colours. build() makes the scene, met_raster.h draws it.
// Plain C++ without ESPHome, so sim/met_render.cpp in flexit-ci600-firmware can render it on a PC.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <type_traits>
#include "../panel/rgb.h"
#include "met_forecast.h"
#include "met_sky_math.h"

namespace flexit::panel::met {

inline constexpr int W = 320, H = 240;
inline constexpr int HORIZON = 156;  // just above the panel's clock band, the bottom 74 rows
inline constexpr int CABIN_X = 178;  // in the band's gap between the clock and the temperatures
// The panel's clock and date go over the bottom left of the picture, its temperatures over the bottom right, with
// the texts where most have them. What a show parks on the land goes in the gap between them, or under the
// temperatures right of the date.
inline constexpr int GAP_L = 126, GAP_R = 228, DATE_R = 154, TEMPS_BOT = 222;
inline constexpr int OV_H = 64;                            // rows of the forecast overlay, at the top
inline constexpr int16_t BAND[5] = {0, 52, 92, 118, 138};  // first row of each sky band
inline constexpr int RB_RX = 100, RB_RY = 96;              // the rainbow's outer radii
inline constexpr int LH_X = 268;                           // the lighthouse, on the coast's point
inline constexpr int PENNANT_MAX = 36;                     // the pennant's length in a strong wind
inline constexpr int PENNANT_LEFT = PENNANT_MAX + 1;       // how far from the pole it reaches blown left,
inline constexpr int PENNANT_RIGHT = PENNANT_MAX + 3;      // and right
inline constexpr uint8_t NONE = 255;                       // a row in the per-column arrays: none in this column
// Radii of the sun's disc and its rings, and of the moon's disc and its glow
inline constexpr int SUN_R = 13, RING1_R = 21, RING2_R = 30;
inline constexpr int MOON_R = 9, MOON_GLOW_R = 15;

// What the picture shows besides the weather. The names are met.yaml's Panel Weather Scene and Aurora options.
enum Ground : uint8_t { G_HILLS, G_COAST, G_MOUNTAINS, G_CITY };
enum Aurora : uint8_t { A_AUTO, A_OFF, A_ALWAYS };
inline constexpr const char *GROUNDS[] = {"hills", "coast", "mountains", "city"};
inline constexpr const char *AURORAS[] = {"auto", "off", "always"};
static_assert(std::size(GROUNDS) == G_CITY + 1 && std::size(AURORAS) == A_ALWAYS + 1, "a name for each");
// The rows each landscape's middle and near hills wave about
struct Baseline {
  int16_t mid, near;
};
inline constexpr Baseline BASELINES[] = {{174, 204}, {184, 206}, {186, 206}, {178, 204}};  // by Ground
// The city's middle ground, flat. The buildings stand on it and the near windows end 4 rows above it.
// Thick fog lies at this row in every landscape.
inline constexpr int CITY_GROUND_Y = BASELINES[G_CITY].mid;
// What the weather animations draw in the picture's place, see met_live.h
enum : uint16_t {
  L_BIRDS = 1 << 0,
  L_BATS = 1 << 1,
  L_SMOKE = 1 << 2,  // the chimney's puffs or hearts
  L_FLAG = 1 << 3,   // the flag or the pennant
  L_PREC = 1 << 4,   // rain and snow
  L_BOLT = 1 << 5,
  L_CIRRUS = 1 << 6,  // the high clouds
  L_CLOUDS = 1 << 7,  // the middle and low ones
  L_XMAS = 1 << 8,    // the spruce's lights and star
  L_FIREWORKS = 1 << 9,
  L_BONFIRE = 1 << 10,  // its flames, sparks and smoke, the logs stay
  L_ANTENNA = 1 << 11,  // its red light
  L_BOAT = 1 << 12,
};
struct Options {
  Ground ground = G_HILLS;
  Aurora aurora = A_AUTO;  // auto = some clear nights north of 55 degrees
  int heater_pct = 0;      // the unit's heater %, for the chimney smoke
  bool fireplace = false;  // more smoke
  uint32_t frame = 0;      // counts the redraws, for the lights that blink and the fireworks
};

// Local time minus UTC at t, in seconds
using Offset = int (*)(uint32_t t);

// ---------------------------------------------------------------- colours
// All colours are 4 bits per channel, so the panel's 16-colour palette holds them exactly.

// RGB rounded to 4 bits per channel, multiples of 17, which bin() keeps as they are
inline uint32_t rgb4(int r, int g, int b) {
  auto c = [](int v) { return (uint32_t) std::clamp(v, 0, 255); };
  return bin_rgb(bin(c(r) << 16 | c(g) << 8 | c(b)));
}
inline float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
inline uint32_t mix(uint32_t a, uint32_t b, float t) {
  t = clamp01(t);
  auto ch = [&](int s) { return (int) ((a >> s & 255) + ((int) (b >> s & 255) - (int) (a >> s & 255)) * t + 0.5f); };
  return rgb4(ch(16), ch(8), ch(0));
}

// The scene's colours, a slot each in Scene::rgb. The ones in threes are for each cloud layer (high, middle, low),
// hill layer (far, middle, near) or rainbow band (outermost first).
enum : uint8_t {
  C_SKY,               // 5 bands from BAND, top to the horizon
  C_GLOW = C_SKY + 5,  // around a low sun, and the wide faint ring around that
  C_GLOW_OUT,
  C_SUN,
  C_RING1,
  C_RING2,
  C_MOON,
  C_MOON_GLOW,
  C_STAR,  // catalogue stars
  C_DIM,   // background stars
  C_AUR,   // the aurora's main colour, its top and its light lower edge
  C_AUR_TOP,
  C_AUR_EDGE,
  C_LIT,                // clouds
  C_SHADE = C_LIT + 3,  // clouds, below their shade row
  C_RB = C_SHADE + 3,
  C_RAIN = C_RB + 3,
  C_SNOW,
  C_BOLT,
  C_FOG,
  C_HILL,
  C_TREE = C_HILL + 3,  // spruces on the middle and near hills, the hill's colour but dark green on snow
  C_DARK = C_TREE + 1,  // the near ones', for the dark small things too
  C_SNOWCAP,
  C_BIRCH,
  C_SEA,
  C_WAVE,
  C_BLD,
  C_BLD_BACK,
  C_BLD_EDGE,
  C_RIDGE,
  C_LH,
  C_LH_BAND,
  C_WINDOW,
  C_CABIN,
  C_WHITE,  // for small things and the cabin's trim, borrowed from a lit cloud or the sun
  C_TEXT,
  C_OUTLINE,
  COLOURS
};
static_assert(COLOURS <= 64, "a bit each in drawn() and KEY_COLOURS");

// Small things that would vanish if their colour went to their surroundings', besides the sprites
inline constexpr uint64_t KEY_COLOURS = 1ull << C_STAR | 1ull << C_DIM | 7ull << C_RB | 1ull << C_BOLT |
                                        1ull << C_BIRCH | 1ull << C_WINDOW | 1ull << C_TEXT | 1ull << C_OUTLINE;

// ---------------------------------------------------------------- the scene

struct Ellipse {
  int16_t x, y, rx, ry;
};

struct Cloud {
  int16_t x, top, base, shade;  // x is the left edge in the wrapping field, rows below shade are darker
  Ellipse e[5];                 // x relative to the cloud's x
  uint8_t n;
};

// A one-colour bitmap. Layer 0 is drawn in the sky behind the ground, 1 in front of it, 2 on top of everything.
struct Sprite {
  int16_t x, y;
  uint8_t w, h;                  // at most 64 by 24
  uint16_t layer : 2, tag : 14;  // tag: the L_* it's left out for, or 0
  uint32_t c : 24, ix : 8;       // 0xRRGGBB, and its place in the palette
  uint64_t rows[24];             // leftmost pixel in the highest of w bits
};
inline constexpr int SPRITES = 32;
static_assert(L_BOAT < 1 << 14, "the L_* fit Sprite::tag");
static_assert(PENNANT_MAX + 4 <= 64, "the pennant's sprite fits its rows");

// The land's shape, which only depends on the place and the landscape, so it's kept from one scene to the next
struct Land {
  uint32_t seed;  // the place's, see ground()
  Ground ground;
  bool made;
  uint8_t hill[3][W];  // far, middle, near. Top row in each column, NONE for none
  uint8_t ridge[W];    // mountains, a higher and hazier ridge behind the peaks
  int8_t cap[W];       // mountains, the snow line's wave
};

struct Pt {
  int16_t x, y;
};

struct Tree {
  int16_t x, top, base;
  uint8_t layer, kind;  // kind 0 spruce, 1 birch, 2 bare birch
};

struct Bld {  // a city building
  int16_t x, w, top;
  uint8_t back, kind;  // back: in the hazy far row. kind 0 flat roof, 1 a box on it, 2 antenna, 3 spire
};

struct Light {  // one of the Christmas spruce's
  int16_t x, y;
  uint8_t c;  // 0 red, 1 warm, 2 green
};

// The moment, for the weather animations to pick theirs
struct At {
  bool dark, rain, snow, thunder;
  bool white;  // snow on the ground
  bool fair;   // sunny enough for birds
  bool xmas, newyear, easter_days, midsummer, halloween, valentine, may17, flag_up, fireplace;
  int8_t wdir;                                // which way the wind blows things, 1 to the right
  uint8_t season, hour, month, day, weekday;  // weekday 0 Sunday, season see season_of()
  float light;                                // 0 night .. 1 day
  float fog, cover, wind, gust, sun_el;       // gust 0 if MET didn't say
};

// Everything build() works out, so row() can draw any row on its own
struct Scene {
  uint32_t pal[16];       // the picture's colours
  uint32_t rgb[COLOURS];  // each colour slot's colour
  uint8_t ix[COLOURS];    // and its place in pal
  uint8_t folded;         // colours that didn't fit in 16, drawn in the nearest that did
  bool glow;              // sunrise or sunset light around the sun, low in the sky
  int16_t glow_x, glow_rx, glow_ry;
  bool sun, sun_rings, moon, moon_glow;
  int16_t sun_x, sun_y, moon_x, moon_y;
  float moon_phase;
  int nstars;  // faint background stars at made-up positions
  int16_t star_x[90], star_y[90];
  int nbright;  // catalogue stars, at their real positions
  int16_t bright_x[std::size(STARS)], bright_y[std::size(STARS)];
  uint8_t bright_big[std::size(STARS)];
  bool aurora;                           // a wide ribbon high up and a thin one lower
  uint8_t aur_top[2][W], aur_bot[2][W];  // NONE in a column without it
  int nclouds, drift[3];                 // drift is each layer's offset in the wrapping field
  Cloud cloud[22];
  uint8_t cloud_layer[22];
  bool rainbow;
  int16_t rb_x, rb_y;
  int nprec;
  int16_t prec_x[220], prec_y[220];
  uint8_t prec_kind[220];  // 0 rain streak, 1 snowflake, 2 big snowflake
  uint8_t prec_top[W];     // where it starts falling in each column
  int8_t slant;            // rain's x per 8 rows
  bool bolt;
  int16_t bolt_x[7], bolt_y[7];
  uint8_t fog_top[2][W];  // over the far and the middle hills, NONE without fog
  Ground ground;
  bool snow_trees;  // snow on the spruces' tiers
  int16_t snowline;
  int ntrees;
  Tree tree[18];
  bool sea;  // water from sea_top down to the middle hill
  int16_t sea_top;
  int nwaves;
  int16_t wave_x[70], wave_y[70], wave_w[70];
  int16_t shine_x;  // the sun's or moon's reflection on the water, -1 = none
  int nbld;         // city buildings, a hazy far row and a near row with windows
  Bld bld[28];
  uint32_t win_seed;  // picks the lit windows
  bool ridge;         // mountains, the land's ridge
  bool lighthouse;    // coast only, at LH_X
  int16_t lh_y;       // its base
  bool cabin;
  int16_t cabin_x, cabin_y;  // centre, and floor on the near hill
  bool window_lit;
  int16_t pole_x, pole_top, pole_base;  // flagpole, by the cabin or in the city's park
  int16_t home_x;                       // where the holidays happen, the cabin or the pole
  // where things are, for the weather animations, x -1 for none: the chimney's top, the bonfire's base, the
  // pumpkin, the boat, the antenna's top, the star on the Christmas spruce, the eggs and the spruce's lights
  Pt chimney, fire, pumpkin, boat, antenna, star;
  Pt egg[5];
  Light light[14];
  uint8_t neggs, nlights;
  uint8_t puffs;  // the chimney's, by the heat
  uint16_t live;  // L_*, what row() and palette() leave out for the weather animations
  uint16_t tag;   // L_*, what the sprites made now are left out for
  At at;
  int nsprites;
  bool overlay;  // the forecast drawn over the picture
  // The caller's, kept apart so the scene needs no big block
  Sprite *sprite;        // SPRITES of them
  uint8_t (*ov)[W / 4];  // OV_H rows, 2 bits per pixel. 0 shows the picture, 1 outline, 2 text. Or null
  Land *land;            // made again when the place or landscape changes
};

inline constexpr int FIELD = W + 200;  // clouds drift through a field wider than the screen

struct Rng {  // xorshift32
  uint32_t s;
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  int in(int a, int b) { return a + (int) (next() % (uint32_t) (b - a + 1)); }
  float f() { return (next() & 0xFFFFFF) / 16777216.0f; }
};
// An Rng for a small number like a frame's, spread over the bits and never 0
inline Rng seeded(uint32_t n) { return {n * 2654435761u | 1}; }

// The sky band of a row
inline int band_of(int y) {
  int b = 4;
  while (b > 0 && y < BAND[b])
    b--;
  return b;
}

// ---------------------------------------------------------------- where row() draws things, for the animations too

// Cloud i's left edge on the screen, its ellipses' x from there. The screen is the middle of the field.
inline int cloud_x(const Scene &s, int i) {
  return (s.cloud[i].x + s.drift[s.cloud_layer[i]]) % FIELD - (FIELD - W) / 2;
}

inline bool high_cloud(const Scene &s, int i) { return s.cloud_layer[i] == 0; }  // the rest are middle and low

// Half width of an ellipse dy rows from its centre, -1 if outside
inline int half(int dy, int rx, int ry) {
  if (dy < -ry || dy > ry || ry <= 0)
    return -1;
  float v = 1 - (float) (dy * dy) / (float) (ry * ry);
  return (int) (rx * sqrtf(v) + 0.5f);
}

// A spruce's half width d rows below its top, a narrow cone with ragged tiers, and whether snow lies on it there
inline int spruce_hw(int d) { return std::max(d / 4, d * 2 / 5 - d % 6 / 2); }
inline bool snow_tier(int d) { return d % 6 == 1 && d > 2; }

// The near hill's trees of a kind (0 spruce, 1 birch, 2 bare birch, -1 any) as indexes into s.tree
struct NearTrees {
  uint8_t i[std::extent_v<decltype(Scene::tree)>];
  int n = 0;
  const uint8_t *begin() const { return i; }
  const uint8_t *end() const { return i + n; }
};
inline NearTrees near_trees(const Scene &s, int kind) {
  NearTrees t;
  for (int k = 0; k < s.ntrees; k++)
    if (s.tree[k].layer == 2 && (kind < 0 || s.tree[k].kind == kind))
      t.i[t.n++] = (uint8_t) k;
  return t;
}

// Birch i's crown: its rows from the top, and the leafy one's half width
struct Crown {
  int rows, rx;
};
inline Crown crown_of(const Scene &s, int i) {
  int height = s.tree[i].base - s.tree[i].top;
  return {height * 6 / 10, std::max(3, height / 4)};
}

// The glow around a low sun on the horizon, the wide faint one or the smaller strong one
inline Ellipse glow_of(const Scene &s, bool strong) {
  return {s.glow_x, HORIZON, (int16_t) (strong ? s.glow_rx * 3 / 5 : s.glow_rx * 3 / 2),
          (int16_t) (strong ? s.glow_ry * 3 / 5 : s.glow_ry)};
}

// The near buildings' windows, 2 wide and 5 apart, in rows 2 tall and 5 apart from 3 below the roof, up to 4 rows
// above the ground.
// The row of them y is in on a building with its roof at top, -1 for none. The top floor's go up to the roof, 5
// rows tall, since C's division takes the 3 rows above them, -3..-1, to row 0 too.
inline int win_row(int y, int top) {
  int d = y - top - 3;
  return y >= top && y < CITY_GROUND_Y - 4 && d % 5 < 2 ? d / 5 : -1;
}
// The left x of window k on building b, -1 past its last
inline int win_x(const Bld &b, int k) { return k * 5 + 6 < b.w ? b.x + 3 + k * 5 : -1; }

// The top left of the cabin window's glass, 7 by 6 with a bar down its middle and one across
inline int window_x(const Scene &s) { return s.cabin_x - 9; }
inline int window_y(const Scene &s) { return s.cabin_y - 12; }

// The aurora's colour at row y of a ribbon from top to bot: its top 3 tenths, its light lower edge (a fifth, 2
// rows at least) and the main colour between
inline int aur_band(int y, int top, int bot) {
  int t = bot - top;
  return y - top < t * 3 / 10 ? C_AUR_TOP : bot - y <= std::max(2, t / 5) ? C_AUR_EDGE : C_AUR;
}

// The bolt's x at row y between its points i and i + 1
inline int bolt_at(const int16_t *x, const int16_t *y, int i, int row) {
  return x[i] + (x[i + 1] - x[i]) * (row - y[i]) / (y[i + 1] - y[i]);
}

inline int local_hour(uint32_t t, Offset offset) { return (int) ((t + (uint32_t) (offset(t) + 86400)) % 86400 / 3600); }

// The forecast hours the overlay shows, the next four on 3-hour marks of local time after t, as indexes into
// w.hour. Returns how many.
int marks(const Weather &w, uint32_t t, Offset offset, int *at);

// The light, weather and date the parts of the scene go by, and At for the animations. utc_offset is local time
// minus UTC in seconds.
struct Now : At {
  const Weather &w;
  const Options &o;
  double lat, lon;
  uint32_t t, local;  // UTC, and local time with a day added so it's never negative
  Pos sun, moon;
  float moon_phase, grey, glow;  // grey how overcast, glow see sky()
  int year;
  bool flag_day;

  Now(const Weather &weather, const Options &options, double la, double lo, uint32_t utc, int utc_offset);
};

// The whole scene at t, UTC, but its colours' places: palette() gives them, after s.live is set. Without w.ok
// there's no weather in it. s.sprite, s.ov and s.land are the caller's and stay, s.ov may be null without the
// forecast.
void build(Scene &s, const Weather &w, double lat, double lon, uint32_t t, bool with_forecast, Offset offset,
           const Options &o);

// A one-colour bitmap H rows high from string art: rows split by '|', 'x' a pixel, anything else none.
// The leftmost pixel is the highest of w bits.
template<int H> struct Bitmap {
  uint32_t row[H];
};
template<int H> constexpr Bitmap<H> bitmap(int w, const char *art) {
  Bitmap<H> b = {};
  for (int r = 0, c = 0; *art; art++, c++) {
    if (*art == '|') {
      r++;
      c = -1;
    } else if (*art == 'x') {
      b.row[r] |= 1u << (w - 1 - c);
    }
  }
  return b;
}

// The pumpkin's face, for the picture and its candle's animation
inline constexpr Bitmap<7> FACE = bitmap<7>(11, "..x.....x..|"
                                                ".xxx...xxx.|"
                                                "xxxxxxxxxxx|"
                                                "...........|"
                                                "x.x.x.x.x.x|"
                                                "xx.xx.xx.xx|"
                                                ".xxxxxxxxx.");

}  // namespace flexit::panel::met
