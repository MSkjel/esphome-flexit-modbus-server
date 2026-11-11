// The weather as a 320x240 picture in at most 16 flat colours. build() makes the scene, met_raster.h draws it.
// Plain C++ without ESPHome, so sim/met_render.cpp in flexit-ci600-firmware can render it on a PC.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <type_traits>
#include "../panel/marks.h"
#include "../panel/rgb.h"
#include "met_forecast.h"
#include "met_sky_math.h"

namespace flexit::panel::met {

inline constexpr int W = 320, H = 240;
inline constexpr int HORIZON = 156;  // just above the panel's clock band, the bottom 74 rows
inline constexpr int CABIN_X = 184;  // in the band's gap between the clock and the temperatures
// The cabin from the corner of its gable end: the gable on the left with a window and the loft's over it, the long
// wall on the right with the door and another window, on a foundation. Columns from its middle, cabin_x: the
// gable's left edge, the corner between the walls and the long wall's right edge, the ends of the eaves and of the
// ridge, and the chimney's two middle columns' left one. Rows above the foundation's, cabin_y: the eaves, the ridge
// and the chimney's top. Its pole is POLE_H tall.
inline constexpr int CABIN_L = -29, CABIN_CORNER = -7, CABIN_R = 28, EAVES_L = -30, EAVES_R = 30;
inline constexpr int RIDGE_L = -16, RIDGE_R = 19, CHIMNEY_X = 12;
inline constexpr int CABIN_EAVES = 21, CABIN_RIDGE = 32, CABIN_TOP = 38, POLE_H = 56;
// The panel's clock and date go over the bottom left of the picture, its temperatures over the bottom right, with
// the texts where most have them. What an act parks on the land goes in the gap between them, or under the
// temperatures right of the date.
inline constexpr int GAP_L = 126, GAP_R = 228, DATE_R = 154, TEMPS_BOT = 222;
inline constexpr int TEXTS_TOP = 168;  // the first row a text has, what's wholly above it is clear of them
inline constexpr int OV_H = 64;        // rows of the forecast overlay, at the top
inline constexpr int BANDS = 3;        // of sky, each shading into the next
inline constexpr int16_t BAND[BANDS] = {0, 56, 120};   // first row of each
inline constexpr int RB_RX = 100, RB_RY = 96;          // the rainbow's outer radii
inline constexpr int LH_X = 268;                       // the lighthouse, on the coast's point
inline constexpr int PENNANT_H = 9;                    // the pennant's rows at its hoist
inline constexpr int PENNANT_MAX = 36;                 // the pennant's length in a strong wind
inline constexpr int PENNANT_LEFT = PENNANT_MAX + 1;   // how far from the pole it reaches blown left,
inline constexpr int PENNANT_RIGHT = PENNANT_MAX + 3;  // and right
inline constexpr uint8_t NONE = 255;                   // a row in the per-column arrays: none in this column
// Radii of the sun's disc and its halo, and of the moon's disc and its glow
inline constexpr int SUN_R = 13, HALO_R = 24;
inline constexpr int MOON_R = 9, MOON_GLOW_R = 15;
// Rows the sun's halo, the moon's glow and the glow round a low sun shade out over at their rims
inline constexpr int HALO_SOFT = 14, MOON_SOFT = 8, GLOW_SOFT = 24;
// The lamps' and windows' warm light, the same so they share one of the 16 colours, and the animations' lit windows
// the same again. rgb4(255, 210, 122)
inline constexpr uint32_t WARM_LIGHT = 0xFFCC77;

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
inline constexpr Baseline BASELINES[] = {{174, 204}, {184, 213}, {186, 206}, {178, 204}};  // by Ground
// The city's middle ground, flat. The buildings stand on it and the near windows end 4 rows above it.
// Thick fog lies at this row in every landscape.
inline constexpr int CITY_GROUND_Y = BASELINES[G_CITY].mid;
// The city's road along the buildings' foot, rows from the far pavement to the near one: the far lane, its dashed
// middle, the near lane, the kerb and the red bike lane. What stands behind it stands on the far pavement, and
// nothing in front reaches up into it, so what drives on it never passes through anything.
inline constexpr int ROAD_FAR = CITY_GROUND_Y, ROAD_MID = ROAD_FAR + 4, ROAD_KERB = ROAD_FAR + 8,
                     ROAD_NEAR = ROAD_FAR + 11;
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
// How far off things are, for the panel's K_PIC_PART and a copy's depth. The picture's parts are the land's layers,
// each in front of the animations farther off than it. The forecast is in front of them all.
enum Depth : uint8_t {
  D_SKY = 0,        // behind the land
  D_RIDGE = 20,     // the mountains' far ridge
  D_FAR = 40,       // the far hills
  D_SEA = 60,       // the water and what's on it, and the fog over the far hills
  D_MID = 80,       // the middle hill and its trees, the lighthouse and the town
  D_MID_ON = 90,    // on the middle hill or the town's road, in front of them
  D_NEAR = 120,     // the near hill
  D_NEAR_ON = 130,  // on the near hill, in front of it, behind its trees, the cabin and the pole
  D_FRONT = 160,    // the near hill's trees, the cabin and the pole
  D_AIR = 200,      // in the open, in front of all the land and behind the forecast
};
struct Options {
  Ground ground = G_HILLS;
  Aurora aurora = A_AUTO;  // auto = when the Kp reaches kp_needed() here
  float facing = 180;      // azimuth in the picture's middle, clockwise from north: 0 north, 90 east, 180 south
  int heater_pct = 0;      // the unit's heater %, for the chimney smoke
  bool fireplace = false;  // more smoke
  uint32_t frame = 0;      // counts the redraws, for the lights that blink and the fireworks
  float kp = NAN;          // NOAA's for now, NAN without it, see met_kp.h
  float snow_cm = NAN;     // snow on the ground, NAN to go by the temperature and the month, see SnowCover
};

// Snow on the ground in cm, which MET's forecast doesn't give, kept from one picture to the next: it builds up
// while it snows and melts by the hour above zero, faster in rain. A cold clear night leaves the ground bare, and
// a mild hour in January leaves it white.
struct SnowCover {
  float cm = NAN;   // NAN before the first weather
  uint32_t at = 0;  // the UTC it's for
  // On to t in weather w. After half a day without any it starts again from snow_likely().
  void step(const Weather &w, uint32_t t);
};
// Whether snow lies in weather w that month, as far as the weather alone can say
bool snow_likely(const Weather &w, int month);

// The Kp it takes for the auroral oval to reach over lat, lon, 0 where it's there even when it's quiet
float kp_needed(double lat, double lon);

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
  C_SKY,                      // BANDS of it, from the top
  C_LOW = C_SKY + BANDS - 1,  // the sky at the horizon
  C_GLOW,                     // around a low sun
  C_SUN,
  C_HALO,
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
  C_TREE = C_HILL + 3,  // spruces on the middle hills, a little hazy, and on the near one, as the cabin's door
  C_DARK = C_TREE + 2,  // slate: the cabin's roof and panes and the other dark things
  C_SNOWCAP,
  C_BIRCH,
  C_SEA,
  C_WAVE,
  C_BLD,
  C_BLD_BACK,
  C_BLD_EDGE,
  C_OCHRE,  // the plaster of an old town house
  C_ROAD,   // the road's asphalt, its kerbs and dashes, and the bike lane
  C_KERB,
  C_LANE,
  C_RIDGE,
  C_LH,
  C_LH_BAND,
  C_WINDOW,
  C_CABIN,
  C_CABIN_SHADE,  // its wall that faces away from the sun
  C_STRAW,        // the julenek's oats
  C_WHITE,        // for small things and the cabin's trim, borrowed from a lit cloud or the sun
  COLOURS
};
static_assert(COLOURS <= 64, "a bit each in drawn() and KEY_COLOURS");

