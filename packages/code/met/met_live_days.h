// Animations for the days: Christmas Eve, New Year's Eve, Easter, 17 May, Sankthans, Halloween and Valentine's
// Day
#pragma once
#include "met_live.h"
#include "met_live_rare.h"

namespace flexit::panel::met::live {

// ---- Christmas Eve

// The sleigh across the sky now and then, a few times an evening, its colours as dim as the night's
inline void sleigh(int y) {
  sometimes(minutes(30), minutes(3, 25), [y] {
    return across("sleigh", -36, y, 330, y - 18, 45000, "wave y -3, 3, 4000, 4000 jitter 20 slow both");
  });
}

// The lights on the spruce glint one by one over the picture's and the star on top glitters
inline void spruce_lights(const Scene &s) {
  int x0, y0;
  add("treelights", lights(s.light, s.nlights, &x0, &y0));
  place("treelights", 0, x0, y0);
  place("treestar", 0, s.star.x - 1, s.star.y - 1);  // its middle on the picture's star's
}

// And on Christmas Eve a candle of the arch in the window flickers and the sleigh comes over
inline void xmas(const Scene &s) {
  if (s.cabin)
    place("candles", 0, stake_x(s), stake_y(s));  // over the picture's
  sleigh(s.overlay ? 84 : 60);
}

// Five single lights on the park's spruce glow in turn, at five of the scene's, under a small star
inline void xmas_town(const Scene &s) {
  static const uint32_t COL[] = {0xFF5555, 0xFFDD66, 0x55DD77, 0xFF5555, 0xFFDD66};
  int x[5], y[5], x0 = W, y0 = H, x1 = 0, y1 = 0;
  for (int i = 0; i < 5; i++) {
    int k = 1 + i * 3 * s.nlights / 15;
    x[i] = s.light[k].x, y[i] = s.light[k].y;
    x0 = std::min(x0, x[i]), y0 = std::min(y0, y[i]), x1 = std::max(x1, x[i]), y1 = std::max(y1, y[i]);
  }
  add("parklights", {x1 - x0 + 1, y1 - y0 + 1, {700, 500, 800, 600, 700, 500}, [=](int f, const Canvas &c) {
                       for (int i = 0; i < 5; i++)
                         c.put(x[i] - x0, y[i] - y0, (i + f) % 3 ? COL[i] : 0xFFFFFF);
                     }});
  place("parklights", 0, x0, y0);
  place("citystar", 0, s.star.x + 1, s.star.y + 1);
  sleigh(s.overlay ? 74 : 50);
}

// ---- New Year's Eve

// Minutes to or from midnight on the clock
inline int from_midnight() {
  ESPTime t = ESPTime::from_epoch_local((time_t) id(flexit_panel_time).now().timestamp);
  int m = t.hour * 60 + t.minute;
  return m < 720 ? m : 1440 - m;
}

// The bursts' colours
inline constexpr uint32_t FW[] = {0xFF77BB, 0x66DDFF, 0xFFDD66};

// A rocket goes up from the near hill with a spitting trail and bursts in one of FW as half a firework and its
// mirror, a round peony or a crackling ring: two copies, the mirror hidden till the burst. The ms it all takes.
inline uint32_t rocket(int lane, const NearHill &hill, int top) {
  const char *n = "firework";
  int x = (int) rnd(50, 270), y = top + (int) rnd(0, 50), fly = (int) rnd(1100, 1600);
  const char *burst = chance(0.5f) ? "ring" : "peony";
  overlay::Consts k = {{"x", x}, {"y", y}, {"fly", fly}, {"lasts", ms_of(n, burst)}, {"colour", FW[(int) rnd(0, 3)]}};
  place(n, lane * 2, x - 1, hill.at(x) - 9, std::string(R"(
    blend add
    clip rise
    move x - 1, y over fly ease out
    wait fly
    move x - 25, y - 24
    tint colour
    clip )") + burst + R"(
    wait lasts - 300
    end fade 300
  )",
        k, {.fade = 60});
  place(n, lane * 2 + 1, x, y - 24, std::string(R"(
    blend add
    flip x
    tint colour
    glide opacity 0
    wait fly
    clip )") + burst + R"(
    glide opacity 100 over 60
    wait lasts - 300
    end fade 300
  )",
        k, {.fade = 60});
  return (uint32_t) (fly + ms_of(n, burst));
}

// The lane's next rocket once its last has burst, sooner the nearer it gets to midnight
inline void rockets(int lane, NearHill hill, int top) {
  later((uint32_t) (2500 * (8 + from_midnight()) / 8 * rnd(0.5f, 1.5f)),
        [=] { later(rocket(lane, hill, top), [=] { rockets(lane, hill, top); }); });
}

inline void new_year(const Scene &s) {
  NearHill hill(s);
  int top = s.overlay ? 80 : 42;  // under the forecast
  for (int lane = 0; lane < 2; lane++)
    later(5000 + lane * 1500, [=] { rockets(lane, hill, top); });
}

// ---- Easter

// Chicks peck and potter about in front of the eggs, and now and then a hare three chicks long hops along the near
// hill a leg at a time, stopping to sit up and look around just left of the cabin, between the clock and it
inline void easter(const Scene &s) {
  for (int i = 0; i < 3; i++) {  // around eggs 0, 2 and 4, below every egg there
    int e = std::min(i * 2, s.neggs - 1), l = std::max<int>(DATE_R, s.egg[e].x - 14),
        r = std::min<int>(GAP_R, s.egg[e].x + 14), top = 0;
    for (int k = 0; k < s.neggs; k++)
      if (s.egg[k].x + 5 > l && s.egg[k].x - 5 < r)
        top = std::max(top, s.egg[k].y + 6);
    top = std::min(top, H - 10);
    place("chick", i, (l + r) / 2 - 3, top + 1, R"(
      area l, top, r, bottom bounce
      wander 2, 5, 0, 180, 1000, 3000, 1500, 4000
      face 0 mirror level
      frame i
    )",
          {{"l", l}, {"top", top}, {"r", r}, {"bottom", std::min(top + 14, H)}, {"i", i}});
  }
  int sit = s.cabin ? GAP_L + 2 : s.home_x + 44;  // left of the cabin, or past the park's pole in town
  sometimes(minutes(8), minutes(1, 6), [hill = NearHill(s), sit] {
    int stop = sit + (int) rnd(0, 12);
    Walk in = walk(hill, -22, stop, 14, 10, 8, 0, 32), on = walk(hill, stop, W + 10, 18, 10, 8, 0, 32);  // 32 px hops
    place("hare", 0, -22, in.y[0], R"(
      clip hop
      b = n_in; d = 1; t = t_in
    )" + in.loop("in") + R"(
      clip sit
      wait rounds 1
      clip hop
      k = 0; b = n_on; t = t_on
    )" + on.loop("on") + "end",
          {{"n_in", in.n}, {"t_in", in.leg(14)}, {"n_on", on.n}, {"t_on", on.leg(18)}});
    return (uint32_t) (in.n * in.leg(14) + ms_of("hare", "sit") + on.n * on.leg(18));
  });
}

