// Animations on the land: gulls, a crow and a squirrel on the roof, smoke from the chimney, a magpie, firelight, the
// cat and a fox, a moose and a roe deer, reindeer, ptarmigan, a hiker and an eagle, a skier and others out on the
// snow, butterflies, swallows, sheep and the mower, falling leaves, a mushroom picker and geese
#pragma once
#include "met_live.h"

namespace flexit::panel::met::live {

// How far the smoke drifts downwind, px
inline int smoke_drift(const Scene &s) { return std::min(60, 10 + (int) (s.at.wind * 4)); }

// What the land's acts are made of, so they start again when it changes: the cabin, so the landscape, then the way
// the wind blows, then the smoke's puffs, drift and colour and Christmas
inline uint32_t cabin_at(const Scene &s) { return (uint32_t) s.cabin_x << 8 | (uint32_t) s.cabin_y; }
inline uint32_t cabin_wind(const Scene &s) { return cabin_at(s) | (uint32_t) (s.at.wdir > 0) << 17; }
inline uint32_t cabin_smoke(const Scene &s) {
  return cabin_wind(s) | (uint32_t) s.puffs << 18 | (uint32_t) smoke_drift(s) / 8 << 21 |
         (uint32_t) (s.at.dark + (s.at.light < 0.5f)) << 25 | (uint32_t) s.at.yule << 27;
}

// The top row of what an act sets about the meadow in front of the cabin, below its floor
inline int meadow(const Scene &s) { return std::min(s.cabin_y + 10, H - 16); }

// ---- birds that come down

// Where a bird can come down: the column its feet's middle is at and the row they stand on, how big it's seen there,
// percent, and how deep it is there, so what's nearer is in front of it. What's far off is smaller.
struct Perch {
  int16_t x, y;
  uint8_t size, depth;
};
inline constexpr int FAR_BIRD = 60;  // percent a bird is seen at on the middle hill, the lighthouse and the town

// The picture's, each in full view with room for a bird on it clear of the forecast and of the panel's texts: the
// cabin's ridge left of its chimney, the flagpole's top, the lighthouse's, and the town's flat roofs. With trees,
// the tops of the near spruces too, and of those on the middle hill.
inline std::vector<Perch> perches(const Scene &s, bool trees) {
  std::vector<Perch> all;
  // whether something nearer than the middle hill stands over column x at row y: the near hill, its spruces, the
  // cabin or the pole
  auto behind = [&](int x, int y) {
    x = std::clamp(x, 0, W - 1);
    bool hid =
        s.land->hill[2][x] <= y || (s.pole_x >= 0 && abs(x - s.pole_x) < 2 && y >= s.pole_top) ||
        (s.cabin && x >= s.cabin_x + EAVES_L - 1 && x <= s.cabin_x + EAVES_R + 1 && y >= s.cabin_y - CABIN_TOP - 1);
    for (int i : near_trees(s, -1))
      hid = hid || (abs(x - s.tree[i].x) <= (s.tree[i].base - s.tree[i].top) / 2 + 2 && y >= s.tree[i].top);
    return hid;
  };
  auto add = [&](int x, int y, bool far) {
    constexpr int HALF = 11, UP = 15;  // half the widest of the birds, and the tallest
    bool texts = y >= TEXTS_TOP && (x - HALF < GAP_L || x + HALF > GAP_R);
    if (x - HALF < 0 || x + HALF >= W || y - UP < (s.overlay ? OV_H : 0) || texts)
      return;
    for (int dx = -5; far && dx <= 5; dx += 5)
      if (behind(x + dx, y) || behind(x + dx, y - UP * FAR_BIRD / 100))
        return;
    all.push_back({(int16_t) x, (int16_t) y, (uint8_t) (far ? FAR_BIRD : 100), (uint8_t) (far ? D_MID_ON : D_AIR)});
  };
  if (s.cabin)
    add(s.cabin_x + RIDGE_L + 15, ridge_y(s), false);
  if (s.pole_x >= 0)
    add(s.pole_x, s.pole_top, false);
  if (s.lighthouse)
    add(LH_X, s.lh_y - 34, true);
  for (int i = 0; trees && i < s.ntrees; i++)
    if (s.tree[i].kind == 0 && s.tree[i].layer)
      add(s.tree[i].x, s.tree[i].top, s.tree[i].layer < 2);
  for (int i = 0; i < s.nbld; i++)
    if (!s.bld[i].back && s.bld[i].kind == 0 && s.bld[i].w >= 14)
      add(s.bld[i].x + s.bld[i].w / 2, s.bld[i].top - 1, true);
  return all;
}

// A bird that comes down: its sprite, with a clip it flies in and one it lands, stays and takes off in, the frames it
// flies in and off from, and the box it stands in. Where its flying box is in that one as it comes and as it leaves,
// how far above its perch it comes in from and leaves to, how far short of the perch it starts down from level
// flight, 0 for all the way, its px/s in and off, whether it flies back the way it came, whether it's lit as the
// scene is, and what its sprite takes of the pool over a visitor's.
struct Lander {
  const char *sprite, *fly, *stay;
  int8_t fly_in, fly_off, w, h, dx, dy_off, lean;
  int16_t drop, rise, lead;
  float in, off;
  bool back, lit;
  int16_t more;
};
// A herring gull glides in against the wind, flares, lands, looks about, calls and preens, and flies on. A hooded
// crow comes in level and drops onto it, caws and looks about, and flies back the way it came.
inline constexpr Lander GULL = {.sprite = "gullvisit",
                                .fly = "fly",
                                .stay = "stay",
                                .fly_in = 7,
                                .fly_off = 2,
                                .w = 21,
                                .h = 15,
                                .dx = 1,
                                .dy_off = 4,
                                .lean = 1,
                                .drop = 43,
                                .rise = 50,
                                .lead = 0,
                                .in = 13,
                                .off = 19,
                                .back = false,
                                .lit = false,
                                .more = 600};
inline constexpr Lander CROW = {.sprite = "crow",
                                .fly = "fly",
                                .stay = "sit",
                                .fly_in = 0,
                                .fly_off = 3,
                                .w = 11,
                                .h = 9,
                                .dx = 0,
                                .dy_off = 1,
                                .lean = 0,
                                .drop = 31,
                                .rise = 56,
                                .lead = 60,
                                .in = 32,
                                .off = 40,
                                .back = true,
                                .lit = true,
                                .more = 0};

// Whether copy i of a flying sprite is on the panel
inline bool flying(const char *n, int i) { return overlay::copy_for(key(n), i).second; }

// The longest one of two that fly back and forth is given to go out of sight on the side it's waited for at, across
// the picture and back, ms
inline constexpr uint32_t OUT_OF_SIGHT = 200000;

// One of the two of a sprite that fly back and forth: its copy and how it flies
struct Flying {
  const char *sprite;
  int copy;
  Bird bird;
};

// Bird b comes down on perch p: in from off the right or the left, smaller on its way to one far off, it lands,
// stays a round of its clip, takes off and flies off, as big as it was by then. As one of two flying back and forth:
// that one goes out of sight on the side it comes from first, at one of its turns, and stays away till it has left,
// then comes back in where it went. The ms it takes at the most, 0 without the room for it.
inline uint32_t land(const Lander &b, Perch p, bool from_right, const Flying *flock = nullptr) {
  if (b.more && !room_for({0, 0, b.more}))
    return 0;
  const Frames *sp = frames_of(b.sprite);
  if (!sp)
    return 0;
  int dir = from_right ? -1 : 1, x = p.x - b.w / 2 - dir * b.lean, y = p.y - b.h + 1;  // its box, standing
  int x0 = from_right ? W + 2 : -b.w - 2, x1 = b.back ? x0 : W - b.w - x0;             // in from, and off to
  int level = b.lead && abs(x - x0) > b.lead ? x - dir * b.lead : x0;                  // where it starts down
  int in = abs(level - x0) * 1000 / (int) b.in, down = std::max(2000, abs(x - level) * 1000 / (int) b.in);
  int off = std::max(3000, abs(x1 - x) * 1000 / (int) b.off), stay = ms_of(b.sprite, b.stay);
  bool far = p.size < 100;
  std::string size = far ? "pivot 50, 100\nglide size " + num(p.size) + " over " + num(in + down) + "\n" : "";
  std::string t = std::string(flock ? "shared lit, away\nwait until away == 1\n" : "") + "clip " + b.fly + "\nframe " +
                  num(b.fly_in) + "\n" + size + path({{level, y - b.drop, in}, {x + b.dx, y, down}}) +
                  (far ? "depth " + num(p.depth) + "\n" : "") + "clip " + b.stay + "\nmove sx, sy\nwait rounds 1\n" +
                  (b.back ? from_right ? "flip none\n" : "flip x\n" : "") + "clip " + b.fly + "\nframe " +
                  num(b.fly_off) + "\n" + (far ? "depth " + num(D_AIR) + "\nglide size 100 over off\n" : "") + R"(
    move sx + dx, sy + dy
    move x1, sy - rise over off ease in
    wait off
  )" + (flock ? "away = 2\n" : "") +
                  "end";
  if (b.lit)
    in_light(b.sprite);
  place(b.sprite, 0, x0, y - b.drop, t,
        {{"sx", x}, {"sy", y}, {"dx", b.dx}, {"dy", b.dy_off}, {"x1", x1}, {"rise", b.rise}, {"off", off}},
        {.left = from_right});
  uint32_t ms = (uint32_t) (in + down + stay + off);
  if (!flock)
    return ms;
  // out at a turn on the side the other comes in from, if it gets there in time, away while that one's here, and
  // back in where it flew off
  script(flock->sprite, flock->copy,
         R"(
    shared lit, away
    away = 0
    t = ms + out
    wait until ms > t or )" +
             std::string(from_right ? "x >= 322" : "x <= -20") + R"(
    if ms > t goto late
    glide opacity 0
    away = 1
    t = ms + limit
    wait until away == 2 or ms > t
    away = 0
    move back, y1
    glide opacity 100
    )" + ROAMS +
             "late:\n",
         {{"out", OUT_OF_SIGHT - 20000},
          {"limit", ms + 10000},
          {"back", x1 < 0 ? -20 : 322},
          {"y1", y - b.rise},
          {"s0", flock->bird.s0},
          {"s1", flock->bird.s1}});
  return OUT_OF_SIGHT + ms;
}

