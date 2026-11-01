// The weather animations' sprites, see met_sprites.h
#include "met_sprites.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace flexit::panel::met::live {

// ---------------------------------------------------------------- drawing

// x, y pairs in colour col
static void dots(const Canvas &c, std::initializer_list<int8_t> xy, uint32_t col) {
  for (const int8_t *p = xy.begin(); p < xy.end(); p += 2)
    c.put(p[0], p[1], col);
}
static void line(const Canvas &c, int x0, int y0, int x1, int y1, uint32_t col) {
  int n = std::max(abs(x1 - x0), abs(y1 - y0));
  for (int i = 0; i <= n; i++)
    c.put(n ? x0 + iround((float) (x1 - x0) * i / n) : x0, n ? y0 + iround((float) (y1 - y0) * i / n) : y0, col);
}
// Rows of art split by '|' from ox, oy: each char of keys in its colour of cols, anything else left empty
static void rows(const Canvas &c, const char *art, const char *keys, std::initializer_list<uint32_t> cols, int ox = 0,
                 int oy = 0) {
  for (int x = ox, y = oy; *art; art++, x++) {
    if (*art == '|') {
      x = ox - 1, y++;
      continue;
    }
    if (const char *k = strchr(keys, *art))
      c.put(x, y, cols.begin()[k - keys]);
  }
}
// 0..99, the same for the same pixel, for ragged edges and thinning out
static int noise(int x, int y, int k) {
  uint32_t h = ((uint32_t) x * 374761393u + (uint32_t) y * 668265263u + (uint32_t) k * 2246822519u) * 3266489917u;
  return (int) ((h ^ h >> 15) % 100);
}

// ---------------------------------------------------------------- shapes the sprites share

// A gull from the front, k: 0 up, 1 upmid, 2 m, 3 m2, 4 flat, 5 down
static void gull_pose(const Canvas &c, int k, int ox, int oy) {
  static const char *const LOOK[] = {"k...........k|.g.........g|..ll.....ll|....lwwwl|......wy",
                                     "|k...........k|.gll.....llg|....lwwwl|......wy",
                                     "|..lll...lll|.g...l.l...g|k....www....k|......wy",
                                     "|...ll...ll|.gl..l.l..lg|k....www....k|......wy",
                                     "|||kggllwwwllggk|......wy",
                                     "|||....lwwwl|..ll..wy.ll|.g.........g|k...........k"};
  rows(c, LOOK[k], "kglwy", {0x333344, 0xAAAABB, 0xDDDDEE, 0xFFFFFF, 0xFFBB33}, ox, oy);
}
// k: 0 full, 1 luffing, 2 heeled a little, 3 luffing hard as it starts to turn. flip mirrors it
static void boat_pose(const Canvas &c, int k, bool flip) {
  auto put = [&](int x, int y, uint32_t col) { c.put(flip ? 12 - x : x, y, col); };
  int heel = k == 2;
  for (int x = 0; x <= 12; x++)
    put(x, 12, 0x773333);
  for (int x = 1; x <= 11; x++)
    put(x, 13, 0x662222);
  for (int x = 2; x <= 10; x++)
    put(x, 14, 0x662222);
  for (int y = 1; y <= 11; y++)
    put(6 + (heel && y < 5), y, 0x444433);
  for (int r = 2; r <= 11; r++) {  // the main, aft of the mast
    int full = (k == 1 || k == 3) && r % 3 == 0 ? 0 : 1, belly = k == 0 && r >= 5 && r <= 8;
    int len = (r - 1) / 2 + belly - (k == 3);
    for (int x = 7; x <= 7 + len * full + !full; x++)
      put(x + (heel && r < 5), r, r == 3 && k != 1 ? 0xDDDDDD : 0xEEEEEE);
  }
  for (int r = 5; r <= 11; r++)  // the jib
    for (int x = 5 - (r - 4) / 2 + (k == 3 && r % 2); x <= 5; x++)
      put(x, r, 0xCCCCDD);
  put(6 + heel, 0, 0x444433);
}
static void sparkle(const Canvas &c, int k) {
  if (!k)
    return;
  c.put(2, 2, 0xFFFFFF);
  dots(c, {1, 2, 3, 2, 2, 1, 2, 3}, k == 1 ? 0xAAAABB : 0xEEEEFF);
  if (k == 2)
    dots(c, {0, 2, 4, 2, 2, 0, 2, 4}, 0x9999BB);
}
static void leaf_pose(const Canvas &c, int k) {
  static const char *const LOOK[] = {"..xx|.xxxx|xxvxxx|.xxxx|..xx|...s",
                                     "|..xx|.xxvx|..xx|..s",
                                     "||xxxxxs",
                                     "|..uu|.uuvu|..uu|...s",
                                     "..uu|.uuuu|uuvuuu|.uuuu|..uu|..s",
                                     "|.uu|.uuvu|..uuu|....s",
                                     "||.sxxxx",
                                     "|...xx|.xvxx|.xx|.s",
                                     "|||.xxxx|xxvxxs|.xxx"};
  rows(c, LOOK[k], "xuvs", {0xEECC33, 0xDDBB44, 0xCCAA22, 0x887755});
}
// A hooded crow flying right, from the side, its wings k: 0 up, 1 level, 2 down with their fingers spread. Grey,
// its head, heavy bill, wings and tail black. legs: hanging, to land
static void crow_flying(const Canvas &c, int k, int ox, int oy, bool legs) {
  static const char *const WING[] = {"k.k.k|.kkkk|..kkkk", "||kkkkkk", "|||||.kkkk|.kkkk|k.k.k"};
  rows(c, "|||.kkggggkkk|kkkggggkkkk", "kg", {0x16161C, 0xA4A6AC}, ox, oy);
  rows(c, WING[k], "k", {0x16161C}, ox + 1, oy);
  if (legs)
    rows(c, "|||||.....k.k", "k", {0x16161C}, ox, oy);
}
// A rock ptarmigan walking right, frames 0-7 its steps, then standing and pecking. In summer mottled grey-brown with
// a white belly, in winter all white but for its black tail and the cock's black from the eye to the bill, and his
// red comb
static void ptarmigan_pose(const Canvas &c, int f, bool winter) {
  int head = f < 8 ? f % 2 : f == 9 || f == 11 ? 2 : 0;  // up, forward, down
  if (winter) {
    static const char *const HEAD[] = {"..r|..we", "...r|...we", "|||...we"};
    rows(c, f < 8 && f % 2 ? "||kwww|.s.s" : "||kwww|.ss", "kws", {0x1A1A1A, 0xFFFFFF, 0xAAB6C8});
    rows(c, HEAD[head], "rwe", {0xB83030, 0xFFFFFF, 0x1A1A1A});
  } else {
    static const char *const HEAD[] = {"|...m", "|....m", "|||....m"};
    rows(c, f < 8 ? (f % 2 ? "||nmnm|.w.w" : "||nmnm|.ww") : "||nmnm|.www", "mnw", {0x8C7C64, 0x5C4A36, 0xF4F2EC});
    rows(c, HEAD[head], "m", {0x8C7C64});
  }
}
// A red fox facing right, f 0 to 3 a trot and 4 standing. head: 0 ahead, 1 turned toward you, 2 and an ear flicked
static void fox_pose(const Canvas &c, int f, int head) {
  static const int8_t STEP[] = {1, -1, 1, -1, 0, 0, 0, 0, -1, 1, -1, 1, 0, 0, 0, 0};  // the feet from the hips
  for (int i : {1, 2, 0, 3}) {  // near hind, far hind, far front, near front, the far ones first and darker
    int x = i < 2 ? 3 + i : 5 + i, d = f < 4 ? STEP[f * 4 + i] : 0, up = (f == 1 && i % 2 == 0) || (f == 3 && i % 2);
    line(c, x, 3, x + d, 4 - up, i == 1 || i == 2 ? 0x4A3226 : 0x2A1C16);
  }
  static const char *const LOOK[] = {"........k|...hhhhhoo|whooooowwok", "........k.k|...hhhhhooo|whoooooowkw",
                                     "........k|...hhhhhook|whoooooowkw"};
  rows(c, LOOK[head], "ohwk", {0xD2642A, 0xEC8C46, 0xF2EEE6, 0x2A1C16});
}
// The roe buck's colours: its red coat, the white rump, the near legs and ears, the far legs, its nose and antlers
static void roe(const Canvas &c, const char *art) {
  rows(c, art, "rhswdka", {0xA8582C, 0xC4743E, 0x7A3E20, 0xF0ECE2, 0x5A3020, 0x1E1612, 0xB8A888});
}
// The bull moose, elg, facing right: the high hump over the shoulders and the back sloping down behind it, a long
// heavy head with the overhanging muzzle and the bell under its throat, broad palmate antlers, long legs pale grey
// below the knees. graze -1 walking, f its frame of MOOSE_MS; else the head down to the grass, 0 grazing, 1 chewing,
// 2 an ear flicked back
static void moose_pose(const Canvas &c, int f, int graze) {
  static const char *const KEYS = "BHDkLMa";  // body, the light along its back, shade, hooves, legs, far legs, antlers
  std::initializer_list<uint32_t> cols = {0x5A3C2A, 0x8A6446, 0x34221A, 0x1A100C, 0xB0A694, 0x847C6C, 0xB49A74};
  // the foot from under the hip a frame each, the last one lifted and swung forward, and the knee with it
  static const int8_t FOOT[] = {2, 1, 0, -1, -2, 0}, KNEE[] = {1, 0, 0, -1, -1, 0};
  static const int8_t X[] = {5, 7, 12, 14}, PHASE[] = {0, 3, 1, 4};
  for (int i : {1, 2, 0, 3}) {  // near hind, far hind, far front, near front, the far ones first and darker
    int k = (f + PHASE[i]) % 6, d = graze < 0 ? FOOT[k] : 0, up = graze < 0 && k == 5;
    int hind = i < 2, near = i == 0 || i == 3, x = X[i], jx = x - hind + (graze < 0 ? KNEE[k] : 0), jy = 16 - hind;
    line(c, x, 12, jx, jy, near ? 0x5A3C2A : 0x34221A);  // the hind legs bend back at the hock
    line(c, jx, jy + 1, x + d, 19 - 2 * up, near ? 0xB0A694 : 0x847C6C);
    c.put(x + d, 20 - 2 * up, 0x1A100C);
  }
  if (graze < 0) {
    rows(c,
         "||||...........HH|.........HHBBH...H|......HHHBBBBBH.HHHH|....HHBBBBBBBBBBBBBBkHH|...HBBBBBBBBBBBBBBBBBBHH|"
         "...BBBBBBBBBBBBBBDDBBBBB|...BBBBBBBBBBBBBD.DD.DkB|...DBBBBBBBBBBBD...D..D|....DD.....DDDD",
         KEYS, cols);
    rows(c,
         "|...................a.a.a|..............a.a..aaaaa|..............aaaa.aaaa|...............aaaaaa|"
         "..................a",
         KEYS, cols);
    return;
  }
  rows(c,
       "||||...........HH|.........HHBBH|......HHHBBBBBH|....HHBBBBBBBBBB|...HBBBBBBBBBBBBB|...BBBBBBBBBBBBBBB|"
       "...BBBBBBBBBBBBBBBB|...DBBBBBBBBBBBDBBB|....DD.....DDDD.DBB|.................DBBH|.................BkBH|"
       ".................DBBH|..................BBH|..................BBBH|..................DBBH|"
       "...................BkB",
       KEYS, cols);
  rows(c, graze == 1 ? "....................DB" : "...................DB", KEYS, cols, 0, 20);  // the jaw
  rows(c,
       "||||||||....................a.a|...................aaaaa|...................aaa.a|....................aaa|"
       "....................a",
       KEYS, cols);
  c.put(graze == 2 ? 18 : 19, graze == 2 ? 11 : 12, 0x8A6446);  // its ear
}
// An Oslo city bus in Ruter's red, the front to the right, its wheels turning. By night its windows lit and its
// headlights on, the rest as dark as the night
static void bus_pose(const Canvas &c, int f, bool night) {
  const uint32_t red = night ? 0x5A1E26 : 0xDD2222, dark = night ? 0x3E161C : 0xAA1A1A,
                 skirt = night ? 0x111118 : 0x222222;
  for (int x = 8; x <= 19; x++)  // the roof's cover
    c.put(x, 0, dark);
  for (int y = 1; y <= 6; y++)
    for (int x = y == 1 ? 2 : 1; x <= (y == 1 ? 27 : 28); x++)
      c.put(x, y, y == 6 ? dark : red);
  for (int x = 1; x <= 28; x++)
    c.put(x, 7, skirt);
  for (int y = 2; y <= 6; y++)  // the front
    c.put(29, y, y == 6 ? skirt : red);
  // the windows, the doors, the windscreen and the sign over it
  static const int8_t PANE[] = {2, 5, 7, 10, 15, 18, 20, 22};
  for (int i = 0; i < 8; i += 2)
    for (int x = PANE[i]; x <= PANE[i + 1]; x++)
      for (int y = 2; y <= 4; y++)
        c.put(x, y, night ? (x % 7 ? 0xFFCC77 : 0xEEBB66) : y == 2 ? 0x7799BB : 0x334455);
  if (night)  // a few heads
    dots(c, {3, 4, 9, 3, 9, 4, 16, 4, 21, 3, 21, 4}, 0x664433);
  for (int x : {12, 13, 24, 25})
    for (int y = 2; y <= 6; y++)
      c.put(x, y, night ? (y < 5 ? 0xDDAA66 : 0x886644) : y == 2 ? 0x7799BB : 0x2A3A4A);
  for (int x = 27; x <= 29; x++)
    for (int y = 2; y <= 4; y++)
      c.put(x, y, night ? 0x222233 : y == 2 ? 0x88AACC : 0x445566);
  for (int x = 25; x <= 28; x++)
    c.put(x, 1, 0x111111);
  dots(c, {26, 1, 27, 1}, night ? 0xFFAA33 : 0xDD8822);
  c.put(29, 5, night ? 0xFFFFDD : 0xDDDDCC);
  c.put(1, 5, night ? 0xFF3333 : 0xAA2222);
  static const int8_t SPOT[] = {1, 0, 2, 1, 1, 2, 0, 1};  // round the wheel the way it turns
  for (int wx : {5, 20}) {
    for (int x = wx - 1; x <= wx + 3; x++)  // the arch
      c.put(x, 6, skirt);
    for (int i = 0; i < 9; i++)
      c.put(wx + i % 3, 7 + i / 3, i == 4 ? (night ? 0x555555 : 0x999999) : 0x161616);
    c.put(wx + SPOT[f * 2], 7 + SPOT[f * 2 + 1], night ? 0x3A3A3A : 0x666666);
  }
}

// ---------------------------------------------------------------- factories

int aur_wig(int x, int w, int k) {
  float s = sinf(kPiF * x / w);
  return iround(1.8f * s * s * sinf(2 * kPiF * x / w + k * kPiF / 2));
}

Box bolt_box(const int16_t *x, const int16_t *y, int n) {
  int x0 = 9999, y0 = 9999, x1 = -9999, y1 = -9999;
  for (int i = 0; i < n; i++) {
    x0 = std::min<int>(x0, x[i]), x1 = std::max<int>(x1, x[i]);
    y0 = std::min<int>(y0, y[i]), y1 = std::max<int>(y1, y[i]);
  }
  return {x0 - 3, y0 - 2, x1 - x0 + 6, y1 - y0 + 6};
}
Frames bolt(const int16_t *x, const int16_t *y, int n, int *x0, int *y0) {
  decltype(Scene::bolt_x) px = {}, py = {};
  n = std::min(n, (int) std::size(px));
  Box b = bolt_box(x, y, n);
  *x0 = b.x;
  *y0 = b.y;
  for (int i = 0; i < n; i++)
    px[i] = (int16_t) (x[i] - b.x), py[i] = (int16_t) (y[i] - b.y);
  return {b.w, b.h, {1000, 90, 70, 110, 140, 160}, [px, py, n](int f, const Canvas &c) {
            static const uint32_t CORE[] = {0, 0xFFFFCC, 0x9999AA, 0xFFFFCC, 0xCCCCBB, 0x666677};
            for (int i = 0; f && i + 1 < n; i++)
              for (int y = py[i]; y < py[i + 1]; y++) {
                int x = bolt_at(px, py, i, y);  // as the scene draws it
                c.put(x, y, CORE[f]);
                c.put(x + 1, y, CORE[f]);
                if (f == 1 || f == 3) {
                  c.put(x - 1, y, 0x9999AA);
                  c.put(x + 2, y, 0x9999AA);
                }
              }
          }};
}