// ---- 17 May

// Now and then a little children's parade walks across the meadow, and sometimes a balloon gets away with the
// wind
inline void may17(const Scene &s) {
  int y = std::min(hill_low(s, 0, W) + 6, H - 14), wdir = s.at.wdir;  // below all of the near hill's top
  sometimes(minutes(60), minutes(5, 40), [y] { return across("parade", -42, y - 4, 330, y, 60000); });
  sometimes(minutes(15), minutes(3, 15), [hill = NearHill(s), wdir] {
    int x = wdir < 0 ? (int) rnd(90, 300) : (int) rnd(20, 230);
    place("balloon", 0, x, hill.at(x) + 8, R"(
      wave x -3, 3, 2600, 2600 hold 300, 300 jitter 30 slow both
      move away, -16 over 60000
      wait 60000
      end
    )",
          {{"away", x + wdir * 70}}, {.fade = 600});
    return 60000u;
  });
}

// ---- Sankthans

// The bonfire on its pyre of logs burns and flickers, sparks rise from it and go out, and its smoke drifts off
// downwind
inline void sankthans(const Scene &s) {
  int x = s.fire.x, y = s.fire.y;
  place("pyre", 0, x - 12, y - 27);  // its logs on the picture's
  for (int i = 0; i < 2; i++)        // the second hidden till 1.7 s on
    place("spark", i, x - 1, y - 27, std::string(i ? "glide opacity 0\n" : "") + R"(
      area x - 24, y - 77, x + 36, y - 23 screen
      map opacity 100, 100, 0, 100
      wait after
      frame i
      wave y -46, 0, 0, 3400 slow low
      wave x -6, 6, 1500, 1500 jitter 30 random slow both
    )" + (i ? "glide opacity 100 over 300" : ""),
          {{"x", x}, {"y", y}, {"i", i}, {"after", 1700 * i}});
  if (const Frames *puff = frames_of("puffdusk"))  // the bonfire's own, the chimney may smoke too
    add("pyresmoke", Frames(*puff));
  chimney("pyresmoke", 4, x, y - 36, 50, 12, s.at.wdir);
}

