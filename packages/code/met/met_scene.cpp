// The weather scene for a place and a time, see met_scene.h
#include "met_scene.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace flexit::panel::met {

// The lamps' and windows' warm light, the same so they share one of the 16 colours. rgb4(255, 210, 122)
static constexpr uint32_t WARM_LIGHT = 0xFFCC77;

static bool has(const Weather &w, const char *s) { return strstr(w.symbol, s) != nullptr; }

// ---------------------------------------------------------------- the forecast over the picture

// 5x7 font. '^' is the degree sign.
struct Glyph {
  char c;
  uint8_t w;
  Bitmap<7> b;
};
// clang-format off: a glyph a line
static constexpr Glyph FONT[] = {
    {'0', 5, bitmap<7>(5, ".xxx.|x...x|x..xx|x.x.x|xx..x|x...x|.xxx.")},
    {'1', 5, bitmap<7>(5, "..x..|.xx..|..x..|..x..|..x..|..x..|.xxx.")},
    {'2', 5, bitmap<7>(5, ".xxx.|x...x|....x|...x.|..x..|.x...|xxxxx")},
    {'3', 5, bitmap<7>(5, "xxxxx|...x.|..x..|...x.|....x|x...x|.xxx.")},
    {'4', 5, bitmap<7>(5, "...x.|..xx.|.x.x.|x..x.|xxxxx|...x.|...x.")},
    {'5', 5, bitmap<7>(5, "xxxxx|x....|xxxx.|....x|....x|x...x|.xxx.")},
    {'6', 5, bitmap<7>(5, "..xx.|.x...|x....|xxxx.|x...x|x...x|.xxx.")},
    {'7', 5, bitmap<7>(5, "xxxxx|....x|...x.|..x..|.x...|.x...|.x...")},
    {'8', 5, bitmap<7>(5, ".xxx.|x...x|x...x|.xxx.|x...x|x...x|.xxx.")},
    {'9', 5, bitmap<7>(5, ".xxx.|x...x|x...x|.xxxx|....x|...x.|.xx..")},
    {'-', 4, bitmap<7>(4, "....|....|....|xxxx|....|....|....")},
    {'.', 2, bitmap<7>(2, "..|..|..|..|..|xx|xx")},
    {'^', 4, bitmap<7>(4, ".xx.|x..x|x..x|.xx.|....|....|....")},
    {'m', 5, bitmap<7>(5, ".....|.....|xx.x.|x.x.x|x.x.x|x.x.x|x.x.x")},
    {' ', 2, bitmap<7>(2, "..|..|..|..|..|..|..")},
};
// clang-format on

// 9x9 icons
static constexpr Bitmap<9> ICON_SUN = bitmap<9>(9, "....x....|"
                                                   ".x.....x.|"
                                                   "...xxx...|"
                                                   "..xxxxx..|"
                                                   "x.xxxxx.x|"
                                                   "..xxxxx..|"
                                                   "...xxx...|"
                                                   ".x.....x.|"
                                                   "....x....");
static constexpr Bitmap<9> ICON_MOON = bitmap<9>(9, "...xxxx..|"
                                                    "..xxxx...|"
                                                    ".xxxx....|"
                                                    ".xxx.....|"
                                                    ".xxx.....|"
                                                    ".xxx.....|"
                                                    ".xxxx....|"
                                                    "..xxxx...|"
                                                    "...xxxx..");
static constexpr Bitmap<9> ICON_CLOUD = bitmap<9>(9, ".........|"
                                                     ".........|"
                                                     "...xxx...|"
                                                     "..xxxxx..|"
                                                     ".xxxxxxx.|"
                                                     "xxxxxxxxx|"
                                                     "xxxxxxxxx|"
                                                     ".xxxxxxx.|"
                                                     ".........");
static constexpr Bitmap<9> ICON_RAIN = bitmap<9>(9, "...xxx...|"
                                                    "..xxxxx..|"
                                                    ".xxxxxxx.|"
                                                    "xxxxxxxxx|"
                                                    ".xxxxxxx.|"
                                                    ".........|"
                                                    ".x..x..x.|"
                                                    "x..x..x..|"
                                                    ".........");
static constexpr Bitmap<9> ICON_SNOW = bitmap<9>(9, "...xxx...|"
                                                    "..xxxxx..|"
                                                    ".xxxxxxx.|"
                                                    "xxxxxxxxx|"
                                                    ".xxxxxxx.|"
                                                    ".........|"
                                                    ".x..x..x.|"
                                                    ".........|"
                                                    "x..x..x..");
static constexpr Bitmap<9> ICON_SLEET = bitmap<9>(9, "...xxx...|"
                                                     "..xxxxx..|"
                                                     ".xxxxxxx.|"
                                                     "xxxxxxxxx|"
                                                     ".xxxxxxx.|"
                                                     ".........|"
                                                     ".x..x..x.|"
                                                     "x..x..x..|"
                                                     "..x..x..x");
static constexpr Bitmap<9> ICON_THUNDER = bitmap<9>(9, "...xxx...|"
                                                       "..xxxxx..|"
                                                       ".xxxxxxx.|"
                                                       "xxxxxxxxx|"
                                                       ".xxxxxxx.|"
                                                       "...xx....|"
                                                       "..xx.....|"
                                                       "...xx....|"
                                                       "..xx.....");
static constexpr Bitmap<9> ICON_FOG = bitmap<9>(9, ".........|"
                                                   "xxxxxxxx.|"
                                                   ".........|"
                                                   ".xxxxxxxx|"
                                                   ".........|"
                                                   "xxxxxxxx.|"
                                                   ".........|"
                                                   ".xxxxxxxx|"
                                                   ".........");
// Each Icon's bitmap, and for the partly ones a cloud in front of it
static const struct {
  const uint32_t *b, *cloud;
} ICONS[] = {{nullptr, nullptr},
             {ICON_SUN.row, nullptr},
             {ICON_MOON.row, nullptr},
             {ICON_SUN.row, ICON_CLOUD.row},
             {ICON_MOON.row, ICON_CLOUD.row},
             {ICON_CLOUD.row, nullptr},
             {ICON_RAIN.row, nullptr},
             {ICON_SNOW.row, nullptr},
             {ICON_SLEET.row, nullptr},
             {ICON_THUNDER.row, nullptr},
             {ICON_FOG.row, nullptr}};
static_assert(sizeof ICONS / sizeof *ICONS == I_FOG + 1, "a bitmap for each Icon");

static void ov_put(Scene &s, int x, int y, int v) {
  if (x < 0 || x >= W || y < 0 || y >= OV_H)
    return;
  uint8_t &b = s.ov[y][x / 4];
  b = (uint8_t) ((b & ~(3 << (x % 4 * 2))) | v << (x % 4 * 2));
}

// A bitmap of w-bit rows scaled k times. Pass 1 draws its outline, pass 2 the bitmap.
template<typename T> static void ov_bitmap(Scene &s, int x0, int y0, const T *rows, int w, int h, int k, int pass) {
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++) {
      if (!(rows[r] >> (w - 1 - c) & 1))
        continue;
      for (int dy = 0; dy < k; dy++)
        for (int dx = 0; dx < k; dx++) {
          int x = x0 + c * k + dx, y = y0 + r * k + dy;
          if (pass == 2)
            ov_put(s, x, y, 2);
          else
            for (int oy = -1; oy <= 1; oy++)
              for (int ox = -1; ox <= 1; ox++)
                ov_put(s, x + ox, y + oy, 1);
        }
    }
}

static int ov_text_width(const char *t, int k) {
  int w = 0;
  for (; *t; t++)
    for (const Glyph &g : FONT)
      if (g.c == *t)
        w += (g.w + 1) * k;
  return w > 0 ? w - k : 0;
}

static void ov_text(Scene &s, int x, int y, const char *t, int k) {
  for (int pass = 1; pass <= 2; pass++) {  // all outlines first, so they don't cut into the letters
    int cx = x;
    for (const char *p = t; *p; p++)
      for (const Glyph &g : FONT)
        if (g.c == *p) {
          ov_bitmap(s, cx, y, g.b.row, g.w, 7, k, pass);
          cx += (g.w + 1) * k;
        }
  }
}

