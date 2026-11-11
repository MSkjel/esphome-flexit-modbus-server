// Drawing the weather scene a row at a time, and its 16 colours, see met_raster.h
#include "met_raster.h"
#include <algorithm>
#include <cstring>

namespace flexit::panel::met {

// cloud i is left out for the animations
static bool hidden(const Scene &s, int i) { return s.live & (s.cloud_layer[i] ? L_CLOUDS : L_CIRRUS); }

// cloud i hangs in front of the mountains, which stand in the middle and low clouds
static bool shrouds(const Scene &s, int i) { return s.ground == G_MOUNTAINS && !high_cloud(s, i); }

static void span(uint8_t *row, int a, int b, uint8_t c) {
  if (a < 0)
    a = 0;
  if (b > W - 1)
    b = W - 1;
  if (a <= b)
    memset(row + a, c, (size_t) (b - a + 1));
}

// span() at both edges too, where the sky behind the picture is, north when it faces south
static void wrap_span(uint8_t *row, int a, int b, uint8_t c) {
  for (int k = -1; k <= 1; k++)
    span(row, a + k * W, b + k * W, c);
}

// An ellipse, round the edges too, that shades out over soft rows at its rim: each pixel of BAYER's tile has a rim
// of its own
static void ellipse(uint8_t *row, int y, int cx, int cy, int rx, int ry, int soft, uint8_t c) {
  for (int k = 0; k < (soft ? 4 : 1); k++) {
    int out = (2 * BAYER[y & 3][k] - 15) * soft / 32, h = half(y - cy, rx + out * rx / ry, ry + out);
    if (h < 0)
      continue;
    if (!soft) {
      wrap_span(row, cx - h, cx + h, c);
      continue;
    }
    for (int x = cx - h + ((k - (cx - h)) & 3); x <= cx + h; x += 4)
      row[(x % W + W) % W] = c;
  }
}

// The stamps of a layer, but those left out for the animations
static void stamps(const Scene &s, int y, uint8_t *out, StampLayer layer) {
  for (int i = 0; i < s.nstamps; i++) {
    const Stamp &p = s.stamps->at[i];
    if (p.layer != layer || (p.tag & s.live) || y < p.y || y >= p.y + p.h)
      continue;
    uint64_t bits = p.rows[y - p.y];
    for (int c = 0; c < p.w; c++)
      if (bits >> (p.w - 1 - c) & 1) {
        int x = p.x + c;
        if (x >= 0 && x < W)
          out[x] = p.ix;
      }
  }
}

bool win_lit(const Scene &s, int i, int r, int k) {
  uint32_t h = (s.win_seed ^ (uint32_t) (i * 7919 + r * 131 + k * 17)) * 2246822519u;
  return s.window_lit && (h >> 13) % 3 == 0;
}

static void tree(const Scene &s, int i, int y, uint8_t *out) {
  const Tree &t = s.tree[i];
  int d = y - t.top;
  uint8_t c = s.ix[C_TREE + t.layer - 1];
  if (t.kind == 0) {
    Spruce spruce(t.base - t.top);
    int hw = spruce.hw(d);
    span(out, t.x - hw, t.x + hw, s.snow_trees && spruce.snow(d) ? s.ix[C_HILL + 2] : c);
    return;
  }
  Crown crown = crown_of(s, i);
  int wide = t.base - t.top > 24;  // a big one's trunk is two wide
  // The trunk from in under the crown, white with its bark's dark marks on one side, or dark against the snow
  if (d >= crown.rows * 2 / 3 || t.kind == 2)
    span(out, t.x, t.x + (d >= crown.rows ? wide : 0), s.ix[s.snow_trees ? C_DARK : C_WHITE]);
  if (d > crown.rows && d % 4 == 2)
    span(out, t.x + (d % 8 == 2 ? wide : 0), t.x + (d % 8 == 2 ? wide : 0), s.ix[C_DARK]);
  if (d < crown.rows && t.kind == 1) {  // a round crown
    int h = half(d - crown.rows / 2, crown.rx, crown.rows / 2);
    if (h >= 0)
      span(out, t.x - h, t.x + h, s.ix[C_BIRCH]);
  }
  // or bare: a twig up and out to each side from every third row, the lower ones longer
  for (int from = crown.rows - 1; t.kind == 2 && from >= d && from > 2; from -= 3)
    if (from - d <= 1 + from / 8)
      for (int x : {t.x - 1 - (from - d), t.x + 1 + (from - d)})
        span(out, x, x, s.ix[C_DARK]);
}

// The sky's bands, the glow around a low sun, the aurora, the stars, the moon and the sun
static void sky_row(const Scene &s, int y, uint8_t *out) {
  const uint8_t *ix = s.ix;
  for (int x = 0; x < W; x++)
    out[x] = ix[C_SKY + band_at(x, y)];
  if (y > HORIZON)
    return;
  if (s.glow) {
    Ellipse e = glow_of(s);
    ellipse(out, y, e.x, e.y, e.rx, e.ry, GLOW_SOFT, ix[C_GLOW]);
  }
  if (s.aurora)  // top colour, main colour, light lower edge
    for (int r = 0; r < 2; r++)
      for (int x = 0; x < W; x++) {
        int top = s.aur_top[r][x], bot = s.aur_bot[r][x];
        if (y < top || y >= bot)
          continue;
        out[x] = ix[aur_band(y, top, bot)];
      }
  for (int i = 0; i < s.nstars; i++)
    if (s.star_y[i] == y)
      out[s.star_x[i]] = ix[C_DIM];
  for (int i = 0; i < s.nbright; i++) {  // catalogue stars are 2x2, the brightest a round 4x4
    int dy = y - s.bright_y[i], x = s.bright_x[i];
    if (dy == 0 || dy == 1)
      span(out, x - (s.bright_big[i] ? 1 : 0), x + 1 + (s.bright_big[i] ? 1 : 0), ix[C_STAR]);
    else if (s.bright_big[i] && (dy == -1 || dy == 2))
      span(out, x, x + 1, ix[C_STAR]);
  }
  if (s.moon) {
    if (s.moon_glow)
      ellipse(out, y, s.moon_x, s.moon_y, MOON_GLOW_R, MOON_GLOW_R, MOON_SOFT, ix[C_MOON_GLOW]);
    int dy = y - s.moon_y, h = half(dy, MOON_R, MOON_R);
    if (h >= 0) {  // lit from the right while waxing, from the left after full
      float lim = cosf(s.moon_phase * 2 * kPiF) * MOON_R * sqrtf(1 - dy * dy / (float) (MOON_R * MOON_R));
      if (s.moon_phase < 0.5f)
        wrap_span(out, s.moon_x + std::max(-h, (int) floorf(lim) + 1), s.moon_x + h, ix[C_MOON]);
      else
        wrap_span(out, s.moon_x - h, s.moon_x + std::min(h, (int) ceilf(-lim) - 1), ix[C_MOON]);
    }
  }
  if (s.sun) {
    if (s.sun_halo)
      ellipse(out, y, s.sun_x, s.sun_y, HALO_R, HALO_R, HALO_SOFT, ix[C_HALO]);
    ellipse(out, y, s.sun_x, s.sun_y, SUN_R, SUN_R, 0, ix[C_SUN]);
  }
}

// The clouds. over: only those in front of the mountains, where the ridge or a peak is behind them. The sky has them
// already, with what flies there.
static void clouds_row(const Scene &s, int y, uint8_t *out, bool over = false) {
  const Land &land = *s.land;
  auto put = [&](int a, int b, uint8_t col) {
    if (!over)
      return span(out, a, b, col);
    for (int x = std::max(a, 0); x <= std::min(b, W - 1); x++)
      if (y >= std::min(land.ridge[x], land.hill[0][x]))
        out[x] = col;
  };
  for (int i = 0; i < s.nclouds; i++) {
    const Cloud &c = s.cloud[i];
    if (y < c.top || y > c.base || hidden(s, i) || (over && !shrouds(s, i)))
      continue;
    uint8_t col = s.ix[(y > c.shade ? C_SHADE : C_LIT) + s.cloud_layer[i]];
    int x0 = cloud_x(s, i);
    for (int j = 0; j < c.n; j++) {
      int cx = x0 + c.e[j].x, h = half(y - c.e[j].y, c.e[j].rx, c.e[j].ry);
      Wisp w = s.cloud_layer[i] ? Wisp{{(int16_t) -h}, {(int16_t) h}, h >= 0} : wisp(c.e[j], y - c.e[j].y);
      for (int k = 0; k < w.n; k++) {
        put(cx + w.a[k], cx + w.b[k], col);
        put(cx + w.a[k] - FIELD, cx + w.b[k] - FIELD, col);
      }
    }
  }
}

static void rainbow_row(const Scene &s, int y, uint8_t *out) {
  if (!s.rainbow || y >= HORIZON)
    return;
  for (int k = 0; k < 3; k++) {  // three bands, each 4 rows thick, red outermost
    int outer = half(y - s.rb_y, RB_RX - k * 4, RB_RY - k * 4),
        inner = half(y - s.rb_y, RB_RX - k * 4 - 4, RB_RY - k * 4 - 4);  // -1 below it
    if (outer < 0)
      continue;
    span(out, s.rb_x - outer, s.rb_x - inner - 1, s.ix[C_RB + k]);
    span(out, s.rb_x + inner + 1, s.rb_x + outer, s.ix[C_RB + k]);
  }
}

// Hill l, the far one with the mountains' ridge behind it and snow above the snow line
static void hill_row(const Scene &s, int y, uint8_t *out, int l) {
  const Land &land = *s.land;
  if (l == 0 && s.ridge)
    for (int x = 0; x < W; x++)
      if (y >= land.ridge[x])
        out[x] = s.ix[C_RIDGE];
  const uint8_t *top = land.hill[l];
  uint8_t c = s.ix[C_HILL + l];
  if (l == 0 && s.ground == G_MOUNTAINS) {
    for (int x = 0; x < W; x++)
      if (y >= top[x])
        out[x] = y < s.snowline + land.cap[x] ? s.ix[C_SNOWCAP] : c;
  } else {
    for (int x = 0; x < W; x++)
      if (y >= top[x])
        out[x] = c;
  }
}

// The water from the far hills to the middle ones, its waves and the reflection
static void sea_row(const Scene &s, int y, uint8_t *out) {
  if (!s.sea || y < s.sea_top)
    return;
  const uint8_t (*hill)[W] = s.land->hill;
  for (int x = 0; x < W; x++)
    if (y < hill[1][x] && (s.ground != G_COAST || y >= hill[0][x] || hill[0][x] == NONE))
      out[x] = s.ix[C_SEA];
  for (int i = 0; i < s.nwaves; i++)
    if (s.wave_y[i] == y && y < hill[1][s.wave_x[i]])
      span(out, s.wave_x[i], s.wave_x[i] + s.wave_w[i], s.ix[C_WAVE]);
  if (s.shine_x >= 0 && (y & 1) && (y / 2) % 3 != 2 && y < hill[1][s.shine_x]) {  // a broken path of light
    int spread = std::min(3, 1 + (y - s.sea_top) / 6);
    span(out, s.shine_x - spread, s.shine_x + spread, s.ix[C_WAVE]);
  }
}

static void fog_row(const Scene &s, int y, uint8_t *out, int l) {
  for (int x = 0; x < W; x++)
    if (y >= s.fog_top[l][x])
      out[x] = s.ix[C_FOG];
}

// The city, the far row first
static void city_row(const Scene &s, int y, uint8_t *out) {
  if (!s.nbld || y >= CITY_GROUND_Y)
    return;
  const uint8_t *ix = s.ix;
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < s.nbld; i++) {
      const Bld &b = s.bld[i];
      if ((b.back != 0) != (pass == 0))
        continue;
      int cx = b.x + b.w / 2, up = b.top - y;
      uint8_t c = ix[wall_of(b)], roof = ix[s.snow_trees ? C_HILL + 2 : C_DARK];
      if (up > 0) {  // what's on the roof
        if (up > ROOF_UP[b.kind])
          continue;
        if (b.kind == 1)
          span(out, cx - b.w / 6, cx + b.w / 6, c);
        else if (b.kind == 2)
          span(out, cx, cx, ix[C_BLD_EDGE]);
        else if (b.kind == 3)
          span(out, cx - (14 - up) * (b.w / 2) / 14, cx + (14 - up) * (b.w / 2) / 14, roof);
        else if (b.kind == 4)  // tiles or snow from the eaves in to the ridge
          span(out, b.x + up - 2, b.x + b.w + 1 - up, roof);
        for (int x = b.x; s.yule_lights && b.kind == 4 && up == 1 && x < b.x + b.w; x += 3)  // lights along the eaves
          span(out, x, x, ix[C_WINDOW]);
        continue;
      }
      span(out, b.x, b.x + b.w - 1, c);
      if (pass == 0)
        continue;
      out[std::min(W - 1, b.x + b.w - 1)] = ix[C_BLD_EDGE];
      int r = win_row(y, b.top);  // windows, some lit at night
      for (int k = 0, x; r >= 0 && (x = win_x(b, k)) >= 0; k++)
        span(out, x, x + 1, ix[win_lit(s, i, r, k) ? C_WINDOW : C_BLD_EDGE]);
    }
}