// Two of a sprite fly back and forth, copies from first, in its clip if it has one, and about every five minutes one
// of them comes down on one of the picture's perches, against the wind there is by then
inline void flock_lands(const Scene &s, const Lander &b, const char *sprite, int first, const char *clip, bool trees,
                        const std::vector<Bird> &two) {
  flock(sprite, first, clip, two);
  std::vector<Perch> where = perches(s, trees);
  if (!where.empty())
    visitors(minutes(4.5f), minutes(1, 3), 1, [=, &b](int) {
      int i = (int) rnd(0, 2);
      Flying one{sprite, first + i, two[(size_t) i]};
      return land(b, where[(size_t) rnd(0, (float) where.size())], R.wdir > 0, &one);
    });
}

// A red squirrel's visit to the roof: it bounds along the hill from off the left through the forest, up the gable's
// left side to the ridge and along it, sits there nibbling a nut, and runs back down and off the way it came, the
// chimney being on the right. cx, cy are the cabin's. The ms it takes.
inline uint32_t squirrel_visit(const NearHill &h, int cx, int cy) {
  in_light("squirrel");
  const Frames *f = frames_of("squirrel");
  int x0 = -(f ? f->w : 0) - 2, foot = cx + EAVES_L - 9, run = (foot - x0) * 1000 / 14;
  Walk up = walk(h, x0, foot, 14, 4, 4);
  place("squirrel", 0, x0, up.y[0], R"(
    clip run
    b = n; d = 1; t = leg
  )" + up.loop("up") + R"(
    depth 255              # up onto the eaves, in front of the roof
    move eaves, ey over 300
    wait 300
    move top, ry over 900
    wait 900
    move nut - 2, ry over 500
    wait 500
    clip nut
    move nut, ry - 3
    wait rounds 1
    flip x
    clip run
    move nut - 3, ry
    move top, ry over 500
    wait 500
    move eaves, ey over 900
    wait 900
    move foot, fy over 300
    wait 300
    depth behind           # behind the trees
    move x0, y0 over run
    wait run
    end
  )",
        {{"n", up.n},
         {"leg", up.leg(14)},
         {"eaves", cx + EAVES_L - 3},
         {"ey", cy - CABIN_EAVES - 5},  // its 5 rows on the eaves' end, then on the ridge
         {"top", cx + RIDGE_L - 6},
         {"ry", cy - CABIN_RIDGE - 5},
         {"nut", cx + RIDGE_L + 5},
         {"foot", foot},
         {"fy", up.y[up.n]},
         {"behind", D_NEAR_ON},
         {"x0", x0},
         {"y0", up.y[0]},
         {"run", run}},
        {.depth = D_NEAR_ON});
  return (uint32_t) (up.n * up.leg(14) + 2 * (300 + 900 + 500) + ms_of("squirrel", "nut") + run);
}