Frames cirrus_cloud(const Ellipse *e, int n, int y0, int h, uint32_t col) {
  Ellipse k[5] = {};
  int x0 = 9999, x1 = -9999;
  n = std::min(n, 5);
  for (int j = 0; j < n; j++) {
    k[j] = e[j];
    x0 = std::min(x0, e[j].x - e[j].rx);
    x1 = std::max(x1, e[j].x + e[j].rx);
  }
  x0 -= 3;
  return {x1 + 4 - x0, h, {16000, 12000, 14000}, [k, n, y0, h, x0, col](int f, const Canvas &c) {
            for (int j = 0; j < n; j++) {
              int d = f == 0 ? 0 : f == 1 ? (j == 1 ? 3 : -1) : (j == 0 ? 2 : 1);
              for (int y = 0; y < h; y++) {
                int hw = half(y0 + y - k[j].y, k[j].rx + d, k[j].ry);
                for (int x = k[j].x - hw; x <= k[j].x + hw && hw >= 0; x++)
                  c.put(x - x0, y, col);
              }
            }
          }};
}
Frames cumulus_cloud(const Ellipse *e, int n, int top, int base, int shade, uint32_t lit, uint32_t dark) {
  Ellipse k[5] = {};
  int x0 = 9999, x1 = -9999;
  n = std::min(n, 5);
  for (int j = 0; j < n; j++) {
    k[j] = e[j];
    x0 = std::min(x0, e[j].x - e[j].rx);
    x1 = std::max(x1, e[j].x + e[j].rx);
  }
  return {x1 - x0 + 3, base - top + 3, {21000, 15000, 18000}, [=](int f, const Canvas &c) {
            for (int j = 0; j < n; j++) {
              int g = (f == 1 && j == n / 2) || (f == 2 && j == 1);
              for (int y = -2; y <= base - top; y++) {
                int hw = half(top + y - k[j].y + g, k[j].rx + g, k[j].ry + g);
                for (int x = k[j].x - hw; x <= k[j].x + hw && hw >= 0; x++)
                  c.put(x - x0 + 1, y + 2, y > shade - top ? dark : lit);
              }
            }
          }};
}

// one or two kinds of rain or snow from a list
struct Falls {
  Fall k[2] = {};
  int n = 0;
  Falls(std::initializer_list<Fall> kinds) {
    for (const Fall &fall : kinds)
      if (n < (int) std::size(k))
        k[n++] = fall;
  }
};

// x round the tile's sides
static int round_w(int x, int w) { return (x % w + w) % w; }

Frames rain_tile(int w, int h, int slant, std::initializer_list<Fall> kinds, uint32_t seed) {
  Falls k(kinds);
  return {w, h, std::vector<uint16_t>(8, 95), [=](int f, const Canvas &c) {
            Rng r = seeded(seed);
            for (const Fall *fall = k.k; fall < k.k + k.n; fall++) {
              int step = (int) fall->step, period = 8 * step, count = iround(fall->dens * w * period);
              for (int i = 0; i < count; i++) {
                int u = r.in(0, w - 1), v = r.in(0, period - 1);
                for (int z = -1; z * period < h + fall->size + period; z++) {
                  int y0 = v + f * step + (z - 1) * period, x0 = u + slant * y0 / 8;
                  for (int d = 0; d < fall->size; d++)
                    c.put(round_w(x0 + slant * d / 8, w), y0 + d, fall->col);
                }
              }
            }
          }};
}
Frames snow_tile(int w, int h, int slant, std::initializer_list<Fall> kinds, uint32_t seed) {
  Falls k(kinds);
  return {w, h, std::vector<uint16_t>(32, 110), [=](int f, const Canvas &c) {
            Rng r = seeded(seed);
            for (const Fall *fall = k.k; fall < k.k + k.n; fall++) {
              float period = 32 * fall->step;
              int count = iround(fall->dens * w * period);
              for (int i = 0; i < count; i++) {
                float u = r.f() * w, v = r.f() * period, ph = r.f() * 2 * kPiF, lam = 26 + r.f() * 20,
                      amp = fall->size > 1 ? 2 : 1.4f;
                for (int z = -1; z * period < h + period; z++) {
                  float y = floorf(v + f * fall->step + (z - 1) * period);
                  int x = iround(u + (float) slant * y / 8 + amp * sinf(ph + 2 * kPiF * y / lam));
                  for (int a = 0; a < fall->size; a++)
                    for (int b = 0; b < fall->size; b++)
                      c.put(round_w(x + a, w), (int) y + b, fall->col);
                }
              }
            }
          }};
}
Frames fog_bank(int w, int h, int seed, std::array<uint32_t, 3> k) {
  return {w, h, {6000, 5000, 6500, 5500}, [=](int f, const Canvas &c) {
            for (int x = 0; x < w; x++) {
              // sinf() dips a hair below 0 at the right end, and powf() of that is NaN
              float env = powf(std::max(0.0f, sinf(kPiF * x / (w - 1))), 0.6f),
                    wave = sinf(x / 9.0f + seed + f * 0.9f) * sinf(x / 23.0f + seed * 2 - f * 0.5f);
              int top = h - 1 - iround(env * (h - 2) * (0.7f + 0.3f * wave));
              for (int y = top; y < h; y++)
                if (env > 0.35f || noise(x, y, seed) < env * 220)
                  c.put(x, y, k[y == top ? 0 : y > h - 3 ? 2 : 1]);
            }
          }};
}
// A puff of smoke's life from the chimney to where it's gone, in k light, main, dark: a wisp, growing and
// rounding as it rises, then spreading and thinning to nothing. Its delays add up to the rise and one step more
static Frames smoke_puff(std::array<uint32_t, 3> k) {
  return {19, 15, {700, 900, 1000, 1000, 1050, 1100, 1100, 1100, 1100}, [k](int f, const Canvas &c) {
            static const float RX[] = {1.2f, 2.3f, 3.3f, 4.2f, 5, 5.8f, 6.5f, 7.1f, 7.6f};
            float rx = RX[f] + 0.45f, ry = RX[f] * 0.75f + 0.45f;
            for (int y = 0; y < 15; y++)
              for (int x = 0; x < 19; x++) {
                float dx = (x - 9) / rx, dy = (y - 7) / ry, a = atan2f(dy, dx);
                float d =
                    sqrtf(dx * dx + dy * dy) / (1 + 0.14f * sinf(3 * a + f * 0.9f) + 0.08f * sinf(5 * a - f * 0.6f));
                if (d > 1 || (f >= 6 && noise(x, y, f) < (f - 5) * 16 * (0.3f + d)))
                  continue;  // outside it, or thinning out at the end from the edges in
                bool core = f >= 1 && f <= 5 && d < 0.45f, lit = dx + dy < -0.2f;
                int i = f == 0 || f >= 7             ? 1 + (d > 0.6f)
                        : core || (lit && d < 0.75f) ? 0
                        : d > 0.8f || dx + dy > 0.7f ? 2
                                                     : 1;
                c.put(x, y, k[i]);
              }
          }};
}
Frames lights(const Light *l, int n, int *x0, int *y0) {
  decltype(Scene::light) k = {};
  int ox = 9999, oy = 9999, x1 = -9999, y1 = -9999;
  n = std::min(n, (int) std::size(k));
  for (int i = 0; i < n; i++) {
    k[i] = l[i];
    ox = std::min<int>(ox, l[i].x), x1 = std::max<int>(x1, l[i].x);
    oy = std::min<int>(oy, l[i].y), y1 = std::max<int>(y1, l[i].y);
  }
  ox--, oy--;
  *x0 = ox;
  *y0 = oy;
  return {x1 - ox + 3, y1 - oy + 3, {520, 470, 560, 500, 480, 540, 510, 450}, [k, n, ox, oy](int f, const Canvas &c) {
            static const uint32_t GLINT[] = {0xFFAAAA, 0xFFEEAA, 0xAAFFBB};
            for (int i = 0; i < n; i++) {
              int g = (i * 5 + i / 3) % 8, x = k[i].x - ox, y = k[i].y - oy;
              uint32_t col = GLINT[k[i].c % 3];
              if (f != g && f != (g + 1) % 8)
                continue;
              for (int j = 0; j < 4; j++)
                c.put(x + j % 2, y + j / 2, f == g ? 0xFFFFFF : col);
              if (f == g)
                dots(c,
                     {(int8_t) (x - 1), (int8_t) y, (int8_t) (x + 2), (int8_t) (y + 1), (int8_t) x, (int8_t) (y - 1),
                      (int8_t) (x + 1), (int8_t) (y + 2)},
                     col);
            }
          }};
}
// Half a firework, the other half its mirror: frames 0-7 a round peony, 8-15 a ring that crackles, each falling
// into glitter that twinkles down and goes out. A burst plays one of them. k5 from white to the darkest
static Frames firework(std::array<uint32_t, 5> k5, int seed) {
  return {25,
          50,
          {70, 80, 90, 110, 140, 200, 260, 320, 70, 80, 100, 130, 170, 220, 280, 340},
          [k5, seed](int f, const Canvas &c) {
            int ring = f >= 8, k = f % 8, cx = 25, cy = 24, rays = ring ? 20 : 17;
            Rng r = seeded((uint32_t) (seed * 31 + f));
            float rad = 23 * (1 - powf(1 - (k + 1) / 8.0f, 2.2f)) + ring, droop = k * k * 0.14f;
            for (int i = 0; i < rays; i++) {
              float a = kPiF / 2 + (float) i / rays * kPiF * (ring ? 1 : 1.04f) + seed % 3 * 0.05f, ca = cosf(a),
                    sa = sinf(a);
              if (ca > 0.02f)
                continue;  // the left half
              for (int j = 0; j < (ring ? 1 : 3); j++) {
                float d = rad - 2.2f * j;
                if (d < 1 || (k >= 5 && r.f() < 0.3f + 0.1f * j))
                  continue;
                int t = k < 2 ? 0 : k < 4 ? 1 + j * 3 / 5 : k < 6 ? 2 + j / 2 : 3 + j / 2;
                c.put(iround(cx + d * ca), iround(cy + d * sa + droop * (1 + j * 0.3f)), k5[std::min(t, 4)]);
              }
              if (ring && k >= 3 && k <= 6 && r.f() < 0.5f)  // crackle
                c.put(iround(cx + (rad + 2) * ca), iround(cy + (rad + 2) * sa + droop), k5[0]);
            }
            for (int g = 0; k >= 5 && g < 7; g++) {  // glitter falling
              float x = cx - 3 - r.f() * rad * 0.9f, y = cy + r.f() * rad * 0.8f + (k - 4) * 2.5f;
              if (r.f() < 0.6f)
                c.put(iround(x), iround(y), r.f() < 0.35f ? k5[0] : k5[3]);
            }
            if (k == 0)
              c.put(cx - 1, cy, k5[0]);
          }};
}
// an Oslo tram, blue, the front to the right, its wheels turning
static Frames tram(bool night) {
  return {36, 13, std::vector<uint16_t>(4, 260), [night](int f, const Canvas &c) {
            uint32_t body = night ? 0x223355 : 0x3366AA, roof = night ? 0x222233 : 0x555577,
                     band = night ? 0x444455 : 0xCCCCDD;
            line(c, 14, 0, 22, 0, 0x333333);  // the pantograph
            line(c, 16, 1, 18, 3, 0x333333);
            line(c, 20, 1, 18, 3, 0x333333);
            for (int x = 2; x <= 33; x++)
              c.put(x, 4, roof);
            for (int y = 5; y <= 10; y++) {
              bool end = y == 5 || y == 10;
              for (int x = end ? 2 : 1; x <= (end ? 33 : 34); x++)
                c.put(x, y, y == 9 ? band : body);
            }
            for (int x = 3; x <= 32; x++)  // windows, and the doors
              if (x % 5 != 2 && (x < 9 || x > 10) && (x < 25 || x > 26))
                for (int y = 6; y <= 7; y++)
                  c.put(x, y, night ? (x % 7 ? 0xFFCC77 : 0xEEBB66) : 0x99BBDD);
            for (int x : {9, 10, 25, 26})
              for (int y = 6; y <= 9; y++)
                c.put(x, y, night ? 0x111122 : 0x224466);
            c.put(34, 8, night ? 0xFFFFDD : 0xDDDDCC);
            c.put(1, 8, 0xCC3333);
            for (int bx : {5, 26})
              for (int x = bx; x <= bx + 4; x++)
                c.put(x, 11, (x + f) % 2 ? 0x111111 : 0x333333);
          }};
}
// Butterflies flutter for 3 s, 24 frames bobbing with each beat, then sit for 4 s with their wings slowly opening
// and closing. Colours of the wings, their dark tips, the spots and the undersides
static Frames butterfly(uint32_t w, uint32_t d, uint32_t b, uint32_t u) {
  std::vector<uint16_t> delays(29, 125);
  static const uint16_t SIT[] = {1300, 350, 1300, 350, 700};
  for (int i = 0; i < 5; i++)
    delays[24 + i] = SIT[i];
  return {9, 6, delays, [w, d, b, u](int f, const Canvas &c) {
            // open, half, shut, then sitting shut, half, open
            static const char *const LOOK[] = {"w.......w|wwwb.bwww|.www.www|..ww.ww",
                                               "|..wb.bw|..ww.ww|...w.w",
                                               "|....w|....w",
                                               "|....u|....uw|....u",
                                               "|...ubu|...uwu|....u",
                                               "d.......d|wwwb.bwww|.www.www|..ww.ww"};
            int p = f < 24 ? (f % 4 == 3 ? 1 : f % 4) : "34543"[f - 24] - '0', bob = std::min(p, 2);
            rows(c, LOOK[p], "wdbu", {w, d, b, u}, 0, bob);
            for (int y = 1; y <= 3; y++)
              c.put(4, y + bob, 0x443322);
          }};
}

// ---------------------------------------------------------------- the fixed sprites

struct Fixed {
  const char *name;
  uint8_t w, h;
  const uint16_t *delays;  // ms each frame shows
  uint8_t n;               // frames, as many as delays has
  void (*draw)(int f, const Canvas &c);
  template<size_t N>
  constexpr Fixed(const char *name, uint8_t w, uint8_t h, const uint16_t (&delays)[N],
                  void (*draw)(int, const Canvas &))
      : name(name), w(w), h(h), delays(delays), n((uint8_t) N), draw(draw) {}
};
// ms each of their frames shows
static constexpr uint16_t GULL_MS[] = {110, 110, 110, 110, 110, 110, 1700, 1300};
static constexpr uint16_t GULLFAR_MS[] = {160, 160, 160, 160, 1600, 1100};
static constexpr uint16_t GULLVISIT_MS[] = {240, 240, 240, 300,  400, 2600, 1300, 2200,
                                            800, 700, 800, 1900, 350, 200,  200};
static constexpr uint16_t EAGLE_MS[] = {1900, 1500, 1500, 1500, 260, 220, 260, 220};
static constexpr uint16_t GEESE_MS[] = {240, 240, 240, 240};
static constexpr uint16_t BOAT_MS[] = {1300, 800, 1300, 900};
static constexpr uint16_t TACK_MS[] = {700, 700, 900, 700, 700};
static constexpr uint16_t GLITTER_MS[] = {700, 110, 160, 110, 2400};
static constexpr uint16_t EIDERS_MS[] = {700, 700, 700, 700, 700, 700, 700, 700, 700, 700, 500, 400, 2600, 600};
static constexpr uint16_t SWEEP_MS[] = {1100, 450, 330, 260, 340, 260, 330, 450, 1000, 650, 3400, 650};
static constexpr uint16_t SHIP_MS[] = {450, 380, 500, 420};
static constexpr uint16_t TWINKLE_MS[] = {2900, 140, 190, 140, 4300, 120, 1700, 140, 3600};
static constexpr uint16_t TWINKLE2_MS[] = {5100, 150, 210, 150, 2300, 130, 170, 130, 6900};
static constexpr uint16_t SHOOTING_MS[] = {70, 70, 70, 70, 70, 70, 70};
static constexpr uint16_t FLAG_MS[] = {380, 380, 380, 380, 380, 380};
static constexpr uint16_t FIREWIN_MS[] = {170, 120, 240, 140, 200, 110, 260, 150};
static constexpr uint16_t ADVENTSTAR_MS[] = {1500, 1300, 1500, 1300};
static constexpr uint16_t CANDLES_MS[] = {1100, 160, 900, 200};
static constexpr uint16_t MOOSE_MS[] = {300, 300, 300, 300, 300, 300};
static constexpr uint16_t MOOSEGRAZE_MS[] = {1900, 700, 1500};
static constexpr uint16_t REINDEER_MS[] = {500, 500, 500, 500, 500, 500, 1600, 900};
static constexpr uint16_t SKIER_MS[] = {280, 280, 280, 280};
static constexpr uint16_t SNOWDROP_MS[] = {500, 300, 160, 160, 200, 260, 320, 420, 40000};
static constexpr uint16_t LEAF_MS[] = {375, 375, 375, 375, 375, 375, 375, 375, 5000};
static constexpr uint16_t LEAFFALL_MS[] = {300, 300, 300, 300, 300, 300, 300, 300};
static constexpr uint16_t PYRE_MS[] = {90, 110, 100, 130, 90, 120, 100, 110};
static constexpr uint16_t SPARK_MS[] = {120, 160, 120};
static constexpr uint16_t TREESTAR_MS[] = {900, 700, 900};
static constexpr uint16_t CITYSTAR_MS[] = {1000, 600, 800};
static constexpr uint16_t SLEIGH_MS[] = {170, 170, 170, 170};
static constexpr uint16_t ROCKET_MS[] = {70, 70, 70};
static constexpr uint16_t CHICK_MS[] = {700, 160, 160, 500};
// one round is one hop: crouched a long while, then off, stretched out and down
static constexpr uint16_t HARE_MS[] = {1250, 150, 300, 550};
static constexpr uint16_t HARESIT_MS[] = {1400, 160, 900};
static constexpr uint16_t PARADE_MS[] = {210, 210, 210, 210, 210, 210};
static constexpr uint16_t BALLOON_MS[] = {700, 600, 700, 600};
// the heart's last two, fading, fill up the rise and a step
static constexpr uint16_t HEART_MS[] = {800, 900, 900, 450, 450, 900, 450, 900, 900, 900};
static constexpr uint16_t OWL_MS[] = {260, 260, 260, 260, 1500, 1200};
static constexpr uint16_t OWLPERCH_MS[] = {2300, 700, 1600, 700, 400, 160, 1900, 700, 1200};
static constexpr uint16_t PUMPKIN_MS[] = {160, 120, 220, 140, 180, 130};
static constexpr uint16_t BEACON_MS[] = {1000, 1600};
static constexpr uint16_t WINLIT_MS[] = {5200, 300, 3900, 260};
static constexpr uint16_t WINDARK_MS[] = {6100, 140, 110, 180, 4300, 120};
static constexpr uint16_t TRAMPOLINE_MS[] = {130, 130, 130, 130, 130, 130};
static constexpr uint16_t UMBRELLA_MS[] = {150, 150, 150};
static constexpr uint16_t SHIRT_MS[] = {160, 160, 160, 160};
static constexpr uint16_t COW_MS[] = {220, 220};
// the lights go round while it comes and looks, then the beam, then round again until it's off
static constexpr uint16_t UFO_MS[] = {350, 350, 350, 350, 350, 350, 350, 350, 350, 350,
                                      250, 800, 800, 800, 250, 350, 350, 350, 350};