static void ov_icon(Scene &s, int x, int y, Icon i, int k) {
  auto one = [&](const uint32_t *b, int dx, int dy) {
    for (int pass = 1; pass <= 2; pass++)
      ov_bitmap(s, x + dx * k, y + dy * k, b, 9, 9, k, pass);
  };
  if (ICONS[i].cloud) {  // up and left, the cloud down and right
    one(ICONS[i].b, -1, -2);
    one(ICONS[i].cloud, 2, 1);
  } else if (ICONS[i].b) {
    one(ICONS[i].b, 0, 0);
  }
}

int marks(const Weather &w, uint32_t t, Offset offset, int *at) {
  int k = 0;
  for (int i = 0; i < w.hours && k < 4; i++)
    if (w.hour[i].time > t && local_hour(w.hour[i].time, offset) % 3 == 0)
      at[k++] = i;
  return k;
}

// Now at top left, the next four 3-hour marks at top right
static void forecast(Scene &s, const Weather &w, uint32_t t, Offset offset) {
  char b[16];
  Hour now = now_of(w, t);
  ov_icon(s, 6, 7, now.icon != I_NONE ? now.icon : icon_of(w.symbol), 3);
  snprintf(b, sizeof b, "%d^", iround(now.temp));
  ov_text(s, 46, 6, b, 4);
  if (now.time == w.time && w.rain >= 0.1f) {  // the rain is only known for the first hour
    snprintf(b, sizeof b, "%.1f mm", w.rain);
    ov_text(s, 8, 42, b, 2);
  }
  int at[4], n = marks(w, t, offset, at);
  for (int col = 0; col < n; col++) {
    const Hour &h = w.hour[at[col]];
    int cx = W - 6 - (4 - col) * 40 + 20;  // middle of the column
    snprintf(b, sizeof b, "%02d", local_hour(h.time, offset));
    ov_text(s, cx - ov_text_width(b, 2) / 2, 6, b, 2);
    ov_icon(s, cx - 9, 24, h.icon, 2);
    snprintf(b, sizeof b, "%d^", iround(h.temp));
    ov_text(s, cx - ov_text_width(b, 2) / 2 + 2, 46, b, 2);
  }
}

// ---------------------------------------------------------------- sprites

// A sprite tagged with s.tag
static Sprite *sprite(Scene &s, int x, int y, int w, int h, int layer, uint32_t c) {
  if (s.nsprites >= SPRITES)
    return nullptr;
  Sprite &p = s.sprite[s.nsprites++];
  p = Sprite{};
  p.x = (int16_t) x;
  p.y = (int16_t) y;
  p.w = (uint8_t) (w > 32 ? 32 : w);
  p.h = (uint8_t) (h > 24 ? 24 : h);
  p.layer = (uint16_t) layer;
  p.tag = s.tag;
  p.c = c;
  return &p;
}

static void sprite_rows(Sprite *p, const uint32_t *rows) {
  if (p)
    for (int r = 0; r < p->h; r++)
      p->rows[r] = rows[r];
}

static void sprite_set(Sprite *p, int x, int y) {
  if (p && x >= 0 && x < p->w && y >= 0 && y < p->h)
    p->rows[y] |= 1u << (p->w - 1 - x);
}

// a filled disc as a sprite
static Sprite *blob(Scene &s, int cx, int cy, int rx, int ry, int layer, uint32_t c) {
  if (rx > 15 || ry > 11)
    return nullptr;
  Sprite *p = sprite(s, cx - rx, cy - ry, 2 * rx + 1, 2 * ry + 1, layer, c);
  if (!p)
    return nullptr;
  for (int dy = -ry; dy <= ry; dy++) {
    int h = half(dy, rx, ry);
    for (int dx = -h; dx <= h; dx++)
      sprite_set(p, dx + rx, dy + ry);
  }
  return p;
}

static Sprite *box(Scene &s, int x, int y, int w, int h, int layer, uint32_t c) {
  Sprite *p = sprite(s, x, y, w, h, layer, c);
  if (p)
    for (int r = 0; r < p->h; r++)
      p->rows[r] = p->w == 32 ? 0xFFFFFFFFu : (1u << p->w) - 1;
  return p;
}

// a firework of rays around a bright middle
static void burst(Scene &s, int cx, int cy, int r, uint32_t c, uint32_t heart) {
  Sprite *p = sprite(s, cx - r, cy - r, 2 * r + 1, 2 * r + 1, 0, c);
  for (int k = 0; k < 12; k++) {
    float a = k * 2 * kPiF / 12;
    for (int d = 3; d <= r; d++)
      if ((d + k) % 3 != 2)  // gaps so the rays look like they're burning out
        sprite_set(p, r + iround(d * cosf(a)), r + iround(d * sinf(a)));
  }
  Sprite *in = sprite(s, cx - 2, cy - 2, 5, 5, 0, heart);
  for (int d = -2; d <= 2; d++) {
    sprite_set(in, 2 + d, 2);
    sprite_set(in, 2, 2 + d);
  }
}

// The Norwegian flag, 22 by 16 in the official 6-1-2-1-12 by 6-1-2-1-6 proportions, on layer 2. Flies to the
// left of its pole when dir < 0, the cross still nearest the pole.
static void flag_norway(Scene &s, int x, int y, int dir, uint32_t red, uint32_t white, uint32_t blue) {
  Sprite *r = sprite(s, x, y, 22, 16, 2, red), *w = sprite(s, x, y, 22, 16, 2, white),
         *b = sprite(s, x, y, 22, 16, 2, blue);
  for (int row = 0; row < 16; row++)
    for (int col = 0; col < 22; col++) {
      bool vb = col == 7 || col == 8, vw = col == 6 || col == 9, hb = row == 7 || row == 8, hw = row == 6 || row == 9;
      sprite_set(vb || hb ? b : vw || hw ? w : r, dir < 0 ? 21 - col : col, row);
    }
}

// The vimpel, a red pennant 9 rows tall at the pole and len long, with a white-blue-white stripe. It narrows to 5
// rows, then the stripe ends and the red comes to a point. Points left when dir < 0.
static void pennant(Scene &s, int x, int y, int len, int dir, uint32_t red, uint32_t white, uint32_t blue) {
  Sprite *r = sprite(s, x, y, len, 9, 2, red), *w = sprite(s, x, y, len, 9, 2, white),
         *b = sprite(s, x, y, len, 9, 2, blue);
  for (int col = 0; col < len; col++) {
    int hi = col == len - 1 ? 0 : col == len - 2 ? 1 : (int) (4 - 2.0f * col / (len - 3) + 0.5f);  // half height
    for (int row = 4 - hi; row <= 4 + hi; row++) {
      int d = abs(row - 4);
      sprite_set(hi < 2 || d > 1 ? r : d ? w : b, dir < 0 ? len - 1 - col : col, row);
    }
  }
}

// The sprites' bitmaps
static constexpr Bitmap<3> BIRD = bitmap<3>(7, "x.....x|"
                                               ".xx.xx.|"
                                               "...x...");
static constexpr Bitmap<4> BAT = bitmap<4>(9, "xx.....xx|"
                                              "xxxx.xxxx|"
                                              ".xxxxxxx.|"
                                              "...x.x...");
static constexpr Bitmap<11> HEART = bitmap<11>(13, "..xxx...xxx..|"
                                                   ".xxxxx.xxxxx.|"
                                                   "xxxxxxxxxxxxx|"
                                                   "xxxxxxxxxxxxx|"
                                                   "xxxxxxxxxxxxx|"
                                                   ".xxxxxxxxxxx.|"
                                                   "..xxxxxxxxx..|"
                                                   "...xxxxxxx...|"
                                                   "....xxxxx....|"
                                                   ".....xxx.....|"
                                                   "......x......");
