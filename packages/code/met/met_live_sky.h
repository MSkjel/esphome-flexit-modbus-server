// The sky's weather animations: rain, snow and thunder from under the clouds, the stars, the clouds and the fog,
// drawn the scene's own way in place of its still ones
#pragma once
#include "met_live.h"
#include "met_live_land.h"
#include "met_live_town.h"

namespace flexit::panel::met::live {

// Pool bytes the sky's sprites take besides their frame, upper bounds of what the panel's encoder makes of them.
// The firmware's sim/esp_yaml/live_sky.inc checks them.
// A still of rain or snow: to start with, a row, and a row a streak or flake goes through
struct FallCost {
  int start, row, each;
};
inline constexpr FallCost FALL_COST = {40, 3, 4};
// A tile of swaying snow besides its frame: to start with, and a flake
struct SwayCost {
  int start, each;
};
inline constexpr SwayCost SWAY_COST = {1530, 270};
// The bolt: to start with, and a row
inline constexpr int BOLT_START = 170, BOLT_ROW = 5;
// A cloud: fifths of its frame, and to start with
struct CloudCost {
  int fifths, start;
};
inline constexpr CloudCost CIRRUS_COST = {5, 405}, CUMULUS_COST = {6, 235};
// The flag next to the clouds
inline constexpr int FLAG_BYTES = 600;
// What a copy takes besides: a layer of rain or snow for its box, shaped to the clouds' bases in up to SHAPE_RUNS
// runs, and how it falls, and a cloud for its drift
inline constexpr int FALL_COPY = 200, CLOUD_COPY = 30;

// A layer of rain or snow and its copy: its kinds, its still's size, how fast they fall and how far they sway either
// way all together, px/s and px. Or its frames, flakes swaying each on its own in a tile as high as they fall in them.
struct Layer {
  Fall fall[2];
  int falls, w, h;
  float speed, sway;
  int frames;
};

// What the sky's acts keep between redraws
struct Sky {
  Layer layer[2];                          // the rain or snow, the nearer first
  float dens;                              // streaks or flakes per px
  int kind, slant;                         // 0 rain, 1 snow, 2 both, and their x per 8 rows
  decltype(Scene::bolt_x) bolt_x, bolt_y;  // the scene's bolt
  uint8_t top[W];                          // the skyline it strikes behind
  int16_t shoot[6][3];                     // where a shooting star may start, and which way, x 0 for none
  int16_t sat, sat_dy;                     // the row a satellite crosses from and how much higher it ends, -1 none
  bool clear;                              // the night sky is, for them
};
inline Sky SKY;

// Drifts copy 0 of name, w wide, from x at vx px/s round the scene's field of clouds, so it goes out at one edge
// and comes back at the other the way row() wraps them, doing what also says too
inline void field_drift(const std::string &name, int x, int y, int w, float vx, const std::string &also = "") {
  int lo = (w - (FIELD - W)) / 2, hi = lo + FIELD - w;  // off the screen at both edges, FIELD apart
  int at = lo - w + ((x - lo + w) % FIELD + FIELD) % FIELD;
  place(name, 0, at, y, also + "\nsmooth on; area lo, 0, hi, 240 cross screen; drift vx, 0",
        {{"lo", lo}, {"hi", hi}, {"vx", vx}});
}

// ---- rain and snow

// x per 8 rows the rain or snow falls: the scene's lean for the rain and sleet, and snow, which falls slower, blown a
// px more for each m/s, near level in a gale. In steps of 3, so a little more wind doesn't make it again.
inline int prec_slant(const Scene &s) {
  return s.at.snow && !s.at.rain ? s.at.wdir * std::min(24, 3 * (int) lroundf(s.at.wind / 3)) : s.slant;
}

// The scene's streaks or flakes per px of its fall, heavier in thunder
inline float prec_dens(const Scene &s) {
  int fall = 0;
  for (int x = 0; x < W; x++)
    fall += H - s.prec_top[x];
  float d = fall ? (float) s.nprec / (float) fall : 0;
  return s.at.thunder && s.at.rain && !s.at.snow ? std::max(d, 1 / 229.0f) : d;
}

// The layers of rain (0), snow (1) or sleet (2), d streaks or flakes per px between them: the near rain and the far
// one, fainter and slower, the bigger flakes and the smaller ones, or the drops and the flakes. With each, the flakes
// sway each on its own.
inline void layers(const Scene &s, int kind, float d, bool each, Layer *l) {
  uint32_t rain = s.rgb[C_RAIN], far = mix(rain, s.rgb[C_LOW - 1], 0.3f), snow = s.rgb[C_SNOW];
  int n = each ? 32 : 0;  // their frames, 110 ms each
  if (kind == 0) {
    l[0] = {{{7, rain, 0.7f * d, false}}, 1, 80, 64, 74, 0, 0};
    l[1] = {{{5, far, 0.3f * d, false}}, 1, 72, 56, 53, 0, 0};
  } else if (kind == 1) {
    l[0] = {{{2, snow, 0.25f * d, true}}, 1, each ? 48 : 80, each ? 80 : 64, 2.5f / 0.11f, 2, n};
    l[1] = {{{1, snow, 0.75f * d, true}}, 1, each ? 48 : 72, each ? 48 : 56, 1.5f / 0.11f, 1, n};
  } else {
    l[0] = {{{7, rain, 0.5f * d, false}}, 1, 80, 64, 74, 0, 0};
    l[1] = {{{2, snow, 0.125f * d, true}, {1, snow, 0.375f * d, true}},
            2,
            each ? 48 : 72,
            each ? 64 : 56,
            2.0f / 0.11f,
            1,
            n};
  }
}

// What layer l takes of the pool, FALL_COST's or SWAY_COST's
inline float fall_bytes(const Layer &l) {
  float rows = 0, flakes = 0;
  for (int k = 0; k < l.falls; k++) {
    flakes += l.fall[k].dens * (float) (l.w * l.h);
    rows += l.fall[k].dens * (float) (l.w * l.h) * l.fall[k].size;
  }
  if (l.frames)
    return (float) ((l.w + 1) / 2 * l.h + SWAY_COST.start) + (float) SWAY_COST.each * flakes;
  return (float) (FALL_COST.start + FALL_COST.row * l.h) + (float) FALL_COST.each * rows;
}

// Layer i's still, its streaks or flakes along the lean
inline Frames fall_still(int i) {
  const Layer &l = SKY.layer[i];
  uint32_t seed = 5 + 4 * (uint32_t) i;
  if (l.frames)
    return sway_tile(l.w, l.frames, l.speed * 0.11f, {l.fall[0], l.fall[1]}, seed);
  return fall_tile(l.w, l.h, SKY.kind == 1 ? 0 : SKY.slant, {l.fall[0], l.fall[1]}, seed);
}
inline std::string fall_name(int i) { return "fall" + std::to_string(i); }

// The highest base anything falls from
inline int fall_top(const Scene &s) {
  int top = H;
  for (int x = 0; x < W; x++)
    top = std::min<int>(top, s.prec_top[x]);
  return top;
}

// Layer i's copy over the sky, its box from the highest base down shaped to the bases, panning down through its
// still along the lean, round and round, the repeats mirrored where it falls straight, lightening what's behind it
// and fading in under the clouds. A layer of snow sways on waves of its own. again: it's there, and goes where the
// clouds are now.
inline void fall_place(const Scene &s, int i, bool again) {
  const Layer &l = SKY.layer[i];
  int top = fall_top(s);
  std::string box = "glide w " + num(W) + "\nglide h " + num(H - top) + "\n",
              shape = "soft 0, 0, 10, 0\n" + column_shape(s.prec_top, nullptr, top);
  if (again) {
    script(fall_name(i), 0, "move 0, " + num(top) + "\n" + box + shape);
    return;
  }
  std::string t = box + "blend screen\nrepeat both" + (SKY.slant || l.frames ? "" : " mirror") + "\nscroll " +
                  overlay::decimal(l.speed * (float) SKY.slant / 8) + ", " + overlay::decimal(l.frames ? 0 : l.speed) +
                  "\n";  // its frames fall through it
  if (l.sway && !l.frames)
    t += "wave pan_x " + num(-(int) l.sway) + ", " + num((int) l.sway) + ", " + num((int) (18000 / l.speed)) + ", " +
         num((int) (18000 / l.speed)) + " jitter 40 random slow both\n";
  place(fall_name(i), 0, 0, top, t + shape, {}, {.depth = (uint8_t) (D_AIR - i)});
}

// 0 rain, 1 snow, 2 both
inline int prec_kind(const Scene &s) { return s.at.snow ? (s.at.rain ? 2 : 1) : 0; }

// What the scene's rain or snow takes of the pool: at its fullest, the flakes swaying each on its own, with them
// swaying together, and with none at all
struct FallBytes {
  float full, together, none;
};
inline FallBytes prec_bytes(const Scene &s) {
  Layer l[2];
  FallBytes b{};
  layers(s, prec_kind(s), prec_dens(s), true, l);
  b.full = fall_bytes(l[0]) + fall_bytes(l[1]);
  layers(s, prec_kind(s), prec_dens(s), false, l);
  b.together = fall_bytes(l[0]) + fall_bytes(l[1]);
  for (const Layer &k : l)
    b.none += (float) (FALL_COST.start + FALL_COST.row * k.h);
  return b;
}
// What it takes at the least, half as thick as the scene's, with its two copies, and what more at its fullest
inline int prec_least(const Scene &s) {
  FallBytes b = prec_bytes(s);
  return (int) ceilf((b.none + b.together) / 2) + 2 * FALL_COPY;
}
inline int prec_more(const Scene &s) { return (int) ceilf(prec_bytes(s).full) + 2 * FALL_COPY - prec_least(s); }

// Rain or snow in room bytes of the pool: the flakes swaying together if each on its own takes more, and thinned if
// that does too
inline void prec_start(const Scene &s, int room) {
  SKY.dens = prec_dens(s);
  SKY.kind = prec_kind(s);
  SKY.slant = prec_slant(s);
  if (!s.nprec)
    return;
  FallBytes b = prec_bytes(s);
  bool each = b.full <= (float) room;
  float thin = each || b.together <= (float) room ? 1 : clamp01(((float) room - b.none) / (b.together - b.none));
  layers(s, SKY.kind, SKY.dens * thin, each, SKY.layer);
  for (int i = 0; i < 2; i++) {
    add(fall_name(i), fall_still(i));
    fall_place(s, i, false);
  }
}

// The scene's bolt, for its flashes after the scene's gone, and what it strikes behind: not the mountains, whose
// clouds hang in front of them
inline void keep_bolt(const Scene &s) {
  memcpy(SKY.bolt_x, s.bolt_x, sizeof s.bolt_x);
  memcpy(SKY.bolt_y, s.bolt_y, sizeof s.bolt_y);
  skyline(s, SKY.top, s.ground == G_MOUNTAINS ? 0 : 2);
}
// What the scene's bolt takes of the pool
inline int bolt_bytes(const Scene &s) {
  Box b = bolt_box(s.bolt_x, s.bolt_y, (int) std::size(s.bolt_x));
  return BOLT_ROW * b.h + BOLT_START;
}

// The bolt strikes out of the cloud, flickers and goes: a second on the panel unseen, then its box grows down
// through it, its light comes and goes. It's made again each time, and the pool has its room only while it flashes,
// see come(). The ms it takes.
inline constexpr int FLASH_MS = 1000 + 90 + 70 + 110 + 140 + 160;
inline uint32_t flash() {
  unload("bolt");  // before come(), so it's new to it
  return come(
      [] {
        int x, y;
        Frames b = bolt(SKY.bolt_x, SKY.bolt_y, (int) std::size(SKY.bolt_x), &x, &y);
        auto whole = b.draw;
        b.draw = [whole, x, y, w = b.w, h = b.h](int f, const Canvas &c) {  // behind the town and the land
          whole(f, c);
          for (int yy = 0; yy < h; yy++)
            for (int xx = 0; xx < w; xx++)
              if (y + yy >= SKY.top[std::clamp(x + xx, 0, W - 1)])
                c.px[yy * c.w + xx] = 0;
        };
        int h = b.h;
        add("bolt", std::move(b));
        place("bolt", 0, x, y, "blend add\nglide h 0\nwait 1000\nglide h " + num(h) + R"( over 60
          wait 90; glide light 55; wait 70; glide light 100; wait 110; glide light 75; wait 140; glide light 35
          wait 160; end fade 0)",
              {}, {.fade = 0});
        return (uint32_t) FLASH_MS;
      },
      ON_PANEL);
}