static constexpr uint16_t SERPENT_MS[] = {280, 280, 280, 280};
static constexpr uint16_t TROLL_MS[] = {380, 380};
static constexpr uint16_t LONGSHIP_MS[] = {320, 320, 320, 320};
// it sits, lifts its head and howls, twice, and is still at the end
static constexpr uint16_t WOLF_MS[] = {1600, 300, 2400, 300, 1400, 300, 2800, 300, 900};
static constexpr uint16_t WITCH_MS[] = {240, 200, 240, 200};

// two hops first, which the show moves it on over, then it stands about
static constexpr uint16_t MAGPIE_MS[] = {90, 130, 90, 130, 160, 1100, 700, 500, 220, 180, 220, 900, 160, 700};
static constexpr uint16_t CROWFLY_MS[] = {130, 110, 130, 110};
// it lands with its wings up and folds them, looks about, caws three times, looks about again and takes off
static constexpr uint16_t CROW_MS[] = {260, 300, 260, 220,  1400, 900, 600, 280, 200,
                                       280, 200, 320, 1200, 800,  700, 300, 180};
// they sit and bob a while, then lift off together, wheel round and settle again
static constexpr uint16_t PIGEONS_MS[] = {1400, 900, 1100, 700, 1300, 800, 1200, 600, 160, 160, 160,
                                          160,  160, 160,  160, 160,  160, 160,  160, 160, 180, 220};
// eight steps first, which the show moves it on over, then it stands and pecks
static constexpr uint16_t PTARMIGAN_MS[] = {180, 180, 180, 180, 180, 180, 180, 180, 700, 220, 200, 220, 900, 600};
// quick beats, then a glide
static constexpr uint16_t SWALLOW_MS[] = {70, 70, 70, 70, 70, 70, 450, 70, 70, 70, 300};
// it soars and banks slowly one way, beats once, deep, and banks the other way
static constexpr uint16_t SEAEAGLE_MS[] = {2200, 800, 1600, 800, 1400, 300, 160, 340, 800, 1600, 800};
// the glide down as long as the show takes to bring it down to the water, the last as long as it takes it away
static constexpr uint16_t SEAEAGLECATCH_MS[] = {3400, 300, 250, 350, 260, 260, 280, 300, 3800};
// a bound: gathered, off, stretched out, landing on its front feet
static constexpr uint16_t SQUIRREL_MS[] = {110, 90, 120, 100};
// nibbling, the nut lowered, a look about, a flick of the tail
static constexpr uint16_t SQUIRRELNUT_MS[] = {160, 140, 160, 140, 160, 140, 160, 500, 900, 800, 250, 600};
// a trot, the diagonal legs together
static constexpr uint16_t FOX_MS[] = {150, 130, 150, 130};
// it stops, turns its head to look at you a while, flicks an ear, and looks ahead again
static constexpr uint16_t FOXLOOK_MS[] = {600, 1800, 200, 1400, 500};

// grazing, a tug at the grass, then the head up a while, the ears twitching
static constexpr uint16_t ROEDEER_MS[] = {1800, 500, 1300, 400, 1600, 180, 1000, 150, 800};
// a bound: gathered, off its hind feet, in the air, landing on its front feet
static constexpr uint16_t ROEDEERRUN_MS[] = {120, 110, 150, 120};

// each lifting its head from the grass and putting it down again in its own time, one at a time
static constexpr uint16_t SHEEP_MS[] = {1400, 900, 1000, 800, 1100, 900, 300, 400};
// three strides of its walk, then it stops, looks at you, behind it, at you again and ahead
static constexpr uint16_t LYNX_MS[] = {220, 220, 220, 220, 220,  220,  220, 220, 220,
                                       220, 220, 220, 500, 1500, 1200, 700, 400};
// under a while, a roll, a breath under, a second roll
static constexpr uint16_t PORPOISE_MS[] = {2600, 140, 220, 160, 150, 400, 1400, 140, 220, 160, 150, 500};
// calm, the bull blows, surfaces and goes under, then the cow, then her flukes come up and slap down
static constexpr uint16_t ORCAS_MS[] = {1800, 400, 350, 700, 300, 300, 400, 350,
                                        600,  300, 700, 250, 300, 200, 250, 400};

// two kicks, each a step forward, the push back and the leg out behind, then gliding on both runners
static constexpr uint16_t KICKSLED_MS[] = {180, 150, 230, 180, 150, 230, 900};
// the snow spraying off the runners, then a bump: up off it and down again in a burst of snow
static constexpr uint16_t SLEDGE_MS[] = {140, 140, 140, 120, 160};
static constexpr uint16_t CYCLIST_MS[] = {170, 170, 170, 170};
// a stride and a step, the stick planted ahead with the first
static constexpr uint16_t HIKER_MS[] = {240, 240, 240, 240};
// two strides and steps, stopping by a chanterelle, bending down to it, picking it, straightening up with it and
// standing a while
static constexpr uint16_t MUSHROOMER_MS[] = {240, 240, 240, 240, 240, 240, 240, 240, 700, 300, 900, 300, 1100};
// a round is one jump: in flight wobbling a little, touching down, in a telemark, then riding out upright
static constexpr uint16_t SKIJUMPER_MS[] = {500, 450, 500, 450, 500, 450, 500, 450, 160, 1400, 1800};
static constexpr uint16_t BUS_MS[] = {240, 240, 240, 240};
static constexpr uint16_t PLOUGH_MS[] = {170, 150, 170, 160};
// it drives along with its light blinking, stops, turns toward you and is ready to go either way
static constexpr uint16_t MOWER_MS[] = {2600, 400, 2600, 400, 600, 350, 900};
static constexpr uint16_t PLANE_MS[] = {950, 120, 400, 70, 90, 70};
// a long while a faint dot, then it flares and fades
static constexpr uint16_t SATELLITE_MS[] = {60000, 700, 500, 400, 600, 900};
// a gull beats now and then, and the last four the swell lifts the bow
static constexpr uint16_t SJARK_MS[] = {1400, 200, 200, 200, 1600, 200, 200, 200, 1400, 200, 200, 200, 900};

// firewin's flicker three times over, the cat doing something now and then
static constexpr uint16_t FIREWINCAT_MS[] = {170, 120, 240, 140, 200, 110, 260, 150, 180, 130, 250, 140,
                                             190, 120, 240, 160, 170, 120, 230, 150, 200, 110, 250, 140};
// she stands a while, turns, and walks off; played once, the show moving her from the turn on
static constexpr uint16_t HULDRA_MS[] = {2200, 900, 1800, 500, 340, 340, 340, 340,
                                         340,  340, 340,  340, 340, 340, 340, 340};
// played once: in on short steps, a stop to hold up the lantern and look back and ahead, then on again
static constexpr uint16_t NISSE_MS[] = {300,  300, 300, 300, 300, 300, 300, 300, 300, 300, 300, 300, 700, 1400, 1300,
                                        1000, 500, 300, 300, 300, 300, 300, 300, 300, 300, 300, 300, 300, 300};
// two strokes, a wave lifting it and letting it down
static constexpr uint16_t DRAUGEN_MS[] = {320, 420, 300, 520, 320, 420, 300, 520};

// Frame f's digit in seq, which has a digit for each frame in MS
template<const auto &MS, size_t N> constexpr int pose(const char (&seq)[N], int f) {
  static_assert(N == std::size(MS) + 1, "a digit for each frame");
  return seq[f] - '0';
}

