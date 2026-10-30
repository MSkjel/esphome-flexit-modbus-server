// Drawing the weather scene a row at a time, and its 16 colours, see met_raster.h
#include "met_raster.h"
#include <cstring>

namespace flexit::panel::met {

static int ov_get(const Scene &s, int x, int y) { return s.ov[y][x / 4] >> (x % 4 * 2) & 3; }

// cloud i is left out for the animations
static bool hidden(const Scene &s, int i) { return s.live & (s.cloud_layer[i] ? L_CLOUDS : L_CIRRUS); }

static void span(uint8_t *row, int a, int b, uint8_t c) {
  if (a < 0)
    a = 0;
  if (b > W - 1)
    b = W - 1;
  if (a <= b)
    memset(row + a, c, (size_t) (b - a + 1));
}

// span() at both edges too, where north is
static void wrap_span(uint8_t *row, int a, int b, uint8_t c) {
  for (int k = -1; k <= 1; k++)
    span(row, a + k * W, b + k * W, c);
}

static void disc(uint8_t *row, int y, int cx, int cy, int r, uint8_t c) {
  int h = half(y - cy, r, r);
  if (h >= 0)
    wrap_span(row, cx - h, cx + h, c);
}

// The sprites of a layer, but those left out for the animations
static void sprites(const Scene &s, int y, uint8_t *out, int layer) {
  for (int i = 0; i < s.nsprites; i++) {
    const Sprite &p = s.sprite[i];
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
  if (t.kind == 0) {  // spruce
    int hw = spruce_hw(d);
    bool snowy = s.snow_trees && snow_tier(d);
    span(out, t.x - hw, t.x + hw, snowy ? s.ix[C_HILL + 2] : c);
    return;
  }
  Crown crown = crown_of(s, i);
  if (d < crown.rows && t.kind == 1) {  // a round crown
    int h = half(d - crown.rows / 2, crown.rx, crown.rows / 2);
    if (h >= 0)
      span(out, t.x - h, t.x + h, s.ix[C_BIRCH]);
  } else if (d < crown.rows) {  // bare, a few twigs
    if (d % 4 == 0)
      span(out, t.x - 1 - d / 5, t.x + 1 + d / 5, c);
  }
  if (d >= crown.rows / 3)
    span(out, t.x, t.x + (t.base - t.top > 24 ? 1 : 0), c);  // the trunk
}

// The sky's bands, the glow around a low sun, the aurora, the stars, the moon and the sun
static void sky_row(const Scene &s, int y, uint8_t *out) {
  const uint8_t *ix = s.ix;
  span(out, 0, W - 1, ix[C_SKY + band_of(y)]);
  if (y > HORIZON)
    return;
  if (s.glow)
    for (int strong = 0; strong < 2; strong++) {  // the wide faint one, then the smaller strong one
      Ellipse e = glow_of(s, strong);
      int h = half(y - e.y, e.rx, e.ry);
      if (h >= 0)
        wrap_span(out, e.x - h, e.x + h, ix[strong ? C_GLOW : C_GLOW_OUT]);
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
      disc(out, y, s.moon_x, s.moon_y, MOON_GLOW_R, ix[C_MOON_GLOW]);
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
    if (s.sun_rings) {
      disc(out, y, s.sun_x, s.sun_y, RING2_R, ix[C_RING2]);
      disc(out, y, s.sun_x, s.sun_y, RING1_R, ix[C_RING1]);
    }
    disc(out, y, s.sun_x, s.sun_y, SUN_R, ix[C_SUN]);
  }
}

static void clouds_row(const Scene &s, int y, uint8_t *out) {
  for (int i = 0; i < s.nclouds; i++) {
    const Cloud &c = s.cloud[i];
    if (y < c.top || y > c.base || hidden(s, i))
      continue;
    uint8_t col = s.ix[(y > c.shade ? C_SHADE : C_LIT) + s.cloud_layer[i]];
    int x0 = cloud_x(s, i);
    for (int j = 0; j < c.n; j++) {
      int h = half(y - c.e[j].y, c.e[j].rx, c.e[j].ry);
      if (h < 0)
        continue;
      int cx = x0 + c.e[j].x;
      span(out, cx - h, cx + h, col);
      span(out, cx - h - FIELD, cx + h - FIELD, col);
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
  if (s.shine_x >= 0 && (y & 1) && y < hill[1][s.shine_x]) {
    int spread = 1 + (y - s.sea_top) / 6;
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
      int cx = b.x + b.w / 2;
      uint8_t c = ix[pass == 0 ? C_BLD_BACK : C_BLD];
      if (y < b.top) {  // what's on the roof
        if (b.kind == 1 && y >= b.top - 5)
          span(out, cx - b.w / 6, cx + b.w / 6, c);
        else if (b.kind == 2 && y >= b.top - 12)
          span(out, cx, cx, ix[C_BLD_EDGE]);
        else if (b.kind == 3 && y >= b.top - 14)
          span(out, cx - (y - b.top + 14) * (b.w / 2) / 14, cx + (y - b.top + 14) * (b.w / 2) / 14, c);
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

// The cabin, falu red with white trim
static void cabin_row(const Scene &s, int y, uint8_t *out) {
  if (!s.cabin)
    return;
  int cx = s.cabin_x, r = s.cabin_y - y;  // r: rows above the floor
  uint8_t white = s.ix[C_WHITE], dark = s.ix[C_DARK];
  if (r >= 0 && r < 16) {
    span(out, cx - 13, cx + 13, s.ix[C_CABIN]);
    span(out, cx - 13, cx - 13, white);
    span(out, cx + 13, cx + 13, white);
  }
  if (r >= 16 && r < 26) {  // the roof in the near hill's colour, so it's turf or snow, with white barge boards
    int hw = 16 - (r - 16) * 16 / 10;
    span(out, cx - hw, cx + hw, s.ix[C_HILL + 2]);
    span(out, cx - hw, cx - hw + 1, white);
    span(out, cx + hw - 1, cx + hw, white);
  }
  if (r >= 22 && r < 32)  // the chimney
    span(out, cx + 7, cx + 10, dark);
  int wx = window_x(s), wy = window_y(s);  // the window frame, then four panes, lit at night
  if (y >= wy - 1 && y <= wy + 6)
    span(out, wx - 1, wx + 7, white);
  if (y >= wy && y < wy + 6 && y != wy + 2) {
    uint8_t glass = s.window_lit ? s.ix[C_WINDOW] : dark;
    span(out, wx, wx + 2, glass);
    span(out, wx + 4, wx + 6, glass);
  }
  if (r >= 0 && r < 12)  // the door and its frame
    span(out, cx + 2, cx + 8, white);
  if (r >= 0 && r < 11)
    span(out, cx + 3, cx + 7, dark);
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
  sprites(s, y, out, 0);
  hill_row(s, y, out, 0);
  sea_row(s, y, out);
  fog_row(s, y, out, 0);
  hill_row(s, y, out, 1);
  city_row(s, y, out);
  lighthouse_row(s, y, out);
  trees_row(s, y, out, 1);
  fog_row(s, y, out, 1);
  hill_row(s, y, out, 2);
  trees_row(s, y, out, 2);
  cabin_row(s, y, out);
  if (s.pole_x >= 0 && y >= s.pole_top && y <= s.pole_base)  // the flagpole
    span(out, s.pole_x, s.pole_x + 1, s.ix[C_DARK]);
  sprites(s, y, out, 1);
  prec_row(s, y, out);
  bolt_row(s, y, out);
  sprites(s, y, out, 2);
  if (s.overlay && y < OV_H)  // the forecast
    for (int x = 0; x < W; x++) {
      int v = ov_get(s, x, y);
      if (v)
        out[x] = s.ix[v == 2 ? C_TEXT : C_OUTLINE];
    }
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
  for (int b = 1; b < 5; b++)
    use(C_SKY + b, shows(0, W - 1, BAND[b]));
  for (int strong = 0; strong < 2; strong++) {
    Ellipse e = glow_of(s, strong);
    use(strong ? C_GLOW : C_GLOW_OUT, s.glow && shows(e.x - e.rx, e.x + e.rx, e.y - e.ry));
  }
  for (int c : {C_SUN, C_RING1, C_RING2}) {
    int r = c == C_SUN ? SUN_R : c == C_RING1 ? RING1_R : RING2_R;
    use(c, s.sun && (c == C_SUN || s.sun_rings) && shows(s.sun_x - r, s.sun_x + r, s.sun_y - r));
  }
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
        use(C_LIT + l, a <= b && shows(a, b, c.top));
        use(C_SHADE + l, l > 0 && a <= b && shows(a, b, c.shade + 1));
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
    use(s.bld[i].back ? C_BLD_BACK : C_BLD, true);
    use(C_BLD_EDGE, !s.bld[i].back);
    use(C_WINDOW, !s.bld[i].back && s.window_lit);
  }
  use(C_LH, s.lighthouse);
  use(C_LH_BAND, s.lighthouse);
  use(C_DARK, s.lighthouse || s.cabin || s.pole_x >= 0);
  for (int i = 0; i < s.ntrees; i++) {
    use(C_TREE + s.tree[i].layer - 1, true);
    use(C_BIRCH, s.tree[i].kind == 1);
  }
  use(C_CABIN, s.cabin);
  use(C_WHITE, s.cabin);
  use(C_WINDOW, s.cabin && s.window_lit);
  use(C_TEXT, s.overlay);
  use(C_OUTLINE, s.overlay);
  return u;
}

void palette(Scene &s, const uint32_t *prev) {
  static constexpr int MAX = COLOURS + SPRITES;
  uint32_t c[MAX], used[MAX] = {};
  bool key[MAX];
  int n = 0;
  auto add = [&](uint32_t rgb, bool k) {
    int i = 0;
    while (i < n && c[i] != rgb)
      i++;
    if (i == n) {
      c[n] = rgb;
      key[n++] = k;
    }
    key[i] |= k;
  };
  uint64_t u = drawn(s);
  for (int i = 0; i < COLOURS; i++)
    if (u >> i & 1)
      add(s.rgb[i], KEY_COLOURS >> i & 1);
  for (int i = 0; i < s.nsprites; i++)
    if (!(s.sprite[i].tag & s.live))
      add(s.sprite[i].c, true);
  auto place = [&](const uint32_t *pal, int k) {
    for (int i = 0; i < COLOURS; i++)
      s.ix[i] = (uint8_t) nearest(s.rgb[i], pal, k);
    for (int i = 0; i < s.nsprites; i++)
      s.sprite[i].ix = (uint8_t) nearest(s.sprite[i].c, pal, k);
  };
  s.folded = 0;
  if (n > 16) {  // count the pixels of each, to leave out what's hidden and fold what's least missed
    place(c, n);
    uint8_t r[W];
    for (int y = 0; y < H; y++) {
      row(s, y, r);
      for (int x = 0; x < W; x++)
        used[r[x]]++;
    }
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
  place(s.pal, 16);
}

}  // namespace flexit::panel::met