// The lighthouse tower with its band, then the lamp room and its roof
static void lighthouse_row(const Scene &s, int y, uint8_t *out) {
  if (!s.lighthouse)
    return;
  int cx = LH_X, by = s.lh_y;
  if (y > by - 25 && y <= by)
    span(out, cx - 3 - (y > by - 8 ? 1 : 0), cx + 3 + (y > by - 8 ? 1 : 0),
         s.ix[y > by - 15 && y <= by - 10 ? C_LH_BAND : C_LH]);
  else if (y > by - 30 && y <= by - 25)
    span(out, cx - 2, cx + 2, s.ix[C_DARK]);
  else if (y > by - 33 && y <= by - 30)
    span(out, cx - (y - (by - 33)), cx + (y - (by - 33)), s.ix[C_DARK]);
}

static void trees_row(const Scene &s, int y, uint8_t *out, int l) {
  for (int i = 0; i < s.ntrees; i++)
    if (s.tree[i].layer == l && y >= s.tree[i].top && y <= s.tree[i].base)
      tree(s, i, y, out);
}

// Row y of art, its rows split by '|' from x0, y0: each char of keys in its colour slot of slots, the rest left
static void art_row(const Scene &s, const char *art, const char *keys, std::initializer_list<uint8_t> slots, int x0,
                    int y0, int y, uint8_t *out) {
  for (int row = y0, x = x0; *art && row <= y; art++) {
    if (*art == '|') {
      row++;
      x = x0;
      continue;
    }
    if (const char *k = strchr(keys, *art); k && row == y && x >= 0 && x < W)
      out[x] = s.ix[slots.begin()[k - keys]];
    x++;
  }
}

