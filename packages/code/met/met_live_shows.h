// The weather animations' shows in the order they're tried. The days' and the weather's run when they fit the
// picture, then the night's take half-hour turns with each other and the day's with each other, those that fit.
// The shows themselves are in met_live_*.h, with met_live_rare.h's encounters now and then over any of them.
#pragma once
#include "met_live.h"
#include "met_live_days.h"
#include "met_live_land.h"
#include "met_live_rare.h"
#include "met_live_sky.h"
#include "met_live_town.h"

namespace flexit::panel::met::live {

// What the picture leaves out for it, with L_FLAG room for the flag on flag days, an animation and a copy. How it
// starts, how it follows a picture that changed under it, like clouds moving over the rain, and what it's made of,
// so it starts again when that changes, the two if any. Whether it fits now, and which shows it takes turns with:
// those with its number, 0 for one that runs whenever it fits.
struct Show {
  const char *name;
  uint16_t live;  // L_*
  void (*start)(const Scene &s);
  void (*redo)(const Scene &s);
  uint32_t (*made_of)(const Scene &s);
  bool (*fits)(const Scene &s);
  uint8_t turns = 0;
};
enum : uint8_t { BY_NIGHT = 1, BY_DAY };

// clouds of the high layer, or of the middle and low ones
inline bool has_clouds(const Scene &s, bool high) {
  for (int i = 0; i < s.nclouds; i++)
    if (high_cloud(s, i) == high)
      return true;
  return false;
}

// clang-format off: a show a line, and whether it fits on the next
inline const Show SHOWS[] = {
    // the days, when the picture has them
    {"Christmas Eve in town", L_XMAS | L_FLAG, xmas_town, nullptr, nullptr,
     [](const Scene &s) { return s.at.xmas && s.at.dark && s.ground == G_CITY && s.nlights; }},
    {"Christmas Eve", 0, xmas, nullptr, nullptr,
     [](const Scene &s) { return s.at.xmas && s.at.dark && s.nlights; }},
    {"New Year's Eve", L_FIREWORKS, new_year, nullptr, nullptr,
     [](const Scene &s) { return s.at.newyear && s.at.dark; }},
    {"Sankthans", L_BONFIRE, sankthans, nullptr, nullptr,
     [](const Scene &s) { return s.at.midsummer && s.fire.x >= 0; }},
    {"Halloween", L_BATS | L_FLAG, halloween, nullptr, nullptr,
     [](const Scene &s) { return s.at.halloween && s.at.dark; }},
    {"Valentine's Day", L_SMOKE | L_FLAG, valentine, nullptr, nullptr,
     [](const Scene &s) { return s.at.valentine && s.cabin && s.puffs; }},
    {"17 May", L_FLAG, may17, nullptr, nullptr,
     [](const Scene &s) { return s.at.may17 && !s.at.dark && !s.at.rain; }},
    {"Easter", L_FLAG, easter, nullptr, nullptr,
     [](const Scene &s) { return s.at.easter_days && s.neggs && !s.at.dark; }},
    // the weather
    {"Thunderstorm", L_PREC | L_BOLT, thunderstorm, prec_redo, nullptr,
     [](const Scene &s) { return s.at.thunder; }},
    {"Gentle snowfall", L_PREC, snowfall, prec_redo, in_gale,
     [](const Scene &s) { return s.at.snow; }},
    {"Light rain", L_PREC, snowfall, prec_redo, in_gale,
     [](const Scene &s) { return s.at.rain; }},
    {"Fog in the valley", L_FLAG, valley_fog, nullptr, nullptr,
     [](const Scene &s) { return s.at.fog > 30 && !s.at.dark; }},
    // the night
    {"The lights on the spruce", 0, spruce_lights, nullptr, nullptr,
     [](const Scene &s) { return s.at.yule && s.at.dark && s.nlights && s.ground != G_CITY; }, BY_NIGHT},
    {"Lighthouse and the coastal express", 0, lighthouse, lighthouse_redo, nullptr,
     [](const Scene &s) { return s.ground == G_COAST && s.at.dark; }, BY_NIGHT},
    {"Evening in town", L_ANTENNA, town_evening, town_redo, nullptr,
     [](const Scene &s) { return s.ground == G_CITY && s.at.sun_el < -3; }, BY_NIGHT},
    {"A cosy evening", L_SMOKE, cosy, nullptr, cabin_smoke,
     [](const Scene &s) { return s.cabin && s.at.dark && s.puffs && s.window_lit; }, BY_NIGHT},
    {"Starry winter night", L_FLAG, starry_night, stars_turn, nullptr,
     [](const Scene &s) { return s.at.dark && s.at.cover < 40 && s.nbright; }, BY_NIGHT},
    // the day
    {"A moose at dusk", L_FLAG, moose, nullptr, cabin_at,
     [](const Scene &s) { return s.at.sun_el > -6 && s.at.sun_el < 4 &&
                                 (s.ground == G_HILLS || s.ground == G_MOUNTAINS); }, BY_DAY},
    {"Sailboat, glitter and eiders", L_BOAT, sail, sail_redo, nullptr,
     [](const Scene &s) { return s.ground == G_COAST && s.at.sun_el > 0 && s.at.wind < 10; }, BY_DAY},
    {"A day in town", L_BIRDS | L_FLAG, town_day, nullptr, nullptr,
     [](const Scene &s) { return s.ground == G_CITY && !s.at.dark; }, BY_DAY},
    {"Reindeer and a golden eagle", L_BIRDS | L_FLAG, reindeer, nullptr, cabin_at,
     [](const Scene &s) { return s.ground == G_MOUNTAINS && !s.at.dark; }, BY_DAY},
    {"Lambs, a wagtail and geese", L_BIRDS | L_FLAG, spring, nullptr, cabin_at,
     [](const Scene &s) { return s.cabin && s.at.season == 2 && !s.at.dark; }, BY_DAY},
    {"Bullfinches at the julenek", L_BIRDS | L_FLAG, julenek, nullptr, cabin_at,
     [](const Scene &s) { return s.nek.x >= 0 && !s.at.dark; }, BY_DAY},
    {"Summer butterflies", L_FLAG, butterflies, nullptr, cabin_at,
     [](const Scene &s) { return s.cabin && s.at.season == 3 && s.at.fair && s.at.wind < 6; }, BY_DAY},
    {"A skier, and snow off the spruces", L_FLAG, skier, nullptr, cabin_at,
     [](const Scene &s) { return s.cabin && s.snow_trees && !s.at.dark; }, BY_DAY},
    {"Birch leaves and geese", L_BIRDS | L_FLAG, leaves, nullptr, cabin_wind,
     [](const Scene &s) { return s.cabin && s.at.season == 4 && !s.at.dark; }, BY_DAY},
    {"Stove smoke and an advent star", L_SMOKE | L_FLAG, smoke, nullptr, cabin_smoke,
     [](const Scene &s) { return s.cabin && s.puffs; }, BY_DAY},
    {"High clouds", L_CIRRUS | L_FLAG, high_clouds, nullptr, high_shapes,
     [](const Scene &s) { return !s.at.dark && has_clouds(s, true) && !has_clouds(s, false) && clouds_fit(s, true); },
     BY_DAY},
    {"Drifting clouds", L_CLOUDS | L_FLAG, lower_clouds, nullptr, lower_shapes,
     [](const Scene &s) { return !s.at.dark && has_clouds(s, false) && clouds_fit(s, false); }, BY_DAY},
    {"Gulls over the cabin", L_BIRDS | L_FLAG, roof_birds, nullptr, cabin_wind,
     [](const Scene &s) { return s.cabin && s.ground == G_COAST && !s.at.dark; }, BY_DAY},
    {"Crows over the cabin", L_BIRDS | L_FLAG, roof_birds, nullptr, cabin_wind,
     [](const Scene &s) { return s.cabin && s.ground != G_COAST && !s.at.dark; }, BY_DAY},
};
// clang-format on
inline constexpr int NSHOWS = sizeof SHOWS / sizeof *SHOWS;

// The flag on its pole on flag days, waving in place of the picture's, if the show has room
inline bool flag_fits(const Scene &s, int show) {
  return show >= 0 && (SHOWS[show].live & L_FLAG) && s.at.flag_up && s.pole_x >= 0;
}

// The northern lights glow over the picture's, in the room the flag has by day
inline bool lights_fit(const Scene &s, int show) {
  return show >= 0 && (SHOWS[show].live & L_FLAG) && s.aurora && !flag_fits(s, show);
}

// The hoist next to the pole, in the light there is
inline void flag_up(const Scene &s) {
  in_light("flag", 0);
  place("flag", 0, s.at.wdir < 0 ? s.pole_x - 16 : s.pole_x + 1, s.pole_top, "", {}, {.left = s.at.wdir > 0});
  R.flown = s.at.wdir;
}

// The first show that fits s, or of those it takes turns with the one whose turn it is
inline int pick(const Scene &s) {
  int fit[NSHOWS], n = 0;
  for (int i = 0; R.on && i < NSHOWS && !(n && SHOWS[i].turns != SHOWS[fit[0]].turns); i++) {
    if (!SHOWS[i].fits(s))
      continue;
    if (!SHOWS[i].turns)
      return i;
    fit[n++] = i;
  }
  return n ? fit[(R.turn < 0 ? s.at.turn : (uint32_t) R.turn) % (uint32_t) n] : -1;
}

// What the picture leaves out for the show that fits it
inline uint16_t leaves_out(const Scene &s) {
  int i = pick(s);
  return i < 0 ? 0 : (uint16_t) ((SHOWS[i].live & ~L_FLAG) | (flag_fits(s, i) ? L_FLAG : 0));
}

// The show that fits the picture s, started if it's another, or started again if the place, the landscape or what
// the show is made of changed under it
inline void apply(const Scene &s) {
  int i = pick(s);
  uint32_t where = s.land->seed * 31u + s.ground;
  if (i >= 0 && SHOWS[i].made_of)
    where = where * 31u + SHOWS[i].made_of(s);
  R.sun_x = (int16_t) (s.sun ? s.sun_x : -1);
  bool relit = lroundf(R.light * 100) != lroundf(s.at.light * 100);
  R.light = s.at.light;
  if (i != R.show || where != R.where) {
    stop();
    R.where = where;
    if (i < 0)
      return;
    R.show = i;
    R.label = std::string("weather: ") + SHOWS[i].name;
    R.rng = seeded((uint32_t) millis());
    ESP_LOGI("met", "Weather animation: %s", SHOWS[i].name);
    overlay::emptied = emptied;
    SHOWS[i].start(s);
    rare_start();
  } else {
    if (i >= 0 && SHOWS[i].redo)
      SHOWS[i].redo(s);
    if (relit)
      relight();
  }
  if (i >= 0)
    rare_seen(s);
  bool flag = flag_fits(s, i);
  if (flag && (!R.flag || R.flown != s.at.wdir)) {
    rare_off();  // the flag may need its slot
    flag_up(s);
  } else if (!flag && R.flag)
    clear("flag");
  R.flag = flag;
  uint32_t lights = lights_fit(s, i) ? lights_at(s) : 0;
  if (lights && lights != R.lights) {
    rare_off();  // as the flag
    northern_lights(s);
  } else if (!lights && R.lights)
    unload("lights");
  R.lights = lights;
}

}  // namespace flexit::panel::met::live
