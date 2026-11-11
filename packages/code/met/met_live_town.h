// Animations by the sea and in town: the sailboat, a sjark, a sea eagle and a porpoise, the lighthouse and the
// coastal express, windows that go on and off, the antenna's light, a plane, the tram, the bus, the plough, a
// cyclist and pigeons
#pragma once
#include <algorithm>
#include <bitset>
#include <cstring>
#include <memory>
#include "met_live.h"
#include "met_live_land.h"

namespace flexit::panel::met::live {

// ---- by the sea

using Cols = std::bitset<W>;

// The shore's row: the middle hill where it's highest, 20 rows or more under the horizon, so not the point. H
// without one.
inline int shore_row(const Scene &s) {
  int shore = H;
  for (int x = 0; x < W; x++)
    if (s.land->hill[1][x] >= s.sea_top + 20)
      shore = std::min<int>(shore, s.land->hill[1][x]);
  return shore;
}

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
                  (y < s.pole_top + PENNANT_H + PENNANT_MAX / 2 || d == 0 || d == 1);
      if ((r[x] != s.ix[C_SEA] && r[x] != s.ix[C_WAVE] && !pole) ||
          (s.lighthouse && abs(x - LH_X) < 5 && y > s.lh_y - 34))
        c.reset(x);
    }
  }
  return c;
}
// The longest run of them in columns from to to as its first and last, and its length, 0 if there's none
inline int longest(const Cols &c, int from, int to, int &l, int &r) {
  int n = 0;
  for (int a = from, b; a <= to; a = b + 1) {
    for (b = a; b <= to && c[b]; b++) {
    }
    if (b - a > n) {
      n = b - a;
      l = a;
      r = b - 1;
    }
  }
  return n;
}

// The run of them around column x as its first and last, or the longest run with x < 0. False if there's none.
inline bool stretch(const Cols &c, int x, int &l, int &r) {
  if (x < 0)
    return longest(c, 0, W - 1, l, r) > 0;
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
inline bool gap_stretch(const Cols &c, int &l, int &r, int least) { return longest(c, GAP_L, GAP_R, l, r) >= least; }

// The sailboat from one end of its course to the other and back, l and r its left there, row its top. It runs
// before the wind at 0.9 px a second and beats back at 0.55. At each end it comes about over frames 4 to 8, the last
// of which is the boat the other way, so the flip doesn't show. Bow to the right it's mirrored.
inline const char *const SAIL = R"(
  if starts_right == 1 goto right
left:
  clip 0 to 3
  drift -vl, 0
  wait until x <= l
  drift 0, 0
  move l, row
  clip 4 to 8
  wait rounds 1
  flip x
right:
  clip 0 to 3
  drift vr, 0
  wait until x >= r
  drift 0, 0
  move r, row
  clip 4 to 8
  wait rounds 1
  flip none
  goto left
)";

// Whether the sun's reflection has open water under the horizon for some columns either side, by the land alone
inline bool glitter_room(const Scene &s) {
  for (int x = s.shine_x - 6; x <= s.shine_x + 6; x++)
    if (s.shine_x < 0 || x < 0 || x >= W ||
        std::min({s.land->hill[0][x], s.land->hill[1][x], s.land->hill[2][x]}) <= s.sea_top + 9)
      return false;
  return true;
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
    int glint = ms_of("glitter", 1, 4), dark = ms_of("glitter") - glint;  // from frame 4 round to 0, then 1 to 3
    place("glitter", i, (int) rnd(l, r - 2), (int) rnd(top, top + 7), std::string(i ? "glide opacity 0\n" : "") + R"(
      blend add
      area l, top, r + 1, top + 10 bounce
      wait after
      frame 4
      wander 3, 6, 0, 180, dark, dark, glint, glint
    )" + (i ? "glide opacity 100 over 300" : ""),
          {{"l", l}, {"r", r}, {"top", top}, {"after", 870 * i}, {"dark", dark}, {"glint", glint}},
          {.depth = D_SEA});  // a little after one another
  }
}

