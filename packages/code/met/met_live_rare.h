// Very seldom something strange: a trampoline, an umbrella, a shirt or a cow blowing by in a gale, a sea serpent off
// the coast at dusk, a Viking longship by day, a troll on the ridge with the sun down, a wolf howling at the full moon,
// a witch across the moon, a flying saucer on a clear night. Over any show, when the panel has room for one more, but
// not on the days with a show of their own.
#pragma once
#include "met_live.h"
#include "met_live_town.h"

namespace flexit::panel::met::live {

// About every RARE_EVERY a roll, each encounter that fits the picture with its odds
inline constexpr uint32_t RARE_EVERY = minutes(20);
// The longest its sprite may take to get to the panel, the link busy, before it's called off
inline constexpr uint32_t RARE_WAIT = 15000;

// What the encounters keep of the picture, from the last apply()
struct Seen {
  At at;
  int top;                      // the sky's top row below the forecast
  int16_t moon_x = -1, moon_y;  // -1 without the moon or with any of it hidden, see moon_clear()
  int16_t moon_up = -1;         // the moon's x when it's up at all
  float moon_phase;             // 0 new, 0.5 full
  bool town;
  int16_t roof_x = -1, roof_y;       // the middle of the cabin's ridge, -1 without the cabin
  int16_t sea_l, sea_r = -1, sea_y;  // open water along row sea_y and the 8 above it, sea_r < sea_l without
  NearHill hill;
  NearHill ridge;                 // the skyline, the land's top against the sky
  int16_t ridge_l, ridge_r = -1;  // its longest stretch to walk along, see rare_seen()
  int16_t den_x = -1, den_y;      // where a wolf can sit on it nearest under the moon, -1 for nowhere
};
inline Seen SEEN;

inline void rare_seen(const Scene &s) {
  Seen &e = SEEN;
  e.at = s.at;
  e.top = s.overlay ? OV_H + 4 : 8;
  e.moon_x = (int16_t) (moon_clear(s) ? s.moon_x : -1);
  e.moon_up = (int16_t) (s.moon ? s.moon_x : -1);
  e.moon_y = s.moon_y;
  e.moon_phase = s.moon_phase;
  e.town = s.ground == G_CITY;
  e.roof_x = (int16_t) (s.cabin ? s.cabin_x : -1);
  e.roof_y = (int16_t) (s.cabin_y - 25);
  e.hill = NearHill(s);
  // The land's longest stretch under the forecast that's no steeper than 45 degrees and has nothing above it but the
  // sky and the far mountains: no sea behind, nothing standing up out of it that should be in front of what walks
  // along it
  uint8_t top[W], sky[W];
  skyline(s, sky, false);
  Cols walk;
  for (int x = 0; x < W; x++) {
    top[x] = std::min({s.land->hill[0][x], s.land->hill[1][x], s.land->hill[2][x]});
    walk[x] =
        top[x] != NONE && top[x] - 24 > e.top && (x == 0 || abs(top[x] - top[x - 1]) <= 1) && sky[x] + 1 >= top[x];
  }
  for (int i = 0; i <= W / 4; i++)
    e.ridge.y[i] = top[std::min(W - 1, i * 4)];
  int l = 0, r = -1;
  e.ridge_r = -1;
  if (stretch(walk, -1, l, r) && r - l >= 60) {
    e.ridge_l = (int16_t) l;
    e.ridge_r = (int16_t) r;
  }
  // 16 px level to 4, the wolf's feet on its lowest so it doesn't float
  e.den_x = -1;
  for (int d = 0; e.moon_x >= 0 && d < W && e.den_x < 0; d++)
    for (int x : {e.moon_x - 8 - d, e.moon_x - 8 + d})
      if (x >= 2 && x <= W - 18 && stretch(walk, x + 8, l, r) && l <= x - 2 && r >= x + 17) {
        int low = *std::max_element(top + x, top + x + 16);
        if (low - *std::min_element(top + x, top + x + 16) > 4)
          continue;
        e.den_x = (int16_t) x;
        e.den_y = (int16_t) (low - 12);
        break;
      }
  e.sea_r = -1;
  if (s.ground != G_COAST || !s.sea)
    return;
  // Its waterline as near as there's 100 px of open water from there up to its head, or up to the horizon, which it
  // may stand out against
  int shore = H, sea = s.sea_top;
  for (int x = 0; x < W; x++)
    if (s.land->hill[1][x] >= sea + 20)
      shore = std::min<int>(shore, s.land->hill[1][x]);
  std::vector<Cols> open;
  for (int y = sea; y <= shore - 3; y++)
    open.push_back(water(s, y, y, false));
  for (int y = shore - 3; y >= sea + 4; y--) {
    Cols c;
    c.set();
    for (int k = std::max(sea, y - 8); k <= y; k++)
      c &= open[k - sea];
    if (stretch(c, -1, l, r) && r - l >= 100) {
      e.sea_l = (int16_t) l;
      e.sea_r = (int16_t) r;
      e.sea_y = (int16_t) y;
      return;
    }
  }
}

// The encounter on goes, at once
inline void rare_off() {
  if (!R.rare.empty())
    unload(R.rare);
  R.rare.clear();
}

// Whether the panel has room for one more animation for ms: a slot and a place, RARE_BYTES of the pool, nothing
// being made or sent, and nothing of the show's due before it's over
inline bool rare_room(uint32_t ms) {
  bool slot = false;
  int places = 0;
  for (auto &a : anim::anims) {
    if (a.name.empty()) {
      slot = true;
      continue;
    }
    if (a.state != anim::SENT)
      return false;
    for (auto &c : a.copies)
      places += c.used;
  }
  if (!slot || places >= ANIM_PLACES || anim::W.phase != anim::IDLE ||
      anim::used_by_others(nullptr) + RARE_BYTES > ANIM_POOL)
    return false;
  uint32_t end = millis() + ms;
  for (const Timer &t : R.timers)
    if (!t.rare && (int32_t) (t.at - end) < 0)
      return false;
  return true;
}

// fn once n is on the panel, its copy placed and its frames started, so what it does starts from there. It's
// called off if that takes RARE_WAIT.
inline void when_on(const std::string &n, std::function<void()> fn, uint32_t waited = 0) {
  if (R.rare != n)
    return;  // gone meanwhile
  const anim::Anim *a = anim::find(key(n));
  if (a && a->state == anim::SENT) {
    fn();
  } else if (!a || a->state == anim::FAILED || waited >= RARE_WAIT) {
    rare_off();
  } else {
    later(STEP, [=] { when_on(n, fn, waited + STEP); });
  }
}

// The encounter's sprite n, in the light there is, placed where it starts
inline void rare_on(const char *n, float light, int x, int y, How o = {}) {
  Frames sp;
  make_sprite(n, sp);
  add(n, dimmed(std::move(sp), light));
  place(n, 0, x, y, o);
  R.rare = n;
}

// n blows across the sky with the wind, rising or dropping a little in each of three gusts, turning at turn degrees a
// second. It crosses in one glide so it never stops, whenever the gusts get to the panel.
inline void blown(const char *n, float turn) {
  const Seen &e = SEEN;
  const Frames *sp = frames_of(n);
  int w = sp->w, h = sp->h, d = e.at.wdir, lo = e.top, hi = HORIZON - 6 - h, y = (int) rnd(lo, hi);
  int x0 = d > 0 ? -w - 2 : W + 2, x1 = d > 0 ? W + 2 : -w - 2;
  float v = 30 + 2.5f * std::max(e.at.wind, e.at.gust * 0.7f);  // px/s
  int gust = (int) ((float) (W + w + 4) * 1000 / v / 3);
  rare_on(n, std::max(e.at.light, 0.5f), x0, y, {false, 0, 0});
  when_on(n, [=] {
    spin(n, 0, turn * (float) d);
    glide(n, 0, "x", x1, 3 * gust);
    for (int k = 0, yy = y; k < 3; k++) {
      yy = std::clamp(yy + (int) rnd(-24, 24), lo, hi);
      later((uint32_t) (k * gust), [=] { glide(n, 0, "y", yy, gust, "both"); });
    }
    later((uint32_t) (3 * gust), rare_off);
  });
}

// A flying saucer comes in from far off, stops over the cabin's roof, or over the hill away from the moon, looks a
// while, shines its beam down and is off up and away in a moment
inline void saucer() {
  const Seen &e = SEEN;
  int x = e.roof_x - 10, base = e.roof_y;  // the beam's end
  if (e.roof_x < 0 || (e.moon_up >= 0 && abs(e.roof_x - e.moon_up) < 40)) {
    for (int k = 0; k < 8; k++) {
      x = (int) rnd(20, W - 41);
      if (e.moon_up < 0 || abs(x + 10 - e.moon_up) >= 40)
        break;
    }
    base = e.hill.at(x + 10);
  }
  bool left = chance(0.5f);
  int y = std::max(e.top, base - 44), come = 2400;
  rare_on("ufo", 1, left ? -40 : W + 20, e.top, {false, 0, 0});
  rounds("ufo", 0, 1);
  when_on("ufo", [=] {
    glide("ufo", 0, "size", 30, 0);
    glide("ufo", 0, "size", 100, come, "out");
    move("ufo", 0, x, y, come, "out");
    move_later(come, "ufo", 0, x, y - 2, 1000, "both");
    move_later(come + 1000, "ufo", 0, x, y, 1000, "both");
    later(ms_of("ufo", 0, 15) + 200, [=] {  // the beam's off
      move("ufo", 0, left ? W + 30 : -50, -50, 700, "in");
      glide("ufo", 0, "size", 40, 700, "in");
    });
    later(ms_of("ufo") + 400, rare_off);
  });
}

// A sea serpent surfaces off the coast, swims along the open water at 3 px/s and goes under again
inline void serpent() {
  const Seen &e = SEEN;
  bool right = chance(0.5f);
  int span = e.sea_r - e.sea_l - 32, len = std::min(span, 150), a = e.sea_l + (int) rnd(0, (float) (span - len));
  int x0 = right ? a : a + len, x1 = right ? a + len : a, y = e.sea_y - 8, ms = len * 1000 / 3;
  rare_on("serpent", std::max(e.at.light, 0.5f), flip_x("serpent", x0, !right), y, {false, 0, 2500});
  if (!right)
    flip("serpent", 0, true, false);
  when_on("serpent", [=] {
    move("serpent", 0, flip_x("serpent", x1, !right), y, ms);
    later((uint32_t) ms, [] { remove("serpent", 0, 3000); });
    later((uint32_t) ms + 3200, rare_off);
  });
}

// A Viking longship rows out of nowhere along the open water off the coast, and back into it, on a fresh day that
// keeps the sailboat in
inline void longship() {
  const Seen &e = SEEN;
  bool right = chance(0.5f);
  int span = e.sea_r - e.sea_l - 32, len = std::min(span, 160), a = e.sea_l + (int) rnd(0, (float) (span - len));
  int x0 = right ? a : a + len, x1 = right ? a + len : a, y = e.sea_y - 16, ms = len * 1000 / 5;
  rare_on("longship", e.at.light, x0, y, {false, 0, 2500});
  if (!right)
    flip("longship", 0, true, false);
  when_on("longship", [=] {
    move("longship", 0, x1, y, ms);
    later((uint32_t) ms, [] { remove("longship", 0, 3000); });
    later((uint32_t) ms + 3200, rare_off);
  });
}

// A troll plods along the ridge with the sun down, and is gone before the sun can catch it
inline void troll() {
  const Seen &e = SEEN;
  NearHill ridge = e.ridge;
  bool right = chance(0.5f);
  int span = e.ridge_r - e.ridge_l - 24, len = std::min(span, 100),
      a = e.ridge_l + 4 + (int) rnd(0, (float) (span - len));
  int x0 = right ? a : a + len, x1 = right ? a + len : a;
  rare_on("troll", std::max(e.at.light, 0.5f), x0, ridge.at(x0 + 8) - 22, {false, 0, 2500});
  if (!right)
    flip("troll", 0, true, false);
  when_on("troll", [=] {
    uint32_t ms = amble("troll", 0, ridge, x0, x1, 5, 8, 22);
    later(ms, [] { remove("troll", 0, 2500); });
    later(ms + 2700, rare_off);
  });
}

// A wolf comes up onto the skyline under the full moon, howls at it twice and goes
inline void wolf() {
  const Seen &e = SEEN;
  rare_on("wolf", 1, e.den_x, e.den_y, {false, 0, 2000});
  if (e.moon_x < e.den_x + 8)
    flip("wolf", 0, true, false);
  rounds("wolf", 0, 1);
  when_on("wolf", [] { later(ms_of("wolf") + 2500, rare_off); });
}

// A witch on her broomstick flies across the moon
inline void witch() {
  Ride r = witch_ride(SEEN.moon_x, SEEN.moon_y, SEEN.top);
  rare_on("witch", 1, r.x[0], r.y[0], {false, 0, 0});
  if (r.x[3] < r.x[0])
    flip("witch", 0, true, false);
  when_on("witch", [=] { later(ride("witch", r), rare_off); });
}

// What a roll can bring: its odds when it fits, the longest it takes once its sprite is on the panel, whether it fits
// the picture and what it does
struct Rare {
  const char *name;
  float odds;
  uint32_t ms;
  bool (*fits)(const Seen &e);
  void (*go)();
};
inline bool blows(const Seen &e) { return gale(e.at) && e.at.light > 0.3f; }
// no rain, snow or fog
inline bool dry(const Seen &e) { return !e.at.rain && !e.at.snow && e.at.fog < 30; }
inline bool moonlit(const Seen &e) { return e.at.dark && e.moon_x >= 0 && e.at.cover < 50 && dry(e); }
inline bool walkable(const Seen &e) { return e.ridge_r > e.ridge_l && !e.town; }
inline bool twilight(const Seen &e) { return e.at.sun_el < 0 && !e.at.dark; }
inline const Rare RARE[] = {
    {"a trampoline", 0.08f, 12000, blows, [] { blown("trampoline", 60); }},
    {"an umbrella", 0.05f, 12000, blows, [] { blown("umbrella", 280); }},
    {"a shirt", 0.05f, 12000, blows, [] { blown("shirt", 150); }},
    {"a cow", 0.015f, 12000, blows, [] { blown("cow", 90); }},
    // after sunset, when the sailboat's in
    {"a sea serpent", 0.03f, 54000,
     [](const Seen &e) {
       return e.sea_r > e.sea_l && e.at.sun_el < 0 && e.at.light > 0.15f && e.at.wind < 8 && dry(e);
     },
     serpent},
    {"a flying saucer", 0.0015f, 9000, [](const Seen &e) { return e.at.dark && e.at.cover < 40 && dry(e); }, saucer},
    {"a Viking longship", 0.02f, 36000,
     [](const Seen &e) {
       return e.sea_r > e.sea_l && e.at.sun_el > 0 && e.at.light > 0.5f && e.at.wind >= 10 && !gale(e.at) && dry(e);
     },
     longship},
    // trolls can't stand the sun
    {"a troll", 0.03f, 24000, [](const Seen &e) { return walkable(e) && twilight(e) && dry(e); }, troll},
    {"a wolf", 0.04f, 14000,
     [](const Seen &e) { return e.den_x >= 0 && !e.town && moonlit(e) && fabsf(e.moon_phase - 0.5f) < 0.08f; }, wolf},
    {"a witch", 0.004f, 12000, moonlit, witch},
};
inline constexpr int NRARE = sizeof RARE / sizeof *RARE;

// Encounter k now if the panel has room for it, whatever the odds. False if it hasn't, or one is on.
inline bool encounter(int k) {
  if (!R.rare.empty() || !rare_room(RARE_WAIT + RARE[k].ms))
    return false;
  ESP_LOGI("met", "Something rare: %s", RARE[k].name);
  RARE[k].go();
  return true;
}

// Christmas, New Year's Eve, Easter, 17 May, Sankthans, Halloween and Valentine's Day
inline bool special_day(const At &a) {
  return a.xmas || a.newyear || a.easter_days || a.may17 || a.midsummer || a.halloween || a.valentine;
}

inline uint32_t rare_roll() {
  if (special_day(SEEN.at))
    return 0;
  for (int k = 0; k < NRARE; k++)
    if (RARE[k].fits(SEEN) && chance(RARE[k].odds))
      return encounter(k) ? RARE_WAIT + RARE[k].ms : 0;
  return 0;
}

// The rolls, from a minute or so after the show starts
inline void rare_start() {
  sometimes(RARE_EVERY, minutes(1, 20), rare_roll);
  R.timers.back().rare = true;
}

}  // namespace flexit::panel::met::live