// Two gulls over the sea, one of which comes down now and then, where a gull would
inline void gulls(const Scene &s) {
  flock_lands(s, GULL, "gull", 0, "", false, {{60, 120, 6, 10, 100, 150}, {230, 105, 5, 8, 95, 140}});
}
// and two hooded crows inland, on the spruces too: the one that's down is copy 0
inline void crows(const Scene &s) {
  in_light("crow");
  flock_lands(s, CROW, "crow", 1, "fly", true, {{70, 112, 8, 12, 96, 140}, {240, 100, 7, 11, 92, 134}});
}

// Puffs from the chimney top, drifting downwind further the more it blows, dark against the night: as many of the
// scene's, which go by the heat, as the act has places for. Those that are there stay as they are.
inline void chimney_smoke(const Scene &s) {
  const char *name = s.at.dark ? "puffnight" : s.at.light < 0.5f ? "puffdusk" : "puff";
  const overlay::Overlay *a = overlay::find(key(name));
  for (int i = 0, n = std::min<int>(s.puffs, granted().places); i < s.puffs; i++) {
    bool there = a && (size_t) i < a->copies.size() && a->copies[i].used;
    if (i < n && !there)
      chimney(name, s.puffs, s.chimney.x, s.chimney.y, 52, smoke_drift(s), s.at.wdir, i, i + 1);
    else if (i >= n && there)
      remove(name, i);
  }
}

// A magpie or a wagtail, n, hops about the meadow in front of the cabin a while, in the gap between the date and the
// temperatures, from row top, its hops in step with its frames. The ms it's there.
inline uint32_t lawn_bird(const char *n, int top) {
  in_light(n);
  int l = DATE_R + 2, r = GAP_R;
  place(n, 0, (int) rnd((float) l, (float) (r - 10)), (int) rnd((float) (top - 2), (float) (top + 8)), R"(
    area l, top - 2, r, top + 15 bounce
    face 0 mirror level
    wander 8, 8, 0, 180, hop, hop, rest, rest
    frame 0
    wait 60000
    end
  )",
        {{"l", l}, {"r", r}, {"top", top}, {"hop", ms_of(n, 0, 5)}, {"rest", ms_of(n, 5)}});
  return 60000;
}

// Through Christmas a paper star in the window
inline void advent_star(const Scene &s) { place("adventstar", 0, window_x(s), window_y(s)); }