static constexpr Fixed FIXED[] = {
    // ---- birds
    // A gull from the front, wings first: an M while it glides, up and down through a V while it beats
    {"gull", 13, 7, GULL_MS, [](int f, const Canvas &c) { gull_pose(c, pose<GULL_MS>("01454123", f), 0, 0); }},
    // the same gull far off, a small v that beats now and then
    {"gullfar", 5, 3, GULLFAR_MS,
     [](int f, const Canvas &c) {
       static const char *const LOOK[] = {"x...x|.x.x|..x", "|xx.xx|..x", "|.xxx|x...x",
                                          "|xx.xx|..x",     "|xx.xx|..x", "x...x|.xxx"};
       rows(c, LOOK[f], "x", {0x777788});
     }},
    // A gull's visit to the ridge: it comes in gliding, flares with its legs down, lands with its wings up, folds
    // them, looks about and preens, then crouches and takes off
    {"gullvisit", 15, 15, GULLVISIT_MS,
     [](int f, const Canvas &c) {
       static const char *const SIT[] = {"......w|.....wwwy|..gggww|.kgggwww|..gggwww|...wwww|....l.l",
                                         "......w|....ywww|..gggww|.kgggwww|..gggwww|...wwww|....l.l",
                                         "||..gggwwy|.kgggwww|..gggwww|...wwww|....l.l",
                                         "||..ggwwwy|.kgggwww|..gggwww|...wwww|....l.l"};
       if (f >= 3 && f <= 12)
         rows(c, SIT[f == 6 ? 1 : f == 8 ? 2 : f == 10 ? 3 : 0], "wgkyl",
              {0xFFFFFF, 0x9999AA, 0x333344, 0xFFBB33, 0xEE9988}, 4, 8);
       if (f == 0)
         gull_pose(c, 2, 1, 0);
       if (f == 1) {
         gull_pose(c, 1, 1, 2);
         dots(c, {6, 7, 8, 7}, 0xEE9988);
       }
       if (f == 2) {
         gull_pose(c, 0, 1, 4);
         dots(c, {6, 9, 8, 9, 6, 10, 8, 10}, 0xEE9988);
       }
       if (f == 3) {
         dots(c, {6, 8, 5, 7, 4, 6, 3, 5, 10, 8, 11, 7, 12, 6}, 0xBBBBCC);
         dots(c, {3, 5, 12, 6}, 0x333344);
       }
       if (f == 4)
         dots(c, {6, 9, 5, 8, 4, 8, 11, 8}, 0xBBBBCC);
       if (f == 12)
         dots(c, {4, 10, 3, 9, 12, 10}, 0x9999AA);
       if (f == 13) {
         gull_pose(c, 0, 1, 5);
         dots(c, {6, 9, 8, 9}, 0xEE9988);
       }
       if (f == 14)
         gull_pose(c, 4, 1, 4);
     }},
    // A golden eagle from below: a long plank with the fingers spread at its tips, a small golden head and a short
    // fanned tail. It soars, banks one way and the other, and beats a few times
    {"eagle", 27, 11, EAGLE_MS,
     [](int f, const Canvas &c) {
       int bank = pose<EAGLE_MS>("12101111", f) - 1, flap = pose<EAGLE_MS>("11110121", f) - 1, m = 13;
       auto lift = [&](int x) {
         float d = abs(x - m) / 11.0f;
         return iround(bank * (m - x) / 8.0f + flap * 2.8f * d * sqrtf(d));
       };
       for (int x = 3; x <= 23; x++) {
         int d = abs(x - m), deep = d <= 3 ? 4 : d <= 7 ? 3 : 2, y = 4 + lift(x);
         c.put(x, y, d < 8 ? 0x665544 : 0x554433);
         for (int k = 1; k < deep; k++)
           c.put(x, y + k, k == deep - 1 ? 0x221111 : 0x332211);
       }
       static const int8_t FINGER[] = {1, -1, 2, 0, 1, 1, 3, 1, 2, 2};
       for (int s = -1; s <= 1; s += 2) {
         int tx = m + s * 10, ty = 4 + lift(tx);
         for (int i = 0; i < 10; i += 2)
           c.put(tx + s * FINGER[i], ty + FINGER[i + 1], FINGER[i + 1] == 2 ? 0x221111 : 0x332211);
       }
       c.put(m, 2, 0xAA8833);
       c.put(m, 3, 0xBB9944);
       dots(c, {12, 3, 14, 3}, 0x665544);
       dots(c, {12, 8, 13, 8, 14, 8}, 0x443322);  // the tail
       dots(c, {11, 9, 12, 9, 13, 9, 14, 9, 15, 9}, 0x221111);
     }},
    // A V of geese far off, each bird beating in its own time, small and grey with the haze
    {"geese", 17, 12, GEESE_MS,
     [](int f, const Canvas &c) {
       static const char *const S[] = {"x.x|.x", "|xxx", ".x|x.x", "|xxx"};
       static const int8_t AT[] = {14, 4, 10, 2, 6, 0, 10, 6, 6, 8, 2, 10};
       for (int i = 0; i < 6; i++)
         rows(c, S[(f + i) % 4], "x", {0x555566}, AT[i * 2], AT[i * 2 + 1]);
     }},
    // A magpie facing right, out in front of the cabin: two hops, then it stands, looks back over its shoulder, pecks
    // twice and flicks its long tail up. Black with a white shoulder and belly, a blue gloss on its wing and a green
    // one on its tail
    {"magpie", 7, 5, MAGPIE_MS,
     [](int f, const Canvas &c) {
       // stand, look back, peck, tail up, crouch, in the air
       static const char *const LOOK[] = {".....kk|gg.kwk.|..kbbw.|....k", "....kk.|gg.kwk.|..kbbw.|....k",
                                          "g|.g.kwk|..kbbwk|....k.k",      "g|.g...kk|..gkwk.|..kbbw.|....k",
                                          "|.....kk|ggkkwk.|...bbw.",      ".....kk|gg.kwk.|..kbbw.|...k"};
       int p = pose<MAGPIE_MS>("45450010202030", f);
       rows(c, LOOK[p], "kbgw", {0x15171D, 0x2F4F8F, 0x2F6F5A, 0xF2F4F6}, 0, p == 3 || p == 5 ? 0 : 1);
     }},
    // A hooded crow flying right, rowing steadily, the fingers of its wings spread on the downstroke
    {"crowfly", 11, 8, CROWFLY_MS,
     [](int f, const Canvas &c) { crow_flying(c, pose<CROWFLY_MS>("0121", f), 0, 0, false); }},
    // A hooded crow's visit to the ridge: it comes in with its legs down, lands with its wings up and folds them,
    // looks about, caws three times, bobbing forward with its bill open and its tail up, and takes off
    {"crow", 11, 9, CROW_MS,
     [](int f, const Canvas &c) {
       // stand, look back, caw, crouch
       static const char *const SIT[] = {"......kk|.....kkkkk|...gggkkk|.ggggkkgg|.kkkkkggg|kkkkkdgg|k....k.k",
                                         ".....kk|...kkkkk|...gggkk|.ggggkkgg|.kkkkkggg|kkkkkdgg|k....k.k",
                                         "|.......kk.k|...ggkkkk|..ggggkkkk|.kkkkkgg.k|kkkkkdgg|k....k.k",
                                         "|......kk|.....kkkkk|.gggggkkk|kggggkkgg|.kkkkdggg|.....k.k"};
       std::initializer_list<uint32_t> cols = {0x16161C, 0xA4A6AC, 0x7C7E86};
       int p = pose<CROW_MS>("00000102020200103", f);
       if (f == 0 || f == 16)  // coming in, legs down, and taking off on a downstroke
         crow_flying(c, f ? 2 : 0, 0, f ? 1 : 0, !f);
       else
         rows(c, SIT[p], "kgd", cols, 0, f == 1 ? 1 : 2);
       if (f >= 1 && f <= 3)  // its wings up as it lands, then folding
         rows(c, f < 3 ? "k.k|.kkk|..kkk|...kk" : "||..kk|...kk", "k", cols);
     }},
    // A rock ptarmigan in summer and in winter: eight steps, its head going with each, then it stands and pecks twice
    {"ptarmigan", 5, 4, PTARMIGAN_MS, [](int f, const Canvas &c) { ptarmigan_pose(c, f, false); }},
    {"ptarmiganw", 5, 4, PTARMIGAN_MS, [](int f, const Canvas &c) { ptarmigan_pose(c, f, true); }},
    // A barn swallow flying right, from the side: steel blue, its face rusty red, its belly cream and its tail
    // forked. Quick beats and short glides. The show sends two of them along curves low over the meadow
    {"swallow", 5, 3, SWALLOW_MS,
     [](int f, const Canvas &c) {
       // up, level, down, swept back to glide
       static const char *const WING[] = {".bb", "", "||.bb", "|bbb"};
       rows(c, "k|.kbbr|k.ww", "kbwr", {0x1C2850, 0x3A5CA8, 0xF2E8D8, 0xC0502E});
       rows(c, WING[pose<SWALLOW_MS>("01210130123", f)], "b", {0x3A5CA8});
     }},
    // A white-tailed sea eagle from below, soaring: long wings as broad all the way out, held flat, the fingers
    // spread at their tips, a big pale head and yellow bill well out in front and a short white wedge of a tail.
    // It banks slowly one way, beats once, deep, and banks the other way
    {"seaeagle", 21, 11, SEAEAGLE_MS,
     [](int f, const Canvas &c) {
       // bank: its right wing up or down a step at a time, beat: its wings up or down, half and all the way
       float bank = (pose<SEAEAGLE_MS>("23432222101", f) - 2) * 0.75f,
             beat = (pose<SEAEAGLE_MS>("22222420222", f) - 2) / 2.0f;
       constexpr int M = 10, Y = 4;  // the middle, the leading edge's row
       auto lift = [&](int x) {
         float d = abs(x - M) / 10.0f;
         return iround(bank * (M - x) / 9.0f - beat * (beat > 0 ? 3.0f : 2.0f) * d * sqrtf(d));
       };
       static const uint32_t ROW[] = {0x7A5C40, 0x4E3A28, 0x2A1E14, 0x2A1E14};
       for (int x = 2; x <= 18; x++)
         for (int k = 0; k < (abs(x - M) <= 3 ? 4 : 3); k++)  // the arm a little deeper by the body
           c.put(x, Y + k + lift(x), ROW[k]);
       static const int8_t FINGER[] = {1, 0, 1, 2, 2, -1, 2, 1, 2, 3, 1, 4};  // out from the wing's end, down
       for (int s = -1; s <= 1; s += 2)
         for (int i = 0; i < 12; i += 2) {
           int x = M + s * (8 + FINGER[i]);
           c.put(x, Y + FINGER[i + 1] + lift(x), 0x2A1E14);
         }
       rows(c, ".y|hyh|hhh", "hy", {0xE2D6B4, 0xF0C030}, M - 1, Y - 3);  // the head and its big bill, the tail
       rows(c, "www|.w", "w", {0xF6F6F2}, M - 1, Y + 4);
     }},
    // ---- the sea
    // A small sailboat, bow to the left: the jib and the main fill and luff, the hull rocks
    {"boat", 13, 15, BOAT_MS, [](int f, const Canvas &c) { boat_pose(c, pose<BOAT_MS>("0102", f), false); }},
    // Coming about: the bow swings toward you, the sails flap edge on, then fill on the other side
    {"tack", 13, 15, TACK_MS,
     [](int f, const Canvas &c) {
       if (f == 0 || f == 4) {
         boat_pose(c, f ? 0 : 3, f == 4);
         return;
       }
       int hw = pose<TACK_MS>("04240", f), side = f == 1 ? 1 : f == 3 ? -1 : 0;
       for (int x = 6 - hw; x <= 6 + hw; x++)
         c.put(x, 12, 0x773333);
       for (int x = 6 - hw + 1; x <= 6 + hw - 1; x++) {
         c.put(x, 13, 0x662222);
         c.put(x, 14, 0x662222);
       }
       if (hw < 3)
         dots(c, {5, 13, 7, 13, 6, 14}, 0x662222);
       for (int y = 0; y <= 11; y++)
         c.put(6, y, 0x444433);
       for (int r = 2; r <= 11; r++) {
         int flap = (r + f) % 3 == 0;
         c.put(6 + (side ? side : r % 2 ? 1 : -1) * (1 + flap), r, 0xEEEEEE);
         if (side && r > 4)
           c.put(6 + side * 2, r, r % 2 ? 0xDDDDDD : 0xEEEEEE);
         if (r > 5)
           c.put(6 - (side ? side : 1), r, 0xCCCCDD);
       }
     }},
    // a point of sunlight on the water: dark, a glint, a small star, gone
    {"glitter", 3, 3, GLITTER_MS,
     [](int f, const Canvas &c) {
       if (f == 1 || f == 3)
         c.put(1, 1, 0xFFFFDD);
       if (f == 2) {
         c.put(1, 1, 0xFFFFFF);
         dots(c, {0, 1, 2, 1, 1, 0, 1, 2}, 0xEEEECC);
       }
     }},
    // A pair of eiders off the shore, bobbing on the swell. Now and then the drake dives and comes up again
    {"eiders", 15, 6, EIDERS_MS,
     [](int f, const Canvas &c) {
       int bob = pose<EIDERS_MS>("01100110010001", f), duck = pose<EIDERS_MS>("11001100110011", f);
       if (f < 10 || f == 13)
         rows(c, "...kk|..kgw|wwwww|kkkkk", "kgw", {0x222222, 0x88AA77, 0xEEEEEE}, 0, 1 + bob);
       if (f == 10)
         rows(c, "kk|wwk|wwwkk", "kw", {0x222222, 0xEEEEEE}, 0, 2);
       if (f == 11)
         rows(c, "k|wk", "kw", {0x222222, 0xEEEEEE}, 0, 3);
       rows(c, "...bb|..bbb|bbbbb|ddddd", "bd", {0x886655, 0x664433}, 8, 1 + duck);
       for (int x = 0; x < 15; x++)
         if ((x + f) % 4 == 0)
           c.put(x, 5, 0x99BBDD);
       if (f == 11 || f == 12)
         dots(c, {0, 4, 5, 4}, 0x99BBDD);
     }},
    // The lighthouse turning, one sweep in 12 frames: the beam points left, shortens as it turns toward you, the
    // lamp flashes, then the beam grows on the right and dims as it turns away
    {"sweep", 141, 11, SWEEP_MS,
     [](int f, const Canvas &c) {
       const int L = 90, ly = 5;  // the lamp in the sprite
       static const uint8_t LEN[] = {90, 58, 32, 14, 0, 14, 32, 48, 50, 38, 0, 48},
                            BRIGHT[] = {20, 20, 21, 23, 0, 23, 21, 20, 19, 11, 0, 10};  // in twentieths
       int dir = pose<SWEEP_MS>("000012222210", f) - 1;
       for (int d = 2; dir && d <= LEN[f]; d++) {
         float half = 0.6f + d * 0.06f, fade = (float) d / LEN[f];
         for (int y = (int) floorf(ly - half); y <= (int) ceilf(ly + half); y++) {
           float off = abs(y - ly) / (half + 0.5f), v = BRIGHT[f] / 20.0f * (1 - 0.75f * fade) * (1 - 0.6f * off);
           if (off <= 1 && v > 0.18f)
             c.put(L + dir * d, y, v > 0.72f ? 0xFFFFEE : v > 0.5f ? 0xDDDDEE : v > 0.32f ? 0x9999AA : 0x555577);
         }
       }
       int r = f == 4 ? 11 : f == 3 || f == 5 ? 5 : 0;  // the glow's radius in half px
       for (int y = 0; r && y < 11; y++)
         for (int x = L - 7; x <= L + 7; x++) {
           int d2 = 4 * ((x - L) * (x - L) + (y - ly) * (y - ly));
           if (d2 < r * r)
             c.put(x, y, d2 * 25 < r * r * 4 ? 0xFFFFFF : d2 * 100 < r * r * 49 ? 0xFFFFEE : 0x9999AA);
         }
       uint32_t lamp = f == 10 ? 0x886633 : f == 4 ? 0xFFFFFF : 0xFFDD88;
       for (int y = ly - 1; y <= ly + 1; y++)
         for (int x = L - 1; x <= L + 1; x++)
           if (f != 4 || (x != L && y != ly))
             c.put(x, y, x == L && y == ly && f != 10 ? 0xFFFFEE : lamp);
     }},
    // A coastal express far out on the horizon at night: rows of warm windows, a white light at the masthead, a
    // red one to port, and their reflections trembling on the water
    {"ship", 22, 9, SHIP_MS,
     [](int f, const Canvas &c) {
       for (int y = 2; y <= 4; y++)
         for (int x = y == 4 ? 1 : y == 3 ? 4 : 6; x <= (y == 4 ? 20 : y == 3 ? 15 : 13); x++)
           c.put(x, y, 0x111122);
       for (int x = 2; x <= 19; x += 2)
         c.put(x, 4, x % 6 == 2 ? 0xEEBB66 : 0xFFCC77);
       for (int x = 5; x <= 14; x += 2)
         c.put(x, 3, 0xFFCC77);
       c.put(9, 0, 0xFFFFFF);
       c.put(9, 1, 0x111122);
       c.put(10, 2, 0xCC3311);
       c.put(17, 3, 0xFF2222);
       dots(c, {16, 1, 16, 2}, 0x333355);
       Rng r = seeded((uint32_t) (40 + f));
       for (int y = 6; y < 9; y++)
         for (int x = 2; x <= 19; x += 2)
           if (r.in(0, 99) < 50 - (y - 6) * 12)
             c.put(x + r.in(0, 1), y, y == 6 ? 0xAA8855 : 0x665533);
       c.put(9 + f % 2, 7, 0xAAAABB);
     }},
    // A harbour porpoise heading right, rolling out of the sea twice and under again: its head, its dark back
    // arching up with the small fin on top, the tail stock last, and rings. The water at row 2
    {"porpoise", 5, 3, PORPOISE_MS,
     [](int f, const Canvas &c) {
       static const char *const LOOK[] = {"", "|...dh|..w.w", "..d|.ddd|r...r", ".d|ddh|..r", "|dd|..r", "||r.r"};
       rows(c, LOOK[pose<PORPOISE_MS>("012345012345", f)], "dhwr", {0x3A4250, 0x6E7888, 0xE6EEF8, 0x8FAAD4});
     }},
    // A sjark, a small fishing boat, the bow to the right: white with a red stripe, its wheelhouse aft and the mast
    // forward, the swell lifting its bow now and then. A few gulls follow it, far off specks
    {"sjark", 24, 9, SJARK_MS,
     [](int f, const Canvas &c) {
       const uint32_t white = 0xF0F0F0;
       const int o = 9;  // the stern's column
       bool lift = f >= 9;
       // the hull: white, a red stripe under the gunwale, its sheer rising to the bow
       static const int8_t TOP[] = {4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 4, 3, 3},
                           BOT[] = {6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 6, 5};
       for (int x = 0; x <= 14; x++)
         for (int y = TOP[x]; y <= BOT[x]; y++)
           c.put(o + x, y - (lift && x >= 12), y == TOP[x] + 1 ? 0xCC2233 : white);
       for (int x = 2; x <= 5; x++) {  // the wheelhouse
         c.put(o + x, 2, 0x556677);
         c.put(o + x, 3, x == 3 || x == 5 ? 0x334455 : white);
         c.put(o + x, 4, white);
       }
       for (int y = 0; y <= 4; y++)  // the mast
         c.put(o + 10, y, 0x6A5A48);
       // foam at the bow, unless it's lifted out, and a short wake
       if (!lift)
         dots(c, {(int8_t) (o + 13), 8, (int8_t) (o + 14), 8}, white);
       c.put(o, 8, white);
       Rng r = seeded((uint32_t) (60 + f % 3));
       for (int x = o - 6; x < o; x++)
         if (r.in(0, 9) < 3)
           c.put(x, 8, 0xC8D8EC);
       // the gulls, wings up or arched
       static const char *const GULL[] = {"x.x|.x", ".x|x.x"};
       static const char *const BEAT[] = {"1010111111111", "1111101011111", "1111111110101"};
       static const int8_t AT[] = {4, 0, 0, 2, 3, 5};
       for (int g = 0; g < 3; g++)
         rows(c, GULL[BEAT[g][f] - '0'], "x", {0xDDDDE6}, AT[g * 2], AT[g * 2 + 1]);
     }},
    // ---- the sky
    // A star sparkling now and then, in irregular times. Two of these with different times
    {"twinkle", 5, 5, TWINKLE_MS, [](int f, const Canvas &c) { sparkle(c, pose<TWINKLE_MS>("012101010", f)); }},
    {"twinkle2", 5, 5, TWINKLE2_MS, [](int f, const Canvas &c) { sparkle(c, pose<TWINKLE2_MS>("012101210", f)); }},
    {"shooting", 24, 8, SHOOTING_MS,
     [](int f, const Canvas &c) {
       int head = std::min(23, 5 + f * 16 / 5);
       for (int i = std::max(0, head - 12); i <= head; i++) {
         int t = (head - i) * 100 / 12;  // how far back the trail, in percent
         c.put(i, (i * 7 + 11) / 23,
               f >= 5 ? (t < 30 ? 0x8888CC : 0x444466)
               : t < 15 ? 0xFFFFFF
               : t < 45 ? 0xCCCCFF
               : t < 75 ? 0x8888CC
                        : 0x444466);
       }
     }},
    // A plane's lights high up on its way in to land, the front to the right: red and green at its wingtips, white
    // at its tail, its landing light ahead, the red beacon blinking and the white strobes flashing twice. Its body
    // only just shows. Drawn in the lights' own colours
    {"plane", 9, 5, PLANE_MS,
     [](int f, const Canvas &c) {
       for (int x = 1; x <= 6; x++)
         c.put(x, 2, 0x3A4054);
       dots(c, {1, 1, 4, 3, 5, 1}, 0x3A4054);
       bool strobe = f == 3 || f == 5;
       c.put(3, 4, strobe ? 0xFFFFFF : 0xFF3333);
       c.put(4, 0, strobe ? 0xFFFFFF : 0x33FF66);
       c.put(0, 2, 0xCCCCDD);
       c.put(7, 2, 0xFFFFDD);
       c.put(8, 2, 0xFFFFFF);
       if (f == 1)
         c.put(5, 3, 0xFF2222);
     }},
    // A satellite crossing the starry sky, a faint dot a long while, then it flares up and fades again
    {"satellite", 3, 3, SATELLITE_MS,
     [](int f, const Canvas &c) {
       static const uint32_t COL[] = {0x8888AA, 0xAAAACC, 0xDDDDEE, 0xFFFFFF, 0xDDDDEE, 0xAAAACC};
       c.put(1, 1, COL[f]);
       if (f == 3)
         dots(c, {0, 1, 2, 1, 1, 0, 1, 2}, 0x8888AA);
     }},
    // ---- home
    // The flag at half the scene's size, flying to the left in a light wind: its fly end droops and folds roll
    // slowly along it
    {"flag", 17, 15, FLAG_MS,
     [](int f, const Canvas &c) {
       for (int k = 0; k < 15; k++) {
         float t = k / 14.0f, ph = k * 0.55f - f * (kPiF / 3), droop = 3.2f * t * t, fold = 0.65f * t * sinf(ph);
         float hgt = 11 - 2.2f * t * (0.6f + 0.4f * sinf(ph + 1));
         bool dark = cosf(ph) > 0.55f;
         for (int r = 0; r < 11; r++) {
           bool v = k >= 4 && k <= 6, h = r >= 4 && r <= 6, blue = k == 5 || r == 5, white = (v || h) && !blue;
           c.put(15 - k, iround(droop + fold + r * hgt / 11),
                 blue    ? (dark ? 0x111133 : 0x112255)
                 : white ? (dark ? 0xBBBBCC : 0xEEEEEE)
                 : dark  ? 0xAA1122
                         : 0xDD2233);
         }
       }
     }},
    // firelight through the cabin's window: both panes flicker together, brightest low down where the fire is
    {"firewin", 7, 6, FIREWIN_MS,
     [](int f, const Canvas &c) {
       static const uint32_t COL[] = {0xDD9944, 0xFFBB55, 0xFFCC66, 0xFFDD88, 0xFFEE99};
       int lv = pose<FIREWIN_MS>("21312032", f), side = pose<FIREWIN_MS>("01001010", f);
       for (int y = 0; y < 6; y++)
         for (int x = 0; y != 2 && x < 7; x++)
           if (x != 3)
             c.put(x, y, COL[std::min(4, lv + (y > 2) + ((x < 3) == (side != 0)))]);
     }},
    // The cabin's window with the cat sitting on the sill, black against the firelight, which flickers like
    // firewin's: it flicks its tail, looks one way and the other and licks a paw. The show swaps it for firewin when
    // it's jumped down
    {"firewincat", 7, 6, FIREWINCAT_MS,
     [](int f, const Canvas &c) {
       static const uint32_t COL[] = {0xDD9944, 0xFFBB55, 0xFFCC66, 0xFFDD88, 0xFFEE99};
       // it sits, its tail up, it looks left and right, and it licks its paw
       static const char *const CAT[] = {".k.k|.kkk|.kkkk", ".k.k|.kkkk|.kkk", ".kk|kkkk|.kkkk",
                                         "..kk|.kkk|.kkkk", "|.k.k|.kkkk",     "|k.k|kkkkk"};
       int lv = pose<FIREWINCAT_MS>("213120321320213121302312", f);
       int side = pose<FIREWINCAT_MS>("010010101001010001010010", f);
       for (int y = 0; y < 6; y++)
         for (int x = 0; y != 2 && x < 7; x++)
           if (x != 3)
             c.put(x, y, COL[std::min(4, lv + (y > 2) + ((x < 3) == (side != 0)))]);
       rows(c, CAT[pose<FIREWINCAT_MS>("001010222003300454540100", f)], "k", {0x2A1E1E}, 0, 3);
     }},
    // A paper advent star in the window, nearly as big as it, lit from inside, its light warm on the glass round it.
    // It turns slowly on its cord, one side then the other in shade. A column wider than the glass, left empty, so
    // the panel's padding to an even width doesn't cover the window's frame.
    {"adventstar", 8, 6, ADVENTSTAR_MS,
     [](int f, const Canvas &c) {
       for (int y = 0; y < 6; y++)
         for (int x = 0; x < 7; x++)
           if (x != 3 && y != 2)
             c.put(x, y, abs(x - 3) + abs(2 * y - 5) / 2 <= 3 ? 0x9C6230 : 0x6A4424);
       rows(c, "...t|..aaa|taacaat|.aacaa|..a.a|.t...t", "tac", {0xF0A040, 0xFFD050, 0xFFEC8C});
       if (f % 2)  // the side turned away
         rows(c, f == 1 ? "|..s|sss|.ss|..s|.s" : "|....s|....sss|....ss|....s|.....s", "s", {0xC07830});
     }},
    // seven candles in an arch standing in the window, one of them flickering a little
    {"candles", 7, 5, CANDLES_MS,
     [](int f, const Canvas &c) {
       rows(c, "...c|..crc|.crrrc|crrrrrc|rrrrrrr", "cr", {0xFFFFEE, 0x443322});
       if (f == 1 || f == 3)
         c.put(f == 1 ? 1 : 5, 2, 0xFFCC66);
       if (f == 2)
         c.put(3, 0, 0xFFFFFF);
     }},
    // ---- animals and people
    // A bull moose walking along the meadow, and grazing there
    {"moose", 24, 21, MOOSE_MS, [](int f, const Canvas &c) { moose_pose(c, f, -1); }},
    {"moosegraze", 24, 21, MOOSEGRAZE_MS, [](int f, const Canvas &c) { moose_pose(c, 0, f); }},
    // A reindeer grazing its way along, grey-brown with a pale neck and mane, a white rump and tail, a darker face, big
    // antlers swept back with a brow tine forward, short sturdy legs: head down a few steps, then up to look about
    {"reindeer", 15, 12, REINDEER_MS,
     [](int f, const Canvas &c) {
       static const int8_t X[] = {3, 5, 9, 11}, STEP[] = {1, 0, 0, 1, 0, 1, 1, 0, -1, 0, 0, -1, 0, -1, -1, 0,
                                                          1, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0,  0, 0,  0,  0};
       for (int i : {1, 2, 0, 3})  // near hind, far hind, far front, near front, the far ones first and darker
         line(c, X[i], 9, X[i] + STEP[f * 4 + i], 11, i == 0 || i == 3 ? 0x4A3C30 : 0x5C4E40);
       rows(c,
            f == 6 ? "........a.a|.......a.aa.a|........aa.ff|.........aPffff|...ddddddPPP|wwdGGGGGGPPP|.wGGGGGGGGPP|"
                     ".wGGGGGGGGPP|..sssssssPP"
                   : "|........a.a|.........a.a|..........aa|...dddddd..a|wwdGGGGGddPfa|.wGGGGGGGGPPf|.wGGGGGGGGPPff|"
                     "..sssssssPP.ff|............ff|.............f",
            "GdPwfas", {0x7E6E5A, 0x5C4E40, 0xD8D2C4, 0xF2EEE6, 0x4A3C30, 0xA89070, 0xA49680});
     }},
    // A red squirrel bounding along, its bushy tail bobbing behind it: gathered, off, stretched out, landing
    {"squirrel", 6, 3, SQUIRREL_MS,
     [](int f, const Canvas &c) {
       static const char *const LOOK[] = {"hh..r|hrrrrr|..dd.b", ".hh..r|h.rrrr|.d...b", "hh...r|.hrrrr|d....b",
                                          "h...r|hhrrrr|...d.b"};
       rows(c, LOOK[f], "rhdb", {0xB4552A, 0xD9843F, 0x7A3418, 0xF0E4C8});
     }},
    // and sitting up with its tail curled over its back, nibbling a nut in its paws, now and then looking about
    {"squirrelnut", 4, 5, SQUIRRELNUT_MS,
     [](int f, const Canvas &c) {
       static const char *const KEYS = "rhbn";
       std::initializer_list<uint32_t> cols = {0xB4552A, 0xD9843F, 0xF0E4C8, 0x8C6A3C};
       // the nut up at its mouth, lowered, and looking away
       static const char *const HEAD[] = {"...r|..rr|..rn", "...r|..rr|..r|...n", "..r|..r|..r|...n"};
       rows(c, pose<SQUIRRELNUT_MS>("000000000010", f) ? "h|hh|h|h" : "hh|h|h|h", KEYS, cols);
       rows(c, "|||.rrb|.rr", KEYS, cols);
       rows(c, HEAD[pose<SQUIRRELNUT_MS>("010101012111", f)], KEYS, cols);
     }},
    // A red fox trotting across the snow, its brush out behind it
    {"fox", 11, 5, FOX_MS, [](int f, const Canvas &c) { fox_pose(c, f, 0); }},
    // and stopping to look at you, then ahead again before it trots on
    {"foxlook", 11, 5, FOXLOOK_MS, [](int f, const Canvas &c) { fox_pose(c, 4, pose<FOXLOOK_MS>("01210", f)); }},
    // The mountain hare in its winter white, only its ear tips black: hopping, gathered, off, in the air, landing
    {"harew", 7, 4, HARE_MS,
     [](int f, const Canvas &c) {
       static const char *const LOOK[] = {"....tw|..wWWWW|.wwwwws|..ss.s", ".....tW|wwWWWWW|ss...s",
                                          ".....tW|.wWWWWW|s.....s", "|....tw|.wWWWW|s.wwws|....s"};
       rows(c, LOOK[f], "Wwst", {0xFFFFFF, 0xE6EAF0, 0xAEB8C8, 0x2A2A2A});
     }},
    // and sitting up, its ears up, one ear back, its nose going
    {"haresitw", 5, 6, HARESIT_MS,
     [](int f, const Canvas &c) {
       static const char *const EARS[] = {"...t|...W", "....t|...W", "...t|...W"};
       rows(c, EARS[f], "Wwst", {0xFFFFFF, 0xE6EAF0, 0xAEB8C8, 0x2A2A2A});
       rows(c, f == 2 ? "||..WWW|.wWWW|wwwws|.ss.s" : "||..WWW|.wWWs|wwwws|.ss.s", "Wwst",
            {0xFFFFFF, 0xE6EAF0, 0xAEB8C8, 0x2A2A2A});
     }},
    // A roe buck at the edge of the meadow, small and slender: grazing, then its head up to look, ears twitching
    {"roedeer", 9, 7, ROEDEER_MS,
     [](int f, const Canvas &c) {
       static const char *const LOOK[] = {"||.whhhhh|..rrrrrr|.......rs|.......hh|........k",  // grazing
                                          "||.whhhhh|..rrrrrr|.......rs|.......hk",            // a tug
                                          "......sa|......hhk|.whhhhr|..rrrrr",                // up
                                          ".....s.a|......hhk|.whhhhr|..rrrrr"};               // an ear back
       roe(c, "||||..sd.ds|..sd.ds|..sd.ds");
       roe(c, LOOK[pose<ROEDEER_MS>("010123232", f)]);
     }},
    // and off in bounds, its white rump flashing
    {"roedeerrun", 10, 7, ROEDEERRUN_MS,
     [](int f, const Canvas &c) {
       static const char *const LOOK[] = {"|......sa|......hhk|.whhhhr|.wrrrrr|...sds|...s.s",
                                          "......sa|......hhk|whhhhhr|wwrrrrrs|.s.....ds|s........s",
                                          "......sa|......hhk|whhhhhr|wwrrrrrs|.ds...dss|s",
                                          "......sa|......hhk|.whhhhr|wwrrrrrr|..ds..ds|....s..s|.......s"};
       roe(c, LOOK[f]);
     }},
    // Three white sheep grazing on the hillside in summer, the middle one facing the other way, each lifting its dark
    // head from the grass now and then
    {"sheep", 19, 4, SHEEP_MS,
     [](int f, const Canvas &c) {
       static const int8_t AT[] = {0, 0, 8, 1, 15, 0};
       int head[] = {pose<SHEEP_MS>("02200011", f), pose<SHEEP_MS>("00222000", f), pose<SHEEP_MS>("00001110", f)};
       for (int i = 0; i < 3; i++) {  // 0 grazing, 1 level, 2 up
         int x = AT[i * 2], y = AT[i * 2 + 1];
         rows(c, "WWW|www|s.s", "Wws", {0xF4F2EA, 0xD6D2C6, 0xA8A498}, x + (i == 1), y);
         c.put(i == 1 ? x : x + 3, y + 2 - head[i], 0x2A2420);
       }
     }},
    // Someone on a kick-sled facing right, in a red jacket and a knitted hat with a bobble: standing on a runner
    // behind the chair with their hands on the handlebar, kicking off twice with one foot, then gliding with both on
    // the runners
    {"kicksled", 14, 12, KICKSLED_MS,
     [](int f, const Canvas &c) {
       static const char *const KEYS = "whjdstkmb";
       std::initializer_list<uint32_t> cols = {0xEEEEEE, 0x3A6ABF, 0xC8342C, 0x8E2420, 0xFFCC99,
                                               0x60606F, 0x222222, 0x99AABB, 0xA06A30};
       rows(c,
            "......w|.....hh|.....hs|....jjj|....jjjd|...jjj..hh|...jjj...h|...tk....hbbb|....k....h..h|....k....h..h|"
            "....kk...h..h|.mmmmmmmmmmmm",
            KEYS, cols);
       c.put(13, 10, 0x99AABB);
       static const char *const LEG[] = {"....t|.....t|......kk", "...t|..t|.kk", "...t|..t|kt",
                                          ".....t|.....t|.....kk"};
       int k = pose<KICKSLED_MS>("0120123", f);
       rows(c, LEG[k], KEYS, cols, 0, k == 3 ? 8 : 9);
     }},
    // Two children in snowsuits on a wooden sledge, sliding down a slope to the right, the sledge tilted with it: the
    // snow sprays out behind, and over a bump it lifts and lands
    {"sledge", 11, 9, SLEDGE_MS,
     [](int f, const Canvas &c) {
       static const char *const KEYS = "wsrbkpg";
       std::initializer_list<uint32_t> cols = {0xB8823C, 0xFFCC99, 0xDD3333, 0x2E6FD8, 0x333333, 0xFFFFFF, 0xB8C8DC};
       int b = f == 3 ? 0 : 1;  // up off the bump
       rows(c, "....r|....s|...bbb.b|...bbbbs|....bbrrrk|.......rr", KEYS, cols, 0, b);
       line(c, 3, 5 + b, 9, 6 + b, 0xB8823C);
       line(c, 3, 6 + b, 9, 7 + b, 0x333333);
       c.put(10, 6 + b, 0x333333);
       static const char *const SPRAY[] = {"p|.g|p.g", ".p|g|.gp", "||..g", "g.p|.p.|gpg"};
       rows(c, SPRAY[pose<SLEDGE_MS>("01023", f)], KEYS, cols, 0, 4);
     }},
    // A hiker on a trip in the mountains, walking to the right: a red anorak, a rucksack, knickers and a walking
    // stick planted ahead every other step
    {"hiker", 13, 14, HIKER_MS,
     [](int f, const Canvas &c) {
       int up = f % 2;  // up a row passing the other foot
       static const int8_t STICK[] = {11, 13, 10, 13, 9, 13, 11, 11};
       line(c, 9, 3 - up, STICK[f * 2], STICK[f * 2 + 1], 0xB08A50);
       rows(c, ".....oo|.....oss|..ooddrr|..oordrr|..oordrr.r|..oordrrr|...odrr|....rrr", "rdos",
            {0xCC3322, 0x992219, 0x6A6A3A, 0xFFCC99}, 0, 1 - up);
       // the legs from the hip: the near one's knee and foot, then the far one's
       static const int8_t LEG[] = {7, 10, 8, 12, 5, 10, 3, 12, 6, 10, 6, 12, 6, 10, 5, 11,
                                    5, 10, 4, 12, 7, 10, 8, 12, 6, 10, 5, 11, 6, 10, 6, 12};
       const int8_t *l = LEG + f * 8;
       for (int k = 1; k >= 0; k--) {
         int hip = 9 - up, kx = l[k * 4], ky = l[k * 4 + 1] - up, fx = l[k * 4 + 2], fy = l[k * 4 + 3];
         uint32_t col = k ? 0x6A5C42 : 0x8A7A5A;
         line(c, 6, hip, kx, ky, col);
         line(c, kx, ky, fx, fy, col);
         dots(c, {(int8_t) fx, (int8_t) (fy + 1), (int8_t) (fx + 1), (int8_t) (fy + 1)}, 0x3A2A1A);
       }
     }},
    // Out picking mushrooms in the autumn, to the right with a basket on the arm, in a knitted hat and green rubber
    // boots: two strides, then a stop by a chanterelle, bending down to it and picking it, and up again
    {"mushroomer", 13, 14, MUSHROOMER_MS,
     [](int f, const Canvas &c) {
       static const char *const KEYS = "hsjdtgbcy";
       std::initializer_list<uint32_t> cols = {0xAA3333, 0xFFCC99, 0x3A62B0, 0x2A4A88, 0x3A3F55,
                                               0x2F6A3A, 0xB88A4A, 0xE8A030, 0x7A5A2A};
       // 0-3 walking, 4 by the chanterelle, 5 bending to it, 6 picking it, 7 up with it, 8 standing
       int p = pose<MUSHROOMER_MS>("0123012345678", f), up = p < 4 ? p % 2 : 0;
       if (p >= 4 && p <= 6)
         rows(c, "ccc|.c", KEYS, cols, 10, 12);
       if (p >= 5 && p <= 7) {  // bent at the hips, the basket swinging down to the ground with the arm
         static const char *const BENT[] = {
             "|||.......hh|......dhss|.....jjjd|....jjjjd|....jjjd|...tttdy|...t.tbccb|...t.t.bb|...t.t|...g.g|...gggg",
             "||||||....jjjjj|...jjjjjhh|...ttjjdhss|...t.t.d|...t.t.ybby|...t.t.bccb|...g.g..bb|...gggg"};
         rows(c, BENT[p == 6], KEYS, cols);
         return;
       }
       rows(c, ".....hh|.....hss|....jjd|....jjd|....jjd|....jjdy|....jjjbccb|.......bbbb|........bb",
            KEYS, cols, 0, 1 - up);
       // the legs from the hip: the near one's knee and foot, then the far one's
       static const int8_t LEG[] = {7, 11, 8, 12, 4, 11, 3, 12, 6, 11, 6, 12, 6, 11, 5, 11, 4, 11, 3, 12,
                                    7, 11, 8, 12, 6, 11, 5, 11, 6, 11, 6, 12, 5, 11, 5, 12, 4, 11, 4, 12};
       const int8_t *l = LEG + (p < 4 ? p : 4) * 8;
       for (int k = 1; k >= 0; k--) {
         int hip = 9 - up, kx = l[k * 4], ky = l[k * 4 + 1] - up, fx = l[k * 4 + 2], fy = l[k * 4 + 3];
         line(c, 5, hip, kx, ky, k ? 0x2A2E40 : 0x3A3F55);
         line(c, kx, ky + 1, fx, fy, 0x2F6A3A);
         dots(c, {(int8_t) fx, (int8_t) (fy + 1), (int8_t) (fx + 1), (int8_t) (fy + 1)}, 0x2F6A3A);
       }
     }},
    // A robot lawnmower, the front to the right: it drives along with its light blinking, stops, and turns toward
    // you, ready to go off either way
    {"mower", 5, 3, MOWER_MS,
     [](int f, const Canvas &c) {
       static const char *const LOOK[] = {".hhg|sssss|kk.k", ".hgh|sssss|kk..k", ".hgh|sssss|k...k"};
       int k = f < 5 ? 0 : f - 4;
       rows(c, LOOK[k], "hsgk", {0xC4C9D0, 0x8C929A, f == 1 || f == 3 ? 0xC4C9D0u : 0x55EE77u, 0x18191B});
     }},
    {"skier", 14, 14, SKIER_MS,
     [](int f, const Canvas &c) {
       bool left = f < 2, glide = f % 2 == 1;
       dots(c, {8, 0, 9, 0}, 0xCC2222);
       dots(c, {8, 1, 9, 1}, 0xFFCCAA);
       for (int y = 2; y <= 6; y++) {
         c.put(7 + (y < 4), y, 0x224499);
         c.put(8, y, 0x224499);
       }
       line(c, 8, 3, glide ? 10 : left ? 11 : 5, 8, 0x9999AA);  // poles
       line(c, 7, 3, glide ? 7 : left ? 5 : 11, 8, 0x9999AA);
       line(c, 8, 7, glide ? 8 : left ? 10 : 6, 11, 0x333344);  // legs
       line(c, 7, 7, glide ? 7 : left ? 5 : 10, 11, 0x333344);
       for (int x = 1; x <= 13; x++)
         c.put(x, 12, 0xAA6633);
       for (int x = left ? 1 : 0; x <= (left ? 5 : 4); x++)  // the front ski's tip, turned up
         c.put(x, 11, 0xAA6633);
     }},
    // Snow slipping off the end of a spruce's tier or the eaves: the clump sags, drops in a puff of powder that
    // drifts down and thins, and the bare branch springs up. Its top row lies on the tier's last 4 px. The clump
    // is in shadow, so it shows against snow too.
    {"snowdrop", 9, 22, SNOWDROP_MS,
     [](int f, const Canvas &c) {
       const uint32_t T = 0x224444, S = 0x98A8C4, P = 0xCCD6E8;
       for (int x = 0; x < 4; x++)
         c.put(x, 0, f ? T : S);
       if (f <= 1) {  // the clump, then sagging
         dots(c, {2, 1, 3, 1}, S);
         if (f)
           dots(c, {4, 1, 3, 2, 4, 2}, S);
         return;
       }
       if (f == 8)
         return;
       static const uint8_t DROP[] = {0, 0, 4, 8, 12, 15, 17, 19}, SPREAD[] = {0, 0, 5, 8, 12, 15, 17, 19};  // fifths
       int drop = DROP[f], spread = SPREAD[f];
       if (f <= 3)
         dots(c, {3, (int8_t) drop, 4, (int8_t) drop, 3, (int8_t) (drop + 1)}, S);
       Rng r = seeded((uint32_t) (70 + f));
       for (int k = 0; k < 9 - f; k++)
         c.put(3 + r.in(-spread, spread) / 5, drop - 1 - r.in(0, 10 + 2 * spread) / 5, r.in(0, 1) ? P : S);
       if (f <= 4)
         dots(c, {4, 0}, T);
     }},
    // A birch leaf tumbling along for 3 s, then lying still on the grass for 5, in step with its wander
    {"leaf", 6, 6, LEAF_MS, [](int f, const Canvas &c) { leaf_pose(c, f); }},
    // and one tumbling all the way down from the crown
    {"leaffall", 6, 6, LEAFFALL_MS, [](int f, const Canvas &c) { leaf_pose(c, f); }},
    // The midsummer bonfire: a pyre of stacked logs with the flames licking up through it
    {"pyre", 25, 28, PYRE_MS,
     [](int f, const Canvas &c) {
       Rng r = seeded((uint32_t) (100 + f));
       for (int y = 0; y < 22; y++) {  // the flames over and through the pyre
         float t = y / 21.0f, wd = 7.5f * sqrtf(t * sqrtf(t)) * (0.875f + 0.25f * r.f()) + 0.5f,
               cx = 12 + 1.5f * sinf(y * 0.5f + f * 1.3f) * (1 - t);
         for (int x = 0; x < 25; x++) {
           float d = fabsf(x - cx);
           if (d > wd || (y < 6 && r.in(0, 9) < 4))
             continue;
           c.put(x, y, d < wd * 0.22f && y > 13 ? 0xFFFFDD : d < wd * 0.7f ? 0xFFCC33 : y < 7 ? 0xDD4422 : 0xFF7711);
         }
       }
       for (int k = 0; k < 7; k++) {  // logs leaning in, dark against the fire
         int yb = 25 - k * 2, hw = (110 - k * 14) / 10, top = hw * 7 / 20;
         line(c, 12 - hw, yb, 12 - top, yb - 6, 0x222211);
         line(c, 12 + hw, yb, 12 + top, yb - 6, 0x222211);
       }
       for (int x = 1; x <= 23; x++) {
         c.put(x, 26, 0x222211);
         c.put(x, 27, x % 3 ? 0x333322 : 0x222211);
       }
       for (int x = 6; x <= 18; x += 6)  // embers
         c.put(x + f % 2, 25, 0xFFBB44);
     }},
    {"spark", 2, 2, SPARK_MS,
     [](int f, const Canvas &c) {
       if (f == 0)
         c.put(0, 0, 0xFFDD66);
       if (f == 1) {
         c.put(0, 0, 0xFFEE99);
         c.put(1, 1, 0xFFBB44);
       }
       if (f == 2)
         c.put(1, 0, 0xFFBB44);
     }},
    // ---- holidays
    {"treestar", 9, 9, TREESTAR_MS,
     [](int f, const Canvas &c) {
       static const int8_t P[2][16] = {{4, 3, 4, 5, 3, 4, 5, 4, 4, 2, 4, 6, 2, 4, 6, 4},
                                       {3, 3, 5, 5, 3, 5, 5, 3, 2, 2, 6, 6, 2, 6, 6, 2}};
       c.put(4, 4, 0xFFFFFF);
       for (int i = 0; i < 8; i++)
         c.put(P[f == 1][i * 2], P[f == 1][i * 2 + 1], i < 4 ? 0xFFFFDD : 0xFFDD66);
       if (f != 1)
         dots(c, {4, 1, 4, 7, 1, 4, 7, 4}, 0xDDAA33);
     }},
    // the park spruce's small star
    {"citystar", 5, 5, CITYSTAR_MS,
     [](int f, const Canvas &c) {
       rows(c, f == 1 ? "y...y|.yWy|yWWWy|.yWy|y...y" : "..y|.yWy|yWWWy|.yWy|..y", "yW",
            {f == 2 ? 0xFFDD66u : 0xEECC44u, 0xFFFFEE});
     }},
    // the sleigh by night, its colours dimmed like the rest of the night, the moon on the backs and the rail,
    // Rudolph's nose still bright
    {"sleigh", 37, 12, SLEIGH_MS,
     [](int f, const Canvas &c) {
       const uint32_t gold = 0x887755, red = 0x773333, deer = 0x554433, dark = 0x332211, moon = 0x887766;
       for (int x = 1; x <= 13; x++)  // the runner
         c.put(x, 9, gold);
       dots(c, {0, 8, 14, 8}, gold);
       for (int x = 2; x <= 12; x++) {
         c.put(x, 4, moon);  // the rail
         for (int y = 5; y <= 8; y++)
           c.put(x, y, y == 5 ? red : 0x662222);
       }
       for (int x = 4; x <= 7; x++)  // the sack
         for (int y = 2; y <= 4; y++)
           c.put(x, y, 0x443322);
       for (int y = 0; y <= 4; y++) {  // and him
         c.put(9, y, y ? red : 0xAA8877);
         c.put(10, y, red);
       }
       dots(c, {11, 0, 10, 2, 11, 2}, 0x9999AA);
       line(c, 13, 6, 18, 6, 0x554433);  // the trace
       static const int8_t S[] = {1, -1, 0, 0, -1, 1, 0, 0};
       for (int ox = 18; ox <= 27; ox += 9) {  // two reindeer
         for (int x = ox; x <= ox + 6; x++) {
           c.put(x, 4, moon);
           for (int y = 5; y <= 6; y++)
             c.put(x, y, deer);
         }
         dots(c, {(int8_t) (ox + 7), 4, (int8_t) (ox + 8), 4, (int8_t) (ox + 8), 3}, deer);
         c.put(ox + 7, 3, moon);
         c.put(ox + 9, 4, ox == 27 ? 0xFF3333 : deer);
         dots(c, {(int8_t) (ox + 7), 2, (int8_t) (ox + 8), 1, (int8_t) (ox + 6), 1}, dark);
         line(c, ox + 1, 7, ox + 1 + S[f * 2], 9, dark);
         line(c, ox + 6, 7, ox + 6 + S[f * 2 + 1], 9, dark);
       }
     }},
    // a rocket with a short spitting trail
    {"rocket", 3, 9, ROCKET_MS,
     [](int f, const Canvas &c) {
       c.put(1, 0, 0xFFFFEE);
       c.put(1, 1, 0xFFDD88);
       Rng r = seeded((uint32_t) (30 + f));
       for (int y = 2; y < 9; y++)
         if (r.in(0, 9) < 10 - y)
           c.put(1 + (r.in(0, 3) ? 0 : r.in(0, 1) * 2 - 1), y, y < 4 ? 0xFFBB55 : y < 6 ? 0xCC8844 : 0x885533);
     }},
    // Easter chicks peck and potter about
    {"chick", 6, 6, CHICK_MS,
     [](int f, const Canvas &c) {
       static const char *const LOOK[] = {"..yy|.yyeyb|yyyy|yyyy|.yy|.l.l", "|..yy|.yyey|yyyyyb|.yy|.l.l",
                                          "..yy|.yyeyb|yyyy|yyyy|.yy|.l.l", ".yy|yyeyb|yyy|yyy|.yy|.l.l"};
       rows(c, LOOK[f], "yebl", {0xFFEE55, 0x222222, 0xFF9922, 0xFF9922});
     }},
    // A hare three chicks long, hopping: crouched, pushing off, stretched out in the air, landing
    {"hare", 21, 12, HARE_MS,
     [](int f, const Canvas &c) {
       static const char *const LOOK[] = {
           "..............ee|...............ee|.............hhhh|......bbbbbbbbhhhhk|...bbbbbbbbbbbbbb|"
           ".wbbbbbbbbbbbbbb|..bbbbbbbbbbbb|...ll..........l|..lll.........ll",
           "................ee|.................ee|...............hhhh|....bbbbbbbbbbbbhhhhk|.bbbbbbbbbbbbbbbbb|"
           "wbbbbbbbbbbbbbbb|.lll.........ll|lll",
           "...............ee|................ee|..............hhhh|bbbbbbbbbbbbbbbhhhhk|.bbbbbbbbbbbbbbbb|"
           "wwbbbbbbbbbbbbb|lll............lll|...............ll",
           ".............ee|..............ee|............hhhh|.......bbbbbbhhhhk|...bbbbbbbbbbbbbb|"
           ".wbbbbbbbbbbbbbbb|..bbbbbbbbbbbbbb|.lll.........lll|lll"};
       rows(c, LOOK[f], "bhekwl", {0x998866, 0xAA9977, 0x887755, 0x332222, 0xEEEEEE, 0x776655}, 0,
            f == 1 || f == 2 ? 1 : 3);
     }},
    // sitting up, ears twitching, nose going
    {"haresit", 21, 12, HARESIT_MS,
     [](int f, const Canvas &c) {
       static const char *const EARS[] = {"...........e.e|...........e.e|...........e.e",
                                          "............ee|...........e|...........e.e",
                                          "...........e.e|...........e.e|...........e.e"};
       const char *body = f == 2 ? "..........hhhh|........bbhhhh.k|....bbbbbbbbb|"
                                   "...wbbbbbbbbb|....bbbbbbbbb|....ll...lll"
                                 : "..........hhhh|........bbhhhhk|....bbbbbbbbb|"
                                   "...wbbbbbbbbb|....bbbbbbbbb|....ll...lll";
       rows(c, EARS[f], "e", {0x887755}, 2, 3);
       rows(c, body, "bhkwl", {0x998866, 0xAA9977, 0x332222, 0xEEEEEE, 0x776655}, 2, 6);
     }},
    // 17 May: a few children walking in a row, each waving a small flag, the first with a drum
    {"parade", 34, 13, PARADE_MS,
     [](int f, const Canvas &c) {
       static const uint32_t COAT[] = {0x3366CC, 0xDD3333, 0xEEEEEE, 0x33AA55},
                             SKIN[] = {0xFFCC88, 0xEECC99, 0xFFCCAA, 0xDDBB88};
       static const char *const FLAG[] = {"rwbr|rwbr|wwbw", "rrw|rrw|www", "rwb|wwb|rwb"};
       for (int i = 0; i < 4; i++) {
         int x0 = i * 8, step = (f + i) % 2, wave = (f / 2 + i) % 3, y0 = 5;
         for (int x = x0 + 2; x <= x0 + 3; x++) {
           c.put(x, y0, 0x664422);  // hair
           c.put(x, y0 + 1, SKIN[i]);
         }
         for (int y = y0 + 2; y <= y0 + 4; y++)
           for (int x = x0 + 1; x <= x0 + 4; x++)
             c.put(x, y, COAT[i]);
         for (int s : {x0 + 1 + step, x0 + 4 - step}) {
           c.put(s, y0 + 5, 0x333355);
           c.put(s, y0 + 6, 0x222222);
         }
         if (i == 0) {  // the drum
           dots(c, {(int8_t) (x0 + 5), (int8_t) (y0 + 3), (int8_t) (x0 + 6), (int8_t) (y0 + 4)}, 0xEEEEEE);
           dots(c, {(int8_t) (x0 + 6), (int8_t) (y0 + 3), (int8_t) (x0 + 5), (int8_t) (y0 + 4)}, 0xDD3333);
           continue;
         }
         int sx = x0 + 5, top = y0 - 4 + (wave == 1);
         for (int y = top; y <= y0 + 2; y++)
           c.put(sx, y, 0x887755);
         rows(c, FLAG[wave], "rwb", {0xDD2233, 0xEEEEEE, 0x112255}, sx + 1, top);
       }
     }},
    // a balloon that got away, rising and swaying on its string
    {"balloon", 7, 13, BALLOON_MS,
     [](int f, const Canvas &c) {
       rows(c, ".rrr|rrrrr|rwrrr|rrrrr|.rrr|..r", "rw", {0xDD2233, 0xFF9999}, 1, 0);
       int sway = pose<BALLOON_MS>("1210", f) - 1;
       for (int y = 6; y < 13; y++)
         c.put(3 + iround(sway * sinf((y - 6) * kPiF / 6)), y, 0xCCCCCC);
     }},
    // Instead of smoke, hearts: a tiny one out of the chimney, growing, beating as it rises, then fading out.
    // Drawn at each size, never scaled
    {"heart", 9, 8, HEART_MS,
     [](int f, const Canvas &c) {
       static const char *const H[] = {"x.x|.x", ".x.x|xxxxx|.xxx|..x", ".xx.xx|xhxxxxx|xxxxxxx|.xxxxx|..xxx|...x",
                                       ".xxx.xxx|xhxxxxxxx|xxxxxxxxx|xxxxxxxxx|.xxxxxxx|..xxxxx|...xxx|....x"};
       int s = pose<HEART_MS>("0123223222", f), o = 3 - s;  // o centres it
       rows(c, H[s], "xh", {f == 3 || f == 6 ? 0xFF5588u : 0xEE4477u, 0xFFBBDD}, o, o);
       for (int i = 0; f >= 8 && i < c.w * c.h; i++)  // thinning out, a little and then most of it
         if (noise(i % c.w - o, i / c.w - o, 3) < (f == 8 ? 25 : 60))
           c.px[i] = 0;
     }},
    // the owl, gliding with slow deep beats, its pale face and eyes catching what light there is
    {"owl", 15, 8, OWL_MS,
     [](int f, const Canvas &c) {
       static const int8_t TIP[] = {3, 1, 0, 0, 3, 3, 0, 3, 3, 4, 1, 6, 3, 3, 0, 4};  // up, mid, down, glide
       const int8_t *t = TIP + pose<OWL_MS>("012133", f) * 4;
       for (int s = 0; s < 2; s++) {  // the wings two rows thick, the moon on their leading edge
         auto m = [&](int x) { return s ? 14 - x : x; };
         line(c, m(5), 3, m(t[0]), t[1], 0x776655);
         line(c, m(5), 4, m(t[0]), t[1] + 1, 0x443322);
         line(c, m(t[0]), t[1], m(t[2]), t[3], 0x776655);
         line(c, m(t[0]), t[1] + 1, m(t[2]), t[3] + 1, 0x443322);
       }
       for (int x = 5; x <= 9; x++)
         for (int y = 2; y <= 5; y++)
           c.put(x, y, 0x554433);
       dots(c, {6, 2, 7, 2, 8, 2, 7, 3}, 0x998877);
       dots(c, {6, 3, 8, 3}, 0xFFCC33);
       c.put(7, 4, 0x776655);
       dots(c, {7, 6, 6, 6, 8, 6}, 0x443322);
     }},
    // and perched on a spruce top: it sits, turns its head right round and back, blinks, hoots
    {"owlperch", 9, 11, OWLPERCH_MS,
     [](int f, const Canvas &c) {
       static const char *const HEAD[] = {
           "..b...b|..bbbbb|.bfffffb|.bfefefb|.bffkffb", "..b...b|..bbbbb|.bbbffff|.bbbefef|.bbbffkf",
           "..b...b|..bbbbb|.bbbbbbb|.bbbbbbb|.bbbbbbb", "..b...b|..bbbbb|.bfffffb|.bfffffb|.bffkffb",
           "..b...b|..bbbbb|.bfffffb|.bfefefb|.bffkffb", "...b...b|...bbbbb|..bfffffb|..bfefefb|..bffkffb",
           "..b...b|..bbbbb|.bfffffb|.bfefefb|.bffoffb"};
       static const char *const KEYS = "bfekowt";
       std::initializer_list<uint32_t> cols = {0x554433, 0x998877, 0xFFCC33, 0x443322, 0x221111, 0x776655, 0x443322};
       rows(c, HEAD[pose<OWLPERCH_MS>("012103456", f)], KEYS, cols);
       rows(c, ".bbbbbbb|.bwbbbwb|.bbwbwbb|..bbbbb|...t.t|..ttttt", KEYS, cols, 0, 5);
     }},
    // The pumpkin's candle: the picture's FACE lights up and dims all at once, brighter at the bottom where the
    // flame is
    {"pumpkin", 11, 7, PUMPKIN_MS,
     [](int f, const Canvas &c) {
       static const uint32_t COL[] = {0xDD8822, 0xFFAA33, 0xFFCC44, 0xFFDD66, 0xFFEE88};
       for (int y = 0; y < 7; y++)
         for (int x = 0; x < 11; x++)
           if (FACE.row[y] >> (10 - x) & 1)
             c.put(x, y, COL[std::min(4, pose<PUMPKIN_MS>("213021", f) + (y > 3))]);
     }},
    // ---- the rare ones, see met_live_rare.h
    // A lynx facing right on long legs, grey-tawny with a spot or two, its ears tufted and its short tail black at the
    // tip: it walks three strides, stops, looks at you and behind it, and looks ahead again to walk on
    {"lynx", 8, 5, LYNX_MS,
     [](int f, const Canvas &c) {
       static const char *const KEYS = "ohspwk";
       std::initializer_list<uint32_t> cols = {0xA8906E, 0xC4AC88, 0x806A50, 0x5E4C3A, 0xE2D8C6, 0x1E1A16};
       static const int8_t STEP[] = {-1, 1, -1, 1, 0, 0, 0, 1, 1, -1, 1, -1, 0, 0, 1, 0};  // the feet from the hips
       int walk = f < 12 ? f % 4 : -1;
       for (int i : {1, 2, 0, 3}) {  // near hind, far hind, far front, near front, the far ones first and darker
         int x = i < 2 ? 1 + i : 3 + i, d = walk < 0 ? 0 : STEP[walk * 4 + i];
         int up = (walk == 1 && i == 0) || (walk == 3 && i == 1);
         line(c, x, 3, x + d, 4 - up, i == 1 || i == 2 ? 0x6E5C46 : 0x806A50);
       }
       rows(c, "|khhhhh|.oopoop", KEYS, cols);
       static const char *const HEAD[] = {"......k|.....hoo|.....oow", ".....k.k|.....ooo|.....wkw",
                                          "......k|.....ooh|.....woo"};  // ahead, at you, behind it
       rows(c, HEAD[f < 12 ? 0 : "01210"[f - 12] - '0'], KEYS, cols);
     }},
    // Two orcas heading right in a fjord, surfacing in turn: a blow of mist, the back with its dorsal fin, the bull's
    // tall and straight, the cow's lower and curved back, the grey saddle behind it. Then the cow lifts her flukes and
    // slaps them down in a splash. The water at row 7
    {"orcas", 24, 8, ORCAS_MS,
     [](int f, const Canvas &c) {
       static const char *const KEYS = "khwgmnr";
       std::initializer_list<uint32_t> cols = {0x16181E, 0x3E4656, 0xF0F4F8, 0x98A2B0, 0xD8E0E8, 0xAAB6C4, 0x8FAAD4};
       static const char *const BULL[] = {"||||||......kkh|.....w....w", "|||||....k|....kk|..hkkkkk|.w......w",
                                          "||....k|....k|....k|...kk|.kgkkkkh|w........w",
                                          "||||...k|..kk|.kgkkk|r.....r", "|||||||r.r..r.r"};
       static const char *const COW[] = {"||||||.....kkh|....w....w", "||||||..kkkkh|.w......w",
                                         "|||...k|....k|....kk|..kgkkkkh|w.......w", "|||||..k|.kgkk|r....r"};
       static const char *const BLOW[] = {"|.n.n|.mmn|nmmm|.mn|..m", "n...n|.n.n|..n.n|...n"};
       static const char *const TAIL[] = {"||||||...kk|..kkkk|.w...w", "|||k..k|.kk|..k|..k|.w.w",
                                          "|||.w...w|..w.w|w.www.w|.wwwww", "|||||.w..w|..ww|.wwww",
                                          "|||||||r.r..r"};
       int bull = pose<ORCAS_MS>("-01234----------", f), cow = pose<ORCAS_MS>("------0123------", f),
           tail = pose<ORCAS_MS>("-----------01234", f);
       if (bull >= 0)
         rows(c, BULL[bull], KEYS, cols);
       if (bull >= 0 && bull < 2)
         rows(c, BLOW[bull], KEYS, cols, 4);
       if (cow >= 0)
         rows(c, COW[cow], KEYS, cols, 13);
       if (cow >= 0 && cow < 2)
         rows(c, BLOW[cow], KEYS, cols, 16, 1);
       if (tail >= 0)
         rows(c, TAIL[tail], KEYS, cols, 14);
     }},
    // The sea eagle's rare catch, from the side and facing right, the water at its bottom row: it glides down with its
    // talons out, throws its wings up and strikes, the spray flies round its feet, and it beats up off the water with
    // a fish and flies off with it. The show brings it down to the water over the first frame, holds it there to the
    // end of the first beat, so the spray stays on the water, and takes it away over the rest
    {"seaeaglecatch", 12, 10, SEAEAGLECATCH_MS,
     [](int f, const Canvas &c) {
       static const char *const KEYS = "dblpykwsaf";
       std::initializer_list<uint32_t> cols = {0x2E2218, 0x5A4430, 0x7A6048, 0xDCCCA4, 0xF0C030,
                                               0x1A1A1A, 0xF4F4F0, 0xFFFFFF, 0xA8D0EC, 0xD0D8E0};
       // gliding, its wings high to strike, a beat down and up with the fish
       static const char *const LOOK[] = {
           "..dd|...ddd|wwbbbbpy|...ddd|.....ky", "d|.d..d|.dddd|..dddd|.wbbbpy|...bb|....yk",
           "|||.wwbbbbpy|..dddddd|...dddd|.....kff", ".dd|..ddd|...ddd|.wwbbbbpy|.....kff"};
       int p = pose<SEAEAGLECATCH_MS>("011123230", f);
       rows(c, LOOK[p], KEYS, cols, 0, f == 1 ? 1 : 2);
       if (f == 8)  // off with the fish
         rows(c, "||||.....kff", KEYS, cols, 0, 2);
       if (f == 2)  // the strike, the spray, falling back
         rows(c, "..s....s|...sssss", KEYS, cols, 0, 8);
       if (f == 3)
         rows(c, "s.......s|.s.....s|..s.s.s.s|.aassssaa", KEYS, cols, 0, 6);
       if (f == 4)
         rows(c, "..s..s..s", KEYS, cols, 0, 9);
     }},
    // A ski jumper flying to the right in the V style, lying forward over the skis with the arms back, wobbling a
    // little, then touching down in a telemark, arms out, and riding out upright. Its skis on the bottom row once down
    {"skijumper", 22, 13, SKIJUMPER_MS,
     [](int f, const Canvas &c) {
       const uint32_t suit = 0xD03030, shade = 0x8A1E1E, legs = 0x28407A, ski = 0x2A2A3A, helmet = 0xEEEEEE;
       int k = pose<SKIJUMPER_MS>("01020102345", f);
       if (k < 3) {  // in flight: the skis' tips apart, or wider, or the body a row lower
         int spread = k == 1, pitch = k == 2;
         line(c, 2, 11, 21, 4 - spread, ski);
         line(c, 2, 11, 21, 8 + spread, ski);
         line(c, 6, 9, 10, 6 + pitch, legs);
         line(c, 7, 9, 11, 6 + pitch, legs);
         line(c, 11, 5 + pitch, 15, 3 + pitch, suit);
         line(c, 11, 6 + pitch, 15, 4 + pitch, suit);
         line(c, 14, 5 + pitch, 9, 7 + pitch, shade);  // the near arm back along the body
         dots(c, {16, (int8_t) (2 + pitch), 17, (int8_t) (2 + pitch), 16, (int8_t) (3 + pitch)}, helmet);
         c.put(17, 3 + pitch, 0xFFCC99);
         return;
       }
       line(c, 1, 12, 20, 12, ski);
       c.put(21, 11, ski);
       static const char *const LOOK[] = {
           "||||...........ww|..........rrs|.........rrr|...sddddrrr|........ll|........lll|.........ll|........lll",
           "|.........ww|.........ws|....s....rr....s|.....dddrrrddd|.........rr|.........rr|........ll|"
           "........l.l|.......l...l|.......l...l|....lll....ll",
           "|.........ww|.........ws|.........rr|........rrrd|........rr.d|........rr..s|........ll|.........ll|"
           ".........ll|........ll|........lll"};
       rows(c, LOOK[k - 3], "wsdrl", {helmet, 0xFFCC99, shade, suit, legs});
     }},
    // The huldra at the edge of the forest, a fair young woman in a blue-green dress with her hair down to her
    // knees: she stands facing out, her hair stirring, then turns away and walks off to the right, her cow's tail
    // showing under the hem, and looks back over her shoulder once
    {"huldra", 9, 12, HULDRA_MS,
     [](int f, const Canvas &c) {
       static const char *const KEYS = "hHsdDwktT";
       std::initializer_list<uint32_t> cols = {0xF2DC8C, 0xC9A75A, 0xF2C8A8, 0x3E9A8E, 0x24685F,
                                               0xEEEEE6, 0x3A2E28, 0xD0A47A, 0xA87E58};
       int p = pose<HULDRA_MS>("0102343456563434", f);
       if (p < 2) {
         rows(c, "...hhH|..hssH|..hssHH|.hwddwH|.hwddwH|.hsddsH|.hdddDH|..ddddD|.dddddDD|.ddddddD|...s.s|...k.k", KEYS,
              cols);
         if (p == 1)  // a lock lifts in the breeze
           c.put(7, 4, 0xC9A75A);
         return;
       }
       if (p == 2) {  // turning
         rows(c, "...hhh|...hhhs|..hhhs|..hhdw|..hhdw|..hhds|..Hhddd|..ddddd|.ddddddd|.ddddddd|.t..s.s|TT..k.k", KEYS,
              cols);
         return;
       }
       rows(c, p > 4 ? "...hhh|..shhh|..shh" : "...hhh|...hhhs|...hhs", KEYS, cols);  // looking back, or ahead
       rows(c, "...hdw|...hdw|...hds|...Hddd|..ddddd|..dddddd|.ddddddd", KEYS, cols, 0, 3);
       if (p % 2)  // striding, the tail swung out behind
         rows(c, ".t.s...s|T..k...k", KEYS, cols, 0, 10);
       else  // passing, the tail hanging
         rows(c, "..t..ss|.TT..kk", KEYS, cols, 0, 10);
     }},
    // A nisse facing right in his grey homespun, a red cap and a white beard, his lantern lit: he walks on short
    // steps, stops, holds the lantern up and looks back and ahead, lowers it and walks on
    {"nisse", 8, 8, NISSE_MS,
     [](int f, const Canvas &c) {
       static const char *const KEYS = "rRsnwgGkL";
       std::initializer_list<uint32_t> cols = {0xD82A22, 0x962018, 0xF0B898, 0xE88870, 0xF2F2EE,
                                               0x7E7A76, 0x4E4A47, 0x2E2622, LAMP};
       static const char *const LOOK[] = {
           // walking, the lantern low
           "...r|..rrr|.Rrrrr|R.wsn|..www|.gwwgs|.gGgg.L|.k..k", "...r|..rrr|.Rrrrr|R.wsn|..www|.gwwgs|.gGgg.L|..kk",
           "...r|..rrr|.Rrrrr|R.wsn|..www|.gwwgs|.gGgg.L|.k.k",  // standing
           // holding it up, looking ahead and back
           "...r..L|..rrrsL|.Rrrrg|R.wsng|..wwwg|.gwwg|.gGgg|.k.k",
           "...r..L|..rrrsL|.rrrrg|.nswRg|..wwwg|.gwwg|.gGgg|.k.k"};
       rows(c, LOOK[pose<NISSE_MS>("01010101010123432010101010101", f)], KEYS, cols);
     }},
    // Draugen, facing right: a hunched fisherman in black oilskins with a clump of seaweed for a head, pushing his
    // oars facing the way he goes in half a boat, its broken end behind him. It rises and falls on the swell, the sea
    // at row 7
    {"draugen", 12, 8, DRAUGEN_MS,
     [](int f, const Canvas &c) {
       static const char *const KEYS = "ohrkKbgc";
       std::initializer_list<uint32_t> cols = {0x34362E, 0x7A806A, 0xD0D8E8, 0x52622E,
                                               0x8A9848, 0x6E6458, 0xA09484, 0xC8B898};
       static const char *const MAN[] = {// the stroke: hands in, pushing, out, and back
                                         "|...rrK|..rook|..rooh|..oooo", "|....rrK|...rook|...rooh|..ooooo",
                                         "|....rrK|...rook|...roooh|..ooooo", "|...rrK|..rook|..rooh|..oooo"};
       static const int8_t OAR[] = {6, 3, 9, 7, 7, 3, 7, 7, 8, 4, 5, 7, 7, 3, 9, 5};  // hands to blade
       int p = f % 4, dy = pose<DRAUGEN_MS>("00111100", f);
       rows(c, "..........b|..........b|.........b|.........b|........bb|cggggggggb|cbbbbbbbb", KEYS, cols, 0, dy);
       rows(c, MAN[p], KEYS, cols, 0, dy);
       const int8_t *o = OAR + p * 4;
       line(c, o[0], o[1] + dy, o[2], std::min(7, o[3] + dy), 0xB8A888);
       rows(c, dy ? ".ff.ff.ff.f" : "ff.fff.ff.ff", "f", {0xD8EAF0}, 0, 7);  // the foam
     }},
    // A garden trampoline tumbling end over end, a sixth of a turn a frame: its black mat in the blue padding, and
    // the legs bent out under it
    {"trampoline", 26, 26, TRAMPOLINE_MS,
     [](int f, const Canvas &c) {
       float a = 0.5f + (float) f * 6.2832f / 6, ca = cosf(a), sa = sinf(a);
       // x across, y up out of the mat and z through it, turned by a about x
       auto leg = [&](float x0, float y0, float z0, float x1, float y1, float z1) {
         line(c, iround(12.5f + x0), iround(12.5f - y0 * ca + z0 * sa), iround(12.5f + x1),
              iround(12.5f - y1 * ca + z1 * sa), 0xB0BCC8);
       };
       auto legs = [&] {
         for (float l : {1.57f, 3.67f, 5.76f}) {
           float x = cosf(l), z = sinf(l);
           leg(9.5f * x, 0, 9.5f * z, 12 * x, -4, 12 * z);  // out to the knee, then in to the foot
           leg(12 * x, -4, 12 * z, 10.5f * x, -8, 10.5f * z);
         }
       };
       if (sa > 0)
         legs();  // behind the mat, you see its top
       for (int v = 0; v < 26; v++)
         for (int u = 0; u < 26; u++) {
           float x = (float) u - 12.5f, z = ((float) v - 12.5f) / sa, r = sqrtf(x * x + z * z);
           if (r <= 8.5f)
             c.put(u, v, sa > 0 ? 0x22222A : 0x44444E);
           else if (r <= 10.5f)
             c.put(u, v, r <= 9.5f && sa > 0 ? 0x3388EE : 0x2266CC);
         }
       if (sa <= 0)
         legs();
     }},
    // An umbrella blown inside out, red, its ribs sticking up out of it, on its shaft and crook
    {"umbrella", 13, 13, UMBRELLA_MS,
     [](int f, const Canvas &c) {
       static const char *const TOP[] = {"k.....k.....k|rk...rkr...kr|.rrr.rrr.rrr.",
                                         ".k....k....k.|krr..rkr..rrk|.rrrrrrrrrrr.",
                                         "k.....k......|.rk..rkr...k.|.rrrrrrrr.rr."};
       static const char *const KEYS = "rkdh";
       std::initializer_list<uint32_t> cols = {0xDD2E2E, 0x333333, 0x777777, 0x8A5A2A};
       rows(c, TOP[f], KEYS, cols);
       rows(c,
            "..rrrrrrrrr..|...rrrrrrr...|....rrrrr....|.....rdr.....|"
            "......d|......d|......d|......d|......d..h|.......hh",
            KEYS, cols, 0, 3);
     }},
    // Someone's striped T-shirt off the line, its sleeves flapping up and down and its hem rippling
    {"shirt", 12, 12, SHIRT_MS,
     [](int f, const Canvas &c) {
       auto col = [](int y) { return y % 2 ? 0x2A3F7A : 0xF0F0F0; };
       for (int y = 3; y < 12; y++)
         for (int x = 3; x <= 8; x++)
           if (!(y == 3 && (x == 5 || x == 6)) && !(y == 11 && (x + f) % 3 == 0))
             c.put(x, y, col(y));
       static const int8_t TIP[] = {-2, 0, 2, 0};  // the sleeves' tips, rows up or down
       int tip = TIP[f];
       for (int k = 0; k < 3; k++)
         for (int w = 0; w < 2; w++) {
           int y = 3 + w + tip * (k + 1) / 3;
           c.put(2 - k, y, col(y));
           c.put(9 + k, y, col(y));
         }
     }},
    // A Norwegian Red, red and white, on its side as it tumbles, its legs kicking
    {"cow", 20, 12, COW_MS,
     [](int f, const Canvas &c) {
       static const char *const LEGS[] = {"...r.r.....r.r|...r.r.....r.r|...k.k.....k.k",
                                          "..r...r...r...r|.r.....r.r.....r|k.......k.......k"};
       static const char *const KEYS = "rwkpe";
       std::initializer_list<uint32_t> cols = {0xB03A26, 0xF4F1EA, 0x2B1D16, 0xEDA0A0, 0xEEE6CC};
       rows(c,
            "...............e.e|..............rrrr|.k.rrrrrrrrrrr.rrwrr|k.rrrrwwwwrrrrrrwkrr|k.rrrwwwwwrrrrr.rppp|"
            "k.rrrrwwwrrrrrr...pp|k.rrrrrrrrwwwwr|kk.rrrrrrrwwww",
            KEYS, cols);
       rows(c, "..........pp", KEYS, cols, 0, 8);
       rows(c, LEGS[f], KEYS, cols, 0, 9);
     }},
    // A flying saucer from below, its lights going round, and a beam it shines down while it looks
    {"ufo", 21, 44, UFO_MS,
     [](int f, const Canvas &c) {
       rows(c, ".......ggggggg|......gGGGGGGGg|....mmmmmmmmmmmmm|..mmMMMMMMMMMMMMMmm|mmmmmmmmmmmmmmmmmmmmm", "gGmM",
            {0x9FD8E8, 0xCFF0F8, 0xB8C2CC, 0xDDE3EA});
       rows(c, "..ddddddddddddddddd|....ddddddddddddd|......ddddddddd", "d", {0x5A6470}, 0, 5);
       static const uint32_t LIGHT[] = {0xFF4444, 0xFFDD33, 0x44FF77};
       for (int j = 0; j < 5; j++)
         c.put(4 + 3 * j, 5, LIGHT[(j + f) % 3]);
       bool full = f >= 11 && f <= 13;
       if (f < 10 || f > 14)
         return;
       for (int y = 8; y < 44; y++) {
         int half = 2 + (y - 8) * 6 / 35;
         for (int x = 10 - half; x <= 10 + half; x++)
           if (full ? (x + y + f) % 2 == 0 : (x + 2 * y) % 4 == 0)
             c.put(x, y, full ? 0xD8FFB0 : 0xA8D888);
       }
     }},
    // A sea serpent facing right: three humps rolling up out of the water and down again behind its head on its
    // long neck, a ripple where each meets the water, row 8
    {"serpent", 32, 9, SERPENT_MS,
     [](int f, const Canvas &c) {
       for (int k = 0; k < 3; k++) {
         int cx = 4 + 8 * k, h = 2 + (f + 3 - k) % 4;  // 2 to 5 rows out of the water, rolling forward
         for (int x = -3; x <= 3; x++) {
           int top = 8 - iround((float) h * sqrtf(1 - (float) (x * x) / 16));
           for (int y = top; y < 8; y++)
             c.put(cx + x, y, y == top ? 0x8FC79A : 0x3C6B4C);
         }
         dots(c, {(int8_t) (cx - 4), 8, (int8_t) (cx + 4), 8}, 0xCFE3EA);
       }
       rows(c, "....hhhh|...hhhhhe|..hh.hhhh|..hh|..hh|..hh|..hh|.hhh|wwwww", "hew", {0x3C6B4C, 0xE8D040, 0xCFE3EA},
            24, 0);
     }},
    // A troll facing right with a spruce growing out of its head and a long nose, walking: a stride and a step
    {"troll", 16, 24, TROLL_MS,
     [](int f, const Canvas &c) {
       static const char *const KEYS = "hsdnemgbt";
       std::initializer_list<uint32_t> cols = {0x2A2218, 0x7C8462, 0x5A6248, 0x949C74, LAMP,
                                               0x2A2218, 0x2F5A36, 0x5E4E34, 0x6A4A2A};
       int d = f ? 0 : 1;  // down a row on the stride
       rows(c, "......g|.....ggg|......g|.....ggg|....ggggg|......t", KEYS, cols, 0, d);
       rows(c,
            "...hhhhhhhh|..hhhhhhhhhh|..hhhhhsshhh|.hhsssessess|.hhsssssssnnn|.hhsssssssnnnnn|..hsssmmssnnnnn|"
            "...ssssssss.nnn|..bbbbbbbbbb.n|.bbbbbbbbbbbb",
            KEYS, cols, 0, 6 + d);
       static const char *const ARM[] = {"bbbbbbbbbbbbbs|hbbbbbbbbbbb.ss|hhbbbbbbbbbb..ss|.hbbbbbbbbbb...s",
                                         "bbbbbbbbbbbbs|hbbbbbbbbbbbs|hhbbbbbbbbbbs|.hbbbbbbbbbbss"};
       rows(c, ARM[f], KEYS, cols, 0, 16 + d);
       rows(c, "..bbbbbbbbbb", KEYS, cols, 0, 20 + d);
       rows(c, f ? "....dd..dd|....dd..dd|...ddd.ddd" : "..dd......dd|.ddd......ddd", KEYS, cols, 0, f ? 21 : 22);
     }},
    // A Viking longship facing right: a dragon's head on its prow, a red and white sail, shields along its side and
    // five oars pulling, the water at row 16
    {"longship", 32, 18, LONGSHIP_MS,
     [](int f, const Canvas &c) {
       const uint32_t wood = 0x6B4423, dark = 0x4A2E16, mast = 0x5A3A1E, foam = 0xCFE3EA;
       for (int x = 8; x <= 22; x++)  // the sail, bellied a little to the right
         for (int y = 2; y <= 9; y++)
           if (!((x == 8 || x == 22) && y == 9))
             c.put(x + (y > 3 && y < 8), y, (x - 8) / 2 % 2 ? 0xEDE6D6 : 0xB8302A);
       line(c, 7, 1, 23, 1, mast);
       line(c, 15, 0, 15, 11, mast);
       for (int x = 3; x <= 28; x++) {  // the hull, rising to the stem and stern
         int keel = 14 - (x < 7 ? 7 - x : 0) / 2 - (x > 24 ? x - 24 : 0) / 2;
         for (int y = 11; y <= keel; y++)
           c.put(x, y, y == 12 ? dark : wood);
       }
       rows(c, ".www|ww.w|w..w|w|w|ww|.ww", "w", {wood}, 0, 5);  // the stern's curl
       rows(c, "..kkk|.kkekk|..kk.t|..kk|..kw|.kw|kw|w|.w", "kewt", {wood, 0xF2D24A, wood, 0xC0402A}, 26, 2);
       for (int k = 0; k < 6; k++)  // the shields
         dots(c, {(int8_t) (6 + 4 * k), 10, (int8_t) (7 + 4 * k), 10, (int8_t) (6 + 4 * k), 11, (int8_t) (7 + 4 * k), 11},
              k % 2 ? 0xB02A2A : 0xD8B040);
       static const int8_t LEAN[] = {-3, -1, 2, 0};
       for (int k = 0; k < 5; k++) {  // out of the water as they come forward
         int x = 7 + 4 * k;
         line(c, x, 13, x + LEAN[f], f == 1 ? 15 : 16, 0x8A6A40);
         if (f == 2)
           c.put(x + LEAN[f] + 1, 16, foam);
       }
       for (int x = 4; x <= 28; x += 3)
         c.put(x + f % 2, 16, foam);
     }},
    // A wolf sitting on the skyline facing right, its back lit by the moon: still, its head lifting, howling
    {"wolf", 16, 14, WOLF_MS,
     [](int f, const Canvas &c) {
       static const char *const HEAD[] = {"..........r|.........rkk|.........kkkkkk|.........kkkk",
                                          "..........r...k|.........rkkkkk|.........kkkkk|.........kkkk",
                                          "............kk|...........kkk|..........rkkk|.........rkkkk"};
       static const char *const KEYS = "kr";
       std::initializer_list<uint32_t> cols = {0x15171F, 0x8E9AB8};
       rows(c, HEAD[pose<WOLF_MS>("012101210", f)], KEYS, cols);
       rows(c,
            ".........kkkk|........rkkkkk|........kkkkkk|.......kkkkkk|.....rrkkkkkk|...rrkkkkkkkk|..rkkkkkkk.kk|"
            "..kkkkkkkk.kk|r.kkkkkkkk.kk|kkkkkkkkkk.kkk",
            KEYS, cols, 0, 4);
     }},
    // A witch on her broomstick flying right, her cape flying out behind
    {"witch", 20, 12, WITCH_MS,
     [](int f, const Canvas &c) {
       static const char *const KEYS = "kfrcoyhbdt";
       std::initializer_list<uint32_t> cols = {0x16141C, 0x7EB45C, 0xE0702A, 0x6A44A0, 0x7A5430,
                                               0xD8AA50, 0x2C2440, 0x9A6AD0, 0x3A2856, 0xA05A20};
       rows(c, ".......h|........hh|........hhh|.......hbbbh|.....hhhhhhhhh", KEYS, cols);  // its tip blown back
       rows(c, ".......rfff|......rrffff|.......ccc|......cccc", KEYS, cols, 0, 5);
       static const char *const CAPE[] = {"|cccc|.cdd", "|.cccc|cdd", "..cc|.cccc|cd"};  // down, out, up
       rows(c, CAPE[pose<WITCH_MS>("0121", f)], KEYS, cols, 3, 6);
       rows(c, "yy|yyytooooooooooooo|yyy...dd|y.y....kk", KEYS, cols, 0, 8);
     }},
    // ---- the city
    // the red light on the antenna, slowly on and off
    {"beacon", 3, 2, BEACON_MS,
     [](int f, const Canvas &c) {
       for (int i = 0; !f && i < 6; i++)
         c.put(i % 3, i / 3, 0xFF3333);
     }},
    // A window someone has just switched on: their lamp, and now and then someone walking past it
    {"winlit", 2, 2, WINLIT_MS,
     [](int f, const Canvas &c) {
       for (int i = 0; i < 4; i++)
         c.put(i % 2, i / 2, (f == 1 && i % 2 == 0) || (f == 3 && i % 2 == 1) ? 0xAA8855 : 0xFFCC77);
     }},
    // and one just switched off, where now and then a TV flickers blue
    {"windark", 2, 2, WINDARK_MS,
     [](int f, const Canvas &c) {
       static const uint32_t COL[] = {0x111122, 0x333355, 0x222244, 0x444466, 0x111122, 0x222244};
       for (int i = 0; i < 4; i++)
         c.put(i % 2, i / 2, COL[f]);
     }},
    // Pigeons on a roof edge, as far off as the town's roofs are: specks that bob now and then, then lift off
    // together, wheel round once, their wings flashing pale as they turn, and settle again
    {"pigeons", 34, 14, PIGEONS_MS,
     [](int f, const Canvas &c) {
       // where each sits along the bottom row, and its place in the flock, x, y
       static const int8_t SIT[] = {3, 9, 11, 20, 28}, OFF[] = {-4, 1, -1, -1, 1, 1, 3, -1, 0, 2};
       // sitting, a digit a frame: 0 hunched, 1 its head up, 2 a step along and back
       static const char *const BOB[] = {"01000100", "00010002", "10000010", "00200100", "00001000"};
       constexpr uint32_t K = 0x464C5C, W = 0xD0D4DC;
       constexpr int N = 14;  // the flight's frames
       for (int i = 0; i < 5; i++) {
         if (f < 8) {
           int b = BOB[i][f] - '0';
           c.put(SIT[i] + (b == 2), 13, K);
           if (b == 1)
             c.put(SIT[i], 12, K);
           continue;
         }
         // round once and back, rising out of its place over the first frames and into it over the last
         int t = f - 8;
         float a = 1.5708f + 6.2832f * (t + 0.5f) / N, e = std::min(1.0f, std::min(t + 1, N - t) / 3.0f);
         float x = SIT[i] + (16 + 12 * cosf(a) + OFF[2 * i] - SIT[i]) * e;
         float y = 13 + (7.5f + 4.5f * sinf(a) + OFF[2 * i + 1] - 13) * e - 1;
         int xi = iround(x), yi = iround(y);
         c.put(xi, yi, K);  // its wings out, up and pale, or in
         if ((t + i) % 3 == 0)
           c.put(xi + 1, yi, K);
         if ((t + i) % 3 == 1)
           c.put(xi, yi - 1, W);
       }
     }},
    // An Oslo city bus by day, and by night with its windows lit
    {"bus", 30, 10, BUS_MS, [](int f, const Canvas &c) { bus_pose(c, f, false); }},
    {"busnight", 30, 10, BUS_MS, [](int f, const Canvas &c) { bus_pose(c, f, true); }},
    // A snowplough by night after a snowfall, the front to the right: its blade throwing up a curl of snow in the
    // headlights, the amber light on the cab's roof going round, then the one at the back. Drawn in the night's
    // colours
    {"plough", 26, 10, PLOUGH_MS,
     [](int f, const Canvas &c) {
       const uint32_t cab = 0x7A5028, low = 0x5A3A1C, box = 0x3A3A44, rim = 0x52525C, chassis = 0x18181E,
                      glass = 0x26303E, glow = 0xA86C34, ember = 0x664422;
       for (int y = 2; y <= 6; y++)  // the spreader behind the cab
         for (int x = 1; x <= 9; x++)
           c.put(x, y, y == 2 ? rim : box);
       for (int x = 0; x <= 17; x++)
         c.put(x, 7, chassis);
       for (int y = 3; y <= 6; y++)
         c.put(10, y, chassis);
       for (int y = 1; y <= 6; y++)  // the cab
         for (int x = y == 1 ? 12 : 11; x <= 16; x++)
           c.put(x, y, y == 6 ? low : cab);
       for (int y = 2; y <= 3; y++) {
         for (int x = 12; x <= 14; x++)
           c.put(x, y, glass);
         c.put(16, y, glass);
       }
       c.put(17, 4, 0xFFFFDD);
       dots(c, {17, 5, 17, 6, 18, 6}, chassis);
       // the beacons: the cab's flashes, dims and goes dark, and the one at the back flashes in between
       static const uint32_t CAB[] = {0xFFDD66, 0xEE9922, ember, ember};
       dots(c, {13, 0, 14, 0}, CAB[f]);
       for (int x = 12; f == 0 && x <= 15; x++)
         c.put(x, 1, glow);
       c.put(1, 1, f == 2 ? 0xFFDD66 : ember);
       for (int x = 1; f == 2 && x <= 3; x++)
         c.put(x, 2, glow);
       // the blade, curved, and the snow rolling up its face and curling over off the top
       static const int8_t BLADE[] = {20, 2, 19, 3, 19, 4, 18, 5, 18, 6, 18, 7, 19, 8, 19, 9};
       for (int i = 0; i < 16; i += 2) {
         c.put(BLADE[i] - 1, BLADE[i + 1], low);
         c.put(BLADE[i], BLADE[i + 1], glow);
       }
       static const char *const SNOW[] = {"..sW|.sWWWs|.W..sW|W....s|W.....s|W|WW....s|.WWs|.WWWs",
                                          "...sW|.sWWW|.W..Ws|W...sW|W|Ws....s|WW|.WWs..s|.WWWs",
                                          "..sWs|.sWWW.s|.W..sW|Ws...s|W.....s|WW|Ws|.WWW|.WWWs",
                                          "..s.s|.sWWW|.W.sWW|W....W|Ws....s|W|WW....s|.WWW|.WWWs"};
       rows(c, SNOW[f], "Ws", {0xE8EEF6, 0xA0AECA}, 19, 1);
       static const int8_t SPOT[] = {1, 0, 2, 1, 1, 2, 0, 1};  // round the wheel the way it turns
       for (int wx : {1, 5, 12}) {
         for (int i = 0; i < 9; i++)
           c.put(wx + i % 3, 7 + i / 3, i == 4 ? box : chassis);
         c.put(wx + SPOT[f * 2], 7 + SPOT[f * 2 + 1], rim);
       }
     }},
    // Someone cycling to the right on the street, far off: a helmet, a hi-vis jacket, pedalling, the wheels turning
    {"cyclist", 6, 6, CYCLIST_MS,
     [](int f, const Canvas &c) {
       rows(c, "...w|..yy|..y.k|.rjr|kk..kk|kk..kk", "wyjrk", {0xEEEEEE, 0xF2D21E, 0x4A6699, 0xCC2222, 0x222222});
       for (int hx : {0, 4}) {  // a lighter spot going round each wheel
         static const int8_t S[] = {0, 0, 1, 0, 1, 1, 0, 1};
         c.put(hx + S[f * 2], 4 + S[f * 2 + 1], 0x888888);
       }
       c.put(f % 2 ? 3 : 2, 4, 0x4A6699);  // the foot going round
     }},
};

