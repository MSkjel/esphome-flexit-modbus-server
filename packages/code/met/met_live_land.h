// Animations on the land: gulls, a crow and a squirrel on the roof, smoke from the chimney, a magpie, firelight, the
// cat and a fox, a moose and a roe deer, reindeer, ptarmigan, a hiker and an eagle, a skier and others out on the
// snow, butterflies, swallows, sheep and the mower, falling leaves, a mushroom picker and geese
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

// The top row of what a show sets about the meadow in front of the cabin, below its floor
inline int meadow(const Scene &s) { return std::min(s.cabin_y + 10, H - 16); }

// A gull's visit to the ridge a step at a time: it glides in, lands and swaps to the visit, which ends on the flat
// beat it takes off on, then it's gone. x, y are the visit's, its feet on the ridge.
inline void gull_visit(int step, int x, int y, bool from_right) {
  int ms = GLIDE_IN;
  if (step == 0) {
    preload("gullvisit");
    place_flipped("gull", 2, from_right ? W + 2 : -21, y - 43, from_right,
                  {true, 7});  // gliding by the time it's there
    move("gull", 2, flip_x("gull", x + 1, from_right), y, GLIDE_IN);
  } else if (step == 1) {
    swap_flipped("gull", 2, "gullvisit", x, y, from_right, true);
    ms = ms_of("gullvisit") - STEP;
  } else if (step == 2) {
    swap_flipped("gullvisit", 2, "gull", x + 1, y + 4, from_right, true, 2);
    move("gull", 2, from_right ? -21 : W + 2, 96, FLY_OFF, "in");
    ms = FLY_OFF;
  } else {
    remove("gull", 2);
    clear("gullvisit");
    return;
  }
  later(ms, [=] { gull_visit(step + 1, x, y, from_right); });
}

// A hooded crow's visit to the ridge: it flies in from the left over the trees and down onto it, caws and looks
// about, and flies back off the way it came. x, y are the visit's, its feet on the ridge. The ms it takes.
inline uint32_t crow_visit(int x, int y) {
  in_light("crowfly");
  in_light("crow");
  preload("crow");
  int along = 4300, down = 2200, stay = ms_of("crow") - STEP, off = 5000;
  place("crowfly", 0, -11, y - 31);
  move("crowfly", 0, x - 60, y - 31, along);
  move_later(along, "crowfly", 0, x, y, down);
  later(along + down, [=] { swap("crowfly", 0, "crow", x, y, true); });
  later(along + down + stay, [=] {
    swap_flipped("crow", 0, "crowfly", x, y + 1, true, true, 3);
    move("crowfly", 0, -20, y - 56, off, "in");
  });
  return gone_later(along + down + stay + off, "crowfly", 300, "crow");
}

// A red squirrel's visit to the roof: it bounds along the hill from off the left through the forest, up the roof's
// left side to the ridge, sits there nibbling a nut, and runs back down and off the way it came, the chimney being
// on the right. cx, cy are the cabin's. The ms it takes.
inline uint32_t squirrel_visit(const NearHill &h, int cx, int cy) {
  in_light("squirrel");
  in_light("squirrelnut");
  const Frames *f = frames_of("squirrel");
  int x0 = -(f ? f->w : 0) - 2, foot = cx - 24, run = (foot - x0) * 1000 / 14;
  uint32_t up = amble_ms(h, x0, foot, 14, 4, 4), down = up + 1700 + ms_of("squirrelnut") - STEP;
  return set_off("squirrel", "squirrelnut", down + 1700 + run, [=] {
    place("squirrel", 0, x0, h.at(x0 + 4) - 4, {.depth = D_NEAR_ON});  // behind the trees
    amble("squirrel", 0, h, x0, foot, 14, 4, 4);
    later(up, [=] {  // up onto the eaves, in front of the roof
      depth("squirrel", 0, ANIM_NEAREST);
      move("squirrel", 0, cx - 18, cy - 21, 300);
    });
    move_later(up + 300, "squirrel", 0, cx - 4, cy - 29, 1400);  // and up to the ridge
    later(up + 1700, [=] { swap("squirrel", 0, "squirrelnut", cx - 2, cy - 32, true); });
    later(down, [=] {
      swap_flipped("squirrelnut", 0, "squirrel", cx - 4, cy - 29, true);
      move("squirrel", 0, cx - 18, cy - 21, 1400);
    });
    move_later(down + 1400, "squirrel", 0, foot, h.at(foot + 4) - 4, 300);
    later(down + 1700, [=] {
      depth("squirrel", 0, D_NEAR_ON);
      move("squirrel", 0, x0, h.at(x0 + 4) - 4, run);
    });
  });
}

