// The sky's weather animations: rain, snow and thunder from under the clouds, the aurora, the stars, the clouds
// and the fog, drawn the scene's own way in place of its still ones
#pragma once
#include "met_live.h"

namespace flexit::panel::met::live {

// The pool's bytes a show here plans for, a little under the panel's
inline constexpr int SKY_POOL = ANIM_POOL - 144;

// Pool bytes the sky's sprites take besides their frame, upper bounds of what the panel's encoder makes of them.
// The firmware's sim/esp_yaml/live_sky.inc checks them.
// A rain or snow tile by kind (rain, snow, both): to start with, a row, a streak or flake, and that per px of slant
struct TileCost {
  int start, row, each, slant;
};
inline constexpr TileCost TILE_COST[] = {{650, 1, 32, 13}, {930, 4, 76, 0}, {1400, 1, 123, 32}};
// The bolt: to start with, and a row
inline constexpr int BOLT_START = 100, BOLT_ROW = 4;
// A cloud: fifths of its frame, and to start with
struct CloudCost {
  int fifths, start;
};
inline constexpr CloudCost CIRRUS_COST = {5, 330}, CUMULUS_COST = {6, 210};
// The flag next to the clouds, and the far gulls with the lower ones
inline constexpr int FLAG_BYTES = 600, GULLS_FAR_BYTES = 100;
// Widths of the aurora's patches on the high and the low ribbon, what fits the pool on every night
inline constexpr int AUR_HIGH_W = 52, AUR_LOW_W = 72;

// What the sky shows keep between redraws
struct Sky {
  int tiles, tw, h;                        // the rain and snow tiles: how many, their width and height
  float dens;                              // streaks or flakes per px
  int kind, slant;                         // 0 rain, 1 snow, 2 both, and their x per 8 rows
  decltype(Scene::bolt_x) bolt_x, bolt_y;  // the scene's bolt
  int16_t shoot[6][3];                     // where a shooting star may start, and which way, x 0 for none
};
inline Sky SKY;

// Drifts copy 0 of name, w wide, from x at vx px/s round the scene's field of clouds, so it goes out at one edge
// and comes back at the other the way row() wraps them
inline void field_drift(const std::string &name, int x, int y, int w, float vx) {
  int lo = (w - (FIELD - W)) / 2, hi = lo + FIELD - w;  // off the screen at both edges, FIELD apart
  int at = lo - w + ((x - lo + w) % FIELD + FIELD) % FIELD;
  place(name, 0, at, y);
  area(name, 0, lo, 0, hi, H, "cross", "screen");
  drift(name, 0, vx, 0);
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

// Tile i's top: the lowest cloud base over its columns
inline int tile_top(const Scene &s, int i) {
  int top = 0;
  for (int x = i * SKY.tw; x < std::min(W, (i + 1) * SKY.tw); x++)
    top = std::max<int>(top, s.prec_top[x]);
  return top;
}

// The tiles from the highest of their tops to the bottom
inline int tiles_h(const Scene &s) {
  int top = H;
  for (int i = 0; i < SKY.tiles; i++)
    top = std::min(top, tile_top(s, i));
  return H - top;
}

// The scene's rain, snow or both as one still tile under the clouds, thinned if it would take more than room bytes
// of the pool: its frame and TILE_COST.
inline Frames prec_tile(const Scene &s, int room) {
  int w = SKY.tw, h = SKY.h, k = SKY.kind, slant = SKY.slant;
  const TileCost &c = TILE_COST[k];
  float most = (float) (room - w / 2 * h - c.start - c.row * h) / (float) (c.each + c.slant * abs(slant) / 2);
  float d = SKY.dens * clamp01(most / (SKY.dens * (float) (w * h)));
  uint32_t rain = s.rgb[C_RAIN], far = mix(rain, s.rgb[C_SKY + 3], 0.3f), snow = s.rgb[C_SNOW];
  float r = k == 2 ? 0.5f : 1;  // sleet is half of each
  Frames drops = rain_tile(w, h, slant, {{7, 7, rain, d * r * 0.7f}, {5, 5, far, d * r * 0.3f}}, 5);
  Frames flakes = snow_tile(w, h, slant, {{1, 1.5f, snow, d * r * 0.75f}, {2, 2.5f, snow, d * r * 0.25f}}, 9);
  if (k < 2)
    return k ? flakes : drops;
  return {w, h, flakes.delays, [drops, flakes](int f, const Canvas &c) {
            drops.draw(f % 8, c);
            flakes.draw(f, c);
          }};
}

// The tiles edge to edge, each at the lowest cloud base over it. Where it falls straight, every other one mirrored.
// moved: they're there, and go to where the clouds are now.
inline void prec_place(const Scene &s, bool moved) {
  for (int i = 0; i < SKY.tiles; i++) {
    if (moved) {
      move("prec", i, i * SKY.tw, tile_top(s, i), 0);
      continue;
    }
    place("prec", i, i * SKY.tw, tile_top(s, i));
    if (i % 2 && !SKY.slant)
      flip("prec", i, true, false);
  }
}

// Rain or snow, tiles tw wide, in room bytes of the pool
inline void prec_start(const Scene &s, int tw, int room) {
  SKY.tw = tw;
  SKY.tiles = (W + tw - 1) / tw;
  SKY.h = tiles_h(s);
  SKY.dens = prec_dens(s);
  SKY.kind = s.at.snow ? (s.at.rain ? 2 : 1) : 0;
  SKY.slant = prec_slant(s);
  if (!s.nprec)
    return;
  add("prec", prec_tile(s, room));
  prec_place(s, false);
}

// The scene's bolt, for its flashes after the scene's gone
inline void keep_bolt(const Scene &s) {
  memcpy(SKY.bolt_x, s.bolt_x, sizeof s.bolt_x);
  memcpy(SKY.bolt_y, s.bolt_y, sizeof s.bolt_y);
}
// What the scene's bolt takes of the pool
inline int bolt_bytes(const Scene &s) {
  Box b = bolt_box(s.bolt_x, s.bolt_y, (int) std::size(s.bolt_x));
  return (b.w + 1) / 2 * b.h + BOLT_ROW * b.h + BOLT_START;
}

// The bolt flashes out of the cloud and goes. It's made again each time, so its frames start from the first, and
// the pool has its room only while it flashes. The ms it takes.
inline uint32_t flash() {
  int x, y;
  unload("bolt");
  add("bolt", bolt(SKY.bolt_x, SKY.bolt_y, (int) std::size(SKY.bolt_x), &x, &y));
  place("bolt", 0, x, y, {false, 0, 0});
  rounds("bolt", 0, 1);
  later(15000, [] { unload("bolt"); });
  return (uint32_t) ms_of("bolt");
}

// The pool's bytes for the tiles: less the bolt's in thunder, and in a gale RARE_BYTES for something blowing by
inline int prec_room(const Scene &s) {
  return s.at.thunder ? SKY_POOL - bolt_bytes(s) : gale(s.at) ? SKY_POOL - RARE_BYTES : SKY_POOL;
}
// What the rain and snow are made of: whether it blows a gale, so they start again with room for it or without
inline uint32_t in_gale(const Scene &s) { return gale(s.at); }

inline void thunderstorm(const Scene &s) {
  keep_bolt(s);
  prec_start(s, 46, prec_room(s));  // a copy for the bolt
  sometimes(minutes(3), 40000, flash);
}

// Snow or light rain. In a gale a copy fewer, for what blows by
inline void snowfall(const Scene &s) { prec_start(s, gale(s.at) ? 46 : 40, prec_room(s)); }

// The clouds moved: the tiles go to their new bases, or are made again if they're too short now or the rain or wind
// changed.
// The bolt flashes where the scene has it now.
inline void prec_redo(const Scene &s) {
  if (s.at.thunder)
    keep_bolt(s);
  int room = prec_room(s);
  float d = prec_dens(s);
  int kind = s.at.snow ? (s.at.rain ? 2 : 1) : 0;
  if (tiles_h(s) <= SKY.h && kind == SKY.kind && prec_slant(s) == SKY.slant && fabsf(d - SKY.dens) <= SKY.dens / 5 &&
      s.nprec) {
    prec_place(s, true);
    return;
  }
  unload("prec");
  prec_start(s, SKY.tw, room);
}

// ---- the aurora

// A patch of the scene's ribbon r in box at, drawn over the picture's own: its middle bends a px or two in each
// frame and glows brighter, so a wave of its opacity brightens it. At its ends it's the picture's, so it meets the
// still ribbon. Left out where it's the same as the picture or something is in front of the ribbon, a star, the
// moon or a cloud.
inline Frames aur_patch(const Scene &s, int r, const Box &at) {
  struct Patch {
    int w, oy, h;
    std::vector<uint8_t> tb, front;  // top and bottom rows, a bit per px with something in front
    uint32_t col[3][4], sky[5];      // C_AUR's three by glow, the picture's first
  } p;
  int w = at.w, x0 = at.x;
  p.w = w;
  p.oy = at.y;
  p.h = at.h;
  p.tb.resize(2 * w);
  for (int i = 0; i < w; i++) {
    p.tb[i] = s.aur_top[r][x0 + i];
    p.tb[w + i] = s.aur_bot[r][x0 + i];
  }
  for (int b = 0; b < 3; b++) {  // a step of 17 at a time on its strongest channel, away from the sky
    uint32_t c = s.pal[s.ix[C_AUR + b]];
    int d[3], most = 1;
    for (int k = 0; k < 3; k++)
      most = std::max(most, abs(d[k] = (int) (c >> (16 - 8 * k) & 255) - (int) (s.rgb[C_SKY] >> (16 - 8 * k) & 255)));
    for (int g = 0; g < 4; g++)
      p.col[b][g] = rgb4((int) (c >> 16) + d[0] * g * 17 / most, (int) (c >> 8 & 255) + d[1] * g * 17 / most,
                         (int) (c & 255) + d[2] * g * 17 / most);
  }
  for (int b = 0; b < 5; b++)
    p.sky[b] = s.pal[s.ix[C_SKY + b]];
  p.front.assign((size_t) (w * p.h + 7) / 8, 0);
  uint8_t px[W];
  for (int y = 0; y < p.h; y++) {
    row(s, p.oy + y, px);
    for (int i = 0, k = y * w; i < w; i++, k++) {
      int t = p.tb[i], b = p.tb[w + i], yy = p.oy + y;
      if (px[x0 + i] != s.ix[yy >= t && yy < b ? aur_band(yy, t, b) : C_SKY + band_of(yy)])
        p.front[k / 8] |= (uint8_t) (1 << k % 8);
    }
  }
  return {w, p.h, {8000, 7000, 8500, 7500}, [p](int f, const Canvas &c) {
            for (int i = 0; i < p.w; i++) {
              int t = p.tb[i], b = p.tb[p.w + i], g = std::min(3, std::min(i, p.w - 1 - i) * 12 / p.w);
              int d = aur_wig(i, p.w, f), t2 = t + d, b2 = b + d;
              for (int y = 0, k = i; y < p.h && t != NONE; y++, k += p.w) {
                int yy = p.oy + y, was = yy >= t && yy < b ? aur_band(yy, t, b) - C_AUR : -1;
                int now = yy >= t2 && yy < b2 ? aur_band(yy, t2, b2) - C_AUR : -1;
                if (p.front[k / 8] >> k % 8 & 1 || (now == was && (now < 0 || !g)))
                  continue;
                c.put(i, y, now < 0 ? p.sky[band_of(yy)] : p.col[now][g]);
              }
            }
          }};
}

// The rows ribbon r takes over columns x0..x0+w-1, 0 if it isn't there. top gets the first.
inline int aur_rows(const Scene &s, int r, int x0, int w, int *top = nullptr) {
  int lo = 255, hi = 0;
  for (int x = x0; x < x0 + w; x++)
    if (s.aur_top[r][x] != NONE) {
      lo = std::min<int>(lo, s.aur_top[r][x]);
      hi = std::max<int>(hi, s.aur_bot[r][x]);
    }
  if (top)
    *top = lo;
  return hi > lo ? hi - lo : 0;
}

// Where in a..b a patch w wide of ribbon r takes the fewest rows, the middle of it if they're the same. -1 if the
// ribbon isn't there all the way.
inline int aur_spot(const Scene &s, int r, int a, int b, int w) {
  int best = -1, rows = 999, mid = (a + b - w) / 2;
  for (int x = std::max(0, a); x + w <= std::min(W, b); x++) {
    int n = s.aur_top[r][x] == NONE || s.aur_top[r][x + w - 1] == NONE ? 999 : aur_rows(s, r, x, w);
    if (n < rows || (n == rows && abs(x - mid) < abs(best - mid)))
      rows = n, best = x;
  }
  return rows < 999 ? best : -1;
}

// Where the patches go: one in each third of the high ribbon and the last on the low one, where they take the
// fewest rows, with 2 rows above and below. w 0 where there's none.
inline void aur_boxes(const Scene &s, Box (&b)[4]) {
  for (int k = 0; k < 4; k++) {
    int r = k == 3, w = r ? AUR_LOW_W : AUR_HIGH_W, top = 0;
    int x = r ? aur_spot(s, 1, 0, W, w) : aur_spot(s, 0, k * W / 3, (k + 1) * W / 3, w);
    int rows = x < 0 ? 0 : aur_rows(s, r, x, w, &top);
    b[k] = x < 0 ? Box{} : Box{x, top - 2, w, rows + 4};
  }
}

// What the patches are made of: the ribbons, which move up or down a little every hour, and the moon where it's at
// a patch
inline uint32_t aurora_shapes(const Scene &s) {
  uint32_t h = 1;
  for (int r = 0; r < 2; r++)
    for (int x = 0; x < W; x++)
      h = (h * 31 + s.aur_top[r][x]) * 31 + s.aur_bot[r][x];
  Box b[4];
  aur_boxes(s, b);
  for (const Box &p : b)
    if (s.moon && p.w && abs(s.moon_x - p.x - p.w / 2) < p.w / 2 + 10 && abs(s.moon_y - p.y - p.h / 2) < p.h / 2 + 10)
      h = h * 31 + (uint32_t) (s.moon_x * 256 + s.moon_y);
  return h;
}

// The picture keeps the ribbons. A patch of each third of the high one and one of the low one bend and glow, and a
// slow wave brightens and fades them from left to right.
inline void northern_lights(const Scene &s) {
  Box b[4];
  aur_boxes(s, b);
  for (int k = 0, n = 0; k < 4; k++) {
    if (!b[k].w)
      continue;
    int ahead = 0;
    for (const Box &o : b)
      ahead += o.w && o.x < b[k].x;
    std::string name = "aur" + std::to_string(n++);
    add(name, aur_patch(s, k == 3, b[k]));
    place(name, 0, b[k].x, b[k].y);
    later((uint32_t) (ahead * 1400 + 300),
          [name] { wave(name, 0, "opacity", 15, 100, 7000, 7000, 1500, 1500, 0, false, "both"); });
  }
}

// ---- the stars

// A shooting star burns along its path and goes. The ms it takes.
inline uint32_t shoot() {
  int k = (int) rnd(0, 6);
  for (int i = 0; i < 6 && !SKY.shoot[k][0]; i++)
    k = (k + 1) % 6;
  const int16_t *c = SKY.shoot[k];
  if (!c[0])
    return 0;
  int x = c[2] > 0 ? c[0] : c[0] + 40;  // the box from its start to where it goes out
  place("shooting", 0, x, c[1], {true, 0, 0});
  if (c[2] < 0)
    flip("shooting", 0, true, false);
  rounds("shooting", 0, 1);
  move("shooting", 0, x + c[2] * 40, c[1] + 15, 490);
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
        if (px[x] != s.ix[C_SKY + band_of(y)] && px[x] != s.ix[C_DIM] && px[x] != s.ix[C_STAR])
          c[i][0] = 0;
  }
}

// The sky turns: the stars sparkle where they are now, and shooting stars start where it's clear now
inline void stars_turn(const Scene &s) {
  twinkle_stars(s);
  shoot_spots(s);
}

// The stars sparkle, and about every 5 minutes a shooting star, from where the sky is clear, never the moon
inline void starry_night(const Scene &s) {
  stars_turn(s);
  sometimes(minutes(5), minutes(1), shoot);
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
    top = std::min(top, c.e[j].y - c.e[j].ry);
    bot = std::max(bot, c.e[j].y + c.e[j].ry);
  }
  *x = cloud_x(s, i) + left - (l ? 1 : 3);
  if (!l) {
    *y = top - 1;
    return cirrus_cloud(c.e, c.n, *y, bot - top + 3, s.rgb[C_LIT]);
  }
  *y = c.top - 2;
  return cumulus_cloud(c.e, c.n, c.top, c.base, c.shade, s.rgb[C_LIT + l], s.rgb[C_SHADE + l]);
}