// The ones a factory makes
struct Made {
  char name[11];
  Frames (*make)();
};
static const Made MADE[] = {
    // By day light grey, at dusk greyer, at night only a little lighter than the dark
    {"puff", [] { return smoke_puff({0xEEEEEE, 0xCCCCDD, 0x9999AA}); }},
    {"puffdusk", [] { return smoke_puff({0xAAAABB, 0x9999AA, 0x777788}); }},
    {"puffnight", [] { return smoke_puff({0x555566, 0x444455, 0x333344}); }},
    {"butterfly", [] { return butterfly(0xFF8822, 0x221111, 0x3366CC, 0x997755); }},  // a small tortoiseshell
    {"brimstone", [] { return butterfly(0xFFFF66, 0xDDCC33, 0xFFFF66, 0xCCCC77); }},
    {"fwpink", [] { return firework({0xFFFFFF, 0xFFCCEE, 0xFF77BB, 0xBB4488, 0x773344}, 3); }},
    {"fwcyan", [] { return firework({0xFFFFFF, 0xCCFFFF, 0x66DDFF, 0x3388AA, 0x224455}, 5); }},
    {"fwgold", [] { return firework({0xFFFFFF, 0xFFFFDD, 0xFFDD66, 0xCC8833, 0x774422}, 7); }},
    {"tram", [] { return tram(true); }},
    {"tramday", [] { return tram(false); }},
};

bool make_sprite(const std::string &name, Frames &out) {
  for (const Fixed &s : FIXED)
    if (name == s.name) {
      out = Frames{s.w, s.h, std::vector<uint16_t>(s.delays, s.delays + s.n), s.draw};
      return true;
    }
  for (const Made &m : MADE)
    if (name == m.name) {
      out = m.make();
      return true;
    }
  return false;
}

}  // namespace flexit::panel::met::live