// Where something goes along the water: its first and last x as the sprite's left, and its top
struct Lane {
  int16_t l, r = -1, y;
};

// A sjark chugs along its lane from one end, at under 1 px/s, out of the haze and back into it. The ms it takes.
inline uint32_t sjark_by(Lane n) {
  in_light("sjark");
  bool left = chance(0.5f);
  int len = std::min(n.r - n.l, 50), a = n.l + (int) rnd(0, (float) (n.r - n.l - len));
  int x0 = left ? a + len : a, x1 = left ? a : a + len, ms = len * 1000 * 5 / 4;  // 0.8 px/s
  place("sjark", 0, x0, n.y, "move x1, y over ms; wait ms; end", {{"x1", x1}, {"y", n.y}, {"ms", ms}},
        {.left = left, .fade = 4000, .depth = D_SEA});
  return (uint32_t) ms;
}

// A sea eagle soars in over the fjord from the side away from the sun, circles there a while and goes. The ms.
inline uint32_t sea_eagle() {
  in_light("seaeagle");
  return soar("seaeagle", 78, 22, 7);
}

// A porpoise rolls along its lane, up and under twice. The ms it takes.
inline uint32_t porpoise(Lane n) {
  in_light("porpoise");
  bool left = chance(0.5f);
  int len = 26, a = n.l + (int) rnd(0, (float) (n.r - n.l - len)), x0 = left ? a + len : a, x1 = left ? a : a + len;
  int ms = 2 * ms_of("porpoise");
  place("porpoise", 0, x0, n.y, "frame 0; move x1, y over ms; wait rounds 2; end", {{"x1", x1}, {"y", n.y}, {"ms", ms}},
        {.left = left, .fade = 0, .depth = D_SEA});
  return (uint32_t) ms;
}

// Light on the water: faint dashes on a still of the sea, round and round, coming in slowly toward the shore and
// along it with the wind, shaped to the sea between the horizon and the land, in the light there is. Each row its
// own: small, close and faint out at the horizon, wider, further apart and brighter toward the shore, where they
// also go by quicker. Behind all that's on the water.
inline void shimmer(const Scene &s) {
  uint32_t bright = mix(s.rgb[C_SEA], 0xFFFFFF, 0.55f), dim = mix(s.rgb[C_SEA], 0xFFFFFF, 0.3f);
  add("shimmer", {96, 32, {1000}, [bright, dim](int, const Canvas &c) {
                    Rng r{17};
                    for (int i = 0; i < 26; i++) {
                      int u = r.in(0, 95), v = r.in(0, 31), n = r.in(2, 5);
                      for (int k = 0; k < n; k++)
                        c.put((u + k) % 96, v, k && k < n - 1 ? bright : dim);
                    }
                  }});
  in_light("shimmer");
  uint8_t top[W];
  memset(top, (uint8_t) s.sea_top, sizeof top);
  uint8_t bottom[W];
  for (int x = 0; x < W; x++)
    bottom[x] = s.land->hill[1][x] != NONE ? s.land->hill[1][x] : (uint8_t) H;
  place("shimmer", 0, 0, s.sea_top, "glide w " + num(W) + "; glide h " + num(H - s.sea_top) + R"(; glide opacity 45
          blend add
          repeat both
          scroll vx, 2
          wave opacity 60, 100, 5000, 6000 jitter 50 random slow both
          row.size := 55 + row * 2
          row.opacity := 50 + row * 2
          soft 0, 0, 8, 0
        )" + column_shape(top, bottom, s.sea_top),
        {{"vx", s.at.wdir * 0.8f}}, {.depth = D_SEA});
}

