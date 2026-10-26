// Animations on the land: gulls, smoke from the chimney, firelight, a moose, reindeer and an eagle, a skier,
// butterflies, falling leaves and geese
#pragma once
#include "met_live.h"

namespace flexit::panel::met::live {

// How far the smoke drifts downwind, px
inline int smoke_drift(const Scene &s) { return std::min(60, 10 + (int) (s.at.wind * 4)); }

// What the land shows are made of, so they start again when it changes: the cabin, so the landscape, then the way
// the wind blows, then the smoke's puffs, drift and colour and December
inline uint32_t cabin_at(const Scene &s) { return (uint32_t) s.cabin_x << 8 | (uint32_t) s.cabin_y; }
inline uint32_t cabin_wind(const Scene &s) { return cabin_at(s) | (uint32_t) (s.at.wdir > 0) << 17; }
inline uint32_t cabin_smoke(const Scene &s) {
  return cabin_wind(s) | (uint32_t) s.puffs << 18 | (uint32_t) smoke_drift(s) / 8 << 21 |
         (uint32_t) (s.at.dark + (s.at.light < 0.5f)) << 25 | (uint32_t) (s.at.month == 12) << 27;
}

inline constexpr int GLIDE_IN = 21000, FLY_OFF = 17000;  // a visiting gull's glide in and flight off, ms

// A gull's visit to the ridge a step at a time: it glides in, lands and swaps to the visit, which ends on the flat
// beat it takes off on, then it's gone. x, y are the visit's, its feet on the ridge.
inline void gull_visit(int step, int x, int y, bool from_right) {
  int ms = GLIDE_IN;
  if (step == 0) {
    place("gull", 2, from_right ? W + 13 : -14, y - 43, {true, 7});  // gliding by the time it's there
    move("gull", 2, x + 1, y, GLIDE_IN);
  } else if (step == 1) {
    swap("gull", 2, "gullvisit", x, y, true);
    if (from_right)
      flip("gullvisit", 2, true, false);
    ms = ms_of("gullvisit") - STEP;
  } else if (step == 2) {
    swap("gullvisit", 2, "gull", x + 1, y + 4, true, 2);
    move("gull", 2, from_right ? -20 : W + 20, 96, FLY_OFF, "in");
    ms = FLY_OFF;
  } else {
    remove("gull", 2);
    return;
  }
  later(ms, [=] { gull_visit(step + 1, x, y, from_right); });
}

// Gulls, and about every 7 minutes one glides in against the wind and lands on the cabin's ridge, looks about,
// preens and takes off again
inline void gulls(const Scene &s) {
  flock({{60, 120, 6, 10, 100, 150}, {230, 105, 5, 8, 95, 140}}, {{150, 60, 2, 3, 44, 84}});
  preload("gullvisit");
  bool from_right = s.at.wdir > 0;
  int x = s.cabin_x - (from_right ? 6 : 8), y = s.cabin_y - 40;  // the roof's top row under its feet
  sometimes(minutes(7), minutes(0.5f, 3), [=] {
    gull_visit(0, x, y, from_right);
    return (uint32_t) (GLIDE_IN + ms_of("gullvisit") - STEP + FLY_OFF);
  });
}

// Puffs from the chimney top, drifting downwind further the more it blows
inline void stove_smoke(const Scene &s, const char *name) {
  chimney(name, s.puffs, s.chimney.x, s.chimney.y, 52, smoke_drift(s), s.at.wdir);
}

// Smoke from the stove, by the heat, and in December a paper star in the window
inline void smoke(const Scene &s) {
  stove_smoke(s, s.at.dark ? "puffnight" : s.at.light < 0.5f ? "puffdusk" : "puff");
  if (s.at.month == 12)
    place("adventstar", 0, window_x(s), window_y(s));
}

// Firelight in the window and smoke dark against the night
inline void cosy(const Scene &s) {
  place("firewin", 0, window_x(s), window_y(s));
  stove_smoke(s, "puffnight");
}

// A moose's visit a step at a time: out of the trees from x0 to x1, grazing there, and back
inline void moose_visit(int step, const NearHill &h, int x0, int x1) {
  bool right = x1 < x0;  // it came from the right
  uint32_t ms = 24000;
  if (step == 0) {
    place("moose", 0, x0, h.at(x0 + 10) - 14, {false, 0, 3000});
    flip("moose", 0, right, false);
    ms = amble("moose", 0, h, x0, x1, 1.7f, 10, 14);
  } else if (step == 1) {
    swap("moose", 0, "moosegraze", x1, h.at(x1 + 10) - 14);
    flip("moosegraze", 0, right, false);
  } else if (step == 2) {
    swap("moosegraze", 0, "moose", x1, h.at(x1 + 10) - 14);
    flip("moose", 0, !right, false);
    ms = amble("moose", 0, h, x1, right ? x0 + 2 : x0 - 2, 1.6f, 10, 14);
  } else {
    remove("moose", 0, 3000);
    return;
  }
  later(ms, [=] { moose_visit(step + 1, h, x0, x1); });
}

// Now and then at dusk a moose steps out of the trees into the clearing by home, on one side of the cabin or the
// other, grazes a while and goes back
inline void moose(const Scene &s) {
  preload("moosegraze");
  // from the nearest front tree on either side, and up to 40 px out, clear of the cabin and the pole
  int lg = s.cabin_x - 60, rg = s.cabin_x + 30, lx = -W, rx = W;
  for (int i : near_trees(s, -1)) {
    int x = s.tree[i].x - 12;
    if (x <= lg - 16)
      lx = std::max(lx, x);
    if (x >= rg + 16 && x <= W - 24)
      rx = std::min(rx, x);
  }
  NearHill h(s);
  sometimes(minutes(12), (uint32_t) rnd(20000, 240000), [=] {
    bool from_right = rx < W && (lx == -W || chance(0.5f));
    int x0 = from_right ? rx : lx == -W ? lg - 34 : lx;
    moose_visit(0, h, x0, from_right ? std::max(rg, x0 - 40) : std::min(lg, x0 + 40));
    return 90000u;  // its walks and the grazing, about
  });
}

// Three reindeer grazing their way about, each in its own patch of the near land left of the cabin, and now and
// then a golden eagle circling a while in the half of the sky away from the sun
inline void reindeer(const Scene &s) {
  int w = (s.cabin_x - 20 - 4 - 16) / 3;
  for (int i = 0; i < 3; i++) {
    int a = 4 + i * (w + 8), y = hill_low(s, a, a + w) - 7;  // its feet on the land all across
    place("reindeer", i, a + (w - 15) / 2 + (int) rnd(-8, 8), y, {true, i * 2});
    area("reindeer", i, a, y - 3, a + w, y + 15, "bounce", "bounce");
    face("reindeer", i);
    wander("reindeer", i, 1, 2.5f, 0, 180, 2000, 6000, 3000, 9000);
  }
  sometimes(minutes(20), minutes(1, 8), [] {
    bool left = R.sun_x > W / 2;
    place("eagle", 0, left ? -40 : W + 20, 30, {true});
    wave("eagle", 0, "x", -40, 40, 12000, 12000, 0, 0, 20, false, "both");
    wave("eagle", 0, "y", -10, 10, 8000, 8000, 0, 0, 20, false, "both");
    move("eagle", 0, left ? 60 : 225, 46, 12000, "out");
    move_later(40000, "eagle", 0, left ? -60 : W + 40, 18, 14000, "in");
    return remove_later(56000, "eagle", 0);
  });
}

// Now and then a skier along the near hill, quicker downhill, more often at weekends. And snow slips off the end
// of a front spruce's tier, one that shows against what's behind it.
inline void skier(const Scene &s) {
  NearHill h(s);
  bool weekend = s.at.weekday == 0 || s.at.weekday == 6;
  sometimes(minutes(weekend ? 10 : 30), minutes(0.5f, 4), [=] {
    bool from_right = chance(0.5f);
    int x0 = from_right ? W + 2 : -16;
    place("skier", 0, x0, h.at(x0 + 7) - 6);
    if (from_right)
      flip("skier", 0, true, false);
    return remove_later(amble("skier", 0, h, x0, from_right ? -16 : W + 2, 8, 7, 6, 2), "skier", 0);
  });
  struct End {
    int16_t x, y;
    bool left;
  } ends[16];
  int n = 0, seen = 0;  // 16 of them at random
  uint8_t out[W], snow = s.ix[C_HILL + 2];
  for (int i : near_trees(s, 0)) {
    for (int d = 13; s.tree[i].top + d <= s.tree[i].base; d++) {  // the snowy tiers but the top one
      if (!snow_tier(d))
        continue;
      int y = s.tree[i].top + d, hw = spruce_hw(d);
      row(s, y, out);
      for (int side = -1; side <= 1; side += 2) {
        int e = s.tree[i].x + side * hw;  // its last px
        bool shows = e - 4 >= 0 && e + 4 < W && out[e + side] != snow;
        for (int k = 0; k < 4; k++)
          shows = shows && out[e - side * k] == snow;
        End end = {(int16_t) (side > 0 ? e - 3 : e - 5), (int16_t) y, side < 0};
        if (shows && n < 16)
          ends[n++] = end;
        else if (shows && rnd(0, (float) ++seen + 16) < 16)
          ends[(int) rnd(0, 16)] = end;
      }
    }
  }
  if (n)
    sometimes(minutes(2), (uint32_t) rnd(10000, 60000), [=] {
      End e = ends[(int) rnd(0, (float) n)];
      place("snowdrop", 0, e.x, e.y, {true});
      if (e.left)
        flip("snowdrop", 0, true, false);
      return remove_later(30000, "snowdrop", 0, 4000);
    });
}

// Two small tortoiseshells and a brimstone over the meadow in front of home, under the near hill's top. They
// flutter for as long as their frames do and sit for as long as they sit.
inline void butterflies(const Scene &s) {
  int l = std::max(4, s.cabin_x - 120), r = std::min(W - 4, s.cabin_x + 60);
  int top = std::max(s.cabin_y + 1, hill_low(s, l, r) + 3), bot = std::min(H - 2, top + 28);
  for (int k = 0; k < 3; k++) {
    const char *n = k < 2 ? "butterfly" : "brimstone";
    int i = k & 1, x = l + (r - l) * (2 * k + 1) / 6, y = (int) rnd(top, bot - 6);
    later(k * 2300, [=] {
      int fly = ms_of(n, 0, 24), sit = ms_of(n, 24);  // its 24 fluttering frames, then sitting
      place(n, i, x, y, {true});
      area(n, i, l, top, r, bot, "bounce", "bounce");
      wander(n, i, 4, 8, 0, 180, fly, fly, sit, sit);
    });
  }
}

// Leaf i lies on the grass at x, y, and a gust now and then tumbles it along, low, in rows top to bot. 3 and 4
// fell, and take the falling one's place.
inline void leaf_lies(int i, int x, int y, int top, int bot, int heading) {
  if (i > 2)
    swap("leaffall", i, "leaf", x, y, true);
  else
    place("leaf", i, x, y, {true});
  int roll = ms_of("leaf", 0, 8), lie = ms_of("leaf", 8);  // its 8 tumbling frames, then lying
  area("leaf", i, -6, top, W + 6, bot, "cross", "bounce");
  wander("leaf", i, 2, 4, heading, 25, roll, roll, lie, lie);
}

// Birch leaves lie on the grass and now and then a gust tumbles them along, low, a few px/s. About every minute
// one lets go of a front birch and tumbles down to them. Now and then a V of geese far up, small, slow and hazy.
inline void leaves(const Scene &s) {
  int top = hill_low(s, 0, W) + 2;  // under the near hill's top all the way across
  int bot = std::min(H - 2, top + 26), heading = s.at.wdir < 0 ? 180 : 0, wdir = s.at.wdir;
  auto lie = [=](uint32_t at, int i, int x, int y) { later(at, [=] { leaf_lies(i, x, y, top, bot, heading); }); };
  for (int i = 0; i < 3; i++)
    lie(i * 2700, i, 50 + i * 100 + (int) rnd(-12, 12), (int) rnd(top, bot - 6));
  struct Birch {
    int16_t x, top, h, rx;
  } birches[6];
  int n = 0;
  for (int i : near_trees(s, 1))
    if (n < 6) {
      Crown c = crown_of(s, i);
      birches[n++] = {s.tree[i].x, s.tree[i].top, (int16_t) c.rows, (int16_t) c.rx};
    }
  if (n)
    sometimes(minutes(1), (uint32_t) rnd(5000, 30000), [=] {
      static constexpr int FALL = 9000;
      Birch c = birches[(int) rnd(0, (float) n)];
      int x0 = c.x - 3 + (int) rnd(1 - c.rx, (float) c.rx), y0 = c.top + (int) rnd(1, c.h * 0.6f);
      int x1 = std::clamp(x0 + wdir * (int) rnd(18, 30), -2, W - 4), y1 = (int) rnd(top, bot - 6);
      int i = chance(0.5f) ? 3 : 4;  // the leaf it'll be
      place("leaffall", i, x0, y0, {true});
      move("leaffall", i, x1, y1, FALL);
      lie(FALL, i, x1, y1);
      return (uint32_t) FALL;
    });
  sometimes(minutes(30), minutes(1, 10), [] {
    place("geese", 0, -20, 30);
    glide("geese", 0, "opacity", 70, 0);
    wave("geese", 0, "y", -2, 2, 6000, 6000, 0, 0, 20, false, "both");
    for (int k = 1; k <= 4; k++)  // a move takes 65 s at most
      move_later((k - 1) * 35000, "geese", 0, -20 + (W + 30) * k / 4, 30 - 2 * k, 35000);
    return remove_later(140000, "geese", 0);
  });
}

}  // namespace flexit::panel::met::live