// Gulls, and about every 7 minutes a visitor on the cabin's ridge: a gull glides in against the wind, lands, looks
// about, preens and takes off again, a hooded crow comes to caw, or a red squirrel runs up over the roof
inline void gulls(const Scene &s) {
  flock({{60, 120, 6, 10, 100, 150}, {230, 105, 5, 8, 95, 140}});
  bool from_right = s.at.wdir > 0;
  int x = s.cabin_x - (from_right ? 6 : 8), y = s.cabin_y - 40;  // the roof's top row under its feet
  int cx = s.cabin_x, cy = s.cabin_y;
  NearHill h(s);
  visitors(minutes(7), minutes(0.5f, 3), 3, [=](int k) {
    if (k == 1)
      return crow_visit(cx - 5, cy - 34);
    if (k == 2)
      return squirrel_visit(h, cx, cy);
    gull_visit(0, x, y, from_right);
    return (uint32_t) (GLIDE_IN + ms_of("gullvisit") - STEP + FLY_OFF);
  });
}

// Puffs from the chimney top, drifting downwind further the more it blows
inline void stove_smoke(const Scene &s, const char *name) {
  chimney(name, s.puffs, s.chimney.x, s.chimney.y, 52, smoke_drift(s), s.at.wdir);
}

// A magpie hops about the meadow in front of the cabin a while, in the gap between the date and the temperatures,
// from row top, its hops in step with its frames. The ms it's there.
inline uint32_t magpie(int top) {
  in_light("magpie");
  int hop = ms_of("magpie", 0, 5), rest = ms_of("magpie", 5), l = DATE_R + 2, r = GAP_R;
  place("magpie", 0, (int) rnd((float) l, (float) (r - 10)), (int) rnd((float) (top - 2), (float) (top + 8)), {true});
  area("magpie", 0, l, top - 2, r, top + 15, "bounce", "bounce");
  face("magpie", 0);
  wander("magpie", 0, 8, 8, 0, 180, hop, hop, rest, rest);
  return gone_later(60000, "magpie");
}

// Smoke from the stove, by the heat, in December a paper star in the window, and by day now and then a magpie
inline void smoke(const Scene &s) {
  stove_smoke(s, s.at.dark ? "puffnight" : s.at.light < 0.5f ? "puffdusk" : "puff");
  if (s.at.month == 12)
    place("adventstar", 0, window_x(s), window_y(s));
  int top = meadow(s);
  if (!s.at.dark)
    visitors(minutes(6), minutes(1, 4), 1, [top](int) { return magpie(top); });
}

// A fox trots in front of the cabin from off one side, stops under the window to look at you, and trots on or back
// the way it came. y its top, stop where it stops. The ms it takes.
inline uint32_t fox_by(int y, int stop) {
  in_light("fox");
  in_light("foxlook");
  return stop_by("fox", "foxlook", stop, y, 0, 10);
}

// Firelight in the window and smoke dark against the night. Now and then the cat sits on the sill a while, or a fox
// trots by.
inline void cosy(const Scene &s) {
  int x = window_x(s), y = window_y(s), fox = std::min(s.cabin_y + 6, H - 9);
  place("firewin", 0, x, y);
  stove_smoke(s, "puffnight");
  visitors(minutes(6), minutes(1, 4), 2, [=](int k) {
    if (k)
      return fox_by(fox, x - 2);
    preload("firewincat");                                               // on the panel before it's swapped in
    uint32_t sits = (uint32_t) (ms_of("firewincat") * (int) rnd(2, 5));  // whole rounds of its frames
    later(8000, [x, y] { swap("firewin", 0, "firewincat", x, y, true); });
    later(8000 + sits - STEP, [x, y] {
      swap("firewincat", 0, "firewin", x, y);
      clear("firewincat");
    });
    return 8000 + sits;
  });
}