// The cabin, falu red with white boards at its corners, gable and eaves, see CABIN_L. The wall that faces away from
// the sun is in the shade. Snow lies on its roof and chimney when it lies on the spruces.
static void cabin_row(const Scene &s, int y, uint8_t *out) {
  int r = s.cabin_y - y, k = r - CABIN_EAVES;  // rows above the foundation, and up the roof
  if (!s.cabin || r < 0 || r > CABIN_TOP)
    return;
  int l = s.cabin_x + CABIN_L, w = CABIN_R - CABIN_L, corner = CABIN_CORNER - CABIN_L, loft = corner / 2;
  int door = s.cabin_x + DOOR_X - l;
  uint8_t red = s.ix[C_CABIN], shade = s.ix[C_CABIN_SHADE], gable = s.gable_lit ? red : shade;
  uint8_t white = s.ix[C_WHITE], dark = s.ix[C_DARK], snow = s.ix[C_HILL + 2];
  uint8_t glass = s.window_lit ? s.ix[C_WINDOW] : dark;
  auto put = [&](int a, int b, uint8_t c) { span(out, l + a, l + b, c); };
  if (r == 0) {  // the foundation and the doorstep
    put(0, w, dark);
    put(door - 2, door + DOOR_W + 1, white);
  } else if (k < 0) {  // the walls and their corner boards
    put(0, corner, gable);
    put(corner, w, s.gable_lit ? shade : red);
    for (int x : {0, corner, w})
      put(x, x, white);
  } else if (k <= CABIN_RIDGE - CABIN_EAVES) {
    // the roof over the long wall, a row left for each row up, its lowest row the eaves' board
    put(corner + 2 - k, w + 2 - k, s.snow_trees ? (k ? snow : dark) : k ? dark : white);
    put(k + 1, corner - 1 - k, gable);  // the gable between its barge boards
    put(k - 1, k, white);
    put(corner - k, corner + 1 - k, white);
    for (int x = corner + 3; s.yule_lights && k == 0 && x <= w + 1; x += 4)  // the lights along the eaves,
      put(x, x, glass);
    if (s.yule_lights && k % 3 == 1) {  // and up the gable
      put(k, k, glass);
      put(corner - k, corner - k, glass);
    }
    if (k >= 2 && k <= 6)  // the loft's window
      put(loft - 2, loft + 2, white);
    if (k >= 3 && k <= 5)
      put(loft - 1, loft + 1, glass);
  }
  int chimney = s.cabin_x + CHIMNEY_X - l;
  if (r == CABIN_TOP)  // its cap
    put(chimney - 2, chimney + 3, s.snow_trees ? snow : dark);
  else if (r > CABIN_RIDGE - 5)
    put(chimney - 1, chimney + 2, red);
  // Each window's frame, then four panes, lit at night. Through Christmas the long wall's is dark but for the
  // advent candles' arch on its sill, seven lights.
  for (int x : {window_x(s), window_x(s) + WINDOW_2}) {
    int wy = window_y(s);
    uint8_t pane = s.at.yule && x == stake_x(s) ? dark : glass;
    if (y >= wy - 1 && y <= wy + 6)
      span(out, x - 1, x + 7, white);
    if (y >= wy && y < wy + 6 && y != wy + 2) {
      span(out, x, x + 2, pane);
      span(out, x + 4, x + 6, pane);
    }
  }
  if (s.at.yule)
    art_row(s, "...f|..fhf|.fhhhf|fhhhhhf|hhhhhhh", "fh", {C_WINDOW, C_DARK}, stake_x(s), stake_y(s), y, out);
  if (r >= 1 && r <= DOOR_H + 1)  // the door's frame, the door and its handle, and a lamp beside it
    put(door - 1, door + DOOR_W, white);
  if (r >= 1 && r <= DOOR_H)
    put(door, door + DOOR_W - 1, s.ix[C_TREE + 1]);  // as green as the spruces
  if (r == DOOR_H / 2)
    put(door + DOOR_W - 2, door + DOOR_W - 2, white);
  if (s.window_lit && (r == DOOR_H - 2 || r == DOOR_H - 3))
    put(door + DOOR_W + 2, door + DOOR_W + 2, s.ix[C_WINDOW]);
}