// By day about every 7 minutes someone comes to the cabin: a red squirrel up over the roof, a magpie about the
// meadow, and while none are flying about, a crow that comes down on one of the picture's perches, or by the sea a
// gull
inline void about_the_cabin(const Scene &s) {
  bool sea = s.ground == G_COAST;
  int cx = s.cabin_x, cy = s.cabin_y, top = meadow(s);
  NearHill h(s);
  std::vector<Perch> crow = perches(s, true), gull = perches(s, false);
  visitors(minutes(7), minutes(0.5f, 3), sea ? 4 : 3, [=](int k) -> uint32_t {
    if (k == 0)
      return flying("crow", 1) || crow.empty() ? 0 : land(CROW, crow[(size_t) rnd(0, (float) crow.size())], R.wdir > 0);
    if (k == 1)
      return squirrel_visit(h, cx, cy);
    if (k == 2)
      return lawn_bird("magpie", top);
    return flying("gull", 0) || gull.empty() ? 0 : land(GULL, gull[(size_t) rnd(0, (float) gull.size())], R.wdir > 0);
  });
}

// Copy 0 of n walks in from off one side along row y at speed px/s to stop, its walk clip's frames, where it's a
// round of its stay clip's dy rows higher, then on off the other side or back the way it came, facing the way it
// goes. The ms it takes.
inline uint32_t stop_by(const char *n, const char *walk, const char *stay, int stop, int y, int dy, float speed) {
  const Frames *f = frames_of(n);
  int w = f ? f->w : 0;
  bool left = chance(0.5f), back = chance(0.5f), on = back ? !left : left;  // the way it comes and goes on
  int x0 = left ? W + 2 : -w - 2, x1 = on ? -w - 2 : W + 2;
  int in = (int) ((float) abs(stop - x0) * 1000 / speed), out = (int) ((float) abs(x1 - stop) * 1000 / speed);
  place(n, 0, x0, y,
        std::string("clip ") + walk + R"(
    move sx, y over in
    wait in
    clip )" +
            stay + R"(
    move sx, y - dy
    wait rounds 1
    flip )" +
            (on ? "x" : "none") + "\nclip " + walk + R"(
    move sx, y
    move ex, y over out
    wait out
    end
  )",
        {{"sx", stop}, {"ex", x1}, {"y", y}, {"dy", dy}, {"in", in}, {"out", out}}, {.left = left});
  return (uint32_t) (in + ms_of(n, stay) + out);
}

// A fox trots in front of the cabin from off one side, stops under the window to look at you, and trots on or back
// the way it came. y its top, stop where it stops. The ms it takes.
inline uint32_t fox_by(int y, int stop) {
  in_light("fox");
  return stop_by("fox", "trot", "look", stop, y, 0, 10);
}

// Firelight in the window, and now and then the cat sits on the sill a while
inline void firelight(const Scene &s) {
  place("firewin", 0, window_x(s), window_y(s), "clip fire");
  sometimes(minutes(12), minutes(1, 4), [] {
    int sits = (int) rnd(2, 5);  // whole rounds of it
    script("firewin", 0, "clip fire; wait 8000; clip cat; wait rounds sits; clip fire", {{"sits", sits}});
    return (uint32_t) (8000 + sits * ms_of("firewin", "cat"));
  });
}

// By night now and then a fox trots by the cabin
inline void cabin_by_night(const Scene &s) {
  int y = std::min(s.cabin_y + 6, H - 9), stop = window_x(s) - 2;
  visitors(minutes(12), minutes(1, 6), 1, [=](int) { return fox_by(y, stop); });
}

// A moose, n, comes in along the meadow from x0 to x1, grazes there and goes back, a cow with her calf behind her,
// which is her at three fifths the size. MOOSE_DY puts it in the meadow in front of the cabin, its back a little
// under the cabin's floor. The ms it takes.
inline constexpr int MOOSE_DY = -6;
inline uint32_t moose_visit(const NearHill &h, const char *n, bool calf, int x0, int x1) {
  bool right = x1 < x0;  // it came from the right
  in_light(n, 0.6f);     // not as dark as the dusk, or it's lost against the hill
  uint32_t ms = 0;
  for (int i = 0; i <= calf; i++) {
    int behind = i * (right ? 20 : -20);
    Walk w = walk(h, x0 + behind, x1 + behind, 3, 10, MOOSE_DY);
    place(n, i, x0 + behind, w.y[0],
          std::string(i ? "pivot 50, 100\nglide size 60\n" : "") + R"(
      clip walk
      b = n; d = 1; t = in
    )" + w.loop("go") +
              R"(
      if k == 0 goto gone
      clip graze
      move x1, v
      wait 24000
      flip )" +
              (right ? "none" : "x") + R"(
      clip walk
      b = 0; d = -1; t = back
      goto go
    gone:
      end
    )",
          {{"n", w.n}, {"in", w.leg(3)}, {"back", w.leg(2.9f)}, {"x1", x1 + behind}}, {.left = right});
    ms = std::max(ms, (uint32_t) (w.n * (w.leg(3) + w.leg(2.9f)) + 24000));
  }
  return ms;
}

// A roe deer bounds out of the forest at its edge, e, and grazes, lifting its head to look about, then bounds back
// into the trees. The ms it takes.
inline uint32_t roe_deer(const char *n, Edge e) {
  in_light(n, 0.6f);  // as the moose
  int x = e.x + 11, y = e.y - 10;
  int in = out_of(e, n, y - 1), run = (x - in) * 1000 / 29;  // its bounds at 29 px/s
  place(n, 0, in, y - 1, R"(
    clip run
    move x - 1, y - 1 over run
    wait run
    clip graze
    move x, y
    wait rounds 2
    flip x
    clip run
    move x - 1, y - 1
    move in, y - 1 over run
    wait run
    end
  )",
        {{"x", x}, {"y", y}, {"run", run}, {"in", in}}, {.depth = D_NEAR_ON});
  return (uint32_t) (2 * run + 2 * ms_of(n, "graze"));
}