static constexpr Bitmap<20> FLAME = bitmap<20>(15, "......x........|"
                                                   "......x........|"
                                                   ".....xx........|"
                                                   ".....xxx.......|"
                                                   "....xxxx.......|"
                                                   "....xxxx.x.....|"
                                                   "...xxxxx.x.....|"
                                                   "...xxxxxxx.....|"
                                                   "..xxxxxxxx.....|"
                                                   "..xxxxxxxxx....|"
                                                   "..xxxxxxxxx....|"
                                                   ".xxxxxxxxxxx...|"
                                                   ".xxxxxxxxxxx...|"
                                                   ".xxxxxxxxxxxx..|"
                                                   "xxxxxxxxxxxxx..|"
                                                   "xxxxxxxxxxxxxx.|"
                                                   "xxxxxxxxxxxxxxx|"
                                                   "xxxxxxxxxxxxxxx|"
                                                   ".xxxxxxxxxxxxx.|"
                                                   "..xxxxxxxxxxx..");
static constexpr Bitmap<20> FLAME_IN = bitmap<20>(15, "...............|"
                                                      "...............|"
                                                      "...............|"
                                                      "...............|"
                                                      "...............|"
                                                      "...............|"
                                                      "......x........|"
                                                      "......x........|"
                                                      ".....xx........|"
                                                      ".....xxx.......|"
                                                      "....xxxx.......|"
                                                      "....xxxx.......|"
                                                      "....xxxxx......|"
                                                      "...xxxxxx......|"
                                                      "...xxxxxx......|"
                                                      "...xxxxxxx.....|"
                                                      "..xxxxxxxx.....|"
                                                      "..xxxxxxxx.....|"
                                                      "...xxxxxxx.....|"
                                                      "....xxxxx......");
static constexpr Bitmap<7> STAR7 = bitmap<7>(7, "...x...|"
                                                "..xxx..|"
                                                "xxxxxxx|"
                                                ".xxxxx.|"
                                                ".xxxxx.|"
                                                "xx...xx|"
                                                "x.....x");

// ---------------------------------------------------------------- the sky

// Zenith, horizon and glow colours for a sun elevation. The dark ones keep red and green equal, or at 4 bits
// per channel they come out teal or purple.
static void sky_colours(float el, uint32_t *top, uint32_t *bottom, uint32_t *glow) {
  // clang-format off: laid out by hand
  static const struct { float el; uint32_t top, bottom, glow; } K[] = {
      {-90, 0x000022, 0x222255, 0x222255}, {-18, 0x000022, 0x222255, 0x222255},
      {-12, 0x111133, 0x333366, 0x3C3668}, {-6, 0x16225A, 0x544680, 0xA85C7A},
      {-2, 0x22357A, 0x8E6488, 0xF0885A},  {2, 0x2A4994, 0xD88C6C, 0xFFAE60},
      {8, 0x3263B2, 0xEABC90, 0xFFD48E},   {16, 0x3277CC, 0xA6CEEC, 0xE6EEF0},
      {90, 0x2868C4, 0xB2D8F2, 0xFFFFFF}};
  // clang-format on
  int k = 0;
  while (k < 7 && el > K[k + 1].el)
    k++;
  float t = clamp01((el - K[k].el) / (K[k + 1].el - K[k].el));
  *top = mix(K[k].top, K[k + 1].top, t);
  *bottom = mix(K[k].bottom, K[k + 1].bottom, t);
  *glow = mix(K[k].glow, K[k + 1].glow, t);
}

static int sky_x(float az) { return (int) (az / 360 * W) % W; }  // north at the edges, south in the middle
static int sky_y(float el) { return HORIZON - (int) (el * (HORIZON - 26) / 55); }

// 0 winter with snow on the ground, 1 winter without, 2 spring, 3 summer, 4 autumn
static int season_of(int month, int day, bool white) {
  if (white)
    return 0;
  int k = month * 100 + day;
  if (k < 320 || k >= 1201)
    return 1;
  if (k < 601)
    return 2;
  if (k < 901)
    return 3;
  return 4;
}

// Official flag days from forskrift om bruk av statsflagget § 4 as month * 100 + day: New Year's Day, Princess
// Ingrid Alexandra, the Sami people's day, the King, 1 May, Liberation Day, Constitution Day, the union's
// dissolution, the Queen, the Crown Prince, Olsok, the Crown Princess and Christmas Day
static constexpr uint16_t FLAG_DAYS[] = {101, 121, 206, 221, 501, 508, 517, 607, 704, 720, 729, 819, 1225};

Now::Now(const Weather &weather, const Options &options, double la, double lo, uint32_t utc, int utc_offset)
    : w(weather), o(options), lat(la), lon(lo), t(utc), local(utc + (uint32_t) (utc_offset + 86400)) {
  sun = sun_at(lat, lon, t);
  sun_el = sun.el;
  moon = moon_at(lat, lon, t, &moon_phase);
  light = clamp01((sun.el + 6) / 18);
  snow = has(w, "snow") || has(w, "sleet");
  rain = has(w, "rain") || has(w, "sleet");
  if (w.ok && w.rain > 0.05f && !snow && !rain)
    (w.temp > 1 ? rain : snow) = true;
  thunder = has(w, "thunder") || w.thunder >= 30;
  fog = std::max(w.fog, has(w, "fog") ? 85.0f : 0.0f);
  cover = w.ok ? w.cloud : 0;
  grey = clamp01((cover - 45) / 50) * 0.9f + (rain || snow ? 0.1f : 0) + (thunder ? 0.15f : 0);
  glow = (1 - fabsf(sun.el - 1) / 11) * (1 - grey);
  dark = sun.el < -6;
  wind = w.wind;
  gust = w.gust;
  fair = sun.el > 3 && cover < 60 && !rain && !snow && !thunder && fog < 30 && wind < 12;
  fireplace = o.fireplace;
  // The picture faces south, east on the left. A wind from the east half blows things to the right.
  wdir = (int8_t) (w.wind_from > 180 && w.wind_from < 360 ? -1 : 1);
  hour = (uint8_t) (local % 86400 / 3600);
  int m, d;
  civil_from_days((int32_t) (local / 86400) - 1, &year, &m, &d);
  month = (uint8_t) m;
  day = (uint8_t) d;
  white =
      (w.ok && w.temp <= 0 && snow) || (w.ok && w.temp <= -3) || (w.ok && w.temp <= 1 && (month == 12 || month <= 2));
  season = (uint8_t) season_of(month, day, white);
  int doy = day_of_year(year, month, day), easter = easter_day_of_year(year);
  xmas = month == 12 && day >= 24 && day <= 26;
  newyear = (month == 12 && day == 31 && hour >= 18) || (month == 1 && day == 1 && hour < 3);
  easter_days = doy >= easter - 3 && doy <= easter + 1;
  midsummer = month == 6 && day == 23 && hour >= 19;
  halloween = month == 10 && day == 31;
  valentine = month == 2 && day == 14;
  weekday = (uint8_t) ((local / 86400 - 1 + 4) % 7);  // local has a day added
  may17 = month == 5 && day == 17;
  // FLAG_DAYS, Easter Sunday, Pentecost, and the Storting election on the second Monday of September every
  // fourth year
  flag_day = std::find(std::begin(FLAG_DAYS), std::end(FLAG_DAYS), month * 100 + day) != std::end(FLAG_DAYS) ||
             doy == easter || doy == easter + 49 ||
             (year % 4 == 1 && month == 9 && weekday == 1 && day >= 8 && day <= 14);
  // Up at 8 from March to October and at 9 in winter, down at sunset or by 21. From Nordland and north
  // it's 10 to 15 in winter, § 6. The sun has set once it's below the horizon on the west side.
  bool winter_months = month <= 2 || month >= 11, set = sun.el < -0.5f && sun.az > 180;
  flag_up = flag_day && (lat >= 65 && winter_months ? hour >= 10 && hour < 15
                                                    : hour >= (winter_months ? 9 : 8) && hour < 21 && !set);
}