// Small things that would vanish if their colour went to their surroundings', besides the stamps, and the near
// spruces, which would go slate grey
inline constexpr uint64_t KEY_COLOURS = 1ull << C_STAR | 1ull << C_DIM | 7ull << C_RB | 1ull << C_BOLT |
                                        1ull << C_BIRCH | 1ull << C_WINDOW | 1ull << (C_TREE + 1) | 1ull << C_STRAW;

// ---------------------------------------------------------------- the scene

struct Ellipse {
  int16_t x, y, rx, ry;
};

struct Cloud {
  int16_t x, top, base, shade;  // x is the left edge in the wrapping field, rows below shade are darker
  Ellipse e[5];                 // x relative to the cloud's x
  uint8_t n;
};

// Where a stamp is drawn, back to front: in the sky behind the land, on the sea behind the middle land and what
// stands on it, in front of the land, and on top of everything
enum StampLayer : uint8_t { IN_SKY, ON_WATER, ON_LAND, ON_TOP };

// A one-colour bitmap stamped on the picture, like a bird or the flag's red. The animations have sprites instead,
// see met_sprites.h.
struct Stamp {
  int16_t x, y;
  uint8_t w, h;                  // at most 64 wide
  uint16_t layer : 2, tag : 14;  // StampLayer, and the L_* it's left out for or 0
  uint32_t c : 24, ix : 8;       // 0xRRGGBB, and its place in the palette
  uint64_t *rows;                // h of Stamps::rows, the leftmost pixel in the highest of w bits
};
// The stamps and their rows. A year of scenes takes at most 27 stamps and 245 rows, with the pennant hanging.
inline constexpr int STAMPS = 32, STAMP_ROWS = 256;
struct Stamps {
  Stamp at[STAMPS];
  uint64_t rows[STAMP_ROWS];
};
static_assert(L_BOAT < 1 << 14, "the L_* fit Stamp::tag");
static_assert(PENNANT_MAX + 4 <= 64, "the pennant fits a stamp");

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
  uint8_t back, kind;  // back: in the hazy far row. kind 0 flat roof, 1 a box on it, 2 antenna, 3 spire, 4 pitched
  uint8_t paint;       // its walls: 0 concrete, 1 red, 2 ochre, 3 white
};
// How far what a building has on its roof reaches up, by its kind
inline constexpr int8_t ROOF_UP[] = {0, 5, 12, 14, 5};