// The sailboat's course: the open water at its rows from l to r, where it lies, its left and top, and whether
// that's room to sail in
struct Course {
  int l, r, x, y;
  bool room;
};
inline Course course(const Scene &s) {
  Course c{0, 0, s.boat.x - 6, s.boat.y - 14, false};  // the picture's mast and waterline
  // it turns where the open water at its rows ends, off a skerry, a tree or the point, and stays between the
  // clock and the temperatures when there's room for that
  c.room = stretch(water(s, std::max<int>(s.sea_top, c.y), s.boat.y, true), s.boat.x, c.l, c.r) && c.r - c.l > 40;
  if (c.room) {
    in_gap(c.l, c.r);
    c.x = std::clamp(c.x, c.l + 1, c.r - 13);
  }
  return c;
}

// The picture's sailboat sails its course, or rocks where it is without the room
inline void sailboat(const Scene &s) {
  Course c = course(s);
  bool left = s.at.wdir < 0;
  place("boat", 0, c.x, c.y, c.room ? SAIL : "clip 0 to 3",
        {{"l", c.l + 1},
         {"r", c.r - 13},
         {"row", c.y},
         {"vl", left ? 0.9 : 0.55},
         {"vr", left ? 0.55 : 0.9},
         {"starts_right", !left}},
        {.left = !left, .depth = D_SEA});
}

// Now and then a sjark chugging along the fjord near the horizon off the sailboat's course, a sea eagle circling, or
// a porpoise rolling along the water between the sailboat and the shore
inline void off_the_coast(const Scene &s) {
  Course c = course(s);
  int a = c.room ? c.l + 1 : c.x, b = c.room ? c.r : c.x + 13;  // where the sailboat goes
  Lane sjark, roll;
  sjark.y = (int16_t) (s.sea_top - 2);  // its waterline on the 8th row
  Cols open = water(s, s.sea_top, s.sea_top + 6, true);
  for (int k = std::max(0, a - 6); k <= std::min(W - 1, b + 6); k++)
    open.reset(k);
  int sl, sr;
  if (stretch(open, -1, sl, sr) && sr - sl >= 60) {
    sjark.l = (int16_t) (sl + 2);
    sjark.r = (int16_t) (sr - 2 - 24);
  }
  roll.y = (int16_t) (s.boat.y + 1);
  if (gap_stretch(water(s, roll.y, roll.y + 3, false), sl, sr, 40) ||
      (stretch(water(s, roll.y, roll.y + 3, false), -1, sl, sr) && sr - sl >= 40)) {
    roll.l = (int16_t) sl;
    roll.r = (int16_t) (sr - 9);
  }
  visitors(minutes(6), minutes(1, 5), 3, [sjark, roll](int k) -> uint32_t {
    if (k == 1)
      return sjark.r > sjark.l ? sjark_by(sjark) : 0;
    if (k == 2)
      return roll.r > roll.l ? porpoise(roll) : 0;
    return sea_eagle();
  });
}

// A pair of eiders bobbing about on the open water off the shore, not the point's: the stretch in the gap between
// the clock and the temperatures, or the longest
inline void eiders(const Scene &s) {
  int shore = shore_row(s), l, r;
  Cols open = water(s, shore - 10, shore - 1, false);
  if (!gap_stretch(open, l, r, 30) && (!stretch(open, -1, l, r) || r - l < 40))
    return;
  place("eiders", 0, (l + r) / 2 - 7, shore - 8, R"(
    area l, shore - 10, r + 1, shore bounce
    face 180 mirror level
    wander 0.3, 0.6, 180, 30, 8000, 15000, 5000, 20000
    frame 0
  )",
        {{"l", l}, {"r", r}, {"shore", shore}}, {.depth = D_SEA});
}

// The lighthouse's lamp, its beam's length from it and height, and the colour of its light
inline constexpr int BEAM_LEN = 90, BEAM_H = 15;
inline constexpr uint32_t BEAM_COL = 0xFFF4D8;
inline int lamp_y(const Scene &s) { return s.lh_y - 28; }

// Where the lighthouse's beam goes round
inline Box sweep_box(const Scene &s) { return {LH_X - BEAM_LEN, lamp_y(s) - BEAM_H / 2, 2 * BEAM_LEN + 1, BEAM_H}; }