// The sky's bands, the fog's colour and the glow around a low sun
static void sky(Scene &s, const Now &n) {
  uint32_t top, bottom, glow;
  sky_colours(n.sun.el, &top, &bottom, &glow);
  uint32_t grey_top = mix(0x14171F, n.thunder ? 0x5C626C : 0x8A94A2, n.light),
           grey_bottom = mix(0x20242C, n.thunder ? 0x7A808A : 0xBEC4CC, n.light);
  top = mix(top, grey_top, n.grey);
  bottom = mix(bottom, grey_bottom, n.grey);
  float darken = std::min(0.4f, (n.w.ok ? n.w.rain : 0) / 12);
  top = mix(top, 0x2A2F38, darken);
  bottom = mix(bottom, 0x4A505A, darken);
  s.rgb[C_FOG] = mix(mix(0x262C38, 0xC6CCD4, n.light), bottom, 0.3f);
  if (n.fog >= 60) {
    top = mix(top, s.rgb[C_FOG], (n.fog - 60) / 80);
    bottom = mix(bottom, s.rgb[C_FOG], (n.fog - 60) / 60);
  }
  static const float AT[5] = {0, 0.3f, 0.55f, 0.78f, 1};
  for (int i = 0; i < 5; i++)
    s.rgb[C_SKY + i] = mix(top, bottom, AT[i]);
  float g = n.glow;
  if (g > 0.15f) {
    s.glow = true;
    s.glow_x = (int16_t) sky_x(n.sun.az);
    s.glow_rx = (int16_t) (90 + 60 * g);
    s.glow_ry = (int16_t) (16 + 22 * g);
    s.rgb[C_GLOW] = mix(s.rgb[C_SKY + 4], glow, 0.3f + 0.45f * g);
    s.rgb[C_GLOW_OUT] = mix(s.rgb[C_SKY + 3], glow, 0.15f + 0.25f * g);
  }
}

static void sun_moon(Scene &s, const Now &n) {
  if (n.sun.el > -1) {
    s.sun = true;
    s.sun_x = (int16_t) sky_x(n.sun.az);
    s.sun_y = (int16_t) sky_y(n.sun.el);
    uint32_t behind = s.rgb[C_SKY + std::min(band_of(s.sun_y), 3)];
    if (n.grey > 0.75f) {
      s.rgb[C_SUN] = mix(behind, 0xFFFFFF, 0.3f);  // a pale disc through the clouds
    } else {
      s.rgb[C_SUN] = n.sun.el < 6 ? mix(0xFF9A50, 0xFFE0A0, n.sun.el / 6) : rgb4(255, 246, 210);
      s.sun_rings = true;
      s.rgb[C_RING1] = mix(behind, s.rgb[C_SUN], 0.42f);
      s.rgb[C_RING2] = mix(behind, s.rgb[C_SUN], 0.2f);
    }
  }
  if (n.moon.el > 1 && n.sun.el < 10) {
    s.moon = true;
    s.moon_x = (int16_t) sky_x(n.moon.az);
    s.moon_y = (int16_t) sky_y(n.moon.el);
    s.moon_phase = n.moon_phase;
    s.rgb[C_MOON] = mix(0xDCE6F2, 0xF4F6FA, 1 - n.light);
    // No glow round a new moon or a thin crescent, which show little or no disc
    if (n.sun.el < -6 && n.grey < 0.6f && n.moon_phase > 0.1f && n.moon_phase < 0.9f) {
      s.moon_glow = true;
      s.rgb[C_MOON_GLOW] = mix(s.rgb[C_SKY + std::min(band_of(s.moon_y), 2)], s.rgb[C_MOON], 0.16f);
    }
  }
}

// Northern lights, some clear nights up north
static void aurora(Scene &s, const Now &n) {
  if (n.o.aurora == A_OFF || n.sun.el >= -9 || n.cover >= 40 || n.fog >= 30)
    return;
  Rng a{(uint32_t) ((n.local - 43200) / 86400) * 2246822519u + 3};  // from noon to noon, the same all night
  if (n.o.aurora != A_ALWAYS && (n.lat < 55 || a.f() >= (n.lat >= 65 ? 0.6f : 0.35f)))
    return;
  s.aurora = true;
  s.moon_glow = false;  // the ribbons take three of the 16 colours
  float p0 = a.f() * 6.283f, p1 = a.f() * 6.283f, p2 = a.f() * 6.283f, lift = (float) ((n.t / 3600) % 7) * 2;
  int x1 = a.in(40, 120), x2 = a.in(W - 60, W + 80);  // where the lower ribbon starts and ends
  for (int x = 0; x < W; x++) {                       // ribbons thinning out towards their ends
    float mid = 44 + lift + 16 * sinf(x / 58.0f + p0) + 5 * sinf(x / 23.0f + p1);
    float th = 20 * (0.75f + 0.25f * sinf(x / 41.0f + p2)) * std::min(1.0f, 0.3f + 1.4f * sinf(kPi * x / W));
    s.aur_top[0][x] = (uint8_t) (int) (mid - th / 2);
    s.aur_bot[0][x] = (uint8_t) (int) (mid + th / 2);
    float mid2 = 92 + lift + 10 * sinf(x / 47.0f + p1 + 2) + 3 * sinf(x / 17.0f + p2);
    float env = x > x1 && x < x2 ? sinf(kPi * (x - x1) / (x2 - x1)) : 0;
    float th2 = 9 * std::min(1.0f, 1.6f * env);
    s.aur_top[1][x] = (uint8_t) (th2 >= 1 ? (int) (mid2 - th2 / 2) : NONE);
    s.aur_bot[1][x] = (uint8_t) (th2 >= 1 ? (int) (mid2 + th2 / 2) : NONE);
  }
  // mostly green under violet, some nights teal under pink, now and then green under red
  static const uint32_t HUES[3][3] = {
      {0x40B878, 0x7858A8, 0x98E0B8}, {0x38A8A0, 0xB05890, 0x90D8D0}, {0x58B860, 0xB04848, 0xA0E0A0}};
  const uint32_t *hue = HUES[a.f() < 0.6f ? 0 : a.f() < 0.6f ? 1 : 2];
  s.rgb[C_AUR] = mix(s.rgb[C_SKY], hue[0], 0.42f);
  s.rgb[C_AUR_TOP] = mix(s.rgb[C_SKY], hue[1], 0.4f);
  s.rgb[C_AUR_EDGE] = mix(s.rgb[C_SKY], hue[2], 0.5f);
}

// Faint random stars, and the catalogue ones where they really are
static void stars(Scene &s, const Now &n) {
  if (n.sun.el >= -7 || n.grey >= 0.85f)
    return;
  Rng r{0x5EED5u};
  s.nstars = (int) ((n.sun.el < -12 ? 90 : 40) * (1 - n.cover / 100));
  for (int i = 0; i < s.nstars; i++) {
    s.star_x[i] = (int16_t) r.in(0, W - 1);
    s.star_y[i] = (int16_t) r.in(2, 132);
  }
  s.rgb[C_STAR] = 0xFFFFFF;  // same white as the forecast text
  s.rgb[C_DIM] = s.aurora ? s.rgb[C_STAR] : mix(s.rgb[C_SKY + 1], 0xE8EEFA, 0.4f);
  Turn turn = turn_at(n.lat, n.lon, j2000(n.t));
  const float rad = (float) kRad;
  for (unsigned i = 0; i < std::size(STARS) && n.cover < 70; i++) {
    Pos p = horizontal(STARS[i].ra * 15 * rad, STARS[i].dec * rad, turn);
    if (p.el < 1 || p.el > 58)
      continue;
    s.bright_x[s.nbright] = (int16_t) sky_x(p.az);
    s.bright_y[s.nbright] = (int16_t) std::max(3, sky_y(p.el));
    s.bright_big[s.nbright++] = STARS[i].bright;
  }
}