// The pool's bytes for the rain's and snow's stills of what the act was granted: less their copies', and the bolt's
// in thunder
inline int prec_room(const Scene &s) { return granted().bytes - 2 * FALL_COPY - (s.at.thunder ? bolt_bytes(s) : 0); }

inline void thunderstorm(const Scene &s) {
  keep_bolt(s);
  prec_start(s, prec_room(s));
  sometimes(minutes(3), 40000, flash);
}

inline void snowfall(const Scene &s) { prec_start(s, prec_room(s)); }

// The clouds moved: the layers go under their new bases, or are made again if the rain or the wind changed. The bolt
// flashes where the scene has it now.
inline void prec_redo(const Scene &s) {
  if (s.at.thunder)
    keep_bolt(s);
  float d = prec_dens(s);
  int kind = s.at.snow ? (s.at.rain ? 2 : 1) : 0;
  if (kind == SKY.kind && prec_slant(s) == SKY.slant && fabsf(d - SKY.dens) <= SKY.dens / 5 && s.nprec) {
    for (int i = 0; i < 2; i++)
      fall_place(s, i, true);
    return;
  }
  for (int i = 0; i < 2; i++)
    unload(fall_name(i));
  prec_start(s, prec_room(s));
}

// ---- the stars

// A shooting star burns along its path and goes. The ms it takes.
inline uint32_t shoot() {
  int k = (int) rnd(0, 6);
  for (int i = 0; i < 6 && !SKY.shoot[k][0]; i++)
    k = (k + 1) % 6;
  const int16_t *c = SKY.shoot[k];
  if (!c[0] || !SKY.clear)
    return 0;
  int x = c[2] > 0 ? c[0] : c[0] + 40;  // the box from its start to where it goes out
  place("shooting", 0, x, c[1], "blend add; frame 0; move x, y over 490; wait rounds 1; end",
        {{"x", x + c[2] * 40}, {"y", c[1] + 15}}, {.left = c[2] < 0, .fade = 0});
  return 490;
}

