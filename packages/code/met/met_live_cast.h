// The weather animations' acts, and which of them are on: the cast. The day's own come first, then the weather, then
// what lives in the picture, like the lighthouse's beam and the sailboat, each when it fits the picture and the panel
// has the room. What room is left goes to the features, which take turns by lot, two at a time when both fit, and to
// those that come by: an act's visitors and met_live_rare.h's encounters, one at a time. The acts themselves are in
// met_live_*.h.
#pragma once
#include <cstring>
#include "met_live.h"
#include "met_live_days.h"
#include "met_live_land.h"
#include "met_live_rare.h"
#include "met_live_sky.h"
#include "met_live_town.h"

namespace flexit::panel::met::live {

// The pool's bytes the cast plans with, a little under the panel's, and the most the forecast over the picture
// takes of them while it's shown
inline constexpr int CAST_POOL = OVERLAY_POOL - 144, FORECAST_BYTES = 4200;

// In the order they take the panel's room. The day's and the weather may take a visitor's too, the others leave it.
enum Rank : uint8_t { DAY, WEATHER, RESIDENT, FEATURE };

// The part of the picture it draws itself, L_*, which the picture then leaves out and only one act has at a time.
// The most it takes of the panel: what stays, what it brings by itself, and what its visitors take beyond VISIT.
// How it starts, whether it fits now, how it follows a picture that changed under it, and what it's made of, so it
// starts again when that changes. What it takes besides in this scene, and what more if there's room, the weather
// in bytes and the others in copies with each one's bytes, which it finds in granted(). A feature's lots in the
// draw, 1 without.
struct Act {
  const char *name;
  Rank rank;
  uint16_t live;
  Room cost;
  void (*start)(const Scene &s);
  bool (*fits)(const Scene &s);
  void (*redo)(const Scene &s) = nullptr;
  uint32_t (*made_of)(const Scene &s) = nullptr;
  Room (*also)(const Scene &s) = nullptr;
  Room (*more)(const Scene &s) = nullptr;
  float (*lots)(const Scene &s) = nullptr;
};

// clouds of the high layer, or of the middle and low ones
inline bool has_clouds(const Scene &s, bool high) {
  for (int i = 0; i < s.nclouds; i++)
    if (high_cloud(s, i) == high)
      return true;
  return false;
}
inline bool dry(const Scene &s) { return !wet(s.at); }
inline bool by_day(const Scene &s) { return !s.at.dark && dry(s); }
// when the picture has the sailboat out
inline bool fine_at_sea(const Scene &s) { return s.ground == G_COAST && s.at.sun_el > 0 && s.at.wind < 10 && dry(s); }
inline bool by_the_fire(const Scene &s) { return s.cabin && s.at.dark && s.window_lit; }

// clang-format off: an act a line, whether it fits on the next, and the rest after it
inline const Act ACTS[] = {
    // the days, when the picture has them
    {"Christmas Eve in town", DAY, L_XMAS, {3, 3, 1050}, xmas_town,
     [](const Scene &s) { return s.at.xmas && s.at.dark && s.ground == G_CITY && s.nlights; }},
    {"Christmas Eve", DAY, 0, {2, 2, 700}, xmas,
     [](const Scene &s) { return s.at.xmas && s.at.dark && s.ground != G_CITY && s.nlights; }},
    {"New Year's Eve", DAY, L_FIREWORKS, {1, 4, 3400}, new_year,
     [](const Scene &s) { return s.at.newyear && s.at.dark; }},
    {"Sankthans", DAY, L_BONFIRE, {3, 7, 3400}, sankthans,
     [](const Scene &s) { return s.at.midsummer && s.fire.x >= 0; }},
    {"Halloween", DAY, L_BATS, {3, 3, 1600}, halloween,
     [](const Scene &s) { return s.at.halloween && s.at.dark; }},
    {"Valentine's Day", DAY, L_SMOKE, {1, 3, 950}, valentine,
     [](const Scene &s) { return s.at.valentine && s.cabin && s.puffs; }},
    {"17 May", DAY, 0, {2, 2, 1400}, may17,
     [](const Scene &s) { return s.at.may17 && !s.at.dark && !s.at.rain; }},
    {"Easter", DAY, 0, {2, 4, 1400}, easter,
     [](const Scene &s) { return s.at.easter_days && s.neggs && !s.at.dark; }},
    // the weather
    {"Thunderstorm", WEATHER, L_PREC | L_BOLT, {3, 3, 0}, thunderstorm,
     [](const Scene &s) { return s.at.thunder; }, prec_redo, nullptr,
     [](const Scene &s) { return Room{0, 0, prec_least(s) + bolt_bytes(s)}; },
     [](const Scene &s) { return Room{0, 0, prec_more(s)}; }},
    {"Gentle snowfall", WEATHER, L_PREC, {2, 2, 0}, snowfall,
     [](const Scene &s) { return s.at.snow && !s.at.thunder; }, prec_redo, nullptr,
     [](const Scene &s) { return Room{0, 0, prec_least(s)}; }, [](const Scene &s) { return Room{0, 0, prec_more(s)}; }},
    {"Light rain", WEATHER, L_PREC, {2, 2, 0}, snowfall,
     [](const Scene &s) { return s.at.rain && !s.at.snow && !s.at.thunder; }, prec_redo, nullptr,
     [](const Scene &s) { return Room{0, 0, prec_least(s)}; }, [](const Scene &s) { return Room{0, 0, prec_more(s)}; }},
    {"Fog in the valley", WEATHER, 0, {2, 2, 220}, valley_fog,
     [](const Scene &s) { return s.at.fog > 30 && !s.at.dark && dry(s); }},
    // what lives in the picture, the first to have the room when it's short
    {"The lighthouse", RESIDENT, 0, {2, 2, 320}, lighthouse,
     [](const Scene &s) { return s.lighthouse && s.at.dark; }},
    {"The sailboat", RESIDENT, L_BOAT, {1, 1, 900}, sailboat, fine_at_sea},
    {"The lights on the spruce", RESIDENT, 0, {2, 2, 950}, spruce_lights,
     [](const Scene &s) { return s.at.yule && s.at.dark && s.nlights && s.ground != G_CITY; }},
    {"The flag", RESIDENT, L_FLAG, {1, 1, FLAG_BYTES}, flag_up,
     [](const Scene &s) { return s.at.flag_up && s.pole_x >= 0; }, flag_up},
    {"The northern lights", RESIDENT, 0, {1, 1, 300}, northern_lights,
     [](const Scene &s) { return s.aurora; }, nullptr, lights_at},
    {"The stars", RESIDENT, 0, {1, 1, 420}, stars,
     [](const Scene &s) { return s.at.dark && s.nbright > 0; }, stars_turn, nullptr, nullptr,
     [](const Scene &) { return Room{0, 5, 5 * 45}; }},
    {"Smoke from the chimney", RESIDENT, L_SMOKE, {1, 2, 1050}, chimney_smoke,
     [](const Scene &s) { return s.cabin && s.puffs; }, chimney_smoke, cabin_smoke, nullptr,
     [](const Scene &s) { return Room{0, s.puffs - 2, (s.puffs - 2) * 160}; }},
    {"Firelight in the window", RESIDENT, 0, {1, 1, 1000}, firelight, by_the_fire, nullptr, cabin_at},
    {"The antenna's light", RESIDENT, L_ANTENNA, {1, 1, 60}, beacon,
     [](const Scene &s) { return s.ground == G_CITY && s.at.sun_el < -3 && s.antenna.x >= 0; }},
    {"Light on the water", RESIDENT, 0, {1, 1, 650}, shimmer,
     [](const Scene &s) { return s.sea && dry(s); }},
    {"The advent star", RESIDENT, 0, {1, 1, 180}, advent_star,
     [](const Scene &s) { return s.at.yule && s.cabin && !by_the_fire(s); }, nullptr, cabin_at},
    // who comes by them, with nothing of their own on the panel
    {"About the cabin", RESIDENT, 0, {}, about_the_cabin,
     [](const Scene &s) { return s.cabin && by_day(s); }, nullptr, cabin_at},
    {"By the cabin at night", RESIDENT, 0, {}, cabin_by_night,
     [](const Scene &s) { return s.cabin && s.at.dark; }, nullptr, cabin_at},
    {"Off the coast", RESIDENT, 0, {}, off_the_coast, fine_at_sea},
    // the features
    {"Gulls", FEATURE, L_BIRDS, {1, 2, 950}, gulls,
     [](const Scene &s) { return s.ground == G_COAST && by_day(s); }, nullptr, cabin_at, nullptr, nullptr,
     [](const Scene &s) { return 1 + s.at.wind / 6; }},
    {"Crows", FEATURE, L_BIRDS, {1, 2, 1600}, crows,
     [](const Scene &s) { return (s.ground == G_HILLS || s.ground == G_MOUNTAINS) && by_day(s); }, nullptr,
     cabin_at},
    {"Eiders", FEATURE, 0, {1, 1, 850}, eiders, fine_at_sea, nullptr, nullptr, nullptr, nullptr,
     [](const Scene &s) { return s.at.wind < 4 ? 2.0f : 1.0f; }},
    {"Glitter on the water", FEATURE, 0, {1, 4, 500}, glitter,
     [](const Scene &s) { return fine_at_sea(s) && glitter_room(s); }, glitter},
    // its hour is short, so it mostly gets it
    {"A moose at dusk", FEATURE, 0, {1, 2, 2000}, moose,
     [](const Scene &s) { return s.at.sun_el > -6 && s.at.sun_el < 4 &&
                                 (s.ground == G_HILLS || s.ground == G_MOUNTAINS) && dry(s); },
     nullptr, cabin_at, nullptr, nullptr, [](const Scene &) { return 6.0f; }},
    {"Reindeer and a golden eagle", FEATURE, L_BIRDS, {2, 5, 2100}, reindeer,
     [](const Scene &s) { return s.ground == G_MOUNTAINS && by_day(s); }, nullptr, cabin_at},
    {"Lambs, a wagtail and geese", FEATURE, L_BIRDS, {2, 3, 1200}, spring,
     [](const Scene &s) { return s.cabin && s.at.season == 2 && by_day(s); }, nullptr, cabin_at},
    {"Bullfinches at the julenek", FEATURE, L_BIRDS, {2, 2, 150}, julenek,
     [](const Scene &s) { return s.nek.x >= 0 && by_day(s); }, nullptr, cabin_at},
    {"Summer butterflies", FEATURE, 0, {3, 5, 2200}, butterflies,
     [](const Scene &s) { return s.cabin && s.at.season == 3 && s.at.fair && s.at.wind < 6 && dry(s); },
     nullptr, cabin_at, nullptr, nullptr, [](const Scene &) { return 2.0f; }},
    {"A skier, and snow off the spruces", FEATURE, 0, {2, 2, 1200}, skier,
     [](const Scene &s) { return s.cabin && s.snow_trees && by_day(s); }, nullptr, cabin_at, nullptr, nullptr,
     [](const Scene &s) { return s.at.weekday == 0 || s.at.weekday == 6 ? 2.0f : 1.0f; }},
    {"Birch leaves and geese", FEATURE, L_BIRDS, {2, 6, 1450}, leaves,
     [](const Scene &s) { return s.cabin && s.at.season == 4 && by_day(s); }, nullptr, cabin_wind},
    {"High clouds", FEATURE, L_CIRRUS, {}, high_clouds,
     [](const Scene &s) { return by_day(s) && has_clouds(s, true) && !has_clouds(s, false); }, nullptr, high_shapes,
     [](const Scene &s) { return clouds_cost(s, true); }},
    {"Drifting clouds", FEATURE, L_CLOUDS, {}, lower_clouds,
     [](const Scene &s) { return by_day(s) && has_clouds(s, false); }, nullptr, lower_shapes,
     [](const Scene &s) { return clouds_cost(s, false); }},
    {"Evening in town", FEATURE, 0, {2, 5, 300}, town_evening,
     [](const Scene &s) { return s.ground == G_CITY && s.at.sun_el < -3; }, town_redo},
    {"A day in town", FEATURE, L_BIRDS, {2, 3, 2100}, town_day,
     [](const Scene &s) { return s.ground == G_CITY && by_day(s); }},
};
// clang-format on
inline constexpr int NACTS = sizeof ACTS / sizeof *ACTS;
static_assert(NACTS <= 64, "Running::only has a bit for each");

// Features on at a time, when both fit
inline constexpr int LINES = 2;

// The act called name, -1 if there's none
inline int act_of(const char *name) {
  for (int i = 0; i < NACTS; i++)
    if (!strcmp(ACTS[i].name, name))
      return i;
  return -1;
}

// What act i takes in this scene at the least
inline Room cost_of(int i, const Scene &s) { return ACTS[i].also ? ACTS[i].cost + ACTS[i].also(s) : ACTS[i].cost; }

// What the cast has: the panel's room, less the forecast's over the picture
inline Room whole(const Scene &s) {
  Room r{OVERLAY_SLOTS, OVERLAY_PLACES, CAST_POOL};
  return s.overlay ? r - Room{1, 1, FORECAST_BYTES} : r;
}

// A number from three, the same each time
inline uint32_t mixed(uint32_t a, uint32_t b, uint32_t c) {
  Rng r = seeded(a * 0x9E3779B1u ^ b * 0x85EBCA77u ^ c * 0xC2B2AE3Du);
  r.next();
  r.next();
  return r.next();
}

// The turn a line's feature has at clock, minutes of local time: each 20 to 40 minutes by the day, one line's ending
// on even minutes and the other's on odd ones, so one changes at a time
inline uint32_t turn_of(uint32_t clock, int line) {
  uint32_t day = clock / 1440, at = clock % 1440, k = 0;
  for (uint32_t end = 4 + 2 * (mixed(day, (uint32_t) line, 255) % 15) + (uint32_t) line; end <= at; k++)
    end += 20 + 2 * (mixed(day, (uint32_t) line, k) % 11);
  return day * 128 + k;
}
// The minutes from clock until one of them ends
inline uint32_t turn_left(uint32_t clock) {
  for (uint32_t m = 1;; m++)
    for (int line = 0; line < LINES; line++)
      if (turn_of(clock + m, line) != turn_of(clock, line))
        return m;
}

// One of the features in can for a line's turn, by their lots: the more an act has, the likelier. Not but. The same
// each time it's asked for that turn in that place.
inline int drawn(const Scene &s, uint32_t turn, int line, const std::vector<int> &can, int but = -1) {
  int best = -1;
  float nearest = 0;
  for (int i : can) {
    float lots = ACTS[i].lots ? ACTS[i].lots(s) : 1;
    if (i == but || lots <= 0)
      continue;
    uint32_t lot = mixed(turn, s.land->seed * 2 + (uint32_t) line, (uint32_t) i);
    float key = logf((float) ((lot & 0xFFFFFF) + 1) / 16777217.0f) / lots;
    if (best < 0 || key > nearest)
      nearest = key, best = i;
  }
  return best;
}

// The line's feature of those that can be on: the one asked for, or one drawn for its turn, another than the turn
// before had, and so on back to the day's first
inline int feature(const Scene &s, int line, const std::vector<int> &can) {
  if (!line && std::find(can.begin(), can.end(), R.pin) != can.end())
    return R.pin;
  int had = -1;
  for (uint32_t turn = turn_of(s.at.clock, line), k = turn / 128 * 128; k <= turn; k++)
    had = drawn(s, k, line, can, can.size() > 1 ? had : -1);
  return had;
}

// The acts for the picture s, each with what it may take, and what they leave of the panel's room
inline std::vector<On> choose(const Scene &s, Room *spare = nullptr) {
  std::vector<On> cast;
  Room room = whole(s);
  uint16_t drawn_by_one = 0;
  uint64_t on = 0;
  auto may = [&](int i, Rank rank) {
    return ACTS[i].rank == rank && (R.only & ~on) >> i & 1 && !(ACTS[i].live & drawn_by_one) && ACTS[i].fits(s) &&
           (rank < RESIDENT ? room : room - VISIT).has(cost_of(i, s));
  };
  auto take = [&](int i) {
    cast.push_back(On{i, ACTS[i].name, 0, cost_of(i, s), {}});
    room = room - cast.back().grant;
    drawn_by_one |= ACTS[i].live;
    on |= 1ull << i;
  };
  for (Rank rank : {DAY, WEATHER, RESIDENT})
    for (int i = 0; R.on && i < NACTS; i++)
      if (may(i, rank))
        take(i);
  // the rain and snow as thick as the scene's, a visitor's bytes left only for what a gale blows by
  for (On &o : cast)
    if (ACTS[o.act].rank == WEATHER && ACTS[o.act].more) {
      Room more = (gale(s.at) ? room - Room{0, 0, VISIT.bytes} : room).within(ACTS[o.act].more(s));
      o.grant = o.grant + more;
      room = room - more;
    }
  for (int line = 0; R.on && line < LINES; line++) {
    std::vector<int> can;
    for (int i = 0; i < NACTS; i++)
      if (may(i, FEATURE))
        can.push_back(i);
    int i = feature(s, line, can);
    if (i >= 0)
      take(i);
  }
  // and the copies the others have more of, one at a time in turn
  for (bool gave = true; gave;) {
    gave = false;
    for (On &o : cast) {
      if (ACTS[o.act].rank == WEATHER || !ACTS[o.act].more)
        continue;
      Room more = ACTS[o.act].more(s), one = {0, 1, more.places > 0 ? more.bytes / more.places : 0};
      if (o.grant.places < cost_of(o.act, s).places + more.places && (room - VISIT).has(one)) {
        o.grant = o.grant + one;
        room = room - one;
        gave = true;
      }
    }
  }
  if (spare)
    *spare = room;
  return cast;
}

// What the picture leaves out for the acts that fit it
inline uint16_t leaves_out(const Scene &s) {
  uint16_t live = 0;
  for (const On &o : choose(s))
    live |= ACTS[o.act].live;
  return live;
}

// The feature to have on whenever it fits, by name, anything else for the draw alone
inline void ask_for(const char *name) {
  int i = act_of(name);
  R.pin = i >= 0 && ACTS[i].rank == FEATURE ? i : -1;
}

// The acts that fit the picture s: those that no longer do go, new ones start, and one starts again if the place,
// the landscape or what it's made of changed under it. The others follow the picture.
inline void apply(const Scene &s) {
  Room spare;
  std::vector<On> cast = choose(s, &spare);
  uint32_t where = s.land->seed * 31u + s.ground;
  for (On &o : cast) {
    const Act &a = ACTS[o.act];
    o.sig = where * 31u + (a.made_of ? a.made_of(s) : 0);
    // rain or snow thinned to fit starts again when what it has changes
    if (a.rank == WEATHER && a.more && o.grant.bytes < (cost_of(o.act, s) + a.more(s)).bytes)
      o.sig = o.sig * 31u + 1 + (uint32_t) o.grant.bytes / 400;
  }
  R.sun_x = (int16_t) (s.sun ? s.sun_x : -1);
  R.wdir = s.at.wdir;
  bool relit = lroundf(R.light * 100) != lroundf(s.at.light * 100), first = R.cast.empty();
  R.light = s.at.light;
  for (size_t k = R.cast.size(); k-- > 0;) {
    auto same = std::find_if(cast.begin(), cast.end(), [&](const On &o) { return o.act == R.cast[k].act; });
    if (same == cast.end() || same->sig != R.cast[k].sig)
      drop(R.cast[k].act);
  }
  R.spare = spare;
  R.turn_at = cast.empty() ? 0 : millis() + turn_left(s.at.clock) * 60000 + 1000;
  if (!spare.has(R.guest)) {  // whoever is here on that room goes
    while (!R.comers.empty())
      unload(std::string(R.comers.begin()->first));
    R.busy = millis();
  }
  if (cast.empty()) {
    stop();
    return;
  }
  if (first) {
    R.rng = seeded((uint32_t) millis());
    overlay::emptied = emptied;
    rare_start();
  }
  rare_seen(s);
  for (const On &o : cast) {
    As as(o.act);
    if (On *was = on(o.act)) {
      was->grant = o.grant;
      if (ACTS[o.act].redo)
        ACTS[o.act].redo(s);
    } else {
      R.cast.push_back(o);
      ESP_LOGI("met", "Weather animation: %s", o.name);
      ACTS[o.act].start(s);
    }
  }
  if (relit)
    relight();
}

}  // namespace flexit::panel::met::live