// Clouds, seeded by the forecast hour so they don't jump about between redraws
static void clouds(Scene &s, const Now &n) {
  const Weather &w = n.w;
  float layer[3] = {w.cloud_high, w.cloud_mid, w.cloud_low};
  bool wet = n.rain || n.snow;
  uint32_t lit = mix(0x353C56, wet ? 0xC4CAD2 : 0xF6F8FB, n.light),
           shade = mix(0x232839, wet ? 0x8E96A2 : 0xC6CEDA, n.light);
  if (n.thunder) {
    lit = mix(lit, 0x5E646E, 0.6f);
    shade = mix(shade, 0x3A3F48, 0.6f);
  }
  if (s.glow) {  // lit from below at sunrise and sunset
    lit = mix(lit, 0xFFD0A4, 0.75f * n.glow);
    shade = mix(shade, 0x86668A, 0.6f * n.glow);
  }
  for (int l = 0; l < 3; l++) {
    s.rgb[C_LIT + l] = l == 0 ? mix(lit, s.rgb[C_SKY], 0.25f) : lit;
    s.rgb[C_SHADE + l] = l == 2 && n.grey > 0.3f ? mix(shade, 0x404650, 0.25f * n.grey) : shade;
  }
  Rng r{w.time * 2654435761u + 7};
  uint32_t into_hour = w.ok && n.t > w.time ? n.t - w.time : n.t % 3600;
  int made[3];  // clouds in each layer
  for (int l = 0; l < 3; l++) {
    // A layer moves a quarter of an hour's way at a time, each a third of that apart, so a redraw every five
    // minutes moves one layer and the rows only the others are in stay the same
    float minutes = floorf((into_hour / 60.0f + 5 * l) / 15) * 15;
    s.drift[l] = (int) (minutes * (0.3f + w.wind * 0.12f) * (l + 1)) % FIELD;
    int count = (int) (layer[l] / 100 * (l == 0 ? 5 : 7) + 0.5f), first = s.nclouds;
    for (int k = 0; k < count && s.nclouds < (int) std::size(s.cloud); k++) {
      Cloud &c = s.cloud[s.nclouds];
      s.cloud_layer[s.nclouds++] = (uint8_t) l;
      int width = l == 0 ? r.in(60, 120) : l == 1 ? r.in(48, 86) : r.in(70, 128);
      c.x = (int16_t) ((k * FIELD) / count + r.in(-20, 20) + FIELD) % FIELD;
      c.base = (int16_t) (l == 0 ? r.in(20, 64) : l == 1 ? r.in(62, 102) : r.in(100, 146));
      if (l == 0) {  // high ones are thin streaks
        c.n = (uint8_t) r.in(2, 3);
        for (int j = 0; j < c.n; j++)
          c.e[j] = {(int16_t) (width * (j + 1) / (c.n + 1) + r.in(-8, 8)), (int16_t) (c.base - r.in(0, 6)),
                    (int16_t) (width / (c.n + 1) + r.in(4, 14)), (int16_t) r.in(1, 3)};
        c.top = (int16_t) (c.base - 12);
        c.shade = 999;
        c.base += 4;
        continue;
      }
      c.n = (uint8_t) r.in(3, 5);
      int rmax = width / c.n + 4;
      c.top = c.base;
      for (int j = 0; j < c.n; j++) {
        int middle = 2 * j - (c.n - 1);  // bigger towards the middle, the ends still there in a narrow cloud
        int rad = std::max(4, rmax - abs(middle) * 3 + r.in(-3, 3));
        c.e[j] = {(int16_t) (width * (2 * j + 1) / (2 * c.n)), (int16_t) (c.base - rad * 6 / 10), (int16_t) rad,
                  (int16_t) rad};
        c.top = (int16_t) std::min<int>(c.top, c.e[j].y - rad);
      }
      c.shade = (int16_t) (c.base - (c.base - c.top) * 26 / 100);
    }
    made[l] = s.nclouds - first;
  }
  // white for small things and the cabin's trim, borrowed so it doesn't cost one of the 16 colours
  s.rgb[C_WHITE] = made[1] || made[2] ? s.rgb[C_LIT + 1]
                   : made[0]          ? s.rgb[C_LIT]
                   : s.sun            ? s.rgb[C_SUN]
                                      : s.rgb[C_LIT + 1];
}

// The rainbow, rain and snow from under the clouds, not through them, and the bolt from there
static void precipitation(Scene &s, const Now &n) {
  const Weather &w = n.w;
  if (n.rain && !n.snow && n.sun.el > 1 && n.sun.el < 42 && n.grey < 0.8f) {  // with the sun out and low enough
    s.rainbow = true;
    int x = sky_x(n.sun.az + 180);  // opposite the sun, kept whole on the screen
    s.rb_x = (int16_t) (x < RB_RX ? RB_RX : x > W - 1 - RB_RX ? W - 1 - RB_RX : x);
    s.rb_y = (int16_t) (HORIZON + n.sun.el * 1.6f);
    uint32_t behind = s.rgb[C_SKY + 2];
    s.rgb[C_RB] = mix(behind, 0xE05050, 0.55f);
    s.rgb[C_RB + 1] = mix(behind, 0xE8D850, 0.55f);
    s.rgb[C_RB + 2] = mix(behind, 0x6090E0, 0.55f);
    s.sun_rings = false;
  }
  if (n.rain || n.snow || n.thunder) {
    int deck = 0;                          // lowest base of the middle and low clouds, for the columns between them
    for (int i = 0; i < s.nclouds; i++) {  // under each cloud, from its own base
      const Cloud &c = s.cloud[i];
      if (s.cloud_layer[i] == 0)
        continue;  // not from the high thin ones
      deck = std::max<int>(deck, c.base);
      int x0 = cloud_x(s, i);
      for (int j = 0; j < c.n; j++)
        for (int k = -1; k <= 0; k++) {
          int cx = x0 + c.e[j].x + k * FIELD;
          for (int x = std::max(0, cx - c.e[j].rx); x <= std::min(W - 1, cx + c.e[j].rx); x++)
            s.prec_top[x] = (uint8_t) std::max<int>(s.prec_top[x], c.base + 2);
        }
    }
    for (int x = 0; x < W; x++)
      if (!s.prec_top[x])
        s.prec_top[x] = (uint8_t) (deck ? deck : 110);
  }
  if (n.rain || n.snow) {
    Rng p{w.time + 99};
    float amount = std::max(w.rain, has(w, "heavy") ? 4.0f : has(w, "light") ? 0.3f : 1.0f);
    s.nprec = (int) std::min((float) std::size(s.prec_x), 30 + amount * 45);
    float across = n.wdir * std::min(1.0f, w.wind / 9);
    s.slant = (int8_t) (across * 4);
    bool both = has(w, "sleet");
    for (int i = 0; i < s.nprec; i++) {
      int x = p.in(0, W - 1);
      s.prec_x[i] = (int16_t) x;
      s.prec_y[i] = (int16_t) p.in(s.prec_top[x], H - 1);
      s.prec_kind[i] = (uint8_t) (n.snow && (!both || i % 2) ? (p.in(0, 3) ? 1 : 2) : 0);
    }
    s.rgb[C_RAIN] = mix(0x4A5C78, 0x9EBAD6, n.light);
    s.rgb[C_SNOW] = mix(0xAAB4C6, 0xF6FAFF, n.light);
  }
  if (n.thunder) {
    Rng b{w.time + 5};
    s.bolt = true;
    int x = b.in(70, 250), top = s.prec_top[x] - 3,
        step = std::max(6, (HORIZON + 12 - top) / 6);  // out of the cloud, to the hills
    for (int i = 0; i < 7; i++) {
      s.bolt_x[i] = (int16_t) x;
      s.bolt_y[i] = (int16_t) (top + i * step);
      x += (i % 2 ? 1 : -1) * b.in(5, 13);
    }
    s.rgb[C_BOLT] = rgb4(255, 248, 196);
  }
}