// Whether the high clouds, or the middle and low ones, fit the panel as an animation each, next to the flag and
// the gulls.
inline bool clouds_fit(const Scene &s, bool high) {
  bool flag = s.at.flag_up && s.pole_x >= 0;
  int n = 0, bytes = (flag ? FLAG_BYTES : 0) + (high ? 0 : GULLS_FAR_BYTES);
  for (int i = 0; i < s.nclouds; i++)
    if (high_cloud(s, i) == high) {
      int x, y;
      Frames sp = cloud(s, i, &x, &y);
      int frame = (sp.w + 1) / 2 * sp.h;
      const CloudCost &c = high ? CIRRUS_COST : CUMULUS_COST;
      bytes += frame * c.fifths / 5 + c.start;
      n++;
    }
  return n <= 4 - flag - !high && bytes <= SKY_POOL;
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
    field_drift(name, x, y, w, s.at.wdir * v);
    if (high)
      wave(name, 0, "opacity", 55, 100, 9000, 11000, 5000, 3000, 40, true, "both");
  }
}

inline void high_clouds(const Scene &s) { drift_clouds(s, true); }

// The clouds, and a gull or two far off
inline void lower_clouds(const Scene &s) {
  drift_clouds(s, false);
  flock({}, {{(int) rnd(40, 280), HORIZON - 70, 2, 3, HORIZON - 100, HORIZON - 40},
             {(int) rnd(40, 280), HORIZON - 50, 2, 3, HORIZON - 80, HORIZON - 25}});
}

