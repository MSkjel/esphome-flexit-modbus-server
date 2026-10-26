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
// body high on long legs, a hump over the shoulders, a bell and broad antlers
static void moose_pose(const Canvas &c, int f, bool graze) {
  const uint32_t B = 0x221111, D = 0x110000;
  for (int y = 5; y <= 10; y++)
    for (int x = 4 + (y > 9); x <= 16 - (y > 9); x++)
      c.put(x, y, B);
  for (int x = 12; x <= 16; x++)
    c.put(x, 4, B);
  dots(c, {13, 3, 14, 3, 3, 6}, B);
  int dy = graze && f == 1;
  static const int8_t HEAD[2][12] = {{16, 19, 3, 17, 21, 4, 18, 22, 5, 19, 23, 6},
                                     {18, 21, 13, 19, 22, 14, 20, 22, 15}};
  if (graze) {
    line(c, 16, 6, 19, 12 + dy, B);
    line(c, 17, 6, 20, 12 + dy, B);
    line(c, 16, 7, 19, 13 + dy, B);
  }
  for (const int8_t *p = HEAD[graze]; p < HEAD[graze] + (graze ? 9 : 12); p += 3)
    for (int x = p[0]; x <= p[1]; x++)
      c.put(x, p[2] + dy, B);
  if (!graze)
    dots(c, {22, 7, 23, 7, 18, 7, 18, 8, 18, 9}, B);
  rows(c, "a.a.a.a|aaaaaaa|.aa.aa", "a", {B}, graze ? 15 : 14, graze ? 9 + dy : 0);
  static const int8_t X[] = {5, 7, 14, 16},
                      S[] = {1, -1, -1, 1, 1, 0, 0, 1, 0, 1, 1, 0, -1, 1, 1, -1, -1, 0, 0, -1, 0, -1, -1, 0};
  for (int i = 0; i < 4; i++) {  // the legs, the far ones darker
    int x = X[i], d = graze ? 0 : S[f * 4 + i];
    line(c, x, 11, x + d, 19, x == 7 || x == 16 ? D : B);
    c.put(x + d, 20, D);
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

Frames rain_tile(int w, int h, int slant, std::initializer_list<Fall> kinds, uint32_t seed) {
  Falls k(kinds);
  return {w, h, std::vector<uint16_t>(8, 95), [=](int f, const Canvas &c) {
            Rng r = seeded(seed);
            for (const Fall *fall = k.k; fall < k.k + k.n; fall++) {
              int step = (int) fall->step, period = 8 * step, span = w + abs(slant) * h / 8 + 4,
                  count = iround(fall->dens * span * period);
              for (int i = 0; i < count; i++) {
                int u = r.in(-2, span - 3), v = r.in(0, period - 1);
                for (int z = -1; z * period < h + fall->size + period; z++) {
                  int y0 = v + f * step + (z - 1) * period, x0 = u + slant * y0 / 8;
                  for (int d = 0; d < fall->size; d++)
                    c.put(x0 + slant * d / 8, y0 + d, fall->col);
                }
              }
            }
          }};
}
Frames snow_tile(int w, int h, std::initializer_list<Fall> kinds, uint32_t seed) {
  Falls k(kinds);
  return {w, h, std::vector<uint16_t>(32, 110), [=](int f, const Canvas &c) {
            Rng r = seeded(seed);
            for (const Fall *fall = k.k; fall < k.k + k.n; fall++) {
              float period = 32 * fall->step;
              int count = iround(fall->dens * (w + 4) * period);
              for (int i = 0; i < count; i++) {
                float u = r.f() * (w + 4) - 2, v = r.f() * period, ph = r.f() * 2 * kPiF, lam = 26 + r.f() * 20,
                      amp = fall->size > 1 ? 2 : 1.4f;
                for (int z = -1; z * period < h + period; z++) {
                  int y = (int) floorf(v + f * fall->step + (z - 1) * period),
                      x = iround(u + amp * sinf(ph + 2 * kPiF * y / lam));
                  for (int a = 0; a < fall->size; a++)
                    for (int b = 0; b < fall->size; b++)
                      c.put(x + a, y + b, fall->col);
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
  return {7, 6, delays, [w, d, b, u](int f, const Canvas &c) {
            // open, half, shut, then sitting shut, half, open
            static const char *const LOOK[] = {"w.....w|wwb.bww|.ww.ww|..w.w",
                                               "|.wb.bw|.ww.ww|..w.w",
                                               "|...w|...w",
                                               "|...u|...uw|...u",
                                               "|..ubu|..uwu|...u",
                                               "d.....d|wwb.bww|.ww.ww|..w.w"};
            int p = f < 24 ? (f % 4 == 3 ? 1 : f % 4) : "34543"[f - 24] - '0', bob = std::min(p, 2);
            rows(c, LOOK[p], "wdbu", {w, d, b, u}, 0, bob);
            for (int y = 1; y <= 3; y++)
              c.put(3, y + bob, 0x443322);
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
static constexpr uint16_t HARE_MS[] = {150, 120, 150, 260};
static constexpr uint16_t HARESIT_MS[] = {1400, 160, 900};
static constexpr uint16_t PARADE_MS[] = {210, 210, 210, 210, 210, 210};
static constexpr uint16_t BALLOON_MS[] = {700, 600, 700, 600};
// the heart's last one fills up the rise and a step
static constexpr uint16_t HEART_MS[] = {800, 900, 900, 450, 450, 900, 450, 900, 1800};
static constexpr uint16_t OWL_MS[] = {260, 260, 260, 260, 1500, 1200};
static constexpr uint16_t OWLPERCH_MS[] = {2600, 900, 1800, 160, 2200, 700, 1400};
static constexpr uint16_t PUMPKIN_MS[] = {160, 120, 220, 140, 180, 130};
static constexpr uint16_t BEACON_MS[] = {1000, 1600};
static constexpr uint16_t WINLIT_MS[] = {5200, 300, 3900, 260};
static constexpr uint16_t WINDARK_MS[] = {6100, 140, 110, 180, 4300, 120};

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
    // a paper advent star hanging in the window, turning slowly on its cord, its light on the glass
    {"adventstar", 7, 6, ADVENTSTAR_MS,
     [](int f, const Canvas &c) {
       c.put(3, 0, 0x665544);
       rows(c, f % 2 ? "...y|.yywyy|..ywy|.yyyyy|..y.y" : "...y|..ywy|.yywyy|..yyy|.y...y", "yw",
            {f == 2 ? 0xFFDD77u : 0xFFCC66u, 0xFFFFEE}, 0, 1);
       dots(c, {0, 3, 6, 3, 0, 5, 6, 5}, 0x776644);
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
    // A moose as a silhouette in the forest's colour
    {"moose", 24, 21, MOOSE_MS, [](int f, const Canvas &c) { moose_pose(c, f, false); }},
    {"moosegraze", 24, 21, MOOSEGRAZE_MS, [](int f, const Canvas &c) { moose_pose(c, f, true); }},
    // A reindeer ambling as it grazes, head down, now and then looking up
    {"reindeer", 15, 12, REINDEER_MS,
     [](int f, const Canvas &c) {
       static const int8_t STEP[] = {1, 0, 0, 1, -1, 0, 0, -1, 1, 0, 0, 1, 0, 0, 0, 0};
       for (int x = 3; x <= 11; x++)
         for (int y = 4 + (x == 3 || x == 11); y <= 8; y++)
           c.put(x, y, y == 8 ? 0x666655 : 0x888877);
       dots(c, {3, 5, 2, 6}, 0xDDDDDD);
       if (f == 6) {  // looking up
         for (int y = 2; y <= 6; y++) {
           c.put(11, y, 0xCCCCCC);
           c.put(12, y, y > 3 ? 0xCCCCCC : 0x777766);
         }
         dots(c, {12, 1, 13, 1, 13, 2, 14, 2}, 0x777766);
         c.put(14, 3, 0x443333);
         dots(c, {11, 0, 12, 0, 13, 0, 10, 1, 14, 0}, 0x776655);
       } else {
         dots(c, {11, 5, 12, 6}, 0xCCCCCC);
         dots(c, {12, 7, 13, 8, 13, 9, 14, 9}, 0x777766);
         c.put(14, 10, 0x443333);
         dots(c, {12, 4, 13, 4, 11, 3, 14, 5, 13, 3}, 0x776655);
       }
       for (int x : {4, 6, 9, 11})  // the legs
         line(c, x, 9, x + STEP[f * 2 + (x == 6 || x == 9)], 11, 0x554433);
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
       for (int x = left ? 1 : 0; !glide && x <= (left ? 5 : 4); x++)
         c.put(x, 11, 0xAA6633);
     }},
    // Snow slipping off the end of a spruce's tier: the clump sags, drops in a puff of powder that drifts down and
    // thins, and the bare branch springs up. Its top row lies on the tier's last 4 px
    {"snowdrop", 9, 22, SNOWDROP_MS,
     [](int f, const Canvas &c) {
       const uint32_t T = 0x224444, S = 0xBBCCDD, P = 0xDDDDEE;
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
           c.put(x, y, d < wd * 0.35f && y > 9 ? 0xFFFFDD : d < wd * 0.7f ? 0xFFCC33 : y < 7 ? 0xDD4422 : 0xFF7711);
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
    // the sleigh by night, its colours dimmed like the rest of the night, Rudolph's nose still bright
    {"sleigh", 36, 12, SLEIGH_MS,
     [](int f, const Canvas &c) {
       const uint32_t gold = 0x887755, red = 0x773333, deer = 0x443322, dark = 0x332211;
       for (int x = 1; x <= 13; x++)  // the runner
         c.put(x, 9, gold);
       dots(c, {0, 8, 14, 8}, gold);
       for (int x = 2; x <= 12; x++)
         for (int y = 5; y <= 8; y++)
           c.put(x, y, y == 5 ? red : 0x662222);
       for (int x = 4; x <= 7; x++)  // the sack
         for (int y = 2; y <= 4; y++)
           c.put(x, y, 0x443322);
       for (int y = 0; y <= 4; y++) {  // and him
         c.put(9, y, y ? red : 0xAA8877);
         c.put(10, y, red);
       }
       dots(c, {11, 0, 10, 2, 11, 2}, 0x9999AA);
       line(c, 13, 6, 18, 6, dark);
       static const int8_t S[] = {1, -1, 0, 0, -1, 1, 0, 0};
       for (int ox = 18; ox <= 27; ox += 9) {  // two reindeer
         for (int x = ox; x <= ox + 6; x++)
           for (int y = 5; y <= 6; y++)
             c.put(x, y, deer);
         dots(c, {(int8_t) (ox + 7), 4, (int8_t) (ox + 8), 4, (int8_t) (ox + 8), 3}, deer);
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
                                          "..yy|.yyeyb|yyyy|yyyy|.yy|.l.l", ".yy|yyeyb|yyy|yyy|.yy"};
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
       const char *body = f == 2 ? "..........hhhh|........bbhhhh.k|....bbbbbbbbb|...wbbbbbbbbb|....bbbbbbbbb|....ll...lll"
                                 : "..........hhhh|........bbhhhhk|....bbbbbbbbb|...wbbbbbbbbb|....bbbbbbbbb|....ll...lll";
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
         int x0 = i * 8, step = (f + i) % 2, wave = (f + i * 2) % 3, y0 = 5;
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
       int s = pose<HEART_MS>("012322322", f), o = 3 - s;  // o centres it
       rows(c, H[s], "xh", {f == 3 || f == 6 ? 0xFF5588u : 0xEE4477u, 0xFFBBDD}, o, o);
       for (int i = 0; f == 8 && i < c.w * c.h; i++)  // thinning out
         if (noise(i % c.w - o, i / c.w - o, 3) < 45)
           c.px[i] = 0;
     }},
    // the owl, gliding with slow deep beats, its pale face and eyes catching what light there is
    {"owl", 15, 8, OWL_MS,
     [](int f, const Canvas &c) {
       static const int8_t TIP[] = {3, 1, 0, 0, 3, 3, 0, 3, 3, 4, 1, 6, 3, 3, 0, 4};  // up, mid, down, glide
       const int8_t *t = TIP + pose<OWL_MS>("012133", f) * 4;
       for (int s = 0; s < 2; s++) {
         auto m = [&](int x) { return s ? 14 - x : x; };
         line(c, m(5), 3, m(t[0]), t[1], 0x554433);
         line(c, m(5), 4, m(t[0]), t[1] + 1, 0x443322);
         line(c, m(t[0]), t[1], m(t[2]), t[3], 0x443322);
       }
       for (int x = 5; x <= 9; x++)
         for (int y = 2; y <= 5; y++)
           c.put(x, y, 0x554433);
       dots(c, {6, 2, 7, 2, 8, 2, 7, 3}, 0x998877);
       dots(c, {6, 3, 8, 3}, 0xFFCC33);
       c.put(7, 4, 0x776655);
       dots(c, {7, 6, 6, 6, 8, 6}, 0x443322);
     }},
    // and perched on a spruce top: it sits, turns its head right round, blinks, hoots
    {"owlperch", 9, 11, OWLPERCH_MS,
     [](int f, const Canvas &c) {
       static const char *const HEAD[] = {
           "..b...b|..bbbbb|.bfffffb|.bfefefb|.bffkffb", "..b...b|..bbbbb|.bbbffff|.bbbefef|.bbbffkf",
           "..b...b|..bbbbb|.bbbbbbb|.bbbbbbb|.bbbbbbb", "..b...b|..bbbbb|.bfffffb|.bfffffb|.bffkffb",
           "..b...b|..bbbbb|.bfffffb|.bfefefb|.bffkffb", "...b...b|...bbbbb|..bfffffb|..bfefefb|..bffkffb",
           "..b...b|..bbbbb|.bfffffb|.bfefefb|.bffoffb"};
       static const char *const KEYS = "bfekowt";
       std::initializer_list<uint32_t> cols = {0x554433, 0x998877, 0xFFCC33, 0x443322, 0x221111, 0x776655, 0x443322};
       rows(c, HEAD[f], KEYS, cols);
       rows(c,
            f == 2 ? ".bbbbbbb|.bbbbbbb|.bbbbbbb|..bbbbb|...t.t|..ttttt"
                   : ".bbbbbbb|.bwbbbwb|.bbwbwbb|..bbbbb|...t.t|..ttttt",
            KEYS, cols, 0, 5);
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
    {"butterfly", [] { return butterfly(0xEE6622, 0x221111, 0x3366CC, 0x997755); }},  // a small tortoiseshell
    {"brimstone", [] { return butterfly(0xFFEE66, 0xDDCC44, 0xFFEE66, 0xCCCC88); }},
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