// A moose's visit a step at a time: in along the meadow from x0 to x1, grazing there, and back. MOOSE_DY puts it
// in the meadow in front of the cabin, its back a little under the cabin's floor.
inline constexpr int MOOSE_DY = -6;
inline void moose_visit(int step, const NearHill &h, int x0, int x1) {
  bool right = x1 < x0;  // it came from the right
  uint32_t ms = 24000;
  if (step == 0) {
    in_light("moose");
    in_light("moosegraze");
    preload("moosegraze");
    place_flipped("moose", 0, x0, h.at(x0 + 10) - MOOSE_DY, right);
    ms = amble("moose", 0, h, x0, x1, 3, 10, MOOSE_DY);
  } else if (step == 1) {
    swap_flipped("moose", 0, "moosegraze", x1, h.at(x1 + 10) - MOOSE_DY, right);
  } else if (step == 2) {
    swap_flipped("moosegraze", 0, "moose", x1, h.at(x1 + 10) - MOOSE_DY, !right);
    ms = amble("moose", 0, h, x1, right ? x0 + 2 : x0 - 2, 2.9f, 10, MOOSE_DY);
  } else {
    remove("moose", 0);
    return;
  }
  later(ms, [=] { moose_visit(step + 1, h, x0, x1); });
}

// A roe deer bounds out of the forest at its edge, e, and grazes, lifting its head to look about, then bounds back
// into the trees. The ms it takes.
inline uint32_t roe_deer(Edge e) {
  in_light("roedeer");
  in_light("roedeerrun");
  int x = e.x + 11, y = e.y - 6, graze = 2 * ms_of("roedeer") - STEP;   // two rounds of grazing and looking
  int in = out_of(e, "roedeerrun", y - 1), run = (x - in) * 1000 / 29;  // its bounds at 29 px/s
  return set_off("roedeerrun", "roedeer", 2 * run + graze, [=] {
    place("roedeerrun", 0, in, y - 1, {.own = true, .depth = D_NEAR_ON});
    move("roedeerrun", 0, x - 1, y - 1, run);
    later(run, [=] { swap("roedeerrun", 0, "roedeer", x, y, true); });
    later(run + graze, [=] {
      swap_flipped("roedeer", 0, "roedeerrun", x - 1, y - 1, true, true);
      move("roedeerrun", 0, flip_x("roedeerrun", in, true), y - 1, run);
    });
  });
}

// Now and then at dusk a moose comes along the meadow from off one side, grazes a while in front of the cabin, in the
// gap between the clock and the temperatures, and goes back. And a roe deer at the forest's edge.
inline void moose(const Scene &s) {
  Edge e = forest_edge(s);
  if (e.x >= 0)
    visitors(minutes(9), minutes(1, 6), 1, [e](int) { return roe_deer(e); });
  int graze = std::clamp<int>(s.cabin_x - 51, DATE_R, GAP_R - 62);  // the 62 px it takes, centred on the cabin
  NearHill h(s);
  sometimes(minutes(12), (uint32_t) rnd(20000, 240000), [=] {
    int x0 = chance(0.5f) ? W + 2 : -26;
    moose_visit(0, h, x0, graze);
    return (uint32_t) (abs(graze - x0) * 2000 / 3 + 24000);  // its walks and the grazing, about
  });
}