// ---- the fog

// Banks a little lighter than the scene's fog creep along its edges with the wind, the near ones bigger and
// quicker, thickening and thinning on slow waves. Over the far hills, and the middle ones in thick fog.
inline void valley_fog(const Scene &s) {
  uint32_t f = s.rgb[C_FOG];
  std::array<uint32_t, 3> cols = {mix(f, 0xFFFFFF, 0.35f), mix(f, 0xFFFFFF, 0.22f), mix(f, 0xFFFFFF, 0.1f)};
  add("fog0", fog_bank(110, 12, 3, cols));
  add("fog1", fog_bank(76, 9, 7, cols));
  int edge[2];
  for (int l = 0; l < 2; l++) {
    int sum = 0;
    for (int x = 0; x < W; x++)
      sum += s.fog_top[l][x];
    edge[l] = sum / W;
  }
  bool thick = s.fog_top[1][0] != NONE;
  static const float V[4] = {0.6f, 0.55f, 0.45f, 0.4f};
  static const int AT[4] = {1, 5, 3, 7};
  for (int i = 0; i < 4; i++) {
    std::string name = i < 2 ? "fog0" : "fog1";
    int k = i % 2, h = i < 2 ? 12 : 9, y = (i < 2 && thick ? edge[1] : edge[0] + (i < 2) * 3) - h + 4;
    place(name, k, AT[i] * W / 8 - (i < 2 ? 55 : 38), y);
    if (k)
      flip(name, k, true, false);
    area(name, k, -130, 0, 450, H, "cross", "screen");
    drift(name, k, s.at.wdir * V[i], 0);
    wave(name, k, "opacity", 50, 90, 14000, 16000, 6000, 8000, 40, true, "both");
  }
}

}  // namespace flexit::panel::met::live