// The beam going round its lamp, a wedge of light the panel works out as it looks from the side each step: as long as
// it points across, none as it faces you or away, mirrored to the other side. Wider and brighter toward you, faint
// behind, fainter the further from the lamp. Round in 9 s, from anywhere.
inline const char *const TURNING = R"(
  blend add
  pivot 0.5, 50                 # the lamp, the middle of the beam's first column
  shared lit                    # the way it points: 0 at you, 90 to the right. The lamp's glow goes by it too.
  start = rand(0, 359)
  lit := (ms / 25 + start) % 360
  near := max(cos(lit, 100), 0) * max(cos(lit, 100), 0) / 100
  size_x := sin(lit, 100)
  size_y := 100 + near * 2 / 5
  opacity := 58 + cos(lit, 100) * 17 / 40
  soft 0, len, 0, 0
)";
// The lamp's glow flares as the beam faces you, its glare stretching out across
inline const char *const FLARING = R"(
  blend add
  shared lit
  near := max(cos(lit, 100), 0)
  flare := near * near / 100 * (near * near / 100) / 100
  size_x := 20 + flare * 4 / 5
  size_y := 50 + flare / 2
  opacity := 45 + flare * 11 / 20
)";

inline void lighthouse(const Scene &s) {
  float haze = std::max(0.0f, s.at.cover - 50) + s.at.fog;  // the beam shows more in it
  float strength = 0.7f + 0.3f * clamp01(haze / 80);
  // brightest along its middle, and round the lamp
  add_field("beam", BEAM_LEN + 1, BEAM_H, OVERLAY_FIELD_CONE, 20, 0,
            light(BEAM_COL, [=](float v) { return strength * (2 * v - v * v) * (2 * v - v * v); }));
  add_field("lamp", 41, BEAM_H, OVERLAY_FIELD_RADIAL, 0, 0, light(BEAM_COL, [](float v) { return v * v; }));
  place("lamp", 0, LH_X - 20, lamp_y(s) - BEAM_H / 2, FLARING);
  place("beam", 0, LH_X, lamp_y(s) - BEAM_H / 2, TURNING, {{"len", BEAM_LEN}});
  // A couple of times an evening the coastal express crawls along the horizon, from behind the headland to the
  // lighthouse
  int x = 0, l, r;
  while (x < W && s.land->hill[0][x] != NONE)
    x++;
  x += 4;
  if (!stretch(water(s, s.sea_top + 1, s.sea_top + 3, true), x, l, r) || r - 26 < x + 30)
    return;
  int y = s.sea_top - 5, ms = (r - 26 - x) * 10000 / 7;  // 0.7 px/s
  visitors(minutes(60), minutes(10, 40), 1, [=](int) {
    place("ship", 0, x, y, "drift 0.7, 0; wait ms; end", {{"ms", ms}}, {.fade = 5000, .depth = D_SEA});
    return (uint32_t) ms + 5000;
  });
}

// ---- in town

struct Win {
  int16_t x, y;  // x -1: none
  bool lit;
};
// The evening's windows in full view, the one each copy changed, whether the trams run, whether the plough's out,
// and the row a plane comes in from, -1 for none
struct Town {
  std::vector<Win> wins;
  Win slot[5];
  bool trams, snow;
  int plane;
};
inline std::weak_ptr<Town> town;  // the timers hold it, so it goes with the act

// Copy 0 of n along the road at row y and speed px/s, to the right or the left, and gone once it's by. The ms.
inline uint32_t drive(const char *n, bool right, int y, float speed) {
  const Frames *sp = frames_of(n);
  int w = sp ? sp->w : 0, ms = (int) ((float) (W + w + 4) * 1000 / speed);
  return right ? across(n, -w - 2, y, W + 2, y, ms) : across(n, W + 2, y, -w - 2, y, ms);
}
// The top row of something h tall driving to the right, in the near lane, or the left, in the far one
inline int lane(bool right, int h) { return (right ? ROAD_KERB : ROAD_MID) - h; }