// Now and then at dusk a moose comes along the meadow from off one side, grazes a while in front of the cabin, in the
// gap between the clock and the temperatures, and goes back: a bull with his antlers from June till the year is out,
// in summer as often a cow with her calf, and from January one without antlers. And a roe deer at the forest's edge,
// grey from November to April.
inline void moose(const Scene &s) {
  Edge e = forest_edge(s);
  const char *roe = s.at.month >= 11 || s.at.month <= 4 ? "roedeerw" : "roedeer";
  if (e.x >= 0)
    visitors(minutes(9), minutes(1, 6), 1, [roe, e](int) { return roe_deer(roe, e); });
  int graze = std::clamp<int>(s.cabin_x - 51, DATE_R, GAP_R - 62);  // the 62 px it takes, centred on the cabin
  bool antlers = s.at.month >= 6, summer = antlers && s.at.month <= 8;
  NearHill h(s);
  sometimes(minutes(12), (uint32_t) rnd(20000, 240000), [=] {
    bool calf = summer && chance(0.5f);
    return moose_visit(h, antlers && !calf ? "moose" : "moosecow", calf, chance(0.5f) ? W + 2 : -26, graze);
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
// Each grazes its way about its patch, in frames of its own
inline void graze(const Herd &h) {
  in_light("reindeer");
  for (int i = 0; i < 3; i++)
    place("reindeer", i, h.a[i] + (h.w - 15) / 2 + (int) rnd(-8, 8), h.y[i], R"(
      area l, y - 3, l + w, y + 15 bounce
      face 0 mirror level
      wander 1, 2.5, 0, 180, 2000, 6000, 3000, 9000
      frame f
    )",
          {{"l", h.a[i]}, {"y", h.y[i]}, {"w", h.w}, {"f", i * 2}});
}

// A hiker walks the meadow from the left while the reindeer are away, along row y in front of the cabin, down to
// the bottom of the picture before the spruces and on under the temperatures, and the reindeer come back. The ms.
inline uint32_t hike(int y) {
  int bottom = TEMPS_BOT + 3, along = (190 + 14) * 1000 / 8, down = 2700, on = (W + 2 - 205) * 1000 / 8;
  int walks = along + down + on;
  for (int i = 0; i < 3; i++)
    script("reindeer", i, "glide opacity 0 over 3000; frame f; wait 3000 + walks; glide opacity 100 over 3000",
           {{"f", i * 2}, {"walks", walks}});
  in_light("hiker");
  place("hiker", 0, -14, y, "wait 3000\n" + path({{190, y, along}, {205, bottom, down}, {W + 2, bottom, on}}) + "end");
  return (uint32_t) (3000 + walks + 3000);
}

// A pair of ptarmigan, n for winter or summer, walk and peck about the meadow a while from row top, their walks in
// step with their frames, the second a little after the first. The ms they're there.
inline uint32_t ptarmigan(const char *n, int top) {
  in_light(n);
  int l = DATE_R + 2, r = 206;
  for (int i = 0; i < 2; i++)
    place(n, i, (int) rnd((float) l, (float) (r - 7)),
          (int) rnd((float) (top - 2), (float) (top + 4)), std::string(i ? "glide opacity 0\n" : "") + R"(
            area l, top - 2, r, top + 10 bounce
            face 0 mirror level
            wait after
            frame 0
            wander 2.8, 2.8, 0, 180, walk, walk, peck, peck
          )" + (i ? "glide opacity 100 over 1500\n" : "") + "wait 90000 - after\nend",
          {{"l", l}, {"r", r}, {"top", top}, {"after", 600 * i}, {"walk", ms_of(n, 0, 8)}, {"peck", ms_of(n, 8)}},
          {.fade = 1500});
  return 90000;
}

// Three reindeer grazing their way about, now and then away while a hiker walks by, a pair of ptarmigan in front
// of them, white when there's snow, and a golden eagle circling a while in the half of the sky away from the sun
inline void reindeer(const Scene &s) {
  Herd h = herd(s);
  graze(h);
  int y = s.cabin_y + 2, top = std::min(s.cabin_y + 20, H - 12);
  bool white = s.at.white;  // not s, which a [=] would keep whole
  visitors(minutes(8), minutes(1, 6), 2,
           [=](int k) { return k ? ptarmigan(white ? "ptarmiganw" : "ptarmigan", top) : hike(y); });
  sometimes(minutes(10), minutes(1, 8), [] { return soar("eagle", 42, 40, 10); });
}

// Someone on a kick-sled along the meadow in front of the cabin at row y from the left, down to the bottom of the
// picture before the spruces and on under the temperatures, or the other way. The ms it takes.
inline uint32_t kick_sled(int y) {
  in_light("kicksled");
  bool left = chance(0.5f);
  int bottom = TEMPS_BOT + 3, along = (150 + 14) * 1000 / 14, down = 3600, on = (W + 2 - 200) * 1000 / 14;
  if (left)
    place("kicksled", 0, W + 2, bottom, path({{200, bottom, on}, {150, y, down}, {-14, y, along}}) + "end", {},
          {.left = true});
  else
    place("kicksled", 0, -14, y, path({{150, y, along}, {200, bottom, down}, {W + 2, bottom, on}}) + "end");
  return (uint32_t) (along + down + on);
}

// Two children on a sledge down the near hill from its top left of the cabin to the meadow in front of it, quicker
// as they go, then they sit a moment and are gone. The ms it takes.
inline uint32_t sledge(const NearHill &h) {
  in_light("sledge");
  int x = GAP_L, y = h.at(x + 5) - 8;
  place("sledge", 0, x, y,
        path({{x + 41, y + 21, 2600, "in"}, {x + 61, y + 30, 900}, {x + 73, y + 33, 1800, "out"}}) +
            "wait 2700\nend fade 1500");
  return 9500;
}

// A mountain hare hops out of the forest at its edge e, sits up a while, ears up, and hops back in. The ms it takes.
inline uint32_t hare_out(const NearHill &h, Edge e) {
  in_light("harew");
  int x0 = out_of(e, "harew", h.at(e.tx) - 4), x1 = e.x + 19;
  Walk w = walk(h, x0, x1, 7, 5, 4, 0, 16);
  place("harew", 0, x0, w.y[0], R"(
    clip hop
    b = n; d = 1; t = leg
  )" + w.loop("go") + R"(
    if k == 0 goto gone
    clip sit
    move x1, sy
    wait rounds 2
    flip x
    clip hop
    move x1, v
    b = 0; d = -1
    goto go
  gone:
    end
  )",
        {{"n", w.n}, {"leg", w.leg(7)}, {"x1", x1}, {"sy", h.at(x1 + 3) - 7}}, {.depth = D_NEAR_ON});
  return (uint32_t) (2 * w.n * w.leg(7) + 2 * ms_of("harew", "sit"));
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
    Walk w = walk(h, from_right ? W + 2 : -16, from_right ? -16 : W + 2, 8, 7, 6, 2);
    place("skier", 0, w.x0, w.y[0], "b = n; d = 1\n" + w.loop("go") + "end", {{"n", w.n}}, {.left = from_right});
    return (uint32_t) w.all();
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
  if (s.cabin)  // the eaves' ends
    for (int side = -1; side <= 1; side += 2)
      end_at(s.cabin_x + (side < 0 ? EAVES_L : EAVES_R), s.cabin_y - CABIN_EAVES, side);
  for (int i : near_trees(s, 0)) {
    Spruce spruce(s.tree[i].base - s.tree[i].top);
    for (int d = 13; d < spruce.crown; d++)  // the snowy tiers that are wide enough
      if (spruce.snow(d))
        for (int side = -1; side <= 1; side += 2)
          end_at(s.tree[i].x + side * spruce.hw(d), s.tree[i].top + d, side);
  }
  if (g) {
    std::copy(gap, gap + g, ends);
    n = g;
  }
  if (n)
    sometimes(minutes(2), (uint32_t) rnd(10000, 60000), [=] {
      End e = ends[(int) rnd(0, (float) n)];
      place("snowdrop", 0, e.x, e.y, "frame 0; wait 30000; end fade 4000", {}, {.left = e.left});
      return 30000u;
    });
  if (!s.cabin)
    return;
  Edge edge = forest_edge(s);
  int kick = s.cabin_y + 4;
  bool hills = s.ground == G_HILLS;
  visitors(minutes(5), minutes(1, 5), 3, [=](int k) -> uint32_t {
    if (k == 1)
      return hills ? sledge(h) : 0;
    if (k == 2)
      return edge.x >= 0 ? hare_out(h, edge) : 0;
    return kick_sled(kick);
  });
}

