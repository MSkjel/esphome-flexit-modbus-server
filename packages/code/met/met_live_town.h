// Animations by the sea and in town: the sailboat, the lighthouse and the coastal express, windows that go on
// and off, the antenna's light and a tram
#pragma once
#include <algorithm>
#include <bitset>
#include <memory>
#include "met_live.h"

namespace flexit::panel::met::live {

// ---- by the sea

using Cols = std::bitset<W>;

// Columns where rows y0 to y1 of the picture are open water, the sea or its waves. The lighthouse is as white as
// the waves but isn't. pass: the flagpole with its flag or pennant doesn't count, boats go by in front of it.
inline Cols water(const Scene &s, int y0, int y1, bool pass) {
  Cols c;
  c.set();
  uint8_t r[W];
  for (int y = y0; y <= y1; y++) {
    row(s, y, r);
    for (int x = 0; x < W; x++) {
      int d = x - s.pole_x;
      bool pole = pass && s.pole_x >= 0 && d >= -PENNANT_LEFT && d <= PENNANT_RIGHT &&
                  (y < s.pole_top + 16 || d == 0 || d == 1);
      if ((r[x] != s.ix[C_SEA] && r[x] != s.ix[C_WAVE] && !pole) ||
          (s.lighthouse && abs(x - LH_X) < 5 && y > s.lh_y - 34))
        c.reset(x);
    }
  }
  return c;
}
// The run of them around column x as its first and last, or the longest run with x < 0. False if there's none.
inline bool stretch(const Cols &c, int x, int &l, int &r) {
  if (x < 0) {
    int n = 0;
    for (int a = 0, b; a < W; a = b + 1) {
      for (b = a; b < W && c[b]; b++) {
      }
      if (b - a > n) {
        n = b - a;
        l = a;
        r = b - 1;
      }
    }
    return n > 0;
  }
  if (x >= W || !c[x])
    return false;
  for (l = x; l > 0 && c[l - 1]; l--) {
  }
  for (r = x; r < W - 1 && c[r + 1]; r++) {
  }
  return true;
}

// Columns l to r cut to the gap between the clock and the temperatures, if least px of them are in it
inline void in_gap(int &l, int &r, int least = 40) {
  int gl = std::max<int>(l, GAP_L), gr = std::min<int>(r, GAP_R);
  if (gr - gl >= least) {
    l = gl;
    r = gr;
  }
}
// The run of c with the most columns in the gap, cut to it, if that's least or more. False if none has.
inline bool gap_stretch(const Cols &c, int &l, int &r, int least) {
  int best = 0;
  for (int a = GAP_L, b; a <= GAP_R; a = b + 1) {
    for (b = a; b <= GAP_R && c[b]; b++) {
    }
    if (b - a > best) {
      best = b - a;
      l = a;
      r = b - 1;
    }
  }
  return best >= least;
}

// The boat's turning points and row, as the sprite's left and top, and which way the wind blows it
struct Course {
  int16_t a, b, y;
  bool left_runs;
};
// The boat heading left or right from x, a minute's move at a time. At the end it comes about over the tack's
// frames, the last of which is the boat the other way, so the swaps don't show. Bow to the right they're mirrored.
inline void sail_on(Course c, int x, bool left) {
  float v = left == c.left_runs ? 0.9f : 0.55f;  // it runs before the wind and beats back slower
  int d = std::min(abs((left ? c.a : c.b) - x), (int) (60 * v));
  if (d) {
    int to = left ? x - d : x + d, ms = (int) (d * 1000 / v);
    move("boat", 0, flip_x("boat", to, !left), c.y, ms);
    later(ms, [=] { sail_on(c, to, left); });
    return;
  }
  swap("boat", 0, "tack", flip_x("tack", x, !left), c.y, true);
  if (!left)
    flip("tack", 0, true, false);
  later(ms_of("tack") - STEP, [=] {
    swap("tack", 0, "boat", flip_x("boat", x, left), c.y);
    if (left)
      flip("boat", 0, true, false);
    sail_on(c, x, !left);
  });
}

// Points of sunlight on the water near the sun's reflection, somewhere new each time: each moves on while it's
// dark and holds still while it glints, its wander in step with its frames
inline void glitter(const Scene &s) {
  int l = 0, r = -1, x = s.shine_x, top = s.sea_top;
  if (x >= 0 && stretch(water(s, top, top + 9, false), x, l, r)) {
    l = std::max(l, x - 30);
    r = std::min(r, x + 30);
  }
  for (int i = 0; i < 4; i++) {
    if (r - l < 12) {
      remove("glitter", i);
      continue;
    }
    int gx = (int) rnd(l, r - 2), gy = (int) rnd(top, top + 7);
    later(870 * i, [=] {
      int glint = ms_of("glitter", 1, 4), dark = ms_of("glitter") - glint;  // from frame 4 round to 0, then 1 to 3
      place("glitter", i, gx, gy, {true, 4});
      area("glitter", i, l, top, r + 1, top + 10, "bounce", "bounce");
      wander("glitter", i, 3, 6, 0, 180, dark, dark, glint, glint);
    });
  }
}

inline void sail(const Scene &s) {
  int x = s.boat.x - 6, y = s.boat.y - 14, l, r;  // the picture's mast and waterline
  bool left = s.at.wdir < 0;
  preload("tack");
  // it turns where the open water at its rows ends, off a skerry, a tree or the point, and stays between the
  // clock and the temperatures when there's room for that
  bool room = stretch(water(s, std::max<int>(s.sea_top, y), s.boat.y, true), s.boat.x, l, r) && r - l > 40;
  if (room)
    in_gap(l, r);
  if (room)
    x = std::clamp(x, l + 1, r - 13);
  place_flipped("boat", 0, x, y, !left);
  if (room)
    sail_on({(int16_t) (l + 1), (int16_t) (r - 13), (int16_t) y, left}, x, left);
  glitter(s);
  // a pair of eiders bobbing about on the open water off the shore, not the point's: the stretch in the gap between
  // the clock and the temperatures, or the longest
  int shore = H;
  for (int i = 0; i < W; i++)
    if (s.land->hill[1][i] >= s.sea_top + 20)
      shore = std::min<int>(shore, s.land->hill[1][i]);
  Cols open = water(s, shore - 10, shore - 1, false);
  if (!gap_stretch(open, l, r, 30) && (!stretch(open, -1, l, r) || r - l < 40))
    return;
  place("eiders", 0, (l + r) / 2 - 7, shore - 8, {true});
  area("eiders", 0, l, shore - 10, r + 1, shore, "bounce", "bounce");
  face("eiders", 0, 180);
  wander("eiders", 0, 0.3f, 0.6f, 180, 30, 8000, 15000, 5000, 20000);
}
// the reflection moves with the sun
inline void sail_redo(const Scene &s) { glitter(s); }

// The lighthouse's sweep, its lamp on the picture's
inline Box sweep_box(const Scene &s) { return {LH_X - 90, s.lh_y - 33, 141, 11}; }
// the stars turn, but not behind the sweep
inline void lighthouse_redo(const Scene &s) { twinkle_stars(s, sweep_box(s)); }

inline void lighthouse(const Scene &s) {
  Box b = sweep_box(s);
  place("sweep", 0, b.x, b.y);
  lighthouse_redo(s);
  // A couple of times an evening the coastal express crawls along the horizon, from behind the headland to the
  // lighthouse
  int x = 0, l, r;
  while (x < W && s.land->hill[0][x] != NONE)
    x++;
  x += 4;
  if (!stretch(water(s, s.sea_top + 1, s.sea_top + 3, true), x, l, r) || r - 26 < x + 30)
    return;
  int y = s.sea_top - 5, ms = (r - 26 - x) * 10000 / 7;  // 0.7 px/s
  sometimes(minutes(60), minutes(10, 40), [=] {
    place("ship", 0, x, y, {false, 0, 5000});
    drift("ship", 0, 0.7f, 0);
    return remove_later(ms, "ship", 0, 5000) + 5000;
  });
}

// ---- in town

struct Win {
  int16_t x, y;  // x -1: none
  bool lit;
};
// The evening's windows in full view, the one each copy changed, and whether the trams run
struct Town {
  std::vector<Win> wins;
  Win slot[5];
  bool trams;
};
inline std::weak_ptr<Town> town;  // the timers hold it, so it goes with the show

// The tram's row: its wheels on the street just above the park, the near hill the pole stands on
inline int tram_y(const Scene &s) { return hill_high(s, 0, W) - 13; }
// a tram along the park at a walking pace, from either side. The ms it takes.
inline uint32_t tram_by(const char *name, int y) {
  bool right = chance(0.5f);
  return across(name, right ? 330 : -40, y, right ? -40 : 330, y, 46000);
}

// The picture's near windows with nothing in front of them, and whether the trams run
inline void town_redo(const Scene &s) {
  auto t = town.lock();
  if (!t)
    return;
  std::vector<Win> all;
  windows(s, [&](int x, int y, int h, bool lit) {
    if (h == 2)  // the sprites' size
      all.push_back({(int16_t) x, (int16_t) y, lit});
  });
  std::sort(all.begin(), all.end(), [](const Win &a, const Win &b) { return a.y < b.y; });
  t->wins.clear();
  uint8_t r[2][W];
  for (size_t i = 0; i < all.size(); i++) {
    const Win &w = all[i];
    if (!i || w.y != all[i - 1].y) {
      row(s, w.y, r[0]);
      row(s, w.y + 1, r[1]);
    }
    uint8_t c = s.ix[w.lit ? C_WINDOW : C_BLD_EDGE];
    if (r[0][w.x] == c && r[0][w.x + 1] == c && r[1][w.x] == c && r[1][w.x + 1] == c)
      t->wins.push_back(w);
  }
  t->trams = s.at.hour >= 5 || !s.at.hour;
}

inline void town_evening(const Scene &s) {
  auto t = std::make_shared<Town>();
  for (Win &w : t->slot)
    w.x = -1;
  town = t;
  town_redo(s);
  if (s.antenna.x >= 0)
    place("beacon", 0, s.antenna.x, s.antenna.y);
  // About once a minute someone switches a light on or off, or back again
  sometimes(minutes(1), 20000, [t]() -> uint32_t {
    int k = (int) rnd(0, 5);
    Win &w = t->slot[k];
    if (w.x >= 0) {
      remove(w.lit ? "windark" : "winlit", k, 400);
      w.x = -1;
      return 0;
    }
    if (t->wins.empty())
      return 0;
    const Win &n = t->wins[(size_t) rnd(0, (float) t->wins.size())];
    for (const Win &o : t->slot)
      if (o.x == n.x && o.y == n.y)
        return 0;
    place(n.lit ? "windark" : "winlit", k, n.x, n.y, {true, (int) rnd(0, 4), 400});
    w = n;
    return 0;
  });
  int y = tram_y(s);
  sometimes(minutes(10), minutes(2), [t, y] { return t->trams ? tram_by("tram", y) : 0; });
}

inline void town_day(const Scene &s) {
  int roof = H;  // the near row's highest roof, but for the tower and the spire
  for (int i = 0; i < s.nbld; i++)
    if (!s.bld[i].back && s.bld[i].kind < 2)
      roof = std::min<int>(roof, s.bld[i].top);
  flock({{60, roof - 30, 6, 10, roof - 50, roof + 5}, {220, roof - 40, 5, 8, roof - 56, roof}},
        {{140, 40, 2, 3, 24, 62}, {280, 52, 2, 3, 28, 66}});
  int y = tram_y(s);
  sometimes(minutes(10), minutes(2), [y] { return tram_by("tramday", y); });
}

}  // namespace flexit::panel::met::live