// What stands about the cabin in winter: the julenek, a sheaf of oats tied with a red ribbon on a pole, a snowman in
// a hat and a scarf, and a snow lantern, a cone of snowballs with a candle's light out between them
static void yard_row(const Scene &s, int y, uint8_t *out) {
  if (s.nek.x >= 0) {
    if (y >= s.nek.y + NEK_ROWS - 1 && y < s.nek.y + NEK_UP)
      out[s.nek.x] = s.ix[C_DARK];
    art_row(s, "..o.o.o|.oo.o.oo|o.ooooo.o|.ooooooo|..ooooo|...rrr|..ooooo|.ooooooo|.oo.o.oo", "or", {C_STRAW, C_CABIN},
            s.nek.x - 4, s.nek.y, y, out);
  }
  if (s.snowman.x >= 0)
    art_row(s, "..kkk|.kkkkk|..www|..kwk|..www|.rrrrr|.wwwws|wwwkwws|wwwwwws|wwwkwws|.sssss", "wksr",
            {C_WHITE, C_DARK, C_HILL, C_CABIN}, s.snowman.x - 3, s.snowman.y, y, out);
  if (s.lantern.x >= 0)
    art_row(s, "...s|..sgs|.sgsgs|sgsgsgs|sssssss", "sg", {C_HILL + 1, C_WINDOW}, s.lantern.x - 3, s.lantern.y, y, out);
}