// Two swallows swoop low over the meadow from row top, quick and in curves, a while. The ms they're about.
inline uint32_t swallows(int top) {
  in_light("swallow");
  for (int i = 0; i < 2; i++)
    place("swallow", i, i ? W + 2 : -15, top + 4 + 4 * i, std::string(R"(
      area -16, top + 4, 336, top + 15 bounce
      face 0 mirror level
      wander 50, 70, heading, 30, 1500, 3000, 0, 0
      wave y -6, 6, 800, 900 jitter 30 random slow both
    )") + (i ? "frame 7\n" : "") + "wait 40000\nend",
          {{"top", top}, {"heading", i ? 180 : 0}});
  return 40000;
}

// Someone mows the lawn in front of the cabin from row y, in strips to and fro across the gap between the clock and
// the temperatures, each a little nearer. The ms it takes.
inline uint32_t mow(int y) {
  in_light("mower");
  int l = GAP_L, r = GAP_R - 23, strips = 4, step = std::clamp((H - 14 - y) / strips, 0, 3);
  int leg = (r - l) * 1000 / 6;
  std::string t = "frame 0\n";
  for (int k = 0; k < strips; k++) {
    bool back = k % 2;
    t += std::string("flip ") + (back ? "x" : "none") + "\n" + path({{back ? l : r, y + (k + 1) * step, leg}});
  }
  place("mower", 0, l, y, t + "end", {}, {.fade = 1000});
  return (uint32_t) (strips * leg);
}

