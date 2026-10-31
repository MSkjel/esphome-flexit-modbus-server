// Animations for the days: Christmas Eve, New Year's Eve, Easter, 17 May, Sankthans, Halloween and Valentine's
// Day
#pragma once
#include "met_live.h"
#include "met_live_rare.h"

namespace flexit::panel::met::live {

// ---- Christmas Eve

// The sleigh across the sky now and then, a few times an evening, its colours as dim as the night's
inline void sleigh(int y) {
  sometimes(minutes(60), minutes(3, 25), [y] {
    uint32_t ms = across("sleigh", -36, y, 330, y - 18, 45000);
    wave("sleigh", 0, "y", -3, 3, 4000, 4000, 0, 0, 20, false, "both");
    return ms;
  });
}

// The lights on the spruce glint one by one over the picture's, the star on top glitters, and a candle arch
// stands in the cabin window
inline void xmas(const Scene &s) {
  int x0, y0;
  add("treelights", lights(s.light, s.nlights, &x0, &y0));
  place("treelights", 0, x0, y0);
  place("treestar", 0, s.star.x - 1, s.star.y - 1);  // its middle on the picture's star's
  if (s.cabin)
    place("candles", 0, window_x(s), window_y(s) + 1);  // on the window's sill
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

inline const char *const FW[] = {"fwpink", "fwcyan", "fwgold"};
// Each lane's colour of FW. Three and their bursts' frames of their own are over the 6 KB, so the panel has two
// and now and then a lane swaps its for the third.
inline uint8_t fw_lane[2];

// A rocket goes up from the near hill with a spitting trail and bursts in its lane's colour as half a firework and
// its mirror, a round peony or a crackling ring. Both halves get a frame of their own from the burst's first. The
// ms it all takes.
inline int rocket(int lane, const NearHill &hill, int top) {
  std::string col = FW[fw_lane[lane]];
  int x = (int) rnd(50, 270), y = top + (int) rnd(0, 50), fly = (int) rnd(1100, 1600);
  bool ring = chance(0.5f);
  int burst = ms_of(col, ring ? 8 : 0, ring ? 16 : 8);  // firework()'s frames, the peony's or the ring's
  place("rocket", lane, x - 1, hill.at(x) - 9);
  move("rocket", lane, x - 1, y, fly, "out");
  later(fly, [=] {
    remove("rocket", lane, 1);
    for (int h = 0; h < 2; h++)
      place_flipped(col, lane * 2 + h, x - 25 + h * 25, y - 24, h, {true, ring ? 8 : 0, 60});
    later(burst - 300, [=] {  // fading out over its last frame
      remove(col, lane * 2);
      remove(col, lane * 2 + 1);
    });
  });
  return fly + burst;
}

// The lane's next rocket once its last has burst, sooner the nearer it gets to midnight
inline void rockets(int lane, NearHill hill, int top) {
  later((uint32_t) (2500 * (8 + from_midnight()) / 8 * rnd(0.5f, 1.5f)), [=] {
    later((uint32_t) rocket(lane, hill, top), [=] {
      if (chance(0.3f)) {  // the colour neither lane has
        clear(FW[fw_lane[lane]]);
        fw_lane[lane] = (uint8_t) (3 - fw_lane[0] - fw_lane[1]);
        preload(FW[fw_lane[lane]]);
      }
      rockets(lane, hill, top);
    });
  });
}

inline void new_year(const Scene &s) {
  fw_lane[0] = (uint8_t) rnd(0, 3);
  fw_lane[1] = (uint8_t) ((fw_lane[0] + 1) % 3);
  for (const char *n : {"rocket", FW[fw_lane[0]], FW[fw_lane[1]]})
    preload(n);
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
    place("chick", i, (l + r) / 2 - 3, top + 1, {true, i});
    area("chick", i, l, top, r, std::min(top + 14, H), "bounce", "bounce");
    wander("chick", i, 2, 5, 0, 180, 1000, 3000, 1500, 4000);
    face("chick", i);
  }
  sometimes(minutes(8), minutes(1, 6), [hill = NearHill(s)] {
    int stop = (int) rnd(GAP_L + 2, GAP_L + 14), y = hill.at(stop + 10) - 8;
    place("hare", 0, -22, hill.at(-12) - 8);
    later(amble("hare", 0, hill, -22, stop, 14, 10, 8, 0, 32), [=] {  // hops 32 px long
      swap("hare", 0, "haresit", stop, y);
      later(4000, [=] {
        swap("haresit", 0, "hare", stop, y);
        remove_later(amble("hare", 0, hill, stop, W + 10, 18, 10, 8, 0, 32), "hare", 0);
      });
    });
    return 24000u;  // its hops and the sitting, about
  });
}

// ---- 17 May

// Now and then a little children's parade walks across the meadow, and sometimes a balloon gets away with the
// wind
inline void may17(const Scene &s) {
  int y = std::min(hill_low(s, 0, W) + 6, H - 14), wdir = s.at.wdir;  // below all of the near hill's top
  sometimes(minutes(120), minutes(5, 40), [y] { return across("parade", -36, y - 4, 330, y, 60000); });
  sometimes(minutes(15), minutes(3, 15), [hill = NearHill(s), wdir] {
    int x = wdir < 0 ? (int) rnd(90, 300) : (int) rnd(20, 230), ms = 60000;
    place("balloon", 0, x, hill.at(x) + 8, {false, 0, 600});
    move("balloon", 0, x + wdir * 70, -16, ms);
    wave("balloon", 0, "x", -3, 3, 2600, 2600, 300, 300, 30, false, "both");
    return remove_later(ms, "balloon", 0);
  });
}

// ---- Sankthans

// The bonfire on its pyre of logs burns and flickers, sparks rise from it and go out, and its smoke drifts off
// downwind
inline void sankthans(const Scene &s) {
  int x = s.fire.x, y = s.fire.y;
  place("pyre", 0, x - 12, y - 27);  // its logs on the picture's
  auto spark = [x, y](int i) {
    place("spark", i, x - 1, y - 27, {true, i});
    area("spark", i, x - 24, y - 77, x + 36, y - 23, "screen", "screen");
    map("spark", i, "opacity", 100, 100, 0, 100);
    wave("spark", i, "y", -46, 0, 0, 3400, 0, 0, 0, false, "low");
    wave("spark", i, "x", -6, 6, 1500, 1500, 0, 0, 30, true, "both");
  };
  spark(0);
  later(1700, [spark] { spark(1); });
  chimney("puffdusk", 4, x, y - 36, 50, 12, s.at.wdir);
}

// ---- Halloween

// The pumpkin's candle flickers, a witch on her broomstick flies across the moon now and then, or across the sky
// without it, and an owl glides in, perches on the cabin's ridge, a flat roof in town or a spruce top, turns its
// head and hoots, and flies off
inline void halloween(const Scene &s) {
  place("pumpkin", 0, s.pumpkin.x - 5, s.pumpkin.y - 3);
  sometimes(minutes(10), minutes(4, 30), [] {  // where the moon is by then
    Ride r = witch_ride(SEEN.moon_x, SEEN.moon_y, SEEN.top);
    place("witch", 0, r.x[0], r.y[0]);
    if (r.x[3] < r.x[0])
      flip("witch", 0, true, false);
    return remove_later(ride("witch", r), "witch", 0);
  });
  // Where it perches, x and the row under its feet: the ridge, the town's flat roofs and the near spruces in the gap
  // between the clock and the temperatures, or any near spruce if there's none of those
  std::vector<int16_t> tops;
  if (s.cabin)
    tops.insert(tops.end(), {(int16_t) (s.cabin_x - 1), (int16_t) (s.cabin_y - 40)});
  for (int i = 0; i < s.nbld; i++) {
    const Bld &b = s.bld[i];
    int l = std::max<int>(b.x, GAP_L), r = std::min<int>(b.x + b.w - 1, GAP_R);
    if (!b.back && b.kind == 0 && r - l >= 12)
      tops.insert(tops.end(), {(int16_t) ((l + r) / 2), (int16_t) (b.top - 1)});
  }
  for (int i : near_trees(s, 0))
    if (s.tree[i].x - 4 >= GAP_L && s.tree[i].x + 6 <= GAP_R)
      tops.insert(tops.end(), {s.tree[i].x, s.tree[i].top});
  if (tops.empty())
    for (int i : near_trees(s, 0))
      tops.insert(tops.end(), {s.tree[i].x, s.tree[i].top});
  if (tops.empty())
    return;
  sometimes(minutes(12), minutes(2, 10), [tops] {
    int k = (int) rnd(0, (float) (tops.size() / 2)) * 2, x = tops[k] - 4, y = tops[k + 1] - 9;  // its feet on top
    int from = x < W / 2 ? W + 10 : -25, to = x < W / 2 ? -25 : W + 10;                         // in from the far side
    int in = (int) (hypotf((float) (x - from), (float) (y - 70)) * 1000 / 7.5f);
    place("owl", 0, from, 70);
    move("owl", 0, x - 3, y + 1, in);
    later((uint32_t) in, [=] {
      swap("owl", 0, "owlperch", x, y, true);
      later(2 * ms_of("owlperch") - STEP, [=] {  // two rounds of its frames
        swap("owlperch", 0, "owl", x - 3, y + 1);
        int off = (int) (hypotf((float) (to - x), 54) * 1000 / 9);
        move("owl", 0, to, y - 54, off, "in");
        remove_later(off, "owl", 0);
      });
    });
    return 70000u;  // its flights and the perching, about
  });
}

// ---- Valentine's Day

// Instead of smoke, little hearts rise from the chimney, beating as they drift off downwind and fade
inline void valentine(const Scene &s) { chimney("heart", 3, s.chimney.x, s.chimney.y, 56, 22, s.at.wdir); }

}  // namespace flexit::panel::met::live