// Three reindeer, each in its own patch of the meadow along the bottom, in front of everything and under the
// temperatures: the patch's left and the row their feet put their top at
struct Herd {
  int16_t a[3], y[3], w;
};
inline Herd herd(const Scene &s) {
  Herd h;
  h.w = (int16_t) ((W - 4 - DATE_R - 16) / 3);
  for (int i = 0; i < 3; i++) {
    int a = DATE_R + i * (h.w + 8), y = hill_low(s, a, a + h.w) - 7;  // its feet on the land
    if (a + h.w > GAP_R)                                              // under the temperatures
      y = std::max(y, TEMPS_BOT + 3);
    else  // in the gap, in the meadow below the cabin's floor
      y = std::max<int>(y, s.cabin_y + 4);
    h.a[i] = (int16_t) a;
    h.y[i] = (int16_t) y;
  }
  return h;
}
// They graze their way about their patches, faded in over fade ms
inline void graze(const Herd &h, int fade = -1) {
  in_light("reindeer");
  for (int i = 0; i < 3; i++) {
    place("reindeer", i, h.a[i] + (h.w - 15) / 2 + (int) rnd(-8, 8), h.y[i], {true, i * 2, fade});
    area("reindeer", i, h.a[i], h.y[i] - 3, h.a[i] + h.w, h.y[i] + 15, "bounce", "bounce");
    face("reindeer", i);
    wander("reindeer", i, 1, 2.5f, 0, 180, 2000, 6000, 3000, 9000);
  }
}

// A hiker walks the meadow from the left while the reindeer are away, along row y in front of the cabin, down to
// the bottom of the picture before the spruces and on under the temperatures, and the reindeer come back. The ms.
inline uint32_t hike(const Herd &h, int y) {
  for (int i = 0; i < 3; i++)
    remove("reindeer", i, 3000);
  int bottom = TEMPS_BOT + 3, along = (190 + 14) * 1000 / 8, down = 2700, on = (W + 2 - 205) * 1000 / 8;
  later(3000, [=] {
    in_light("hiker");
    place("hiker", 0, -14, y);
    move("hiker", 0, 190, y, along);
    move_later(along, "hiker", 0, 205, bottom, down);
    move_later(along + down, "hiker", 0, W + 2, bottom, on);
  });
  uint32_t t = 3000 + along + down + on;
  later(t, [h] {
    clear("hiker");
    graze(h, 3000);
  });
  return t + 3000;
}

// A pair of ptarmigan, n for winter or summer, walk and peck about the meadow a while from row top, their walks in
// step with their frames. The ms they're there.
inline uint32_t ptarmigan(const char *n, int top) {
  in_light(n);
  int walk = ms_of(n, 0, 8), peck = ms_of(n, 8), l = DATE_R + 2, r = 206;
  for (int i = 0; i < 2; i++)
    later(i * 600, [=] {
      place(n, i, (int) rnd((float) l, (float) (r - 7)), (int) rnd((float) (top - 2), (float) (top + 4)),
            {true, 0, 1500});
      area(n, i, l, top - 2, r, top + 10, "bounce", "bounce");
      face(n, i);
      wander(n, i, 2.8f, 2.8f, 0, 180, walk, walk, peck, peck);
    });
  return gone_later(90000, n, 1500);
}

// Three reindeer grazing their way about, now and then away while a hiker walks by, a pair of ptarmigan in front
// of them, white when there's snow, and a golden eagle circling a while in the half of the sky away from the sun
inline void reindeer(const Scene &s) {
  Herd h = herd(s);
  graze(h);
  int y = s.cabin_y + 2, top = std::min(s.cabin_y + 20, H - 12);
  visitors(minutes(8), minutes(1, 6), 2,
           [=](int k) { return k ? ptarmigan(s.at.white ? "ptarmiganw" : "ptarmigan", top) : hike(h, y); });
  sometimes(minutes(10), minutes(1, 8), [] { return soar("eagle", 42, 40, 10); });
}

// Someone on a kick-sled along the meadow in front of the cabin at row y from the left, down to the bottom of the
// picture before the spruces and on under the temperatures, or the other way. The ms it takes.
inline uint32_t kick_sled(int y) {
  in_light("kicksled");
  bool left = chance(0.5f);
  int bottom = TEMPS_BOT + 3, along = (150 + 14) * 1000 / 14, down = 3600, on = (W + 2 - 200) * 1000 / 14;
  if (left) {
    place_flipped("kicksled", 0, W + 2, bottom, true);
    move("kicksled", 0, 200, bottom, on);
    move_later(on, "kicksled", 0, 150, y, down);
    move_later(on + down, "kicksled", 0, -14, y, along);
  } else {
    place("kicksled", 0, -14, y);
    move("kicksled", 0, 150, y, along);
    move_later(along, "kicksled", 0, 200, bottom, down);
    move_later(along + down, "kicksled", 0, W + 2, bottom, on);
  }
  return gone_later(along + down + on, "kicksled");
}