// Three sheep grazing on the middle hill's face for the while the act's on, shifting a little now and then, but
// for the town. With lambs there are three more beside them at half the size.
inline void sheep(const Scene &s, bool lambs) {
  int x = GAP_L + 5, mid = s.land->hill[1][x + 14], near = s.land->hill[2][x + 14];
  if (s.ground == G_CITY || near - mid < 12)
    return;
  int y = (mid + near) / 2 - 3;
  in_light("sheep");
  for (int i = 0; i <= lambs; i++)
    place("sheep", i, x + 5 * i, y + 3 * i,
          std::string(i ? "pivot 50, 100; glide size 50; flip x; " : "") +
              "area x - 4, y, x + 33, y + 6 bounce; wander 0.3, 0.3, 0, 180, 3000, 3000, 6000, 12000",
          {{"x", x + 5 * i}, {"y", y + 3 * i}}, {.fade = 2000, .depth = D_MID_ON});
}

// Butterflies over the meadow in front of home, under the near hill's top and between the date and the
// temperatures, a little after one another: n of them, the first tortoises small tortoiseshells and the rest
// brimstones. They flutter for as long as their frames do and sit for as long as they sit. The meadow's top row.
inline int flutter(const Scene &s, int tortoises, int n) {
  int l = DATE_R + 2, r = GAP_R - 2;
  int top = std::max(s.cabin_y + 1, hill_low(s, l, r) + 3), bot = std::min(H - 2, top + 28);
  for (int k = 0; k < n; k++) {
    const char *name = k < tortoises ? "butterfly" : "brimstone";
    place(name, k < tortoises ? k : k - tortoises, l + (r - l) * (2 * k + 1) / (2 * n), (int) rnd(top, bot - 6),
          std::string(k ? "glide opacity 0\n" : "") + R"(
      area l, top, r, bot bounce
      wait after
      frame 0
      wander 4, 8, 0, 180, fly, fly, sit, sit
    )" + (k ? "glide opacity 100 over 300" : ""),
          {{"l", l},
           {"top", top},
           {"r", r},
           {"bot", bot},
           {"after", k * 2300},
           {"fly", ms_of(name, 0, 24)},
           {"sit", ms_of(name, 24)}});  // its 24 fluttering frames, then sitting
  }
  return top;
}

// Two small tortoiseshells and a brimstone over the meadow, sheep on the hillside, and now and then swallows
// swooping by or the mower out on the lawn
inline void butterflies(const Scene &s) {
  int top = flutter(s, 2, 3), y = s.cabin_y + 1, low = std::min(top + 2, H - 28);
  sheep(s, false);
  visitors(minutes(6), minutes(1, 5), 2, [=](int k) { return k ? mow(y) : swallows(low); });
}

// A V of geese far up, small, slow and hazy, across to the right, or to the left. The ms it takes.
inline uint32_t geese_by(bool left) {
  int x0 = left ? W + 10 : -30, x1 = left ? -30 : W + 10;
  std::initializer_list<Leg> legs = {{x0 + (x1 - x0) / 4, 28, 35000},
                                     {x0 + (x1 - x0) / 2, 26, 35000},
                                     {x0 + (x1 - x0) * 3 / 4, 24, 35000},
                                     {x1, 22, 35000}};
  place("geese", 0, x0, 30, "glide opacity 70\nwave y -2, 2, 6000, 6000 jitter 20 slow both\n" + path(legs) + "end", {},
        {.left = left});
  return (uint32_t) path_ms(legs);
}

// Spring: the ewes have their lambs with them on the hillside and a brimstone is out over the meadow. Now and then
// a wagtail runs about the lawn wagging its tail, or the geese come back north
inline void spring(const Scene &s) {
  flutter(s, 0, 1);
  sheep(s, true);
  int lawn = meadow(s);
  visitors(minutes(5), minutes(0.5f, 3), 2, [lawn](int k) { return k ? geese_by(true) : lawn_bird("wagtail", lawn); });
}

// A small bird, n, comes to the julenek at nek after a while: it flies in from off one side to the sheaf, dx from
// its middle and dy rows down, pecks at the oats, flits across to the other side of it, pecks on and flies off the
// way it came. The ms it takes.
inline uint32_t to_the_nek(const char *n, Pt nek, int dx, int dy, bool left, int after) {
  in_light(n);
  int y = nek.y + dy - 5, x0 = left ? W + 4 : -10, in = 4500, hop = 400, off = 4000;
  place(n, 0, x0, y - 34, "wait after\nclip fly\n" + path({{nek.x + dx - 3, y, in, "out"}}) + R"(
    clip sit
    wait rounds 2
    clip fly
    move over, y over hop
    wait hop
    clip sit
    wait rounds 2
    flip x
    clip fly
    move x0, y - 44 over off ease in
    wait off
    end
  )",
        {{"after", after}, {"over", nek.x - dx - 3}, {"y", y}, {"hop", hop}, {"x0", x0}, {"off", off}}, {.left = left});
  return (uint32_t) (after + in + hop + off + 4 * ms_of(n, "sit"));
}

// Through Christmas the small birds come to the julenek: a bullfinch cock and his hen and a great tit fly in one
// after the other, peck at the oats and fly off again
inline void julenek(const Scene &s) {
  Pt nek = s.nek;
  visitors(minutes(3), minutes(0.1f, 0.5f), 1, [nek](int) {
    bool left = chance(0.5f);
    uint32_t ms = to_the_nek("bullfinch", nek, -4, 2, left, 0);
    ms = std::max(ms, to_the_nek("finchhen", nek, 4, 2, !left, 2500));
    return std::max(ms, to_the_nek("tit", nek, -3, 7, left, 6000));
  });
}