// ---- Halloween

// The pumpkin's candle flickers, a witch on her broomstick flies across the moon now and then, or across the sky
// without it, and an owl glides in, perches on the cabin's ridge, a flat roof in town or a spruce top, turns its
// head and hoots, and flies off
inline void halloween(const Scene &s) {
  place("pumpkin", 0, s.pumpkin.x - 5, s.pumpkin.y - 3);
  sometimes(minutes(10), minutes(4, 30), [] {  // where the moon is by then
    Ride r = witch_ride(SEEN.moon_x, SEEN.moon_y, SEEN.top);
    place("witch", 0, r.x[0], r.y[0], r.text(), {}, {.left = r.left()});
    return (uint32_t) r.all();
  });
  // It perches where the birds come down, see perches(), smaller on its way to one far off
  std::vector<Perch> tops = perches(s, true);
  if (tops.empty())
    return;
  sometimes(minutes(12), minutes(2, 10), [tops] {
    Perch p = tops[(size_t) rnd(0, (float) tops.size())];
    int x = p.x - 4, y = p.y - 9;                                        // its feet on top
    int from = x < W / 2 ? W + 10 : -25, to = x < W / 2 ? -25 : W + 10;  // in from the far side
    int in = (int) (hypotf((float) (x - from), (float) (y - 70)) * 1000 / 7.5f);
    int off = (int) (hypotf((float) (to - x), 54) * 1000 / 9);
    bool far = p.size < 100;
    place("owl", 0, from, 70, std::string(far ? "pivot 50, 100\nglide size far over in\n" : "") + R"(
      clip fly
      move sx - 3, sy + 1 over in
      wait in
      depth deep
      clip perch
      move sx, sy
      wait rounds 2
      clip fly
      depth out
      glide size 100 over off
      move sx - 3, sy + 1
      move away, sy - 54 over off ease in
      wait off
      end
    )",
          {{"sx", x},
           {"sy", y},
           {"in", in},
           {"away", to},
           {"off", off},
           {"far", p.size},
           {"deep", p.depth},
           {"out", D_AIR}});
    return (uint32_t) (in + 2 * ms_of("owl", "perch") + off);
  });
}

// ---- Valentine's Day

// Instead of smoke, little hearts rise from the chimney, beating as they drift off downwind and fade
inline void valentine(const Scene &s) { chimney("heart", 3, s.chimney.x, s.chimney.y, 56, 22, s.at.wdir); }

}  // namespace flexit::panel::met::live