struct Light {  // one of the Christmas spruce's
  int16_t x, y;
  uint8_t c;  // 0 red, 1 warm, 2 green
};

// The moment, for the weather animations to pick theirs
struct At {
  bool dark, rain, snow, thunder;
  bool white;  // snow on the ground
  bool fair;   // sunny enough for birds
  bool north;  // from Nordland and north, from 65 degrees
  bool xmas, newyear, easter_days, midsummer, halloween, valentine, may17, flag_up, fireplace;
  bool yule;    // from 1 December, or the first Sunday in Advent before it, to the twentieth day of Christmas
  int8_t wdir;  // which way the wind blows things, 1 to the right
  uint8_t season, hour, month, day, weekday;  // weekday 0 Sunday, season see season_of()
  uint32_t clock;                             // minutes of local time, for the turns the features take
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
  bool sun, sun_halo, moon, moon_glow;
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
  uint32_t win_seed;         // picks the lit windows
  bool ridge;                // mountains, the land's ridge
  bool lighthouse;           // coast only, at LH_X
  int16_t lh_y;              // its base
  bool cabin, gable_lit;     // gable_lit: the sun is on the gable's side, so the long wall is in the shade
  int16_t cabin_x, cabin_y;  // its middle, and its foundation's row in the near hill
  bool window_lit;
  int16_t pole_x, pole_top, pole_base;  // flagpole, by the cabin or by the city's road
  int16_t home_x;                       // where the holidays happen, the cabin or the pole
  // where things are, for the weather animations, x -1 for none: the chimney's top, the bonfire's base, the
  // pumpkin, the boat, the antenna's top, the star on the Christmas spruce, the eggs and the spruce's lights
  Pt chimney, fire, pumpkin, boat, antenna, star;
  // and what stands about the cabin or in the park in winter, see yard_row(): the middle of the top of the
  // julenek's sheaf, the snowman's hat and the snow lantern
  Pt nek, snowman, lantern;
  bool yule_lights;  // the string of lights along the cabin's eaves and gable is lit
  Pt egg[5];
  Light light[14];
  uint8_t neggs, nlights;
  uint8_t puffs;  // the chimney's, by the heat
  uint16_t live;  // L_*, what row() and palette() leave out for the weather animations
  uint16_t tag;   // L_*, what the stamps made now are left out for
  At at;
  int nstamps, nrows;  // the stamps made and the rows they took
  bool overlay;        // the forecast shown over it, see forecast_marks()
  // The caller's, kept apart so the scene needs no big block
  Stamps *stamps;
  Land *land;  // made again when the place or landscape changes
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
  int b = BANDS - 1;
  while (b > 0 && y < BAND[b])
    b--;
  return b;
}
// 0..15 spread evenly over a 4 x 4 tile, to shade one colour into another: a pixel takes the other where its number
// is under how many sixteenths of it there should be
inline constexpr uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
// The sky band of a pixel. A band shades into the next over SHADE rows either side of the next one's first, by
// BAYER's tile: a sixteenth more of the next every two rows. Half of those rows pack as one run, like a row of one
// colour, the rest as they are.
inline constexpr int SHADE = 15;
inline int band_at(int x, int y) {
  int b = band_of(y + SHADE), in = y - BAND[b] + SHADE;  // rows into the shading above band b
  if (b == 0 || in >= 2 * SHADE)
    return b;
  return BAYER[y & 3][x & 3] <= in / 2 ? b : b - 1;
}
static_assert(BAND[1] >= SHADE && BAND[2] - BAND[1] >= 2 * SHADE, "a band's two shadings apart");