// A leaf lies on the grass and a gust now and then tumbles it along, low, in rows top to bot, its tumbles in step
// with its 8 tumbling frames
inline const char *const LEAF = R"(
  area -6, top, 326, bot cross bounce
  wander 2, 4, heading, 25, roll, roll, lie, lie
)";

// Someone out picking mushrooms along the near hill's top from x0 behind the spruce at the forest's edge toward the
// cabin: two strides and a chanterelle picked, twice, then back into the trees. The ms it takes.
inline uint32_t mushrooms(const NearHill &h, int x0) {
  in_light("mushroomer");
  std::string t = "frame 0\n";
  for (int k = 0; k < 4; k++) {
    bool back = k >= 2;
    int to = back ? x0 + 13 * (3 - k) : x0 + 13 * (k + 1);
    t += std::string(k == 2 ? "flip x\n" : "") + "move " + num(to) + ", " + num(h.at(to + 6) - 8) + " over walk\n" +
         (k < 3 ? "wait rounds 1\n" : "wait walk\nend");
  }
  place("mushroomer", 0, x0, h.at(x0 + 6) - 8, t, {{"walk", ms_of("mushroomer", 0, 16)}}, {.depth = D_NEAR_ON});
  return (uint32_t) (3 * ms_of("mushroomer") + ms_of("mushroomer", 0, 16));
}

// A red squirrel bounds along the meadow at row y from off one side, sits up a while in the gap nibbling a nut, and
// bounds on or back the way it came. The ms it takes.
inline uint32_t squirrel_by(int y) {
  in_light("squirrel");
  return stop_by("squirrel", "run", "nut", (DATE_R + GAP_R) / 2 + (int) rnd(-15, 15), y, 3, 14);
}

// Birch leaves lie on the grass and now and then a gust tumbles them along, low, a few px/s. About every minute
// one lets go of a front birch and tumbles down to them, and a gust blows the last one that fell off downwind. Now
// and then a V of geese far up, small, slow and hazy, and in the meadow someone picking mushrooms or a squirrel.
inline void leaves(const Scene &s) {
  int top = hill_low(s, 0, W) + 2;  // under the near hill's top all the way across
  int bot = std::min(H - 2, top + 26), heading = s.at.wdir < 0 ? 180 : 0, wdir = s.at.wdir;
  overlay::Consts leaf = {{"top", top},
                          {"bot", bot},
                          {"heading", heading},
                          {"roll", ms_of("leaf", 0, 8)},
                          {"lie", ms_of("leaf", 8)}};  // its 8 tumbling frames, then lying
  for (int i = 0; i < 3; i++) {                        // a little after one another
    overlay::Consts k = leaf;
    k["after"] = i * 2700;
    place("leaf", i, 50 + i * 100 + (int) rnd(-12, 12), (int) rnd(top, bot - 6),
          std::string(i ? "glide opacity 0\n" : "") + "wait after\nframe 0\n" + LEAF +
              (i ? "glide opacity 100 over 300" : ""),
          k);
  }
  struct Birch {
    int16_t x, top, h, rx;
  } birches[6];
  int n = 0;
  for (int i : near_trees(s, 1))
    if (n < 6) {
      Crown c = crown_of(s, i);
      birches[n++] = {s.tree[i].x, s.tree[i].top, (int16_t) c.rows, (int16_t) c.rx};
    }
  // Copies 3 and 4 take turns: one falls while the gust takes the last one off, so there's room for the flag and
  // a visitor
  if (n)
    sometimes(minutes(1), (uint32_t) rnd(5000, 30000), [=, falls = 3]() mutable {
      static constexpr int FALL = 9000, AWAY = 6000;
      Birch c = birches[(int) rnd(0, (float) n)];
      int x0 = c.x - 3 + (int) rnd(1 - c.rx, (float) c.rx), y0 = c.top + (int) rnd(1, c.h * 0.6f);
      int x1 = std::clamp(x0 + wdir * (int) rnd(18, 30), -2, W - 4), y1 = (int) rnd(top, bot - 6);
      script("leaf", falls, "move off, top over away ease in; wait away; end fade 0",
             {{"off", wdir < 0 ? -10 : W + 4}, {"top", top}, {"away", AWAY}});
      falls = 7 - falls;
      overlay::Consts k = leaf;
      k.insert({{"x1", x1}, {"y1", y1}, {"fall", FALL}});
      place("leaf", falls, x0, y0, std::string(R"(
        clip 0 to 7
        rate 125
        move x1, y1 over fall
        wait fall
        rate 100
        clip 0 to 8
      )") + LEAF,
            k);
      return (uint32_t) FALL;
    });
  sometimes(minutes(15), minutes(1, 10), [] { return geese_by(false); });
  NearHill h(s);
  Edge edge = forest_edge(s);
  const Frames *f = frames_of("mushroomer");
  int x0 = edge.x >= 0 && f ? edge.hide(f->w, f->h, h.at(edge.tx) + f->h - 9) : -1, y = std::min(top + 1, H - 6);
  if (!s.cabin || x0 + 26 + (f ? f->w : 0) >= s.cabin_x + EAVES_L)  // its strides clear of the cabin
    x0 = -1;
  visitors(minutes(6), minutes(1, 5), 2,
           [=](int k) -> uint32_t { return k ? (x0 >= 0 ? mushrooms(h, x0) : 0) : squirrel_by(y); });
}

}  // namespace flexit::panel::met::live