// Where a shooting star may start: six at random where its 64 x 24 box from there is all sky
inline void shoot_spots(const Scene &s) {
  uint8_t px[W];
  int16_t (*c)[3] = SKY.shoot;
  for (int i = 0; i < 6; i++) {
    c[i][0] = (int16_t) rnd(10, 236);
    c[i][1] = (int16_t) rnd(12, 64);
    c[i][2] = (int16_t) (chance(0.5f) ? 1 : -1);
    if (s.moon && abs(c[i][0] + 32 - s.moon_x) < 60 && abs(c[i][1] + 12 - s.moon_y) < 36)
      c[i][0] = 0;
  }
  for (int y = 12; y < 64 + 24; y++) {
    row(s, y, px);
    for (int i = 0; i < 6; i++)
      for (int x = c[i][0]; c[i][0] && y >= c[i][1] && y < c[i][1] + 24 && x < c[i][0] + 64; x++)
        if (!sky_px(s, px[x], x, y))
          c[i][0] = 0;
  }
}

// A satellite glides across the sky at 4 px/s, rising a little, a faint dot that flares up once. The ms it takes.
inline uint32_t satellite() {
  if (SKY.sat < 0 || !SKY.clear)
    return 0;
  uint32_t ms = (W + 6) * 1000 / 4;
  place("satellite", 0, -3, SKY.sat, "blend add; frame 0; area -10, 0, 400, 240 cross; drift 4, vy; wait ms; end",
        {{"vy", -4.0 * SKY.sat_dy / (W + 6)}, {"ms", ms}});
  return ms;
}