// The hills of a landscape, from the place's phases
static void shape(Land &d, Ground g, const float *ph) {
  const Baseline &b = BASELINES[g];
  for (int x = 0; x < W; x++) {
    if (g == G_COAST) {  // a headland on the left and a point on the right
      d.hill[0][x] = (uint8_t) (x < 120 ? HORIZON - 4 - (int) (14 * sinf((120 - x) / 76.0f)) : NONE);
      int point = x > 236 && x < 300 ? 22 - abs(x - 268) / 3 : 0;
      d.hill[1][x] = (uint8_t) (int) (b.mid - point - 2 * sinf(x / 13.0f + ph[4]));
      d.hill[2][x] = (uint8_t) (int) (b.near - 5 * sinf(x / 53.0f + ph[5]) - 2 * sinf(x / 17.0f + ph[6]));
    } else if (g == G_MOUNTAINS) {  // peaks over a fjord, a ridge behind
      float tri = fabsf(fmodf(x / 58.0f + ph[0], 2.0f) - 1), tri2 = fabsf(fmodf(x / 23.0f + ph[1], 2.0f) - 1);
      float tri3 = fabsf(fmodf(x / 91.0f + ph[2], 2.0f) - 1), tri4 = fabsf(fmodf(x / 31.0f + ph[3], 2.0f) - 1);
      d.hill[0][x] = (uint8_t) (HORIZON - 20 - (int) (62 * tri) - (int) (10 * tri2));
      d.ridge[x] = (uint8_t) (HORIZON - 36 - (int) (60 * tri3) - (int) (8 * tri4));
      d.hill[1][x] = (uint8_t) (int) (b.mid - 2 * sinf(x / 11.0f + ph[4]));
      d.hill[2][x] = (uint8_t) (int) (b.near - 6 * sinf(x / 71.0f + ph[5]) - 3 * sinf(x / 29.0f + ph[6]));
      d.cap[x] = (int8_t) (4 * sinf(x / 9.0f));
    } else {  // the city's middle ground is flat
      d.hill[0][x] = (uint8_t) (int) (HORIZON - 6 - 7 * sinf(x / 61.0f + ph[0]) - 5 * sinf(x / 23.0f + ph[1]) -
                                      2 * sinf(x / 9.0f + ph[2]));
      d.hill[1][x] =
          (uint8_t) (g == G_CITY ? CITY_GROUND_Y
                                 : (int) (b.mid - 11 * sinf(x / 47.0f + ph[3]) - 6 * sinf(x / 19.0f + ph[4])));
      d.hill[2][x] = (uint8_t) (int) (b.near - 8 * sinf(x / 71.0f + ph[5]) - 4 * sinf(x / 29.0f + ph[6]));
    }
  }
}

// Sea to a rocky shore, a lighthouse on the point, and a sailboat off the coast on a fine day
static void coast(Scene &s, const Now &n, Rng &h) {
  s.sea = true;
  s.sea_top = HORIZON + 1;
  s.lighthouse = true;  // 32 tall
  s.lh_y = (int16_t) (s.land->hill[1][LH_X] + 2);
  s.rgb[C_LH] = mix(s.rgb[C_SKY + 4], 0xFFFFFF, n.light * 0.5f + 0.3f);
  s.rgb[C_LH_BAND] = mix(mix(0x2A1418, 0x9A3426, n.light), s.rgb[C_SKY + 4], n.fog / 200);
  if (n.dark && n.o.frame % 2 == 0)  // the lamp, lit every other redraw
    box(s, LH_X - 1, s.lh_y - 29, 3, 3, 2, WARM_LIGHT);
  for (int k = 0; k < 2; k++) {  // skerries
    int x = k ? h.in(140, 215) : h.in(20, 90);
    blob(s, x, h.in(170, 178), h.in(4, 7), h.in(1, 2), 1, s.rgb[C_HILL + 1]);
  }
  int bx = h.in(120, 225), by = h.in(168, 176);  // in any weather, so the trees after it stay put
  s.boat = {(int16_t) bx, (int16_t) by};
  if (n.sun.el > 0 && n.wind < 10 && !n.rain && !n.snow && !n.thunder) {
    s.tag = L_BOAT;
    box(s, bx - 5, by - 2, 11, 3, 1, s.rgb[C_HILL + 1]);
    box(s, bx, by - 16, 1, 14, 1, s.rgb[C_HILL + 1]);
    Sprite *sail = sprite(s, bx + 1, by - 15, 8, 12, 1, s.rgb[C_LH]);  // white as the lighthouse and the waves
    for (int r = 0; r < 12; r++)
      for (int c = 0; c < r * 8 / 12; c++)
        sprite_set(sail, c, r);
    s.tag = 0;
  }
}

// Snow-capped peaks over a fjord, a hazy ridge behind
static void mountains(Scene &s, const Now &n) {
  s.sea = true;
  s.sea_top = 160;
  s.ridge = true;
  s.snowline = (int16_t) (HORIZON - (n.white ? 20 : n.season == 3 ? 62 : 48));
  s.rgb[C_SNOWCAP] = mix(mix(0x33445A, 0xF0F4F8, n.light), s.rgb[C_SKY + 3], 0.25f);
  s.rgb[C_HILL] = mix(mix(0x1C2838, 0x5A6C7E, n.light), s.rgb[C_SKY + 4], 0.25f);
  s.rgb[C_RIDGE] = mix(s.rgb[C_HILL], s.rgb[C_SKY + 3], 0.55f);
}

// A skyline on the middle ground, a park in front
static void city(Scene &s, const Now &n, Rng &h) {
  s.cabin = false;
  // a far row with gaps and a near row, where one becomes a tall antenna tower and one gets a spire
  for (int back = 1; back >= 0; back--) {
    int x = back ? h.in(10, 40) : 2;
    while (x < W - 8 && s.nbld < (int) std::size(s.bld)) {
      Bld &b = s.bld[s.nbld++];
      b.back = (uint8_t) back;
      b.w = (int16_t) (back ? h.in(20, 44) : h.in(16, 38));
      b.top = (int16_t) (back ? h.in(96, 136) : h.in(120, 160));
      b.kind = (uint8_t) (back ? 0 : h.in(0, 3) == 0 ? 1 : 0);
      b.x = (int16_t) x;
      x += b.w + (back ? h.in(8, 36) : h.in(2, 6));
    }
  }
  int tower = -1, church = -1;
  for (int i = 0; i < s.nbld; i++) {
    const Bld &b = s.bld[i];
    if (b.back)
      continue;
    if (tower < 0 || abs(b.x + b.w / 2 - 170) < abs(s.bld[tower].x + s.bld[tower].w / 2 - 170))
      tower = i;
    if (church < 0 || abs(b.x - 50) < abs(s.bld[church].x - 50))
      church = i;
  }
  if (church >= 0 && church != tower) {
    s.bld[church].top = 146;
    s.bld[church].kind = 3;
  }
  s.rgb[C_BLD] = mix(mix(0x1A1E2C, 0x5E6678, n.light), s.rgb[C_SKY + 4], 0.2f);
  s.rgb[C_BLD_BACK] = mix(s.rgb[C_BLD], s.rgb[C_SKY + 4], 0.45f);
  s.rgb[C_BLD_EDGE] = mix(s.rgb[C_BLD], 0x000000, 0.35f);
  s.win_seed = (uint32_t) (n.local / 1800) * 2654435761u;
  if (tower >= 0) {
    Bld &b = s.bld[tower];
    b.top = 86;
    b.kind = 2;
    s.antenna = {(int16_t) (b.x + b.w / 2 - 1), 86 - 14};
    if (n.dark && n.o.frame % 2 == 0) {  // red light, every other redraw
      s.tag = L_ANTENNA;
      box(s, s.antenna.x, s.antenna.y, 3, 2, 2, rgb4(255, 48, 48));
      s.tag = 0;
    }
  }
  if (n.dark) {  // a street lamp in the park
    Sprite *lamp = sprite(s, 60, 180, 5, 3, 2, WARM_LIGHT);
    if (lamp) {
      lamp->rows[0] = 0x0E;
      lamp->rows[1] = 0x1F;
      lamp->rows[2] = 0x0E;
    }
  }
}

// The sea's waves and the sun's or moon's reflection
static void sea(Scene &s, const Now &n) {
  const Weather &w = n.w;
  s.rgb[C_SEA] = mix(mix(0x0C1C30, 0x2C5C8C, n.light), s.rgb[C_SKY + 4], 0.35f);
  s.rgb[C_WAVE] = n.o.ground == G_MOUNTAINS ? s.rgb[C_SNOWCAP] : s.rgb[C_LH];
  Rng wv{(uint32_t) (n.t / 3600) * 7919u + 11};  // new waves each hour
  s.nwaves = std::min((int) std::size(s.wave_x), 6 + (int) (w.wind * 5));
  for (int i = 0; i < s.nwaves; i++) {
    s.wave_x[i] = (int16_t) wv.in(0, W - 1);
    s.wave_y[i] = (int16_t) wv.in(s.sea_top + 2, 182);
    s.wave_w[i] = (int16_t) wv.in(2, 3 + (int) w.wind);
  }
  if (s.moon && n.dark)
    s.shine_x = s.moon_x;
  else if (s.sun && n.sun.el < 20 && n.grey < 0.5f)
    s.shine_x = s.sun_x;
}