// The city's road, see ROAD_FAR, the dashes gone under snow
static void road_row(const Scene &s, int y, uint8_t *out) {
  if (!s.nbld || y < ROAD_FAR || y > ROAD_NEAR)
    return;
  uint8_t kerb = s.ix[C_KERB];
  span(out, 0, W - 1, y == ROAD_FAR || y == ROAD_KERB || y == ROAD_NEAR ? kerb : s.ix[y > ROAD_KERB ? C_LANE : C_ROAD]);
  for (int x = 0; y == ROAD_MID && !s.snow_trees && x < W; x += 8)
    span(out, x, x + 3, kerb);
}

// Rain and snow from under the clouds
static void prec_row(const Scene &s, int y, uint8_t *out) {
  if (s.live & L_PREC)
    return;
  for (int i = 0; i < s.nprec; i++) {
    int dy = y - s.prec_y[i];
    if (dy < 0 || y < s.prec_top[s.prec_x[i]])
      continue;
    if (s.prec_kind[i] == 0) {  // a streak, 7 rows
      if (dy < 7) {
        int x = s.prec_x[i] + s.slant * dy / 8;
        if (x >= 0 && x < W)
          out[x] = s.ix[C_RAIN];
      }
    } else if (dy < s.prec_kind[i]) {
      span(out, s.prec_x[i], s.prec_x[i] + s.prec_kind[i] - 1, s.ix[C_SNOW]);
    }
  }
}

static void bolt_row(const Scene &s, int y, uint8_t *out) {
  if (!s.bolt || (s.live & L_BOLT))
    return;
  for (int i = 0; i < 6; i++)
    if (y >= s.bolt_y[i] && y < s.bolt_y[i + 1]) {
      int x = bolt_at(s.bolt_x, s.bolt_y, i, y);
      span(out, x, x + 1, s.ix[C_BOLT]);
    }
}

void row(const Scene &s, int y, uint8_t *out) {
  sky_row(s, y, out);
  clouds_row(s, y, out);
  rainbow_row(s, y, out);
  stamps(s, y, out, IN_SKY);
  hill_row(s, y, out, 0);
  clouds_row(s, y, out, true);
  sea_row(s, y, out);
  fog_row(s, y, out, 0);
  stamps(s, y, out, ON_WATER);
  hill_row(s, y, out, 1);
  bolt_row(s, y, out);  // behind the town and the land it strikes
  city_row(s, y, out);
  road_row(s, y, out);
  lighthouse_row(s, y, out);
  trees_row(s, y, out, 1);
  fog_row(s, y, out, 1);
  hill_row(s, y, out, 2);
  trees_row(s, y, out, 2);
  cabin_row(s, y, out);
  if (s.pole_x >= 0 && y >= s.pole_top && y <= s.pole_base)  // the flagpole
    span(out, s.pole_x, s.pole_x + 1, s.ix[C_WHITE]);
  yard_row(s, y, out);
  stamps(s, y, out, ON_LAND);
  prec_row(s, y, out);
  stamps(s, y, out, ON_TOP);
}

// ---------------------------------------------------------------- the 16 colours