// Two children on a sledge down the near hill from its top left of the cabin to the meadow in front of it, quicker
// as they go, then they sit a moment and are gone. The ms it takes.
inline uint32_t sledge(const NearHill &h) {
  in_light("sledge");
  int x = GAP_L, y = h.at(x + 5) - 8;
  place("sledge", 0, x, y);
  move("sledge", 0, x + 41, y + 21, 2600, "in");
  move_later(2600, "sledge", 0, x + 61, y + 30, 900);
  move_later(3500, "sledge", 0, x + 73, y + 33, 1800, "out");
  return gone_later(8000, "sledge", 1500);
}

// A mountain hare hops out of the forest at its edge e, sits up a while, ears up, and hops back in. The ms it takes.
inline uint32_t hare_out(const NearHill &h, Edge e) {
  in_light("harew");
  in_light("haresitw");
  int x0 = out_of(e, "harew", h.at(e.tx) - 4), x1 = e.x + 19, sit = 2 * ms_of("haresitw") - STEP;
  uint32_t t = amble_ms(h, x0, x1, 7, 5, 4, 0, 16);
  return set_off("harew", "haresitw", 2 * t + sit, [=] {
    place("harew", 0, x0, h.at(x0 + 5) - 4, {.depth = D_NEAR_ON});
    amble("harew", 0, h, x0, x1, 7, 5, 4, 0, 16);
    later(t, [=] { swap("harew", 0, "haresitw", x1, h.at(x1 + 3) - 7, true); });
    later(t + sit, [=] {
      swap_flipped("haresitw", 0, "harew", x1, h.at(x1 + 5) - 4, true);
      amble("harew", 0, h, flip_x("harew", x1, true), flip_x("harew", x0, true), 7, 5, 4, 0, 16);
    });
  });
}

// Now and then a skier along the near hill, quicker downhill, more often at weekends. And snow slips off the
// cabin's eaves or the end of a front spruce's snowy tier, those in the gap between the clock and the temperatures
// when there are any there. By the cabin now and then someone on a kick-sled, children on a sledge down the hill, a
// hare out of the forest or a magpie.
inline void skier(const Scene &s) {
  NearHill h(s);
  bool weekend = s.at.weekday == 0 || s.at.weekday == 6;
  sometimes(minutes(weekend ? 10 : 30), minutes(0.5f, 4), [=] {
    bool from_right = chance(0.5f);
    int x0 = from_right ? W + 2 : -16;
    place_flipped("skier", 0, x0, h.at(x0 + 7) - 6, from_right);
    return remove_later(amble("skier", 0, h, x0, from_right ? -16 : W + 2, 8, 7, 6, 2), "skier", 0);
  });
  struct End {
    int16_t x, y;
    bool left;
  } ends[16], gap[4];
  int n = 0, seen = 0, g = 0;                  // 16 of them at random, and those in the gap apart
  auto end_at = [&](int e, int y, int side) {  // the snow's last px e on row y, side -1 left of the rest, 1 right
    if (e - 4 < 0 || e + 4 >= W)
      return;
    End end = {(int16_t) (side > 0 ? e - 3 : e - 4), (int16_t) y, side < 0};
    if (end.x >= GAP_L && end.x + 10 <= GAP_R) {
      if (g < 4)
        gap[g++] = end;
    } else if (n < 16) {
      ends[n++] = end;
    } else if (rnd(0, (float) ++seen + 16) < 16) {
      ends[(int) rnd(0, 16)] = end;
    }
  };
  if (s.cabin) {  // the eaves, the roof's lowest row
    end_at(s.cabin_x - 16, s.cabin_y - 16, -1);
    end_at(s.cabin_x + 16, s.cabin_y - 16, 1);
  }
  for (int i : near_trees(s, 0))
    for (int d = 13; s.tree[i].top + d <= s.tree[i].base; d++)  // the snowy tiers but the top one
      if (snow_tier(d))
        for (int side = -1; side <= 1; side += 2)
          end_at(s.tree[i].x + side * spruce_hw(d), s.tree[i].top + d, side);
  if (g) {
    std::copy(gap, gap + g, ends);
    n = g;
  }
  if (n)
    sometimes(minutes(2), (uint32_t) rnd(10000, 60000), [=] {
      End e = ends[(int) rnd(0, (float) n)];
      place_flipped("snowdrop", 0, e.x, e.y, e.left, {true});
      return remove_later(30000, "snowdrop", 0, 4000);
    });
  if (!s.cabin)
    return;
  Edge edge = forest_edge(s);
  int top = meadow(s), kick = s.cabin_y + 4;
  bool hills = s.ground == G_HILLS;
  visitors(minutes(5), minutes(1, 5), 4, [=](int k) -> uint32_t {
    if (k == 1)
      return hills ? sledge(h) : 0;
    if (k == 2)
      return edge.x >= 0 ? hare_out(h, edge) : 0;
    if (k == 3)
      return magpie(top);
    return kick_sled(kick);
  });
}