// The hills' colours for the season, light and fog, and the trees' on them
static void hill_colours(Scene &s, const Now &n) {
  // far, middle, near hills for each season_of()
  static const uint32_t HILLS[5][3] = {{0xA4B8CE, 0xC8D6E4, 0xE0EAF2},
                                       {0x6C8CA8, 0x4C5C50, 0x303C34},
                                       {0x6C8CA8, 0x6E9A58, 0x3E7040},
                                       {0x6C8CA8, 0x486C60, 0x284434},
                                       {0x6C8CA8, 0x7A6236, 0x5A4420}};
  static const uint32_t BIRCH[5] = {0x8C9CAE, 0x8C9CAE, 0x9AD070, 0x6AA850, 0xE0B030};
  uint32_t night_c[3] = {0x223344u, 0x112233u, n.white ? 0x334455u : 0x111122u};
  for (int l = 0; l < 3; l++) {
    uint32_t &c = s.rgb[C_HILL + l];
    c = mix(night_c[l], HILLS[n.season][l], n.light);
    if (l == 0)
      c = mix(c, s.rgb[C_SKY + 4], 0.3f);
    if (n.fog > 15)
      c = mix(c, s.rgb[C_FOG], n.fog / 100 * (0.5f - l * 0.15f));
    if (l > 0)
      s.rgb[C_TREE + l - 1] = c;
  }
  if (n.white) {  // dark spruces with snow on them, against the white
    s.snow_trees = true;
    s.rgb[C_TREE] = s.rgb[C_TREE + 1] = mix(0x18302A, 0x2E5240, n.light);
  }
  s.rgb[C_BIRCH] = mix(mix(0x223344, BIRCH[n.season], n.light), s.rgb[C_HILL + 2], n.fog / 200);
}

// Spruces and a few birches, the first nfront of them big in front and the rest small on the middle hills
static void trees(Scene &s, const Now &n, Rng &h, int count, int nfront) {
  const uint8_t (*hill)[W] = s.land->hill;
  for (int i = 0; i < count; i++) {
    bool front = i < nfront;
    int x = h.in(0, W - 1), l = front ? 2 : 1;
    if (front && x > s.home_x - 36 && x < s.home_x + 44)  // keep a clearing around home
      x = (x + W / 2) % W;
    Tree &t = s.tree[s.ntrees++];
    t.x = (int16_t) x;
    t.base = (int16_t) (hill[l][x] + 3);
    t.top = (int16_t) (t.base - (front ? h.in(18, 40) : h.in(8, 16)));
    t.layer = (uint8_t) l;
    t.kind = (uint8_t) (i % 5 == 3 ? (n.season <= 1 ? 2 : 1) : 0);  // every fifth a birch, bare in winter
  }
}

// Fog over the far hills, and over the middle ones when it's thick. ph waves its edge.
static void fog_edges(Scene &s, const Now &n, float ph) {
  for (int f = 0; f < 2; f++)
    for (int x = 0; x < W; x++) {
      bool on = f == 0 ? n.fog > 15 : n.fog > 45;
      int edge = f == 0 ? HORIZON - 4 - (int) (n.fog * 0.1f) : CITY_GROUND_Y - (int) (n.fog * 0.12f);
      s.fog_top[f][x] = (uint8_t) (on ? edge + (int) (3 * sinf(x / 17.0f + ph + f)) : NONE);
    }
}

// The hills, the landscape, the cabin or the park, the trees and the fog. Seeded by the location, so it's always
// the same landscape there.
static void ground(Scene &s, const Now &n) {
  uint32_t seed = (uint32_t) (int32_t) (n.lat * 1000) * 31u + (uint32_t) (int32_t) (n.lon * 1000) + 1;
  Rng h{seed};
  float ph[8];
  for (float &v : ph)
    v = h.f() * 6.283f;
  Land &land = *s.land;
  if (!land.made || land.seed != seed || land.ground != n.o.ground) {
    shape(land, n.o.ground, ph);
    land.seed = seed;
    land.ground = n.o.ground;
    land.made = true;
  }
  hill_colours(s, n);
  s.cabin = true;
  s.cabin_x = (int16_t) (CABIN_X + h.in(-8, 8));
  int ntrees = (int) std::size(s.tree), nfront = 11;
  if (n.o.ground == G_COAST) {
    coast(s, n, h);
    ntrees = nfront = 5;
  } else if (n.o.ground == G_MOUNTAINS) {
    mountains(s, n);
    ntrees = 14;
    nfront = 9;
  } else if (n.o.ground == G_CITY) {
    city(s, n, h);
    ntrees = nfront = 6;
  }
  if (s.sea)
    sea(s, n);
  s.cabin_y = (int16_t) (land.hill[2][s.cabin_x] + 6);
  // flagpole by the cabin, or in the city's park on flag days
  int park = h.in(90, 130);
  s.pole_x = (int16_t) (s.cabin ? s.cabin_x + 24 : n.flag_day ? park : -1);  // the park's pole stands all day
  s.pole_base = (int16_t) (s.cabin ? s.cabin_y - 2 : land.hill[2][park] + 4);
  s.pole_top = (int16_t) (s.pole_base - 48);
  s.home_x = (int16_t) (s.cabin ? s.cabin_x : park - 24);
  trees(s, n, h, ntrees, nfront);
  s.window_lit = n.sun.el < -3 && ((n.hour >= 6 && n.hour < 23) || n.xmas || n.newyear);  // asleep after eleven
  s.rgb[C_WINDOW] = n.halloween ? rgb4(255, 140, 40) : WARM_LIGHT;
  s.rgb[C_CABIN] = mix(mix(0x2A1418, 0x9A3426, n.light), s.rgb[C_HILL + 2], n.fog / 200);  // falu red
  fog_edges(s, n, ph[7]);
}

// Birds, bats, the chimney's smoke and the flagpole
static void life(Scene &s, const Now &n, Rng &rng) {
  const Weather &w = n.w;
  if (n.fair && !n.halloween) {
    s.tag = L_BIRDS;
    int count = rng.in(2, 5), bx = rng.in(20, W - 60), by = rng.in(40, 100);
    for (int i = 0; i < count; i++) {
      int x = bx + i * 11 + rng.in(-2, 2), y = by + (i % 2 ? 4 : 0) + rng.in(-1, 1);
      sprite_rows(sprite(s, x, y, 7, 3, 0, s.rgb[C_TREE]), BIRD.row);
    }
  }
  if (n.halloween && n.dark) {  // bats, around the moon if it's up, else low where the sky is lighter
    s.tag = L_BATS;
    int bx = s.moon ? s.moon_x - 40 : rng.in(40, W - 100), by = s.moon ? s.moon_y - 16 : rng.in(70, 110);
    for (int i = 0; i < 7; i++) {
      int x = bx + rng.in(-10, 90), y = by + rng.in(-20, 36);
      sprite_rows(sprite(s, x, y, 9, 4, 0, rgb4(0, 0, 0)), BAT.row);
    }
  }
  uint32_t white = s.rgb[C_WHITE];
  // Chimney smoke while the heater's on, more in fireplace mode, and hearts on Valentine's day. Layer 1, since
  // the cabin stands on the near hill.
  int puffs = s.cabin ? n.o.fireplace ? 5 : n.o.heater_pct > 0 ? 2 + std::min(3, n.o.heater_pct / 30) : 0 : 0;
  if (n.valentine && !puffs)
    puffs = 3;
  if (n.midsummer)
    puffs = 0;
  s.puffs = (uint8_t) puffs;
  int cx = s.cabin ? s.cabin_x + 9 : s.home_x, cy = s.cabin ? s.cabin_y - 32 : s.pole_base - 10;
  s.chimney = {(int16_t) cx, (int16_t) cy};
  s.tag = L_SMOKE;
  for (int i = 0; i < puffs; i++) {
    int r = 3 + i, px = cx + n.wdir * (i * i + 1) * (1 + (int) w.wind / 4) + (int) (rng.in(-1, 1)),
        py = cy - 6 - i * 10;
    if (n.valentine && (i >= puffs - 2 || !s.cabin))
      sprite_rows(sprite(s, px - 6, py - 5, 13, 11, 1, mix(0xE04070, 0xFF80B0, n.light)), HEART.row);
    else
      blob(s, px, py, r, std::max(2, r - 1), 1, white);
  }
  // The pennant, or the flag on flag days. Blue from the top of the sky and white, colours we have anyway.
  // Only the flag's red is extra.
  uint32_t blue = s.rgb[C_SKY];
  int fx = s.pole_x, fy = s.pole_top;
  s.tag = L_FLAG;
  if (n.flag_up && s.pole_x >= 0) {
    flag_norway(s, n.wdir < 0 ? fx - 23 : fx + 2, fy, n.wdir, mix(0x601418, 0xEF2B2D, n.light), white, blue);
  } else if (s.cabin) {
    int len = std::min(PENNANT_MAX, 18 + 2 * (int) w.wind);
    pennant(s, n.wdir < 0 ? fx - len - 1 : fx + 2, fy, len, n.wdir, s.rgb[C_CABIN], white, blue);
  }
  s.tag = 0;
}