// ---------------------------------------------------------------- where row() draws things, for the animations too

// Cloud i's left edge on the screen, its ellipses' x from there. The screen is the middle of the field.
inline int cloud_x(const Scene &s, int i) {
  return (s.cloud[i].x + s.drift[s.cloud_layer[i]]) % FIELD - (FIELD - W) / 2;
}

inline bool high_cloud(const Scene &s, int i) { return s.cloud_layer[i] == 0; }  // the rest are middle and low

// Half width of an ellipse dy rows from its centre, -1 if outside: rx * sqrt(1 - dy^2 / ry^2) rounded, so the
// largest h with (2h - 1)^2 ry^2 <= 4 rx^2 (ry^2 - dy^2). In integers, an ESP32-C3 has no FPU.
inline int half(int dy, int rx, int ry) {
  if (dy < -ry || dy > ry || ry <= 0)
    return -1;
  uint64_t most = 4ull * (uint32_t) (rx * rx) * (uint32_t) (ry * ry - dy * dy);
  int lo = 0, hi = std::max(rx, 0);
  while (lo < hi) {
    int h = (lo + hi + 1) / 2;
    uint64_t q = (uint64_t) (2 * h - 1) * (uint32_t) ry;
    if (q * q <= most)
      lo = h;
    else
      hi = h - 1;
  }
  return lo;
}

// A wisp of high cloud from one of its ellipses, on the row dy below its middle: a thin streak that leans, with a
// hook up at its end. Its spans as columns from its middle, n of them: none, the streak, the hook or both. wide
// stretches it, for the animations.
struct Wisp {
  static constexpr int LEFT = 8, RIGHT = 13, UP = 3, DOWN = 2;  // how far it reaches past its ellipse's ends at most
  int16_t a[2], b[2];
  int n;
};
inline Wisp wisp(const Ellipse &e, int dy, int wide = 0) {
  Wisp w{};
  int rx = e.rx + wide, hw = half(dy, rx, (e.ry + 1) / 2);
  if (hw >= 0)
    w.a[w.n] = (int16_t) (-2 * dy - hw), w.b[w.n++] = (int16_t) (-2 * dy + hw);
  if (dy < 0 && dy >= -Wisp::UP)
    w.a[w.n] = (int16_t) (rx - 3 * dy - 3), w.b[w.n++] = (int16_t) (rx - 3 * dy);
  return w;
}

// A spruce from its top row to its base h rows down: a leader, then tiers that flare out downward and step in under
// each other, the lowest the widest, on a trunk. A small one has as many, shorter.
struct Spruce {
  int crown, tier, wide, lead, trunk;  // rows of crown and of a tier, half width at the crown's foot and of the trunk
  explicit constexpr Spruce(int h)
      : crown(h + 1 -
              (h < 14   ? 1
               : h < 28 ? 2
                        : 3)),
        tier(std::clamp(crown / 4, 3, 6)),
        wide(std::max(2, (h * 38 + 50) / 100)),
        lead(h / 8),
        trunk(h >= 28) {}
  constexpr int cone(int d) const { return ((d + 1) * wide * 2 + crown) / (2 * crown); }
  constexpr int up(int d) const { return (crown - 1 - d) % tier; }  // rows above its tier's lowest
  // Its half width d rows below its top
  constexpr int hw(int d) const {
    if (d >= crown)
      return trunk;
    return d < lead ? 0 : std::max(cone(d + up(d)) - up(d), cone(d) * 6 / 10);
  }
  // Whether snow lies there, on each tier's top row
  constexpr bool snow(int d) const { return d > 1 && d < crown && up(d) == tier - 1; }
};

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