// The sky turns: the stars sparkle where they are now, as many as the act has places for and not behind the
// lighthouse's beam, and shooting stars and satellites go where it's clear now
inline void stars_turn(const Scene &s) {
  twinkle_stars(s, s.lighthouse ? sweep_box(s) : Box{}, std::min(6, granted().places));
  shoot_spots(s);
  SKY.sat_dy = (int16_t) rnd(10, 40);
  SKY.sat = (int16_t) sky_line(s, 3, 3, 50, 120, -SKY.sat_dy);
  SKY.clear = s.at.cover < 40;
}

// The stars sparkle, and on a clear night about every 5 minutes a shooting star, from where the sky is clear, never
// the moon, and now and then a satellite crosses
inline void stars(const Scene &s) {
  stars_turn(s);
  visitors(minutes(4), minutes(1), 4, [](int k) { return k ? shoot() : satellite(); });
}

// ---- the clouds

// The shapes of the high clouds, or the middle and low ones, the same until the forecast's next hour
inline uint32_t cloud_sig(const Scene &s, bool high) {
  uint32_t h = 1;
  for (int i = 0; i < s.nclouds; i++)
    if (high_cloud(s, i) == high)
      h = h * 31 + (uint32_t) (s.cloud[i].x * 1000 + s.cloud[i].base * 8 + s.cloud[i].n);
  return h;
}
inline uint32_t high_shapes(const Scene &s) { return cloud_sig(s, true); }
inline uint32_t lower_shapes(const Scene &s) { return cloud_sig(s, false); }