uint64_t drawn(const Scene &s) {
  const Land &land = *s.land;
  uint8_t top[W];  // the first row the land or the fog covers in each column
  for (int x = 0; x < W; x++)
    top[x] = std::min({land.hill[0][x], land.hill[1][x], land.hill[2][x], s.ridge ? land.ridge[x] : NONE,
                       s.fog_top[0][x], s.fog_top[1][x]});
  // whether a thing from row y down in columns a..b, around the edges, isn't all behind them
  auto shows = [&](int a, int b, int y) {
    for (int x = a; x <= b; x++)
      if (top[(x % W + W) % W] > y)
        return true;
    return false;
  };
  uint64_t u = 1ull << C_SKY | 7ull << C_HILL;
  auto use = [&](int c, bool on) { u |= (uint64_t) on << c; };
  for (int b = 1; b < BANDS; b++)
    use(C_SKY + b, shows(0, W - 1, BAND[b] - SHADE));
  Ellipse glow = glow_of(s);
  use(C_GLOW, s.glow && shows(glow.x - glow.rx, glow.x + glow.rx, glow.y - glow.ry));
  use(C_SUN, s.sun && shows(s.sun_x - SUN_R, s.sun_x + SUN_R, s.sun_y - SUN_R));
  use(C_HALO, s.sun && s.sun_halo && shows(s.sun_x - HALO_R, s.sun_x + HALO_R, s.sun_y - HALO_R));
  use(C_MOON, s.moon && shows(s.moon_x - MOON_R, s.moon_x + MOON_R, s.moon_y - MOON_R));
  use(C_MOON_GLOW,
      s.moon && s.moon_glow && shows(s.moon_x - MOON_GLOW_R, s.moon_x + MOON_GLOW_R, s.moon_y - MOON_GLOW_R));
  use(C_STAR, s.nbright > 0);
  use(C_DIM, s.nstars > 0);
  for (int c = C_AUR; c <= C_AUR_EDGE; c++)
    use(c, s.aurora);
  for (int i = 0; i < s.nclouds; i++) {  // the ones on the screen
    const Cloud &c = s.cloud[i];
    if (hidden(s, i))
      continue;
    int l = s.cloud_layer[i], x0 = cloud_x(s, i);
    for (int j = 0; j < c.n; j++)
      for (int cx : {x0 + c.e[j].x, x0 + c.e[j].x - FIELD}) {
        int a = std::max(0, cx - c.e[j].rx), b = std::min(W - 1, cx + c.e[j].rx);
        use(C_LIT + l, a <= b && (shrouds(s, i) || shows(a, b, c.top)));
        use(C_SHADE + l, l > 0 && a <= b && (shrouds(s, i) || shows(a, b, c.shade + 1)));
      }
  }
  for (int k = 0; k < 3; k++)
    use(C_RB + k, s.rainbow);
  for (int i = 0; i < s.nprec; i++)
    use(s.prec_kind[i] ? C_SNOW : C_RAIN, !(s.live & L_PREC));
  use(C_BOLT, s.bolt && !(s.live & L_BOLT));
  use(C_FOG, s.fog_top[0][0] != NONE);
  use(C_SNOWCAP, s.ground == G_MOUNTAINS);
  use(C_RIDGE, s.ridge);
  use(C_SEA, s.sea);
  use(C_WAVE, s.sea);
  for (int i = 0; i < s.nbld; i++) {
    use(wall_of(s.bld[i]), true);
    use(C_BLD_EDGE, !s.bld[i].back);
    use(C_WINDOW, !s.bld[i].back && s.window_lit);
    use(C_DARK, s.bld[i].kind >= 3 && !s.snow_trees);
  }
  bool road = false;  // unless thick fog hides it
  for (int x = 0; x < W && s.nbld; x++)
    road |= s.fog_top[1][x] > ROAD_FAR;
  use(C_ROAD, road);
  use(C_KERB, road);
  use(C_LANE, road && !s.snow_trees);
  use(C_LH, s.lighthouse);
  use(C_LH_BAND, s.lighthouse);
  bool birch = false;
  for (int i = 0; i < s.ntrees; i++) {
    use(C_TREE + s.tree[i].layer - 1, s.tree[i].kind != 1);
    use(C_BIRCH, s.tree[i].kind == 1);
    birch |= s.tree[i].kind != 0;
  }
  use(C_DARK, s.lighthouse || s.cabin || birch);
  use(C_TREE + 1, s.cabin);
  use(C_CABIN, s.cabin);
  use(C_CABIN_SHADE, s.cabin);
  use(C_STRAW, s.nek.x >= 0);
  use(C_DARK, s.nek.x >= 0 || s.snowman.x >= 0);
  use(C_CABIN, s.nek.x >= 0 || s.snowman.x >= 0);          // the ribbon and the scarf
  use(C_WINDOW, (s.cabin && s.at.yule) || s.yule_lights);  // the candles' arch, and the lights along the eaves
  use(C_WHITE, s.cabin || birch || s.pole_x >= 0 || s.snowman.x >= 0);
  use(C_WINDOW, s.cabin && s.window_lit);
  return u;
}