// The forest's edge on the near hill left of the gap, beside the near spruce nearest it: x the first column clear of
// the spruce, y its base, tx and top its middle and top row. x -1 if there's none.
struct Edge {
  static constexpr int FLOOR = 4;  // rows of the hill under the spruces' bases in front of what walks in the forest
  int16_t x = -1, y = 0, tx = 0, top = 0;
  // The left column that has a thing w by h, its bottom row at bottom, all behind the spruce's crown or the hill
  // its trunk stands in and the floor under it, -1 if it's too big for it
  int hide(int w, int h, int bottom) const {
    Spruce spruce(y - top);
    int l = tx - w / 2;
    for (int r = std::min(bottom, top + spruce.crown - 1); r > bottom - h; r--)
      if (r < top || l < tx - spruce.hw(r - top) || l + w - 1 > tx + spruce.hw(r - top))
        return -1;
    return l >= 0 && bottom <= y + FLOOR ? l : -1;
  }
  // Where it comes out of the forest from and goes back into: behind the spruce, or off the left through the trees
  int out(int w, int h, int bottom) const {
    int l = hide(w, h, bottom);
    return l >= 0 ? l : -w - 2;
  }
};
inline Edge forest_edge(const Scene &s) {
  Edge e;
  for (int i : near_trees(s, 0)) {
    const Tree &t = s.tree[i];
    int x = t.x + Spruce(t.base - t.top).wide + 1;
    if (x <= GAP_L + 10 && x > e.x)
      e = {(int16_t) x, t.base, t.x, t.top};
  }
  return e;
}

// Birch i's crown: its rows from the top, and the leafy one's half width
struct Crown {
  int rows, rx;
};
inline Crown crown_of(const Scene &s, int i) {
  int height = s.tree[i].base - s.tree[i].top;
  return {height * 6 / 10, std::max(3, height / 4)};
}

// The glow around a low sun on the horizon, to the middle of its rim
inline Ellipse glow_of(const Scene &s) { return {s.glow_x, HORIZON, s.glow_rx, (int16_t) (s.glow_ry * 4 / 5)}; }

// The colour of a building's walls: the far row's haze, or its paint
inline int wall_of(const Bld &b) {
  static constexpr uint8_t PAINT[] = {C_BLD, C_CABIN, C_OCHRE, C_WHITE, C_BLD_BACK};
  return PAINT[b.back ? 4 : b.paint];
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

// The top left of the glass of the cabin's gable window, 7 by 6 with a bar down its middle and one across. The long
// wall's is WINDOW_2 to its right, and the door's opening is DOOR_W by DOOR_H from the foundation up, DOOR_X from
// the cabin's middle.
inline constexpr int WINDOW_2 = 35, DOOR_X = -1, DOOR_W = 6, DOOR_H = 16;
inline int window_x(const Scene &s) { return s.cabin_x - 22; }
inline int window_y(const Scene &s) { return s.cabin_y - 16; }
// The top left of the advent candles' arch, 7 by 5 on the sill of the long wall's window
inline int stake_x(const Scene &s) { return window_x(s) + WINDOW_2; }
inline int stake_y(const Scene &s) { return window_y(s) + 1; }
// The julenek: its sheaf's rows, and the rows from its top down to the ground its pole stands in
inline constexpr int NEK_ROWS = 9, NEK_UP = 23;
// The row over the cabin's ridge, where a bird's feet go
inline int ridge_y(const Scene &s) { return s.cabin_y - CABIN_RIDGE - 1; }

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
int hour_marks(const Weather &w, uint32_t t, Offset offset, int *at);

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
  bool leaves;  // the birches are in leaf

  Now(const Weather &weather, const Options &options, double la, double lo, uint32_t utc, int utc_offset);
};

// The whole scene at t, UTC, but its colours' places: palette() gives them, after s.live is set. Without w.ok
// there's no weather in it. s.stamps and s.land are the caller's and stay.
void build(Scene &s, const Weather &w, double lat, double lon, uint32_t t, bool with_forecast, Offset offset,
           const Options &o);

// The forecast over s for marks.h: the weather now at top left, the next four 3-hour marks at top right and on the
// coast the wave height, in a 5x7 font and 9x9 icons, white with a dark outline. None without s.overlay.
marks::Layer forecast_marks(const Scene &s, const Weather &w, uint32_t t, Offset offset);

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