// The scene's cloud i as a sprite, a cirrus or a cumulus, its top left in the field at x, y
inline Frames cloud(const Scene &s, int i, int *x, int *y) {
  const Cloud &c = s.cloud[i];
  int l = s.cloud_layer[i], left = 9999, top = 999, bot = 0;
  for (int j = 0; j < c.n; j++) {
    left = std::min(left, c.e[j].x - c.e[j].rx);
    top = std::min(top, c.e[j].y - (l ? c.e[j].ry : Wisp::UP));
    bot = std::max(bot, c.e[j].y + (l ? c.e[j].ry : Wisp::DOWN));
  }
  *x = cloud_x(s, i) + left - (l ? 1 : Wisp::LEFT);
  if (!l) {
    *y = top - 1;
    return cirrus_cloud(c.e, c.n, *y, bot - top + 3, s.rgb[C_LIT]);
  }
  *y = c.top - 2;
  return cumulus_cloud(c.e, c.n, c.top, c.base, c.shade, s.rgb[C_LIT + l], s.rgb[C_SHADE + l]);
}

// What the high clouds, or the middle and low ones, take as an animation each
inline Room clouds_cost(const Scene &s, bool high) {
  Room r{};
  for (int i = 0; i < s.nclouds; i++)
    if (high_cloud(s, i) == high) {
      int x, y;
      Frames sp = cloud(s, i, &x, &y);
      int frame = (sp.w + 1) / 2 * sp.h;
      const CloudCost &c = high ? CIRRUS_COST : CUMULUS_COST;
      r = r + Room{1, 1, frame * c.fifths / 5 + c.start + CLOUD_COPY};
    }
  return r;
}

// The high clouds drift with the wind at 1 to 2 px/s, the higher ones slower, thinning and thickening on slow
// waves. The middle ones half a px a second, the low ones nearly 1.
inline void drift_clouds(const Scene &s, bool high) {
  for (int i = 0, k = 0; i < s.nclouds; i++) {
    if (!s.cloud_layer[i] != high)
      continue;
    int x, y, l = s.cloud_layer[i];
    Frames sp = cloud(s, i, &x, &y);
    int w = sp.w;
    std::string name = "cloud" + std::to_string(k++);
    add(name, std::move(sp));
    float v = l == 1 ? 0.5f : l == 2 ? 0.9f : std::clamp(1 + (s.cloud[i].base - 24) / 44.0f, 1.0f, 2.0f);
    field_drift(name, x, y, w, s.at.wdir * v,
                high ? "wave opacity 55, 100, 9000, 11000 hold 5000, 3000 jitter 40 random slow both" : "");
  }
}

inline void high_clouds(const Scene &s) { drift_clouds(s, true); }
inline void lower_clouds(const Scene &s) { drift_clouds(s, false); }