// The tram's row: its wheels in the road's far lane
inline int tram_y(const Scene &) { return ROAD_MID - 12; }
// a tram along the road at a walking pace, from either side. The ms it takes.
inline uint32_t tram_by(const char *name, int y) { return drive(name, chance(0.5f), y, 8); }

// The bus, by day or with its windows lit at night, at 10 px/s
inline uint32_t bus_by(const char *name) {
  if (strcmp(name, "busnight"))
    in_light(name);
  bool right = chance(0.5f);
  return drive(name, right, lane(right, 10), 10);
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
  t->snow = s.at.white;
  t->plane = sky_line(s, 9, 5, 12, 90, 6);
}

// The light on the antenna's top over the picture's
inline void beacon(const Scene &s) { place("beacon", 0, s.antenna.x, s.antenna.y); }

inline void town_evening(const Scene &s) {
  auto t = std::make_shared<Town>();
  for (Win &w : t->slot)
    w.x = -1;
  town = t;
  town_redo(s);
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
    place(n.lit ? "windark" : "winlit", k, n.x, n.y, "frame " + num((int) rnd(0, 4)), {}, {.fade = 400});
    w = n;
    return 0;
  });
  // Now and then along the road a tram, the night bus or the plough after snow, or a plane high up coming in to
  // land, left to right as its lights are
  int y = tram_y(s);
  visitors(minutes(5), minutes(2), 4, [t, y](int k) -> uint32_t {
    if (k == 1)
      return bus_by("busnight");
    if (k == 2) {
      bool right = chance(0.5f);
      return t->snow ? drive("plough", right, lane(right, 10), 9) : 0;
    }
    if (k == 3) {
      if (t->plane < 0)
        return 0;
      return across("plane", -10, t->plane, W + 1, t->plane + 6, (W + 11) * 1000 / 4);
    }
    return t->trams ? tram_by("tram", y) : 0;
  });
}

// Pigeons on the widest flat roof of the near row that's clear of the forecast and the flag, now and then wheeling
// round
inline void pigeons(const Scene &s) {
  int best = -1;
  for (int i = 0; i < s.nbld; i++) {
    const Bld &b = s.bld[i];
    int x = b.x + (b.w - 34) / 2;
    bool flag =
        s.pole_x >= 0 && s.pole_top < b.top && s.pole_x + PENNANT_RIGHT >= x && s.pole_x - PENNANT_LEFT <= x + 33;
    if (!b.back && b.kind == 0 && b.w >= 34 && b.top - 16 >= (s.overlay ? OV_H : 0) && !flag &&
        (best < 0 || b.w > s.bld[best].w))
      best = i;
  }
  if (best < 0)
    return;
  const Bld &b = s.bld[best];
  in_light("pigeons");
  place("pigeons", 0, b.x + (b.w - 34) / 2, b.top - 16, "", {}, {.fade = 2000});
}

inline void town_day(const Scene &s) {
  int roof = H;  // the near row's highest roof, but for the tower and the spire
  for (int i = 0; i < s.nbld; i++)
    if (!s.bld[i].back && s.bld[i].kind < 2)
      roof = std::min<int>(roof, s.bld[i].top);
  // two gulls over the roofs, one of them down on a flat one now and then
  flock_lands(s, GULL, "gull", 0, "", false,
              {{60, roof - 30, 6, 10, roof - 50, roof + 5}, {220, roof - 40, 5, 8, roof - 56, roof}});
  // Now and then along the road a tram, the bus or a cyclist in the bike lane, keeping right
  int y = tram_y(s);
  visitors(minutes(4), minutes(2), 3, [y](int k) -> uint32_t {
    if (k == 1)
      return bus_by("bus");
    if (k == 2) {
      in_light("cyclist");
      return drive("cyclist", true, ROAD_NEAR - 6, 12);
    }
    return tram_by("tramday", y);
  });
  pigeons(s);
}

}  // namespace flexit::panel::met::live