// Two swallows swoop low over the meadow from row top, quick and in curves, a while. The ms they're about.
inline uint32_t swallows(int top) {
  in_light("swallow");
  for (int i = 0; i < 2; i++) {
    place("swallow", i, i ? W + 2 : -15, top + 4 + 4 * i, {i > 0, 7 * i});
    area("swallow", i, -16, top + 4, W + 16, top + 15, "bounce", "bounce");
    face("swallow", i);
    wander("swallow", i, 50, 70, i ? 180 : 0, 30, 1500, 3000, 0, 0);
    wave("swallow", i, "y", -6, 6, 800, 900, 0, 0, 30, true, "both");
  }
  return gone_later(40000, "swallow");
}

// Someone mows the lawn in front of the cabin from row y, in strips to and fro across the gap between the clock and
// the temperatures, each a little nearer. The ms it takes.
inline uint32_t mow(int y) {
  in_light("mower");
  int l = GAP_L, r = GAP_R - 23, strips = 4, step = std::clamp((H - 14 - y) / strips, 0, 3);
  int leg = (r - l) * 1000 / 6;
  place("mower", 0, l, y, {true, 0, 1000});
  for (int k = 0; k < strips; k++)
    later(k * leg, [=] {
      bool back = k % 2;
      flip("mower", 0, back, false);
      move("mower", 0, flip_x("mower", back ? l : r, back), y + (k + 1) * step, leg);
    });
  return gone_later(strips * leg, "mower", 1000);
}

// Three sheep grazing on the middle hill's face for the while the show's on, shifting a little now and then, but
// for the town
inline void sheep(const Scene &s) {
  int x = GAP_L + 5, mid = s.land->hill[1][x + 14], near = s.land->hill[2][x + 14];
  if (s.ground == G_CITY || near - mid < 12)
    return;
  int y = (mid + near) / 2 - 3;
  in_light("sheep");
  place("sheep", 0, x, y, {false, 0, 2000, D_MID_ON});
  area("sheep", 0, x - 4, y, x + 33, y + 6, "bounce", "bounce");
  wander("sheep", 0, 0.3f, 0.3f, 0, 180, 3000, 3000, 6000, 12000);
}

// Two small tortoiseshells and a brimstone over the meadow in front of home, under the near hill's top and between
// the date and the temperatures. They flutter for as long as their frames do and sit for as long as they sit. Sheep
// on the hillside, and now and then swallows swooping by or the mower out on the lawn.
inline void butterflies(const Scene &s) {
  int l = DATE_R + 2, r = GAP_R - 2;
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
  sheep(s);
  bool lawn = s.cabin;
  int y = s.cabin_y + 1, low = std::min(top + 2, H - 28);
  visitors(minutes(6), minutes(1, 5), 2, [=](int k) -> uint32_t { return k ? (lawn ? mow(y) : 0) : swallows(low); });
}

// Leaf i lies on the grass at x, y, and a gust now and then tumbles it along, low, in rows top to bot. 3 fell, and
// takes the falling one's place.
inline void leaf_lies(int i, int x, int y, int top, int bot, int heading) {
  if (i == 3)
    swap("leaffall", i, "leaf", x, y, true);
  else
    place("leaf", i, x, y, {true});
  int roll = ms_of("leaf", 0, 8), lie = ms_of("leaf", 8);  // its 8 tumbling frames, then lying
  area("leaf", i, -6, top, W + 6, bot, "cross", "bounce");
  wander("leaf", i, 2, 4, heading, 25, roll, roll, lie, lie);
}