picture::Parts parts_of(const Scene &s) {
  uint64_t u = drawn(s);
  auto ix = [&](uint64_t slots) {  // the palette's indexes of the slots in use
    uint16_t b = 0;
    for (int c = 0; c < COLOURS; c++)
      if ((slots & u) >> c & 1)
        b |= (uint16_t) (1u << s.ix[c]);
    return b;
  };
  auto slots = [](std::initializer_list<int> cs) {
    uint64_t m = 0;
    for (int c : cs)
      m |= 1ull << c;
    return m;
  };
  // The sky and the weather come before the hills and the forecast is over all. The rows each of them may be in,
  // the rest anywhere.
  int lo[COLOURS], hi[COLOURS];
  for (int c = 0; c < COLOURS; c++) {
    lo[c] = 0;
    hi[c] = H;
  }
  for (int b = 0; b < BANDS; b++) {
    lo[C_SKY + b] = b ? BAND[b] - SHADE : 0;
    hi[C_SKY + b] = b < BANDS - 1 ? BAND[b + 1] + SHADE : H;
  }
  for (int c = C_SUN; c <= C_SHADE + 2; c++) {  // the sun's, the moon's, the stars', the aurora's and the clouds'
    lo[c] = H;
    hi[c] = 0;
  }
  auto rows = [&](int c, int a, int b) {  // slot c is in rows a to b - 1 too
    lo[c] = std::min(lo[c], a);
    hi[c] = std::max(hi[c], b);
  };
  for (int c : {C_SUN, C_HALO})
    rows(c, s.sun_y - HALO_R - HALO_SOFT, s.sun_y + HALO_R + HALO_SOFT + 1);
  for (int c : {C_MOON, C_MOON_GLOW})
    rows(c, s.moon_y - MOON_GLOW_R - MOON_SOFT, s.moon_y + MOON_GLOW_R + MOON_SOFT + 1);
  for (int i = 0; i < s.nstars; i++)
    rows(C_DIM, s.star_y[i], s.star_y[i] + 1);
  for (int i = 0; i < s.nbright; i++)
    rows(C_STAR, s.bright_y[i] - 1, s.bright_y[i] + 2);
  for (int r = 0; r < 2 && s.aurora; r++)
    for (int x = 0; x < W; x++)
      if (s.aur_top[r][x] != NONE)
        for (int c = C_AUR; c <= C_AUR_EDGE; c++)
          rows(c, s.aur_top[r][x], s.aur_bot[r][x] + 1);
  for (int i = 0; i < s.nclouds; i++) {
    const Cloud &c = s.cloud[i];
    rows(C_LIT + s.cloud_layer[i], c.top, c.base + 1);
    rows(C_SHADE + s.cloud_layer[i], c.top, c.base + 1);
  }
  // the palette's indexes of the sky in rows y0 to y1 - 1
  auto sky = [&](int y0, int y1) {
    uint16_t b = 0;
    for (int c = 0; c < C_HILL; c++)
      if ((u >> c & 1) && lo[c] < y1 && hi[c] > y0)
        b |= (uint16_t) (1u << s.ix[c]);
    return b;
  };
  uint64_t ridge = slots({C_RIDGE, C_SNOWCAP}), far = slots({C_HILL}), sea = slots({C_SEA, C_WAVE});
  uint64_t mid = slots({C_HILL + 1, C_TREE, C_LH, C_LH_BAND, C_BLD, C_BLD_BACK, C_BLD_EDGE, C_ROAD, C_KERB, C_LANE});
  if (s.nbld)  // the town's painted walls and its roofs
    mid |= slots({C_CABIN, C_OCHRE, C_WHITE, C_DARK});
  // The near hill's trees, the cabin and the pole are in the near hill's colours, the spruces in its very colour
  uint64_t near = slots({C_HILL + 2, C_TREE + 1, C_DARK, C_BIRCH, C_CABIN, C_CABIN_SHADE, C_STRAW, C_WHITE, C_WINDOW});
  const Land &land = *s.land;
  int top[3] = {H, H, H}, ridge_top = H;
  for (int x = 0; x < W; x++) {
    ridge_top = std::min<int>(ridge_top, s.ridge ? land.ridge[x] : NONE);
    top[0] = std::min<int>(top[0], land.hill[0][x]);
    top[1] = std::min<int>(top[1], land.hill[1][x]);
    top[2] = std::min<int>(top[2], land.hill[2][x]);
  }
  for (int i = 0; i < s.ntrees; i++)
    top[s.tree[i].layer] = std::min<int>(top[s.tree[i].layer], s.tree[i].top);
  for (int i = 0; i < s.nbld; i++)
    top[1] = std::min<int>(top[1], s.bld[i].top);
  if (s.lighthouse)
    top[1] = std::min(top[1], s.lh_y - 32);
  if (s.cabin)
    top[2] = std::min(top[2], s.cabin_y - CABIN_TOP);
  if (s.pole_x >= 0)
    top[2] = std::min<int>(top[2], s.pole_top);
  picture::Parts p{};
  auto part = [&](int k, int x0, int y0, int x1, int y1, uint16_t colours, uint8_t depth) {
    colours &= (uint16_t) ~sky(y0, y1);
    for (int i = 0; i < s.nstamps; i++) {  // the sky's stamps in it, like birds, are behind it too
      const Stamp &q = s.stamps->at[i];
      if (q.layer == IN_SKY && !(q.tag & s.live) && q.x < x1 && q.x + q.w > x0 && q.y < y1 && q.y + q.h > y0)
        colours &= (uint16_t) ~(1u << q.ix);
    }
    if (colours && x0 < x1 && y0 < y1)
      p[k] = {(int16_t) x0, (int16_t) y0, (int16_t) x1, (int16_t) y1, colours, depth};
  };
  // The land's layers and the sea, each in front of what's behind it all the way across
  part(0, 0, ridge_top, W, H, ix(ridge), D_RIDGE);
  part(1, 0, top[0], W, H, ix(far) & ~ix(ridge), D_FAR);
  if (s.sea)
    part(2, 0, s.sea_top, W, H, ix(sea) & ~ix(ridge | far), D_SEA);
  part(3, 0, top[1], W, H, ix(mid) & ~ix(ridge | far | sea), D_MID);
  part(4, 0, top[2], W, H, ix(near) & ~ix(ridge | far | sea | mid), D_NEAR);
  // and in front of what walks on the near hill: the forest left of its edge, the cabin and the pole
  Edge e = forest_edge(s);
  if (e.x >= 0) {
    int l = e.x, t = e.top, b = e.y;
    for (int i : near_trees(s, 0))
      if (s.tree[i].x < e.x) {
        l = std::min(l, s.tree[i].x - Spruce(s.tree[i].base - s.tree[i].top).wide);
        t = std::min<int>(t, s.tree[i].top);
        b = std::max<int>(b, s.tree[i].base);
      }
    part(5, std::max(0, l), t, e.x, b + 1 + Edge::FLOOR, ix(near), D_FRONT);
  }
  if (s.cabin)
    part(6, s.cabin_x + EAVES_L, s.cabin_y - CABIN_TOP, s.cabin_x + EAVES_R + 1, s.cabin_y + 1, ix(near), D_FRONT);
  if (s.pole_x >= 0)
    part(7, s.pole_x, s.pole_top, s.pole_x + 2, s.pole_base + 1, ix(slots({C_WHITE})), D_FRONT);
  // and the flag or pennant when it's in the picture, in the colours it borrows from the rest: those only it has
  // where it flies
  int x0 = W, y0 = H, x1 = 0, y1 = 0;
  for (int i = 0; i < s.nstamps; i++) {
    const Stamp &q = s.stamps->at[i];
    if (q.tag == L_FLAG && !(s.live & L_FLAG)) {
      x0 = std::min<int>(x0, std::max<int>(q.x, 0));
      y0 = std::min<int>(y0, std::max<int>(q.y, 0));
      x1 = std::max(x1, std::min(q.x + q.w, W));
      y1 = std::max(y1, std::min(q.y + q.h, H));
    }
  }
  uint16_t in = 0, out = 0;
  for (int y = y0; y < y1; y++) {
    uint8_t px[W];
    row(s, y, px);
    for (int x = x0; x < x1; x++) {
      bool flag = false;
      for (int i = 0; i < s.nstamps && !flag; i++) {
        const Stamp &q = s.stamps->at[i];
        flag = q.tag == L_FLAG && px[x] == q.ix && y >= q.y && y < q.y + q.h && x >= q.x && x < q.x + q.w &&
               (q.rows[y - q.y] >> (q.w - 1 - (x - q.x)) & 1);
      }
      (flag ? in : out) |= (uint16_t) (1u << px[x]);
    }
  }
  if (in & ~out)
    p[8] = {(int16_t) x0, (int16_t) y0, (int16_t) x1, (int16_t) y1, (uint16_t) (in & ~out), D_FRONT};
  return p;
}