// ---- the flag and the northern lights

// The hoist next to the pole, waving in place of the picture's the way the wind blows, in the light there is.
// Where it flies so already it's left as it is.
inline void flag_up(const Scene &s) {
  int x = s.at.wdir < 0 ? s.pole_x - 16 : s.pole_x + 1;
  const overlay::Overlay *a = overlay::find(key("flag"));
  if (a && !a->copies.empty() && a->copies[0].used && a->copies[0].x == x)
    return;
  in_light("flag", 0);
  place("flag", 0, x, s.pole_top, "", {}, {.left = s.at.wdir > 0});
}

// Where the picture's wide ribbon is and what colour, another when it has moved or changed. 0 without one.
inline uint32_t lights_at(const Scene &s) {
  return s.aurora ? 1 + s.aur_top[0][W / 2] + s.aur_bot[0][W / 2] * 256u + (s.rgb[C_AUR_EDGE] << 16) : 0;
}

// The picture's wide ribbon glows: bands of light well up in it, wander along it and reach a little way up over
// it. A field of more here and less there that the panel works out, lit where it's about so much, and how much
// that is goes round its colours in 24 s, four steps a second so it's seldom drawn again. Behind the land.
inline void northern_lights(const Scene &s) {
  constexpr int UP = 9;  // rows it reaches over the ribbon
  uint8_t from[W], to[W];
  int top = H, bottom = 0;
  for (int x = 0; x < W; x++) {
    from[x] = (uint8_t) std::max(0, s.aur_top[0][x] - UP);
    to[x] = s.aur_bot[0][x];
    top = std::min<int>(top, from[x]);
    bottom = std::max<int>(bottom, to[x]);
  }
  add_field("lights", W, bottom - top, OVERLAY_FIELD_NOISE, 1, 22,
            light(mix(s.rgb[C_AUR_EDGE], 0xFFFFFF, 0.15f),
                  [](float v) { return 0.75f * powf(std::max(0.0f, sinf(kPiF * v)), 3); }));
  place("lights", 0, 0, top,
        "blend add\nscroll 2.5, 0\nphase := lerp(0, 16, ms / 250 * 250 % 24000, 24000)\nsoft 0, 0, " + num(UP) +
            ", 2\n" + column_shape(from, to, top),
        {}, {.depth = D_SKY});
}

// ---- the fog

// Fog a little lighter than the scene's along its edges, worked out on the panel: banks with wisps round them,
// creeping with the wind, the nearer quicker, rising and sinking a little, and thickening and thinning a couple of
// times a second at the most, so it's seldom drawn again. In steps, as the picture is drawn. Over the far hills, the
// nearer over the middle ones in thick fog, behind what's nearer.
inline void valley_fog(const Scene &s) {
  auto banks = light(mix(s.rgb[C_FOG], 0xFFFFFF, 0.3f), [](float v) {
    return v < 0.45f ? 0 : v < 0.56f ? 0.35f : v < 0.7f ? 0.6f : 0.85f;
  });
  add_field("fog0", W, 12, OVERLAY_FIELD_NOISE, 2, 40, banks);
  add_field("fog1", W, 9, OVERLAY_FIELD_NOISE, 2, 28, banks);
  int edge[2];
  for (int l = 0; l < 2; l++) {
    int sum = 0;
    for (int x = 0; x < W; x++)
      sum += s.fog_top[l][x];
    edge[l] = sum / W;
  }
  bool thick = s.fog_top[1][0] != NONE;
  for (int i = 0; i < 2; i++) {
    int h = i ? 9 : 12, y = (!i && thick ? edge[1] : edge[0] + 3 * !i) - h + 4;
    place(i ? "fog1" : "fog0", 0, 0, y, R"(
      blend screen
      scroll vx, 0
      wave y -1, 1, 7000, 9000 jitter 40 random slow both
      level := 80 + noise(ms / 400, 20, 45)
      soft 0, 0, 3, 3
    )",
          {{"vx", s.at.wdir * (i ? 0.4f : 0.6f)}}, {.depth = (uint8_t) (!i && thick ? D_MID_ON : D_SEA)});
  }
}

}  // namespace flexit::panel::met::live