// Lights on the spruce nearest home, on either side, and a star on top
static void xmas_lights(Scene &s) {
  int best = -1;
  for (int i : near_trees(s, 0))
    if (best < 0 || abs(s.tree[i].x - s.home_x) < abs(s.tree[best].x - s.home_x))
      best = i;
  if (best < 0)
    return;
  const Tree &t = s.tree[best];
  uint32_t window = s.rgb[C_WINDOW], cols[3] = {rgb4(255, 60, 60), window, rgb4(60, 220, 100)};
  int count = (int) std::size(s.light);
  Rng lt{77};
  s.tag = L_XMAS;
  for (int k = 0; k < count; k++) {
    int d = 7 + (t.base - t.top - 9) * k / count, hw = spruce_hw(d);
    int out = lt.in(hw / 3, hw), lx = k % 2 ? t.x + out - 1 : t.x - out;
    s.light[k] = {(int16_t) lx, (int16_t) (t.top + d), (uint8_t) (k % 3)};
    box(s, lx, t.top + d, 2, 2, 2, cols[k % 3]);
  }
  s.nlights = (uint8_t) count;
  s.star = {(int16_t) (t.x - 3), (int16_t) (t.top - 5)};
  sprite_rows(sprite(s, s.star.x, s.star.y, 7, 7, 2, window), STAR7.row);
  s.tag = 0;
}

// Fireworks, somewhere new every redraw
static void fireworks(Scene &s, const Now &n) {
  Rng fw = seeded(n.o.frame);
  uint32_t cols[3] = {rgb4(255, 100, 170), rgb4(100, 220, 255), rgb4(255, 230, 120)};
  s.tag = L_FIREWORKS;
  for (int i = 0; i < 4; i++)
    burst(s, fw.in(30, W - 30), fw.in(24, 100), fw.in(9, 12), cols[(i + fw.in(0, 1)) % 3],
          s.nstars ? s.rgb[C_STAR] : 0xFFFFFF);
  s.tag = 0;
}

// Striped eggs in the grass in front of home
static void eggs(Scene &s, const Now &n) {
  uint32_t cols[3] = {s.rgb[C_WINDOW], rgb4(230, 100, 170), rgb4(100, 190, 230)};
  Rng eg{(uint32_t) n.year * 7u};
  for (int i = 0; i < (int) std::size(s.egg); i++) {
    int x = (s.home_x - 84 + i * 19 + eg.in(-3, 3) + W) % W, y = s.land->hill[2][x] + 12 + eg.in(0, 8);
    s.egg[s.neggs++] = {(int16_t) x, (int16_t) y};
    blob(s, x, y, 4, 5, 1, cols[i % 3]);
    Sprite *stripes = sprite(s, x - 3, y - 2, 7, 4, 1, s.rgb[C_WHITE]);  // both in one, sprites are scarce at Easter
    if (stripes)
      stripes->rows[0] = stripes->rows[3] = 0x7F;
  }
}

// The bonfire on the near hill away from home: its logs, its flames, sparks and smoke
static void bonfire(Scene &s, const Now &n, Rng &rng) {
  int bx = (s.home_x + W / 2) % W, by = s.land->hill[2][bx] + 14;
  s.fire = {(int16_t) bx, (int16_t) by};
  box(s, bx - 9, by - 2, 19, 3, 2, s.rgb[C_DARK]);
  s.tag = L_BONFIRE;
  sprite_rows(sprite(s, bx - 7, by - 21, 15, 20, 2, rgb4(255, 130, 30)), FLAME.row);
  sprite_rows(sprite(s, bx - 7, by - 21, 15, 20, 2, rgb4(255, 220, 90)), FLAME_IN.row);
  for (int i = 0; i < 10; i++) {
    int x = bx + rng.in(-12, 12), y = by - 24 - rng.in(0, 26);
    box(s, x, y, 1, 1, 2, rgb4(255, 220, 90));
  }
  blob(s, bx + n.wdir * 10, by - 36, 6, 4, 1, s.rgb[C_HILL]);  // smoke in the far hills' blue-grey
  blob(s, bx + n.wdir * 18, by - 46, 8, 5, 1, s.rgb[C_HILL]);
  s.tag = 0;
}

// A pumpkin by the door, or by the pole in the park, its face lit after dark
static void pumpkin(Scene &s, const Now &n) {
  int px = s.cabin ? s.cabin_x - 22 : s.home_x + 36, py = (s.cabin ? s.cabin_y : s.pole_base) - 5;
  s.pumpkin = {(int16_t) px, (int16_t) py};
  blob(s, px, py, 7, 6, 1, rgb4(255, 130, 40));
  box(s, px - 1, py - 8, 3, 3, 1, s.rgb[C_DARK]);  // its stalk
  if (n.dark)
    sprite_rows(sprite(s, px - 5, py - 3, 11, 7, 2, rgb4(17, 17, 34)), FACE.row);
}

// What the days bring to the picture
static void holidays(Scene &s, const Now &n, Rng &rng) {
  if (n.xmas && n.dark)
    xmas_lights(s);
  if (n.newyear && n.dark)
    fireworks(s, n);
  if (n.easter_days && !n.white)
    eggs(s, n);
  if (n.midsummer)
    bonfire(s, n, rng);
  if (n.halloween)
    pumpkin(s, n);
}

// The forecast over the picture, in white with an outline
static void overlay(Scene &s, const Now &n, Offset offset) {
  s.overlay = true;
  memset(s.ov, 0, OV_H * sizeof *s.ov);
  s.rgb[C_TEXT] = rgb4(255, 255, 255);
  // outline in the darkest colour already in the picture, if it's dark enough
  uint32_t cand[] = {s.rgb[C_SKY], s.rgb[C_HILL], s.rgb[C_HILL + 1], s.rgb[C_HILL + 2], s.rgb[C_SHADE + 2]};
  auto luma = [](uint32_t c) { return (int) ((c >> 16) * 3 + (c >> 8 & 255) * 6 + (c & 255)) / 10; };
  uint32_t &out = s.rgb[C_OUTLINE] = cand[0];
  for (uint32_t c : cand)
    if (luma(c) < luma(out))
      out = c;
  if (luma(out) > 70)
    out = rgb4(17, 17, 34);
  forecast(s, n.w, n.t, offset);
}

void build(Scene &s, const Weather &w, double lat, double lon, uint32_t t, bool with_forecast, Offset offset,
           const Options &o) {
  Sprite *sprite = s.sprite;
  uint8_t (*ov)[W / 4] = s.ov;
  Land *land = s.land;
  memset(&s, 0, sizeof s);
  s.sprite = sprite;
  s.ov = ov;
  s.land = land;
  s.ground = o.ground;
  s.shine_x = -1;
  s.chimney = s.fire = s.pumpkin = s.boat = s.antenna = s.star = {-1, -1};
  Now n(w, o, lat, lon, t, offset(t));
  sky(s, n);
  sun_moon(s, n);
  aurora(s, n);
  stars(s, n);
  clouds(s, n);
  precipitation(s, n);
  ground(s, n);
  Rng rng{(uint32_t) (n.local / 3600) * 1664525u + 1013904223u};  // for life() and holidays(), new each hour
  life(s, n, rng);
  holidays(s, n, rng);
  s.at = n;
  if (with_forecast && w.ok && s.ov)
    overlay(s, n, offset);
}

}  // namespace flexit::panel::met