// Each colour slot and stamp to its nearest of pal's k colours
static void place(Scene &s, const uint32_t *pal, int k) {
  for (int i = 0; i < COLOURS; i++)
    s.ix[i] = (uint8_t) nearest(s.rgb[i], pal, k);
  for (int i = 0; i < s.nstamps; i++)
    s.stamps->at[i].ix = (uint8_t) nearest(s.stamps->at[i].c, pal, k);
}

void Palette::start(Scene &s) {
  n = 0;
  auto add = [&](uint32_t rgb, bool k) {
    int i = 0;
    while (i < n && c[i] != rgb)
      i++;
    if (i == n) {
      c[n] = rgb;
      used[n] = 0;
      key[n++] = k;
    }
    key[i] |= k;
  };
  uint64_t u = drawn(s);
  for (int i = 0; i < COLOURS; i++)
    if (u >> i & 1)
      add(s.rgb[i], KEY_COLOURS >> i & 1);
  for (int i = 0; i < s.nstamps; i++)
    if (!(s.stamps->at[i].tag & s.live))
      add(s.stamps->at[i].c, true);
  y = H;
  if (n > 16) {  // count the pixels of each, to leave out what's hidden and fold what's least missed
    place(s, c, n);
    y = 0;
  }
}

bool Palette::count(const Scene &s, int rows) {
  uint8_t r[W];
  for (int end = std::min(H, y + rows); y < end; y++) {
    row(s, y, r);
    for (int x = 0; x < W; x++)
      used[r[x]]++;
  }
  return y >= H;
}

void Palette::end(Scene &s, const uint32_t *prev) {
  s.folded = 0;
  if (n > 16) {
    for (int i = 0; i < n;)
      if (!used[i]) {
        n--;
        c[i] = c[n], key[i] = key[n], used[i] = used[n];
      } else {
        i++;
      }
    for (; n > 16; n--, s.folded++) {
      int fold = -1, into = 0;
      uint64_t cost = UINT64_MAX;
      for (int pass = 0; pass < 2 && fold < 0; pass++)
        for (int i = 0; i < n; i++)
          for (int j = 0; j < n; j++)
            if (j != i && (pass || !key[i]) && (uint64_t) used[i] * colour_dist(c[i], c[j]) < cost) {
              cost = (uint64_t) used[i] * colour_dist(c[i], c[j]);
              fold = i;
              into = j;
            }
      used[into] += used[fold];
      c[fold] = c[n - 1], key[fold] = key[n - 1], used[fold] = used[n - 1];
    }
  }
  bool have[16] = {}, placed[MAX] = {};
  for (int k = 0; prev && k < 16; k++)
    for (int i = 0; i < n && !have[k]; i++)
      if (!placed[i] && c[i] == prev[k]) {
        s.pal[k] = c[i];
        have[k] = placed[i] = true;
      }
  for (int i = 0, k = 0; i < n; i++)
    if (!placed[i]) {
      while (have[k])
        k++;
      s.pal[k] = c[i];
      have[k] = true;
    }
  for (int k = 0; k < 16; k++)
    if (!have[k])
      s.pal[k] = prev ? prev[k] : 0;
  place(s, s.pal, 16);
}

void palette(Scene &s, const uint32_t *prev) {
  Palette p;
  p.start(s);
  p.count(s, H);
  p.end(s, prev);
}

}  // namespace flexit::panel::met