// Someone out picking mushrooms along the near hill's top from x0 behind the spruce at the forest's edge toward the
// cabin: two strides and a chanterelle picked, twice, then back into the trees. The ms it takes.
inline uint32_t mushrooms(const NearHill &h, int x0) {
  in_light("mushroomer");
  int walk = ms_of("mushroomer", 0, 16), round = ms_of("mushroomer");
  return set_off("mushroomer", nullptr, 3 * round + walk, [=] {
    place("mushroomer", 0, x0, h.at(x0 + 6) - 8, {.own = true, .depth = D_NEAR_ON});
    for (int k = 0; k < 4; k++)
      later(k * round, [=] {
        bool back = k >= 2;
        int to = back ? x0 + 13 * (3 - k) : x0 + 13 * (k + 1);
        flip("mushroomer", 0, back, false);
        move("mushroomer", 0, flip_x("mushroomer", to, back), h.at(to + 6) - 8, walk);
      });
  });
}

// A red squirrel bounds along the meadow at row y from off one side, sits up a while in the gap nibbling a nut, and
// bounds on or back the way it came. The ms it takes.
inline uint32_t squirrel_by(int y) {
  in_light("squirrel");
  in_light("squirrelnut");
  return stop_by("squirrel", "squirrelnut", (DATE_R + GAP_R) / 2 + (int) rnd(-15, 15), y, 3, 14);
}

// Birch leaves lie on the grass and now and then a gust tumbles them along, low, a few px/s. About every minute
// one lets go of a front birch and tumbles down to them, and a gust blows the last one that fell off downwind. Now
// and then a V of geese far up, small, slow and hazy, and in the meadow someone picking mushrooms or a squirrel.
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
      static constexpr int FALL = 9000, AWAY = 6000;
      Birch c = birches[(int) rnd(0, (float) n)];
      int x0 = c.x - 3 + (int) rnd(1 - c.rx, (float) c.rx), y0 = c.top + (int) rnd(1, c.h * 0.6f);
      int x1 = std::clamp(x0 + wdir * (int) rnd(18, 30), -2, W - 4), y1 = (int) rnd(top, bot - 6);
      if (anim::copy_for(key("leaf"), 3).second) {  // the last one off, so there's room for the flag and a visitor
        move("leaf", 3, wdir < 0 ? -10 : W + 4, top, AWAY, "in");
        remove_later(AWAY, "leaf", 3, 0);
      }
      place("leaffall", 3, x0, y0, {true});
      move("leaffall", 3, x1, y1, FALL);
      lie(FALL, 3, x1, y1);
      return (uint32_t) FALL;
    });
  sometimes(minutes(15), minutes(1, 10), [] {
    place("geese", 0, -30, 30);
    glide("geese", 0, "opacity", 70, 0);
    wave("geese", 0, "y", -2, 2, 6000, 6000, 0, 0, 20, false, "both");
    for (int k = 1; k <= 4; k++)  // a move takes 65 s at most
      move_later((k - 1) * 35000, "geese", 0, -30 + (W + 40) * k / 4, 30 - 2 * k, 35000);
    return remove_later(140000, "geese", 0);
  });
  NearHill h(s);
  Edge edge = forest_edge(s);
  const Frames *f = frames_of("mushroomer");
  int x0 = edge.x >= 0 && f ? edge.hide(f->w, f->h, h.at(edge.tx) + f->h - 9) : -1, y = std::min(top + 1, H - 6);
  if (!s.cabin || x0 + 26 + (f ? f->w : 0) > s.cabin_x - 17)  // its strides clear of the cabin
    x0 = -1;
  visitors(minutes(6), minutes(1, 5), 2,
           [=](int k) -> uint32_t { return k ? (x0 >= 0 ? mushrooms(h, x0) : 0) : squirrel_by(y); });
}

}  // namespace flexit::panel::met::live
